/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DW3720/QM33120W SPI Communication & Porting Layer
 *
 * Phase 1: Device detection via SPI register read.
 * Phase 2A: Full dwt_spi_s porting layer for Qorvo dwt_uwb_driver.
 *
 * Implements the 5 SPI function pointers required by struct dwt_spi_s
 * (deca_interface.h). Initialization uses Zephyr's SPI API. At the configured
 * fast rate, the exclusive UWB owner uses a polling SPIM EasyDMA transfer to
 * avoid a scheduler round trip for each small timing-critical register access.
 * See the UWB_SPIM comment below for why both transports exist.
 */

#include "dw3720_spi.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/irq.h>
#include <hal/nrf_spim.h>
#include <string.h>

#include "deca_device_api.h"

/* DW3xxx SPI register addresses */
#define DW3XXX_REG_DEV_ID  0x00  /* Device ID register (4 bytes, read-only) */

/* DW3xxx SPI framing: bit 7 of first byte = R/W (0=read, 1=write) */
#define DW3XXX_SPI_READ    0x00
#define DW3XXX_SPI_WRITE   0x80

/* Reset timing (from Qorvo SDK qplatform_uwb_reset) */
#define RESET_ASSERT_MS    1   /* Hold RESET low for 1ms */
#define RESET_RECOVER_MS   5   /* Wait 5ms after releasing RESET */

/* Max SPI transaction size (header + body). SDK uses up to ~255 bytes. */
#define SPI_BUF_MAX        256

/* SPI frequency definitions */
#define SPI_FREQ_SLOW      2000000U   /* 2 MHz — safe for post-reset (<7 MHz) */
#define SPI_FREQ_FAST      DT_PROP(UWB_NODE, spi_max_frequency)

/* UWB radio child node on the high-speed SPIM bus */
#define UWB_NODE DT_NODELABEL(uwb_radio)

#if DT_NODE_EXISTS(UWB_NODE)

/*
 * Two SPI transports share this file, and both are needed:
 *
 *   - Zephyr's SPI API (spi_transceive/spi_write) for the post-reset window,
 *     where the DW3xxx requires < 7 MHz, and for the pre-init device-ID read.
 *   - polling_spi_transfer(), driving the SPIM registers directly at the DT
 *     fast rate, for the hot path. The UWB slot budget is measured in µs
 *     (see uwb_stats last_irq_to_arm_us), which is why the hot path does not
 *     go through the driver.
 *
 * dw3720_setslowrate()/dw3720_setfastrate() flip between them.
 *
 * The register path used to hardcode NRF_SPIM3, which only worked because the
 * Atletec overlay happens to put the radio on &spi3. Derive it from the DT bus
 * instead, so moving the radio moves the register writes with it.
 */
#define UWB_SPIM ((NRF_SPIM_Type *)DT_REG_ADDR(DT_BUS(UWB_NODE)))

/*
 * The register path programs TXD/RXD.PTR and MAXCNT — EasyDMA, which only the
 * SPIM peripheral has. A radio placed on a legacy "nordic,nrf-spi" bus would
 * compile and then write to reserved addresses.
 */
BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_BUS(UWB_NODE), nordic_nrf_spim),
	     "UWB radio must sit on a nordic,nrf-spim bus: the polling transport "
	     "drives EasyDMA registers directly.");

/* Common SPI operation flags */
#define SPI_OP_FLAGS (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

/*
 * SPI DT spec — MUST be const (Zephyr device refs are compile-time constants).
 * Used directly for Phase 1 detection reads.
 */
static const struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(
	UWB_NODE,
	SPI_OP_FLAGS,
	0  /* delay (unused) */
);

/*
 * Runtime copy of SPI config for the porting layer.
 * Initialized from spi_dev.config in dw3720_spi_init(), then frequency
 * can be swapped between slow (SPI_FREQ_SLOW) and fast (SPI_FREQ_FAST, taken
 * from the node's spi-max-frequency — 32 MHz on the Atletec board).
 */
static struct spi_config active_spi_cfg;

/* GPIO specs from devicetree */
static const struct gpio_dt_spec reset_gpio =
	GPIO_DT_SPEC_GET(UWB_NODE, reset_gpios);
static const struct gpio_dt_spec wakeup_gpio =
	GPIO_DT_SPEC_GET(UWB_NODE, wakeup_gpios);
