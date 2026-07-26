/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/dw3110_port.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>

#include <deca_device_api.h>

#define DW3110_NODE DT_NODELABEL(dw3110)
#define DW3110_SPI_SLOW_HZ 2000000U
#define DW3110_SPI_FAST_HZ 32000000U
#define DW3110_SPI_BUFFER_SIZE 256U
#define DW3110_RESET_ASSERT_US 10U
#define DW3110_RESET_RECOVERY_MS 5U
#define DW3110_WAKE_CS_US 600U

#define DW3110_SPI_OPERATION \
	(SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

static const struct spi_dt_spec dw3110_spi =
	SPI_DT_SPEC_GET(DW3110_NODE, DW3110_SPI_OPERATION);
static const struct gpio_dt_spec dw3110_reset =
	GPIO_DT_SPEC_GET(DW3110_NODE, reset_gpios);
static const struct gpio_dt_spec dw3110_irq =
	GPIO_DT_SPEC_GET(DW3110_NODE, irq_gpios);

static struct spi_config active_spi_config;
static struct gpio_callback irq_callback;
static K_SEM_DEFINE(irq_sem, 0, 1);
static volatile uint32_t last_irq_cycles;
static uint8_t spi_tx_buffer[DW3110_SPI_BUFFER_SIZE];
static uint8_t spi_rx_buffer[DW3110_SPI_BUFFER_SIZE];
static bool port_ready;

static int32_t read_from_spi(uint16_t header_length, uint8_t *header_buffer,
			     uint16_t read_length, uint8_t *read_buffer)
{
	size_t total_length = (size_t)header_length + read_length;
	const struct spi_buf tx_buffer = {
		.buf = spi_tx_buffer,
		.len = total_length,
	};
	const struct spi_buf rx_buffer = {
		.buf = spi_rx_buffer,
		.len = total_length,
	};
	const struct spi_buf_set tx_set = {
		.buffers = &tx_buffer,
		.count = 1U,
	};
	const struct spi_buf_set rx_set = {
		.buffers = &rx_buffer,
		.count = 1U,
	};

	if (header_buffer == NULL || read_buffer == NULL ||
	    total_length > sizeof(spi_tx_buffer)) {
		return DWT_ERROR;
	}

	memcpy(spi_tx_buffer, header_buffer, header_length);
	memset(&spi_tx_buffer[header_length], 0, read_length);

	if (spi_transceive(dw3110_spi.bus, &active_spi_config, &tx_set,
			   &rx_set) < 0) {
		return DWT_ERROR;
	}

	memcpy(read_buffer, &spi_rx_buffer[header_length], read_length);
	return DWT_SUCCESS;
}

static int32_t write_to_spi(uint16_t header_length,
			    const uint8_t *header_buffer,
			    uint16_t body_length,
			    const uint8_t *body_buffer)
{
	const struct spi_buf buffers[] = {
		{
			.buf = (void *)header_buffer,
			.len = header_length,
		},
		{
			.buf = (void *)body_buffer,
			.len = body_length,
		},
	};
	const struct spi_buf_set set = {
		.buffers = buffers,
		.count = body_length == 0U ? 1U : 2U,
	};

	if (header_buffer == NULL || (body_length > 0U && body_buffer == NULL)) {
		return DWT_ERROR;
	}

	return spi_write(dw3110_spi.bus, &active_spi_config, &set) < 0
		       ? DWT_ERROR
		       : DWT_SUCCESS;
}

static int32_t write_to_spi_with_crc(uint16_t header_length,
				     const uint8_t *header_buffer,
				     uint16_t body_length,
				     const uint8_t *body_buffer,
				     uint8_t crc)
{
	const struct spi_buf buffers[] = {
		{
			.buf = (void *)header_buffer,
			.len = header_length,
		},
		{
			.buf = (void *)body_buffer,
			.len = body_length,
		},
		{
			.buf = &crc,
			.len = sizeof(crc),
		},
	};
	const struct spi_buf_set set = {
		.buffers = buffers,
		.count = body_length == 0U ? 2U : 3U,
	};

	if (header_buffer == NULL || (body_length > 0U && body_buffer == NULL)) {
		return DWT_ERROR;
	}

	return spi_write(dw3110_spi.bus, &active_spi_config, &set) < 0
		       ? DWT_ERROR
		       : DWT_SUCCESS;
}

static void set_slow_rate(void)
{
	active_spi_config.frequency = DW3110_SPI_SLOW_HZ;
}

static void set_fast_rate(void)
{
	active_spi_config.frequency = DW3110_SPI_FAST_HZ;
}

static struct dwt_spi_s qorvo_spi = {
	.readfromspi = read_from_spi,
	.writetospi = write_to_spi,
	.writetospiwithcrc = write_to_spi_with_crc,
	.setslowrate = set_slow_rate,
	.setfastrate = set_fast_rate,
};

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

int dw3110_port_init(void)
{
	int status;

	if (!spi_is_ready_dt(&dw3110_spi) ||
	    !gpio_is_ready_dt(&dw3110_reset) ||
	    !gpio_is_ready_dt(&dw3110_irq) ||
	    !gpio_is_ready_dt(&dw3110_spi.config.cs.gpio)) {
		return -ENODEV;
	}

	memcpy(&active_spi_config, &dw3110_spi.config,
	       sizeof(active_spi_config));
	set_slow_rate();

	status = gpio_pin_configure_dt(&dw3110_reset, GPIO_INPUT);
	if (status < 0) {
		return status;
	}

	status = gpio_pin_configure_dt(&dw3110_irq, GPIO_INPUT);
	if (status < 0) {
		return status;
	}

	status = gpio_pin_interrupt_configure_dt(&dw3110_irq,
						 GPIO_INT_DISABLE);
	if (status < 0) {
		return status;
	}

	k_sem_reset(&irq_sem);
	gpio_init_callback(&irq_callback, irq_gpio_handler,
			   BIT(dw3110_irq.pin));
	status = gpio_add_callback(dw3110_irq.port, &irq_callback);
	if (status < 0) {
		return status;
	}

	port_ready = true;
	return 0;
}

int dw3110_port_reset(void)
{
	int status;

	if (!port_ready) {
		return -EACCES;
	}

	status = gpio_pin_configure_dt(&dw3110_reset, GPIO_OUTPUT_ACTIVE);
	if (status < 0) {
		return status;
	}
	k_busy_wait(DW3110_RESET_ASSERT_US);

	status = gpio_pin_configure_dt(&dw3110_reset, GPIO_INPUT);
	if (status < 0) {
		return status;
	}
	k_msleep(DW3110_RESET_RECOVERY_MS);
	set_slow_rate();

	return 0;
}

int dw3110_port_irq_enable(bool enable)
{
	if (!port_ready) {
		return -EACCES;
	}

	return gpio_pin_interrupt_configure_dt(
		&dw3110_irq, enable ? GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_DISABLE);
}

void dw3110_port_irq_clear(void)
{
	k_sem_reset(&irq_sem);
}

int dw3110_port_wait_irq(k_timeout_t timeout)
{
	int irq_active = gpio_pin_get_dt(&dw3110_irq);

	if (irq_active < 0) {
		return irq_active;
	}
	if (irq_active != 0) {
		return 0;
	}
	return k_sem_take(&irq_sem, timeout);
}

uint32_t dw3110_port_last_irq_cycles(void)
{
	return last_irq_cycles;
}

const struct dwt_spi_s *dw3110_port_spi(void)
{
	return &qorvo_spi;
}

void dw3110_port_wakeup(void)
{
	const struct gpio_dt_spec *chip_select =
		&dw3110_spi.config.cs.gpio;

	if (!port_ready) {
		return;
	}

	if (gpio_pin_configure_dt(chip_select, GPIO_OUTPUT_ACTIVE) < 0) {
		return;
	}
	k_busy_wait(DW3110_WAKE_CS_US);
	(void)gpio_pin_set_dt(chip_select, 0);
	k_busy_wait(50U);
}

void deca_sleep(unsigned int time_ms)
{
	k_msleep(time_ms);
}

void deca_usleep(unsigned long time_us)
{
	k_busy_wait((uint32_t)time_us);
}

decaIrqStatus_t decamutexon(void)
{
	return (decaIrqStatus_t)irq_lock();
}

void decamutexoff(decaIrqStatus_t state)
{
	irq_unlock((unsigned int)state);
}