static const struct gpio_dt_spec irq_gpio =
	GPIO_DT_SPEC_GET(UWB_NODE, irq_gpios);
static const struct gpio_dt_spec cs_gpio =
	SPI_CS_GPIOS_DT_SPEC_GET(UWB_NODE);

static struct gpio_callback irq_callback;
static K_SEM_DEFINE(irq_sem, 0, 1);
static volatile uint32_t last_irq_cycles;
static bool gpio_initialized;
static bool polling_spi_enabled;

/* Static TX/RX buffers for SPI transactions (avoid stack allocation) */
static uint8_t spi_tx_buf[SPI_BUF_MAX];
static uint8_t spi_rx_buf[SPI_BUF_MAX];

static int polling_spi_transfer(uint8_t *tx_buffer, uint8_t *rx_buffer,
				uint16_t length)
{
	uint32_t spins = 0U;

	UWB_SPIM->INTENCLR = UINT32_MAX;
	UWB_SPIM->EVENTS_END = 0U;
	UWB_SPIM->TXD.PTR = (uint32_t)(uintptr_t)tx_buffer;
	UWB_SPIM->TXD.MAXCNT = length;
	if (rx_buffer != NULL) {
		UWB_SPIM->RXD.PTR = (uint32_t)(uintptr_t)rx_buffer;
		UWB_SPIM->RXD.MAXCNT = length;
	} else {
		UWB_SPIM->RXD.PTR = 0U;
		UWB_SPIM->RXD.MAXCNT = 0U;
	}
	gpio_pin_set_dt(&cs_gpio, 1);
	__DMB();
	UWB_SPIM->TASKS_START = 1U;
	while (UWB_SPIM->EVENTS_END == 0U) {
		if (++spins >= 100000U) {
			UWB_SPIM->TASKS_STOP = 1U;
			gpio_pin_set_dt(&cs_gpio, 0);
			return -ETIMEDOUT;
		}
	}
	gpio_pin_set_dt(&cs_gpio, 0);
	return 0;
}

static void irq_gpio_handler(const struct device *port,
			     struct gpio_callback *callback,
			     gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);

	last_irq_cycles = k_cycle_get_32();
	k_sem_give(&irq_sem);
}

int dw3720_spi_init(void)
{
	int ret;

	if (gpio_initialized) {
		return 0;
	}

	if (!spi_is_ready_dt(&spi_dev)) {
		printk("DW3720: SPI bus not ready\n");
		return -ENODEV;
	}

	/* Copy SPI config from DT spec for runtime frequency switching */
	memcpy(&active_spi_cfg, &spi_dev.config, sizeof(active_spi_cfg));
	active_spi_cfg.frequency = SPI_FREQ_SLOW; /* Start slow (<7 MHz) */
	polling_spi_enabled = false;

	if (!gpio_is_ready_dt(&cs_gpio)) {
		printk("DW3720: CS GPIO not ready\n");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&cs_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("DW3720: CS GPIO config failed (%d)\n", ret);
		return ret;
	}

	/* Configure RESET as output (initially deasserted = high/inactive) */
	if (!gpio_is_ready_dt(&reset_gpio)) {
		printk("DW3720: RESET GPIO not ready\n");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("DW3720: RESET GPIO config failed (%d)\n", ret);
		return ret;
	}

	/* Configure WAKEUP as output (initially low = not waking) */
	if (!gpio_is_ready_dt(&wakeup_gpio)) {
		printk("DW3720: WAKEUP GPIO not ready\n");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&wakeup_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("DW3720: WAKEUP GPIO config failed (%d)\n", ret);
		return ret;
	}

	/* Configure IRQ now; the protocol owner enables the edge after radio setup. */
	if (!gpio_is_ready_dt(&irq_gpio)) {
		printk("DW3720: IRQ GPIO not ready\n");
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&irq_gpio, GPIO_INPUT);
	if (ret < 0) {
		printk("DW3720: IRQ GPIO config failed (%d)\n", ret);
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&irq_gpio, GPIO_INT_DISABLE);
	if (ret < 0) {
		printk("DW3720: IRQ disable failed (%d)\n", ret);
		return ret;
	}
	k_sem_reset(&irq_sem);
	gpio_init_callback(&irq_callback, irq_gpio_handler, BIT(irq_gpio.pin));
	ret = gpio_add_callback(irq_gpio.port, &irq_callback);
	if (ret < 0) {
		printk("DW3720: IRQ callback failed (%d)\n", ret);
		return ret;
	}

	gpio_initialized = true;
	return 0;
}

int dw3720_hw_reset(void)
{
	if (!gpio_initialized) {
		return -EINVAL;
	}

	/* Assert RESET (active-low: set pin to logical active = drive low) */
	gpio_pin_set_dt(&reset_gpio, 1);
	k_msleep(RESET_ASSERT_MS);

	/* Release RESET (deassert = high-Z / high) */
	gpio_pin_set_dt(&reset_gpio, 0);
	k_msleep(RESET_RECOVER_MS);

	return 0;
}

/**
 * @brief Read a 4-byte register from DW3xxx via SPI
 *
 * DW3xxx fast-command SPI read protocol (short header):
 *   TX: [addr | 0x00]  (1 byte: address with bit7=0 for read)
 *   RX: [dummy][D0][D1][D2][D3]  (1 dummy + 4 data bytes, little-endian)
 */
static int dw3720_spi_read_reg32(uint8_t addr, uint32_t *value)
{
	int ret;

	uint8_t tx_buf[1] = { addr | DW3XXX_SPI_READ };
	uint8_t rx_buf[5] = { 0 };  /* 1 dummy + 4 data bytes */

	const struct spi_buf tx_bufs[] = {
		{ .buf = tx_buf, .len = sizeof(tx_buf) },
	};
	const struct spi_buf rx_bufs[] = {
		{ .buf = rx_buf, .len = sizeof(rx_buf) },
	};

	const struct spi_buf_set tx_set = {
		.buffers = tx_bufs,
		.count = 1,
	};
	const struct spi_buf_set rx_set = {
		.buffers = rx_bufs,
		.count = 1,
	};

	ret = spi_transceive_dt(&spi_dev, &tx_set, &rx_set);
	if (ret < 0) {
		return ret;
	}

	/* Data starts at rx_buf[1] (first byte is clocked during TX phase) */
	*value = (uint32_t)rx_buf[1] |
		 ((uint32_t)rx_buf[2] << 8) |
		 ((uint32_t)rx_buf[3] << 16) |
		 ((uint32_t)rx_buf[4] << 24);

	return 0;
}

int dw3720_read_dev_id(uint32_t *dev_id)
{
	int ret;

	if (!dev_id) {
		return -EINVAL;
	}

	/* Reset chip to known state */
	ret = dw3720_hw_reset();
	if (ret < 0) {
		return ret;
	}

	/* Read Device ID register (address 0x00) */
	ret = dw3720_spi_read_reg32(DW3XXX_REG_DEV_ID, dev_id);
	if (ret < 0) {
		return ret;
	}

	/* Validate: 0x00000000 or 0xFFFFFFFF means no device / SPI fault */
	if (*dev_id == 0x00000000 || *dev_id == 0xFFFFFFFF) {
		return -ENODEV;
	}

	return 0;
}

const char *dw3720_dev_id_str(uint32_t id)
{
	switch (id) {
	case DW3720_DEV_ID_QM33120W:
		return "QM33120W";
	case DW3720_DEV_ID_DW3000P:
		return "DW3000 PDOA";
	case DW3720_DEV_ID_QM33110:
		return "QM33110";
	case DW3720_DEV_ID_DW3000:
		return "DW3000";
	default:
		return "Unknown";
	}
}

/* =============================================================================
 * Qorvo dwt_spi_s Porting Layer
 *
 * These functions implement the 5 function pointers in struct dwt_spi_s
 * from deca_interface.h. The Qorvo driver calls them for all SPI I/O.
 * =============================================================================
 */

/**
 * readfromspi — SDK sends header bytes, expects readlength data bytes back.
 *
 * The SDK provides the SPI header (1-3 bytes) in headerBuffer.
 * We send the header and simultaneously read back (headerLength + readlength)
 * bytes. The read data starts at offset headerLength in the RX buffer.
 *
 * Returns 0 (DWT_SUCCESS) on success, -1 (DWT_ERROR) on failure.
 */
int32_t dw3720_readfromspi(uint16_t headerLength, uint8_t *headerBuffer,
			    uint16_t readlength, uint8_t *readBuffer)
{
	uint32_t total = (uint32_t)headerLength + readlength;

	if (total > SPI_BUF_MAX) {
		return -1; /* DWT_ERROR */
	}

	/* Build TX buffer: header + dummy bytes for the read phase */
	memcpy(spi_tx_buf, headerBuffer, headerLength);
	memset(spi_tx_buf + headerLength, 0x00, readlength);

	int ret;

	if (polling_spi_enabled) {
		ret = polling_spi_transfer(spi_tx_buf, spi_rx_buf, total);
	} else {
		const struct spi_buf tx_bufs[] = {
			{ .buf = spi_tx_buf, .len = total },
		};
		const struct spi_buf rx_bufs[] = {
			{ .buf = spi_rx_buf, .len = total },
		};
		const struct spi_buf_set tx_set = {
			.buffers = tx_bufs,
			.count = 1,
		};
		const struct spi_buf_set rx_set = {
			.buffers = rx_bufs,
			.count = 1,
		};

		ret = spi_transceive(spi_dev.bus, &active_spi_cfg,
				     &tx_set, &rx_set);
	}
	if (ret < 0) {
		return -1; /* DWT_ERROR */
	}

	/* Copy read data (skip the header echo bytes) */
	memcpy(readBuffer, spi_rx_buf + headerLength, readlength);
	return 0; /* DWT_SUCCESS */
}

/**
 * writetospi — SDK sends header + body as one SPI write transaction.
 *
 * Returns 0 (DWT_SUCCESS) on success, -1 (DWT_ERROR) on failure.
 */
int32_t dw3720_writetospi(uint16_t headerLength, const uint8_t *headerBuffer,
			   uint16_t bodyLength, const uint8_t *bodyBuffer)
{
	uint32_t total = (uint32_t)headerLength + bodyLength;

	if (total > SPI_BUF_MAX) {
		return -1;
	}
	if (polling_spi_enabled) {
		memcpy(spi_tx_buf, headerBuffer, headerLength);
		memcpy(spi_tx_buf + headerLength, bodyBuffer, bodyLength);
		return polling_spi_transfer(spi_tx_buf, NULL, total) < 0 ?
			-1 : 0;
	}

	/* Use two SPI buffers in one transaction — no copy needed */
	const struct spi_buf tx_bufs[] = {
		{ .buf = (uint8_t *)headerBuffer, .len = headerLength },
		{ .buf = (uint8_t *)bodyBuffer,   .len = bodyLength },
	};

	const struct spi_buf_set tx_set = { .buffers = tx_bufs, .count = 2 };

	int ret = spi_write(spi_dev.bus, &active_spi_cfg, &tx_set);
	return (ret < 0) ? -1 : 0;
}

/**
 * writetospiwithcrc — Same as writetospi but appends a CRC8 byte.
 *
 * Returns 0 (DWT_SUCCESS) on success, -1 (DWT_ERROR) on failure.
 */
int32_t dw3720_writetospiwithcrc(uint16_t headerLength, const uint8_t *headerBuffer,
				  uint16_t bodyLength, const uint8_t *bodyBuffer,
				  uint8_t crc8)
{
	uint8_t crc_byte = crc8;
	uint32_t total = (uint32_t)headerLength + bodyLength + 1U;

	if (total > SPI_BUF_MAX) {
		return -1;
	}
	if (polling_spi_enabled) {
		memcpy(spi_tx_buf, headerBuffer, headerLength);
		memcpy(spi_tx_buf + headerLength, bodyBuffer, bodyLength);
		spi_tx_buf[headerLength + bodyLength] = crc8;
		return polling_spi_transfer(spi_tx_buf, NULL, total) < 0 ?
			-1 : 0;
	}

	const struct spi_buf tx_bufs[] = {
		{ .buf = (uint8_t *)headerBuffer, .len = headerLength },
		{ .buf = (uint8_t *)bodyBuffer,   .len = bodyLength },
		{ .buf = &crc_byte,               .len = 1 },
	};

	const struct spi_buf_set tx_set = { .buffers = tx_bufs, .count = 3 };

	int ret = spi_write(spi_dev.bus, &active_spi_cfg, &tx_set);
	return (ret < 0) ? -1 : 0;
}

/**
 * setslowrate — Switch SPI to slow rate (2 MHz).
 * Used after reset / before init when DW3xxx requires < 7 MHz.
 */
void dw3720_setslowrate(void)
{
	polling_spi_enabled = false;
	active_spi_cfg.frequency = SPI_FREQ_SLOW;
}

/**
 * setfastrate — Switch SPI to the DeviceTree fast rate.
 * Used after driver init completes for normal operation.
 */
void dw3720_setfastrate(void)
{
	active_spi_cfg.frequency = SPI_FREQ_FAST;
	UWB_SPIM->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_M32;
	UWB_SPIM->INTENCLR = UINT32_MAX;
	polling_spi_enabled = true;
}

/* =============================================================================
 * Platform Helpers
 * =============================================================================
 */

/**
 * Wakeup device by toggling the WAKEUP pin high for ~500us.
 * Called by Qorvo driver (via probe_interf.wakeup_device_with_io).
 */
void wakeup_device_with_io(void)
{
	if (!gpio_initialized) {
		return;
	}
	gpio_pin_set_dt(&wakeup_gpio, 1);
	k_busy_wait(500); /* 500 us */
	gpio_pin_set_dt(&wakeup_gpio, 0);
}

int dw3720_irq_enable(bool enable)
{
	if (!gpio_initialized) {
		return -EACCES;
	}

	return gpio_pin_interrupt_configure_dt(
		&irq_gpio, enable ? GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_DISABLE);
}

void dw3720_irq_clear(void)
{
	k_sem_reset(&irq_sem);
}

int dw3720_wait_event(k_timeout_t timeout)
{
	int active;

	if (!gpio_initialized) {
		return -EACCES;
	}
	active = gpio_pin_get_dt(&irq_gpio);
	if (active < 0) {
		return active;
	}
	if (active != 0) {
		return 0;
	}
	return k_sem_take(&irq_sem, timeout);
}

void dw3720_wake_owner(void)
{
	k_sem_give(&irq_sem);
}

uint32_t dw3720_last_irq_cycles(void)
{
	return last_irq_cycles;
}

#else /* !DT_NODE_EXISTS(UWB_NODE) */

/* Stubs when UWB radio node is not in devicetree (e.g., DK build) */

int dw3720_spi_init(void)
{
	printk("DW3720: No UWB node in devicetree\n");
	return -ENOTSUP;
}

int dw3720_hw_reset(void)
{
	return -ENOTSUP;
}

int dw3720_read_dev_id(uint32_t *dev_id)
{
	if (dev_id) {
		*dev_id = 0;
	}
	return -ENOTSUP;
}

const char *dw3720_dev_id_str(uint32_t id)
{
	return "Unknown";
}

int32_t dw3720_readfromspi(uint16_t headerLength, uint8_t *headerBuffer,
			    uint16_t readlength, uint8_t *readBuffer)
{
	return -1;
}

int32_t dw3720_writetospi(uint16_t headerLength, const uint8_t *headerBuffer,
			   uint16_t bodyLength, const uint8_t *bodyBuffer)
{
	return -1;
}

int32_t dw3720_writetospiwithcrc(uint16_t headerLength, const uint8_t *headerBuffer,
				  uint16_t bodyLength, const uint8_t *bodyBuffer,
				  uint8_t crc8)
{
	return -1;
}

void dw3720_setslowrate(void) { }
void dw3720_setfastrate(void) { }
void wakeup_device_with_io(void) { }
int dw3720_irq_enable(bool enable)
{
	ARG_UNUSED(enable);
	return -ENOTSUP;
}
void dw3720_irq_clear(void) { }
int dw3720_wait_event(k_timeout_t timeout)
{
	ARG_UNUSED(timeout);
	return -ENOTSUP;
}
void dw3720_wake_owner(void) { }
uint32_t dw3720_last_irq_cycles(void)
{
	return 0U;
}

#endif /* DT_NODE_EXISTS(UWB_NODE) */

/* =============================================================================
 * Platform functions required by Qorvo driver — always compiled
 * =============================================================================
 */

void deca_sleep(unsigned int time_ms)
{
	k_msleep(time_ms);
}

void deca_usleep(unsigned long time_us)
{
	k_busy_wait((uint32_t)time_us);
}

static K_MUTEX_DEFINE(dw_mutex);

decaIrqStatus_t decamutexon(void)
{
	k_mutex_lock(&dw_mutex, K_FOREVER);
	return 0;
}

void decamutexoff(decaIrqStatus_t s)
{
	ARG_UNUSED(s);
	k_mutex_unlock(&dw_mutex);
}
