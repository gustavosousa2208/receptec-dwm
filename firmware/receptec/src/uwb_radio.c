/* SPDX-License-Identifier: Apache-2.0 */

#include "uwb_radio.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "dw3720_spi.h"
#include "uwb_peer_registry.h"
#include <receptec/uwb_protocol.h>
#include <receptec/uwb_test_frames.h>
#include <receptec/uwb_test_payload.h>
#include <receptec/uwb_test_result.h>

#include "deca_device_api.h"
#include "deca_interface.h"
#include "deca_private.h"
#include "dw3720/dw3720_deca_regs.h"

#define UWB_TX_ANTENNA_DELAY 16385U
#define UWB_RX_ANTENNA_DELAY 16385U
#define UWB_RESPONSE_TIMEOUT_US 100000U
#define UWB_RADIO_EVENT_MASK \
	(DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO)
#define UWB_RX_REARM_ERROR_MASK (SYS_STATUS_ALL_RX_ERR & ~SYS_STATUS_ALL_RX_TO)

static const uint8_t receptec_eui[RECEPTEC_UWB_EUI64_SIZE] = {
	0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E,
};

static const uint8_t default_peer_euis[][RECEPTEC_UWB_EUI64_SIZE] = {
	{ 0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40 },
	{ 0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x41 },
};

static const struct dwt_spi_s dw3720_spi_functions = {
	.readfromspi = dw3720_readfromspi,
	.writetospi = dw3720_writetospi,
	.writetospiwithcrc = dw3720_writetospiwithcrc,
	.setslowrate = dw3720_setslowrate,
	.setfastrate = dw3720_setfastrate,
};

extern const struct dwt_driver_s dw3000_driver;
extern const struct dwt_driver_s dw3720_driver;

static const struct dwt_driver_s *const driver_list[] = {
	&dw3000_driver,
	&dw3720_driver,
};

static struct dwt_probe_s probe_interface = {
	.dw = NULL,
	.spi = (void *)&dw3720_spi_functions,
	.wakeup_device_with_io = wakeup_device_with_io,
	.driver_list = (struct dwt_driver_s **)driver_list,
	.dw_driver_num = ARRAY_SIZE(driver_list),
};

static dwt_config_t radio_config = {
	.chan = 9U,
	.txPreambLength = DWT_PLEN_128,
	.rxPAC = DWT_PAC8,
	.txCode = 9U,
	.rxCode = 9U,
	.sfdType = DWT_SFD_DW_8,
	.dataRate = DWT_BR_6M8,
	.phrMode = DWT_PHRMODE_STD,
	.phrRate = DWT_PHRRATE_STD,
	.sfdTO = 129U,
	.stsMode = DWT_STS_MODE_OFF,
	.stsLength = DWT_STS_LEN_64,
	.pdoaMode = DWT_PDOA_M0,
};

static dwt_txconfig_t tx_config = {
	.PGdly = 0x34U,
	.power = 0xfdfdfdfdUL,
	.PGcount = 0U,
};

static bool radio_ready;
static bool poll_running;
static bool peer_registry_ready;
static struct receptec_uwb_peer_registry peer_registry;
static uint8_t tx_bytes[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
static uint8_t rx_bytes[RECEPTEC_UWB_MAX_LOGICAL_SIZE];

K_MUTEX_DEFINE(uwb_peer_mutex);

static uint32_t us_to_rx_timeout(uint32_t us)
{
	return (uint32_t)(((uint64_t)us * 39ULL + 39ULL) / 40ULL);
}

static void clear_radio_events(uint32_t events)
{
	dwt_writesysstatuslo(events);
	dw3720_irq_clear();
}

static int initialize_qorvo_driver(void)
{
	uint32_t poll;
	uint32_t device_id;
	int ret;

	ret = dw3720_spi_init();
	if (ret < 0) {
		return ret;
	}
	dw3720_setslowrate();
	ret = dw3720_hw_reset();
	if (ret < 0) {
		return ret;
	}
	k_msleep(2);

	if (dwt_probe(&probe_interface) != DWT_SUCCESS) {
		printk("QM33120W: Qorvo driver probe failed\n");
		return -ENODEV;
	}
	for (poll = 0U; poll < 1000U; ++poll) {
		if (dwt_checkidlerc() != 0U) {
			break;
		}
		k_busy_wait(10U);
	}
	if (poll == 1000U) {
		printk("QM33120W: IDLE_RC timeout\n");
		return -ETIMEDOUT;
	}
	if (dwt_initialise(DWT_READ_OTP_ALL) != DWT_SUCCESS) {
		printk("QM33120W: OTP/calibration initialization failed\n");
		return -EIO;
	}

	device_id = dwt_readdevid();
	printk("QM33120W: driver=%s device-id=0x%08X part-id=0x%08X\n",
	       dwt_version_string(), device_id, dwt_getpartid());
	if (device_id != DW3720_DEV_ID_QM33120W ||
	    dwt_check_dev_id() != DWT_SUCCESS) {
		return -ENODEV;
	}
	if (dwt_configure(&radio_config) != DWT_SUCCESS) {
		printk("QM33120W: PHY configuration failed\n");
		return -EIO;
	}
	/*
	 * Hand QM33120W GPIO5 to the receive front-end. It powers up as a plain
	 * high-impedance GPIO; on this carrier GPIO5 feeds the QM14068 VCTRL
	 * through a 10k/12.1k divider (Projeto Receptor, sheet 4, net R_VCTRL), so
	 * left alone the LNAs stay off. The COLETEC had the same defect: enabling
	 * it measured +13.8 dB (nrf52-bringup-fw docs/bench.md).
	 */
	dwt_setlnapamode(DWT_LNA_ENABLE);
	{
		uint32_t gpio_mode = dwt_read_reg(GPIO_MODE_ID);

		printk("QM33120W: external LNA %s (GPIO_MODE=0x%08X)\n",
		       (gpio_mode & GPIO_MODE_MSGP5_MODE_BIT_MASK) != 0U ?
			       "enabled, GPIO5=PDoA_SW1" : "NOT enabled",
		       gpio_mode);
	}
	dwt_configuretxrf(&tx_config);
	dwt_settxantennadelay(UWB_TX_ANTENNA_DELAY);
	dwt_setrxantennadelay(UWB_RX_ANTENNA_DELAY);
	dwt_setinterrupt(UWB_RADIO_EVENT_MASK, 0U, DWT_ENABLE_INT_ONLY);
	ret = dw3720_irq_enable(false);
	if (ret < 0) {
		return ret;
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);

	printk("QM33120W PHY ready: Ch9 6.8M PRF64 preamble=128 code=9 STS=off\n");
	return 0;
}

int receptec_uwb_radio_init(void)
{
	size_t i;
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	ret = receptec_uwb_peer_registry_init(&peer_registry, receptec_eui);
	if (ret == 0) {
		for (i = 0U; i < ARRAY_SIZE(default_peer_euis); ++i) {
			ret = receptec_uwb_peer_registry_register(
				&peer_registry, i, default_peer_euis[i]);
			if (ret < 0) {
				break;
			}
		}
	}
	peer_registry_ready = ret == 0;
	k_mutex_unlock(&uwb_peer_mutex);
	if (ret < 0) {
		return ret;
	}

	ret = initialize_qorvo_driver();

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	radio_ready = ret == 0;
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_peer_register(
	uint8_t slot, const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else {
		ret = receptec_uwb_peer_registry_register(&peer_registry, slot,
							  eui64);
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_peer_unregister(uint8_t slot)
{
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	ret = peer_registry_ready ?
		receptec_uwb_peer_registry_unregister(&peer_registry, slot) : -EAGAIN;
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_peer_get(uint8_t slot,
			  uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	ret = peer_registry_ready ?
		receptec_uwb_peer_registry_get(&peer_registry, slot, eui64) : -EAGAIN;
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

static int load_request_frame(enum receptec_uwb_packet_type type,
			      const uint8_t destination[RECEPTEC_UWB_EUI64_SIZE],
			      uint8_t sequence, uint16_t dt_sampling_us)
{
	uint8_t dt_sampling[RECEPTEC_UWB_DT_SAMPLING_SIZE];
	struct receptec_uwb_frame frame = {
		.sequence = sequence,
		.type = type,
		.payload = dt_sampling,
		.payload_length = sizeof(dt_sampling),
	};
	size_t logical_length;
	uint16_t radio_length;

	receptec_uwb_encode_dt_sampling(dt_sampling_us, dt_sampling);
	memcpy(frame.destination, destination, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(frame.source, receptec_eui, RECEPTEC_UWB_EUI64_SIZE);
	if (receptec_uwb_encode(tx_bytes, sizeof(tx_bytes), &frame,
				&logical_length) != RECEPTEC_UWB_OK) {
		return -EINVAL;
	}
	radio_length = (uint16_t)(logical_length + RECEPTEC_UWB_FCS_SIZE);
	if (dwt_writetxdata(radio_length, tx_bytes, 0U) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl(radio_length, 0U, 0U);
	return 0;
}

static int load_sync_frame(const uint8_t destination[RECEPTEC_UWB_EUI64_SIZE],
			   uint8_t sequence)
{
	return load_request_frame(RECEPTEC_UWB_SYNC, destination, sequence, 0U);
}

static int matching_ack_received(
	const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint8_t sequence)
{
	uint16_t radio_length;
	uint8_t ranging;
	struct receptec_uwb_frame frame;
	int ret;

	radio_length = dwt_getframelength(&ranging);
	if (radio_length != RECEPTEC_UWB_HEADER_SIZE + RECEPTEC_UWB_FCS_SIZE) {
		return -EMSGSIZE;
	}
	dwt_readrxdata(rx_bytes, radio_length - RECEPTEC_UWB_FCS_SIZE, 0U);
	ret = receptec_uwb_decode(rx_bytes,
				  radio_length - RECEPTEC_UWB_FCS_SIZE, &frame);
	if (ret != RECEPTEC_UWB_OK ||
	    !receptec_uwb_ack_matches(&frame, peer_eui64, receptec_eui,
				      sequence)) {
		return -EBADMSG;
	}
	return 0;
}

static int await_matching_ack(
	const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint8_t sequence)
{
	int64_t deadline = k_uptime_get() + (UWB_RESPONSE_TIMEOUT_US / 1000U);

	while (true) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t status;
		int ret;

		if (remaining_ms <= 0) {
			return -ETIMEDOUT;
		}
		ret = dw3720_wait_event(K_MSEC(remaining_ms));
		status = dwt_readsysstatuslo();
		if ((status & DWT_INT_RXFCG_BIT_MASK) != 0U) {
			clear_radio_events(DWT_INT_RXFCG_BIT_MASK);
			ret = matching_ack_received(peer_eui64, sequence);
			if (ret == 0) {
				return 0;
			}
			/* OI-07: an unrelated or malformed frame never extends timeout. */
			printk("SYNC response ignored: status=%d radio=0x%08X\n",
			       ret, status);
		} else if ((status & SYS_STATUS_ALL_RX_TO) != 0U) {
			return -ETIMEDOUT;
		} else if ((status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			printk("SYNC receive error: radio=0x%08X; continuing\n", status);
			clear_radio_events(status & UWB_RX_REARM_ERROR_MASK);
		} else if (ret < 0) {
			return -ETIMEDOUT;
		}

		remaining_ms = deadline - k_uptime_get();
		if (remaining_ms <= 0) {
			return -ETIMEDOUT;
		}
		dwt_setrxtimeout(us_to_rx_timeout((uint32_t)remaining_ms * 1000U));
		if (dwt_rxenable(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
			return -EIO;
		}
	}
}

int receptec_uwb_radio_send_sync(uint8_t peer_slot)
{
	uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE];
	uint8_t sequence;
	int status;
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready) {
		ret = -EAGAIN;
		goto unlock;
	}
	if (poll_running) {
		/* The poll thread owns the radio; stop it with `uwb poll stop`. */
		ret = -EBUSY;
		goto unlock;
	}
	ret = receptec_uwb_peer_registry_get(&peer_registry, peer_slot,
						 peer_eui64);
	if (ret < 0) {
		goto unlock;
	}
	ret = receptec_uwb_peer_registry_next_sequence(&peer_registry,
							      peer_slot, &sequence);
	if (ret < 0) {
		goto unlock;
	}
	ret = load_sync_frame(peer_eui64, sequence);
	if (ret < 0) {
		goto unlock;
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(UWB_RESPONSE_TIMEOUT_US));
	ret = dw3720_irq_enable(true);
	if (ret < 0) {
		goto unlock;
	}
	if (dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED) !=
	    DWT_SUCCESS) {
		(void)dw3720_irq_enable(false);
		ret = -EIO;
		goto unlock;
	}
	status = await_matching_ack(peer_eui64, sequence);
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	(void)dw3720_irq_enable(false);
	if (status == 0) {
		printk("SYNC/ACK OK: seq=%u peer=%u FCS=valid address=valid\n",
		       sequence, peer_slot);
	} else {
		printk("SYNC/ACK failed: seq=%u peer=%u status=%d\n",
		       sequence, peer_slot, status);
	}
	ret = status;

unlock:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

/*
 * Periodic DATA_REQ polling — protocol-v1.0 §2.3, §4.2 and §4.3.
 *
 * One poll thread walks the registered peers once per T_sampling. Peer N owns
 * the slot that starts N × T_slot after the cycle epoch. Inside a slot each
 * transaction makes up to UWB_POLL_ATTEMPTS attempts on a binary exponential
 * backoff schedule measured from the slot epoch, so a retransmission never
 * moves the next cycle, and every attempt carries its own offset as DT_sampling.
 *
 * The timings are bench values (T_slot 20 ms, T_timeout 3 ms, T_base 5 ms,
 * T_jitter_max 0.5 ms); the specification leaves them TBD (OI-04, OI-05).
 * T_base must exceed T_timeout by more than the DATA_REQ airtime and the
 * per-attempt setup: with T_base 3 ms (equal to T_timeout) attempt 1 was still
 * waiting when attempt 2 was due, so every retry failed to schedule (late) and
 * a lost first attempt was never recovered (by_attempt=N/0/0). OI-01 (response-loss retry) still applies: a COLETEC
 * that shuts its receiver after answering will miss the retry of a lost reply.
 */
#define UWB_POLL_SLOT_US 20000U
#define UWB_POLL_RESPONSE_TIMEOUT_US 3000U
#define UWB_POLL_SETUP_US 5000U
#define UWB_POLL_ATTEMPTS 3U
#define UWB_POLL_BACKOFF_BASE_US 5000U
#define UWB_POLL_JITTER_MAX_US 500U
#define UWB_POLL_OFFSET_MAX_US \
	(UWB_POLL_BACKOFF_BASE_US * ((1U << (UWB_POLL_ATTEMPTS - 1U)) - 1U) + \
	 (UWB_POLL_ATTEMPTS - 1U) * UWB_POLL_JITTER_MAX_US)
#define UWB_POLL_THREAD_STACK_SIZE 3072U
#define UWB_POLL_THREAD_PRIORITY 1
/* Diagnostic DATA payload of the fake COLETEC: own EUI-64, then a u32 counter. */
#define UWB_POLL_DIAG_COUNTER_OFFSET RECEPTEC_UWB_EUI64_SIZE

BUILD_ASSERT(UWB_POLL_OFFSET_MAX_US + UWB_POLL_RESPONSE_TIMEOUT_US <=
	     UWB_POLL_SLOT_US,
	     "the last attempt and its response window must fit inside the slot");

static struct receptec_uwb_poll_status poll_status;
/* Latest DATA payload per peer, for `uwb poll last` and the PC viewer. */
static struct {
	uint8_t payload[RECEPTEC_UWB_MAX_PAYLOAD_SIZE];
	size_t length;
	uint32_t count;
	int64_t uptime_ms;
} poll_last[RECEPTEC_UWB_MAX_PEERS];
static atomic_t poll_stream_enabled = ATOMIC_INIT(0);
static atomic_t poll_stop_requested = ATOMIC_INIT(0);
static K_SEM_DEFINE(poll_start_sem, 0, 1);

/* DW3xxx system time is 499.2 MHz x 128: 1 us = 1248 / 5 units of the high word. */
static uint32_t us_to_dx_time(uint32_t us)
{
	return (uint32_t)(((uint64_t)us * 1248ULL + 2ULL) / 5ULL);
}

static int64_t dx_diff_to_us(int32_t dx)
{
	return ((int64_t)dx * 5LL) / 1248LL;
}

static void draw_backoff_offsets(uint16_t offsets_us[UWB_POLL_ATTEMPTS])
{
	uint32_t offset = 0U;

	offsets_us[0] = 0U;
	for (size_t attempt = 1U; attempt < UWB_POLL_ATTEMPTS; ++attempt) {
		uint32_t wait = UWB_POLL_BACKOFF_BASE_US << (attempt - 1U);
		uint32_t jitter = sys_rand32_get() %
				  (UWB_POLL_JITTER_MAX_US + 1U);

		offset += wait + jitter;
		offsets_us[attempt] = (uint16_t)offset;
	}
}

static uint32_t decode_u32_le(const uint8_t *input)
{
	return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
	       ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static int poll_check_data(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
			   uint8_t slot, uint8_t sequence,
			   struct receptec_uwb_poll_peer_stats *stats)
{
	struct receptec_uwb_frame frame;
	uint16_t radio_length;
	uint16_t logical_length;
	uint8_t ranging;

	radio_length = dwt_getframelength(&ranging);
	if (radio_length < RECEPTEC_UWB_HEADER_SIZE + RECEPTEC_UWB_FCS_SIZE ||
	    radio_length > RECEPTEC_UWB_MAX_PSDU_SIZE) {
		stats->bad_frames++;
		return -EMSGSIZE;
	}
	logical_length = radio_length - RECEPTEC_UWB_FCS_SIZE;
	dwt_readrxdata(rx_bytes, logical_length, 0U);
	if (receptec_uwb_decode(rx_bytes, logical_length, &frame) !=
		    RECEPTEC_UWB_OK ||
	    frame.type != RECEPTEC_UWB_DATA ||
	    frame.sequence != sequence ||
	    memcmp(frame.source, peer_eui64, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
	    memcmp(frame.destination, receptec_eui, RECEPTEC_UWB_EUI64_SIZE) != 0) {
		stats->bad_frames++;
		return -EBADMSG;
	}

	stats->payload_bytes += frame.payload_length;
	memcpy(poll_last[slot].payload, frame.payload, frame.payload_length);
	poll_last[slot].length = frame.payload_length;
	poll_last[slot].count++;
	poll_last[slot].uptime_ms = k_uptime_get();
	if (frame.payload_length >= UWB_POLL_DIAG_COUNTER_OFFSET + 4U &&
	    memcmp(frame.payload, peer_eui64, RECEPTEC_UWB_EUI64_SIZE) == 0) {
		uint32_t sample = decode_u32_le(
			&frame.payload[UWB_POLL_DIAG_COUNTER_OFFSET]);

		if (stats->have_sample && sample > stats->last_sample + 1U) {
			stats->sample_gaps += sample - stats->last_sample - 1U;
		}
		stats->last_sample = sample;
		stats->have_sample = true;
	}
	return 0;
}

/* One DATA_REQ attempt: delayed TX at target_dx, then wait for the DATA frame. */
static int poll_attempt(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
			uint8_t slot, uint8_t sequence, uint32_t target_dx,
			uint16_t dt_us,
			struct receptec_uwb_poll_peer_stats *stats)
{
	/* The delayed TX may be up to a slot ahead; never wait longer than that. */
	int64_t deadline = k_uptime_get() +
			   (int64_t)(UWB_POLL_SLOT_US * 4U / 1000U) + 50;
	int status;

	status = load_request_frame(RECEPTEC_UWB_DATA_REQ, peer_eui64, sequence,
				    dt_us);
	if (status < 0) {
		return status;
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(UWB_POLL_RESPONSE_TIMEOUT_US));
	dwt_setdelayedtrxtime(target_dx);
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) !=
	    DWT_SUCCESS) {
		stats->late_schedules++;
		return -ETIME;
	}
	stats->attempts++;

	for (;;) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t radio_status;

		if (remaining_ms <= 0) {
			dwt_forcetrxoff();
			stats->timeouts++;
			status = -ETIMEDOUT;
			break;
		}
		(void)dw3720_wait_event(K_MSEC(remaining_ms));
		radio_status = dwt_readsysstatuslo();
		if ((radio_status & DWT_INT_RXFCG_BIT_MASK) != 0U) {
			status = poll_check_data(peer_eui64, slot, sequence, stats);
			break;
		}
		if ((radio_status & SYS_STATUS_ALL_RX_TO) != 0U) {
			stats->timeouts++;
			status = -ETIMEDOUT;
			break;
		}
		if ((radio_status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			stats->rx_errors++;
			status = -EIO;
			break;
		}
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	return status;
}

/* One line per received DATA: `DATA slot=N n=COUNT len=L HEX` (see sensor-viewer.py). */
static void print_stream_line(uint8_t slot)
{
	char hex[2U * RECEPTEC_UWB_MAX_PAYLOAD_SIZE + 1U];

	for (size_t i = 0U; i < poll_last[slot].length; ++i) {
		snprintk(&hex[2U * i], 3U, "%02X", poll_last[slot].payload[i]);
	}
	hex[2U * poll_last[slot].length] = '\0';
	printk("DATA slot=%u n=%u len=%u %s\n", slot, poll_last[slot].count,
	       (uint32_t)poll_last[slot].length, hex);
}

/* One transaction: up to UWB_POLL_ATTEMPTS attempts inside the peer's slot. */
static bool poll_transaction(uint8_t slot,
			     const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
			     uint32_t slot_epoch_dx)
{
	struct receptec_uwb_poll_peer_stats *stats = &poll_status.peer[slot];
	uint16_t offsets_us[UWB_POLL_ATTEMPTS];
	uint8_t sequence;

	if (receptec_uwb_peer_registry_next_sequence(&peer_registry, slot,
						      &sequence) < 0) {
		return false;
	}
	stats->transactions++;
	draw_backoff_offsets(offsets_us);
	for (size_t attempt = 0U; attempt < UWB_POLL_ATTEMPTS; ++attempt) {
		int ret = poll_attempt(peer_eui64, slot, sequence,
				       slot_epoch_dx +
					       us_to_dx_time(offsets_us[attempt]),
				       offsets_us[attempt], stats);

		if (ret == 0) {
			stats->completed++;
			stats->completed_on_attempt[attempt]++;
			if (atomic_get(&poll_stream_enabled) != 0) {
				print_stream_line(slot);
			}
			return true;
		}
	}
	stats->exhausted++;
	return false;
}

/*
 * Runtime UWB tests, injected into the poll cycle.
 *
 * The production COLETEC only listens in the rhythm the RECEPTEC sets, so a test cannot
 * be fired at an arbitrary moment: it replaces one transaction, in the target peer's own
 * slot, of a running poll. Each test is three phases on that slot, one transaction per
 * cycle: WARMUP (two ordinary transactions must succeed, proving there is a baseline),
 * RUN (the test itself), VERIFY (one ordinary transaction must succeed, proving the test
 * did not poison the peer). Exactly one UWB_TEST line is printed at the end: PASS, FAIL
 * with a reason, a timeout or an abort. Everything runs on the poll thread, under the
 * radio mutex; the shell only arms a test and returns.
 */
#define UT_WARMUP_OK 2U
#define UT_MAX_WARMUP_CYCLES 12U
#define UT_MAX_CYCLES 40U
#define UT_VERIFY_TRIES 4U
/* Delayed TX lands on the requested time within the radio's own granularity (8 ns);
 * 60 us is generous and still catches a schedule that was really missed. */
#define UT_TX_TOLERANCE_US 60
#define UT_PERIOD_MS 1000U

enum ut_kind {
	UT_MUTATION = 1,
	UT_RETRY,
	UT_DATA_PAYLOAD,
	UT_SEQUENCE_WRAP,
};

enum ut_phase {
	UT_WARMUP = 0,
	UT_RUN,
	UT_VERIFY,
};

static struct {
	bool active;
	enum ut_kind kind;
	const char *name;
	enum receptec_uwb_test_mutation mutation;
	uint8_t ignored; /* valid responses to ignore before accepting one */
	uint8_t slot;
	enum ut_phase phase;
	uint32_t cycles;
	uint8_t warm_ok;
	uint8_t verify_fail;
	uint8_t step;
	uint8_t saved_next_sequence;
	bool restore_sequence;
	atomic_t abort;
	bool have_result;
	struct receptec_uwb_test_result result;
} ut;

struct ut_rx {
	int status; /* 0 frame in rx_bytes, -ETIMEDOUT, -EIO, -EMSGSIZE, -ETIME (late schedule) */
	uint16_t logical_length;
	int32_t tx_offset_us; /* measured TX RMARKER relative to the slot epoch */
	int32_t rx_offset_us; /* measured RX RMARKER relative to the slot epoch */
};

static int32_t ut_abs(int32_t value)
{
	return value < 0 ? -value : value;
}

static void ut_pass(void)
{
	ut.result.outcome = RECEPTEC_UWB_TEST_PASS;
	ut.have_result = true;
}

static void ut_fail(const char *reason)
{
	ut.result.outcome = RECEPTEC_UWB_TEST_FAIL;
	ut.result.reason = reason;
	ut.have_result = true;
}

static void ut_finish(void)
{
	char line[RECEPTEC_UWB_TEST_LINE_SIZE];

	if (ut.restore_sequence) {
		(void)receptec_uwb_peer_registry_set_next_sequence(
			&peer_registry, ut.slot, ut.saved_next_sequence);
		ut.restore_sequence = false;
	}
	if (receptec_uwb_test_result_format(&ut.result, line, sizeof(line)) > 0) {
		printk("%s\n", line);
	} else {
		/* Not a UWB_TEST line on purpose: the runner must see no result. */
		printk("test engine error: result for %s does not format\n", ut.name);
	}
	ut.active = false;
	atomic_set(&poll_stop_requested, 1);
}

/* One delayed DATA_REQ-shaped transmission whose frame is already loaded, then the wait. */
static void ut_attempt(uint32_t target_dx, uint32_t epoch_dx, struct ut_rx *rx)
{
	int64_t deadline = k_uptime_get() + (int64_t)(UWB_POLL_SLOT_US * 4U / 1000U) + 50;

	memset(rx, 0, sizeof(*rx));
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(UWB_POLL_RESPONSE_TIMEOUT_US));
	dwt_setdelayedtrxtime(target_dx);
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
		rx->status = -ETIME;
		return;
	}
	for (;;) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t radio_status;

		if (remaining_ms <= 0) {
			rx->status = -ETIMEDOUT;
			break;
		}
		(void)dw3720_wait_event(K_MSEC(remaining_ms));
		radio_status = dwt_readsysstatuslo();
		if ((radio_status & DWT_INT_RXFCG_BIT_MASK) != 0U) {
			uint8_t ranging;
			uint16_t radio_length = dwt_getframelength(&ranging);

			if (radio_length < RECEPTEC_UWB_FCS_SIZE ||
			    radio_length > RECEPTEC_UWB_MAX_PSDU_SIZE) {
				rx->status = -EMSGSIZE;
			} else {
				rx->logical_length = radio_length - RECEPTEC_UWB_FCS_SIZE;
				dwt_readrxdata(rx_bytes, rx->logical_length, 0U);
				rx->rx_offset_us = (int32_t)dx_diff_to_us(
					(int32_t)(dwt_readrxtimestamphi32() - epoch_dx));
				rx->status = 0;
			}
			break;
		}
		if ((radio_status & SYS_STATUS_ALL_RX_TO) != 0U) {
			rx->status = -ETIMEDOUT;
			break;
		}
		if ((radio_status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			rx->status = -EIO;
			break;
		}
	}
	rx->tx_offset_us = (int32_t)dx_diff_to_us((int32_t)(dwt_readtxtimestamphi32() - epoch_dx));
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
}

/* The received frame must be a DATA from this peer to us with the request's sequence. */
static int ut_check_data(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint8_t sequence,
			 const struct ut_rx *rx, struct receptec_uwb_frame *frame)
{
	if (rx->status != 0) {
		return rx->status;
	}
	if (receptec_uwb_decode(rx_bytes, rx->logical_length, frame) != RECEPTEC_UWB_OK ||
	    frame->type != RECEPTEC_UWB_DATA || frame->sequence != sequence ||
	    memcmp(frame->source, peer_eui64, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
	    memcmp(frame->destination, receptec_eui, RECEPTEC_UWB_EUI64_SIZE) != 0) {
		return -EBADMSG;
	}
	return 0;
}

/*
 * retry, duplicate and data_payload: one transaction, one sequence number, the request
 * repeated on the ordinary backoff schedule after a valid response that is deliberately
 * ignored, as if it had been lost. PASS needs ignored+1 valid responses with the request's
 * sequence, byte-identical payloads, every attempt at its generated offset and no response
 * past the slot end. data_payload also checks the payload v1 layout.
 */
static void ut_run_retry(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint32_t epoch_dx)
{
	static uint8_t payload[UWB_POLL_ATTEMPTS][RECEPTEC_UWB_MAX_PAYLOAD_SIZE];
	size_t payload_length[UWB_POLL_ATTEMPTS] = { 0 };
	uint16_t offsets_us[UWB_POLL_ATTEMPTS];
	size_t needed = (size_t)ut.ignored + 1U;
	size_t valid = 0U;
	int32_t worst_tx_us = 0;
	int32_t latest_rx_us = 0;
	uint8_t sequence;

	if (receptec_uwb_peer_registry_next_sequence(&peer_registry, ut.slot, &sequence) < 0) {
		ut_fail("no-sequence");
		return;
	}
	draw_backoff_offsets(offsets_us);
	for (size_t attempt = 0U; attempt < UWB_POLL_ATTEMPTS && valid < needed; ++attempt) {
		struct receptec_uwb_frame frame;
		struct ut_rx rx;
		int32_t tx_error_us;

		if (load_request_frame(RECEPTEC_UWB_DATA_REQ, peer_eui64, sequence,
				       offsets_us[attempt]) < 0) {
			ut_fail("frame-load");
			return;
		}
		ut_attempt(epoch_dx + us_to_dx_time(offsets_us[attempt]), epoch_dx, &rx);
		if (rx.status == -ETIME) {
			ut_fail("late-schedule");
			return;
		}
		tx_error_us = ut_abs(rx.tx_offset_us - (int32_t)offsets_us[attempt]);
		worst_tx_us = MAX(worst_tx_us, tx_error_us);
		if (tx_error_us > UT_TX_TOLERANCE_US) {
			(void)receptec_uwb_test_result_add_u32(&ut.result, "attempt",
							       (uint32_t)attempt + 1U);
			(void)receptec_uwb_test_result_add_i32(&ut.result, "tx-error-us", tx_error_us);
			ut_fail("attempt-timing");
			return;
		}
		if (rx.status != 0) {
			continue;
		}
		if (ut_check_data(peer_eui64, sequence, &rx, &frame) != 0) {
			ut_fail("bad-response");
			return;
		}
		if (rx.rx_offset_us > (int32_t)UWB_POLL_SLOT_US) {
			ut_fail("late-response");
			return;
		}
		latest_rx_us = MAX(latest_rx_us, rx.rx_offset_us);
		memcpy(payload[valid], frame.payload, frame.payload_length);
		payload_length[valid] = frame.payload_length;
		valid++;
	}
	(void)receptec_uwb_test_result_add_u32(&ut.result, "valid", (uint32_t)valid);
	if (valid < needed) {
		(void)receptec_uwb_test_result_add_u32(&ut.result, "needed", (uint32_t)needed);
		ut_fail("missing-response");
		return;
	}
	for (size_t i = 1U; i < valid; ++i) {
		enum receptec_uwb_payload_duplicate same = receptec_uwb_payload_compare(
			payload[0], payload_length[0], payload[i], payload_length[i]);

		if (same != RECEPTEC_UWB_DUPLICATE_SAME) {
			ut_fail(receptec_uwb_payload_duplicate_reason(same));
			return;
		}
	}
	if (ut.kind == UT_DATA_PAYLOAD) {
		struct receptec_uwb_payload_info info;
		enum receptec_uwb_payload_status status = receptec_uwb_payload_v1_check(
			payload[0], payload_length[0], &info);

		if (status != RECEPTEC_UWB_PAYLOAD_OK) {
			ut_fail(receptec_uwb_payload_reason(status));
			return;
		}
		(void)receptec_uwb_test_result_add_u32(&ut.result, "seq", info.sequence);
		(void)receptec_uwb_test_result_add_u32(&ut.result, "age-ms", info.age_ms);
		(void)receptec_uwb_test_result_add_u32(&ut.result, "flags", info.flags);
	}
	(void)receptec_uwb_test_result_add_i32(&ut.result, "tx-error-us", worst_tx_us);
	(void)receptec_uwb_test_result_add_i32(&ut.result, "last-rx-us", latest_rx_us);
	ut_pass();
}

/* wrong_source, bad_type, bad_reserved, bad_length, bad_dt: no response is the PASS. */
static void ut_run_mutation(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint32_t epoch_dx)
{
	struct ut_rx rx;
	size_t logical_length = 0U;
	uint16_t radio_length;
	uint8_t sequence;

	if (receptec_uwb_peer_registry_next_sequence(&peer_registry, ut.slot, &sequence) < 0) {
		ut_fail("no-sequence");
		return;
	}
	if (receptec_uwb_test_build_mutation(ut.mutation, peer_eui64, receptec_eui, sequence, 0U,
					     tx_bytes, sizeof(tx_bytes), &logical_length) < 0) {
		ut_fail("frame-build");
		return;
	}
	radio_length = (uint16_t)(logical_length + RECEPTEC_UWB_FCS_SIZE);
	if (dwt_writetxdata(radio_length, tx_bytes, 0U) != DWT_SUCCESS) {
		ut_fail("frame-load");
		return;
	}
	dwt_writetxfctrl(radio_length, 0U, 0U);
	ut_attempt(epoch_dx, epoch_dx, &rx);
	(void)receptec_uwb_test_result_add_u32(&ut.result, "length", (uint32_t)logical_length);
	switch (rx.status) {
	case -ETIMEDOUT:
		(void)receptec_uwb_test_result_add_token(&ut.result, "response", "none");
		ut_pass();
		break;
	case 0:
		ut_fail("unexpected-response");
		break;
	case -ETIME:
		ut_fail("late-schedule");
		break;
	default:
		ut_fail("rx-error");
		break;
	}
}

/* sequence_wrap: three ordinary transactions with sequences 254, 255 and 0, one per cycle. */
static void ut_run_wrap(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint32_t epoch_dx)
{
	struct receptec_uwb_frame frame;
	struct ut_rx rx;
	uint8_t sequence = receptec_uwb_test_wrap_sequence(ut.step);

	if (ut.step == 0U) {
		uint8_t next;

		if (receptec_uwb_peer_registry_next_sequence(&peer_registry, ut.slot, &next) < 0) {
			ut_fail("no-sequence");
			return;
		}
		/* next_sequence() consumed `next`; the counter now holds next + 1. */
		ut.saved_next_sequence = (uint8_t)(next + 1U);
		ut.restore_sequence = true;
	}
	(void)receptec_uwb_test_result_add_u32(&ut.result, "step", ut.step);
	if (load_request_frame(RECEPTEC_UWB_DATA_REQ, peer_eui64, sequence, 0U) < 0) {
		ut_fail("frame-load");
		return;
	}
	ut_attempt(epoch_dx, epoch_dx, &rx);
	if (rx.status == -ETIME) {
		ut_fail("late-schedule");
		return;
	}
	if (rx.status != 0) {
		ut_fail("missing-response");
		return;
	}
	if (ut_check_data(peer_eui64, sequence, &rx, &frame) != 0) {
		ut_fail("bad-response");
		return;
	}
	ut.result.field_count = 0U; /* keep only the final step in the PASS line */
	ut.step++;
	if (ut.step == 3U) {
		(void)receptec_uwb_test_result_add_token(&ut.result, "order", "254-255-0");
		ut_pass();
	}
}

/* One cycle of the armed test on its slot. Called by the poll thread, mutex held. */
static void ut_cycle(uint8_t slot, const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
		     uint32_t epoch_dx)
{
	ut.cycles++;
	if (atomic_get(&ut.abort) != 0) {
		ut.result.outcome = RECEPTEC_UWB_TEST_ABORT;
		ut.have_result = true;
		ut_finish();
		return;
	}
	if (ut.cycles > UT_MAX_CYCLES) {
		ut.result.outcome = RECEPTEC_UWB_TEST_TIMEOUT;
		ut.have_result = true;
		ut_finish();
		return;
	}
	switch (ut.phase) {
	case UT_WARMUP:
		ut.warm_ok = poll_transaction(slot, peer_eui64, epoch_dx) ? ut.warm_ok + 1U : 0U;
		if (ut.warm_ok >= UT_WARMUP_OK) {
			ut.phase = UT_RUN;
		} else if (ut.cycles >= UT_MAX_WARMUP_CYCLES) {
			ut_fail("no-baseline");
			ut_finish();
		}
		break;
	case UT_RUN:
		switch (ut.kind) {
		case UT_MUTATION:
			ut_run_mutation(peer_eui64, epoch_dx);
			break;
		case UT_RETRY:
		case UT_DATA_PAYLOAD:
			ut_run_retry(peer_eui64, epoch_dx);
			break;
		case UT_SEQUENCE_WRAP:
			ut_run_wrap(peer_eui64, epoch_dx);
			break;
		default:
			ut_fail("internal");
			break;
		}
		if (ut.have_result) {
			ut.phase = UT_VERIFY;
		}
		break;
	case UT_VERIFY:
		if (poll_transaction(slot, peer_eui64, epoch_dx)) {
			ut_finish();
		} else if (++ut.verify_fail >= UT_VERIFY_TRIES) {
			if (ut.result.outcome == RECEPTEC_UWB_TEST_PASS) {
				ut_fail("recovery-failed");
			}
			ut_finish();
		}
		break;
	}
}

int receptec_uwb_test_start(const char *name, uint8_t arg, uint8_t slot)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	int mutation = -1;
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !peer_registry_ready) {
		ret = -EAGAIN;
	} else if (poll_running || ut.active) {
		ret = -EBUSY;
	} else if (receptec_uwb_peer_registry_get(&peer_registry, slot, eui64) < 0) {
		ret = -ENOENT;
	} else {
		memset(&ut, 0, sizeof(ut));
		ret = 0;
		mutation = receptec_uwb_test_mutation_from_name(name);
		if (mutation >= 0) {
			ut.kind = UT_MUTATION;
			ut.mutation = (enum receptec_uwb_test_mutation)mutation;
			ut.name = receptec_uwb_test_mutation_name(ut.mutation);
		} else if (strcmp(name, "retry") == 0 && (arg == 1U || arg == 2U)) {
			ut.kind = UT_RETRY;
			ut.ignored = arg;
			ut.name = "retry";
		} else if (strcmp(name, "duplicate") == 0) {
			ut.kind = UT_RETRY;
			ut.ignored = 1U;
			ut.name = "duplicate";
		} else if (strcmp(name, "data_payload") == 0) {
			ut.kind = UT_DATA_PAYLOAD;
			ut.ignored = 1U;
			ut.name = "data_payload";
		} else if (strcmp(name, "sequence_wrap") == 0) {
			ut.kind = UT_SEQUENCE_WRAP;
			ut.name = "sequence_wrap";
		} else {
			ret = strcmp(name, "retry") == 0 ? -EINVAL : -ENOTSUP;
		}
		if (ret == 0) {
			ut.slot = slot;
			ut.phase = UT_WARMUP;
			receptec_uwb_test_result_init(&ut.result, ut.name, slot);
			if (ut.kind == UT_RETRY && ut.ignored != 0U) {
				(void)receptec_uwb_test_result_add_u32(&ut.result, "ignored", ut.ignored);
			}
			ut.active = true;
		}
	}
	k_mutex_unlock(&uwb_peer_mutex);
	if (ret != 0) {
		return ret;
	}
	ret = receptec_uwb_poll_start(UT_PERIOD_MS);
	if (ret < 0) {
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		ut.active = false;
		k_mutex_unlock(&uwb_peer_mutex);
	}
	return ret;
}

int receptec_uwb_test_abort(void)
{
	bool active;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	active = ut.active;
	k_mutex_unlock(&uwb_peer_mutex);
	if (!active) {
		return -EINVAL;
	}
	atomic_set(&ut.abort, 1);
	return 0;
}

static void run_poll(uint32_t period_us)
{
	uint32_t epoch_dx;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	(void)dw3720_irq_enable(true);
	epoch_dx = dwt_readsystimestamphi32() + us_to_dx_time(UWB_POLL_SETUP_US);
	poll_status.started_ms = k_uptime_get();
	k_mutex_unlock(&uwb_peer_mutex);

	while (atomic_get(&poll_stop_requested) == 0) {
		int64_t remaining_us;
		uint32_t now_dx;

		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		now_dx = dwt_readsystimestamphi32();
		k_mutex_unlock(&uwb_peer_mutex);
		remaining_us = dx_diff_to_us((int32_t)(epoch_dx - now_dx));
		if (remaining_us < 0) {
			/* The previous cycle overran; restart the schedule from now. */
			k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
			poll_status.overruns++;
			k_mutex_unlock(&uwb_peer_mutex);
			epoch_dx = now_dx + us_to_dx_time(UWB_POLL_SETUP_US);
			continue;
		}
		if (remaining_us > (int64_t)UWB_POLL_SETUP_US + 2000) {
			/* Sleep in short pieces so `uwb poll stop` stays responsive. */
			int64_t sleep_ms = (remaining_us - UWB_POLL_SETUP_US) / 1000;

			k_msleep((int32_t)MIN(sleep_ms, 100));
			continue;
		}

		for (uint8_t slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
			uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE];

			if (atomic_get(&poll_stop_requested) != 0) {
				break;
			}
			k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
			if (receptec_uwb_peer_registry_get(&peer_registry, slot,
							   peer_eui64) == 0) {
				uint32_t slot_epoch_dx = epoch_dx + us_to_dx_time(
					(uint32_t)slot * UWB_POLL_SLOT_US);

				if (!ut.active) {
					(void)poll_transaction(slot, peer_eui64,
							       slot_epoch_dx);
				} else if (slot == ut.slot) {
					ut_cycle(slot, peer_eui64, slot_epoch_dx);
				}
			}
			k_mutex_unlock(&uwb_peer_mutex);
		}
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		poll_status.cycles++;
		k_mutex_unlock(&uwb_peer_mutex);
		epoch_dx += us_to_dx_time(period_us);
	}

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (ut.active) {
		/* `uwb poll stop` (or a stop request) ended the poll under an armed test. */
		if (!ut.have_result) {
			ut.result.outcome = RECEPTEC_UWB_TEST_ABORT;
			ut.have_result = true;
		}
		ut_finish();
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	(void)dw3720_irq_enable(false);
	poll_status.stopped_ms = k_uptime_get();
	poll_running = false;
	k_mutex_unlock(&uwb_peer_mutex);
}

static void poll_thread_entry(void *unused_a, void *unused_b, void *unused_c)
{
	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	for (;;) {
		k_sem_take(&poll_start_sem, K_FOREVER);
		run_poll(poll_status.period_us);
	}
}

K_THREAD_DEFINE(uwb_poll_thread, UWB_POLL_THREAD_STACK_SIZE, poll_thread_entry,
		NULL, NULL, NULL, UWB_POLL_THREAD_PRIORITY, 0, 0);

int receptec_uwb_poll_start(uint32_t period_ms)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	uint32_t period_us = period_ms * 1000U;
	int highest = -1;
	int ret = 0;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !peer_registry_ready) {
		ret = -EAGAIN;
		goto unlock;
	}
	if (poll_running) {
		ret = -EBUSY;
		goto unlock;
	}
	for (uint8_t slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		if (receptec_uwb_peer_registry_get(&peer_registry, slot,
						   eui64) == 0) {
			highest = slot;
		}
	}
	if (highest < 0) {
		ret = -ENOENT;
		goto unlock;
	}
	/* The last registered slot has to end before the next cycle begins. */
	if (period_ms == 0U ||
	    period_us < (uint32_t)(highest + 1) * UWB_POLL_SLOT_US) {
		ret = -EINVAL;
		goto unlock;
	}
	memset(&poll_status, 0, sizeof(poll_status));
	memset(poll_last, 0, sizeof(poll_last));
	poll_status.period_us = period_us;
	atomic_set(&poll_stop_requested, 0);
	poll_running = true;
	k_sem_give(&poll_start_sem);
unlock:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_poll_stop(void)
{
	atomic_set(&poll_stop_requested, 1);
	for (int i = 0; i < 40; ++i) {
		bool running;

		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		running = poll_running;
		k_mutex_unlock(&uwb_peer_mutex);
		if (!running) {
			return 0;
		}
		k_msleep(50);
	}
	return -ETIMEDOUT;
}

void receptec_uwb_poll_get(struct receptec_uwb_poll_status *out)
{
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	*out = poll_status;
	out->running = poll_running;
	if (poll_running) {
		out->stopped_ms = k_uptime_get();
	}
	k_mutex_unlock(&uwb_peer_mutex);
}

void receptec_uwb_poll_stream(bool enable)
{
	atomic_set(&poll_stream_enabled, enable ? 1 : 0);
}

int receptec_uwb_poll_last(uint8_t slot, uint8_t *payload, size_t capacity,
			   size_t *length, uint32_t *count, uint32_t *age_ms)
{
	int ret = 0;

	if (slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (poll_last[slot].count == 0U) {
		ret = -ENODATA;
	} else if (capacity < poll_last[slot].length) {
		ret = -EMSGSIZE;
	} else {
		memcpy(payload, poll_last[slot].payload, poll_last[slot].length);
		*length = poll_last[slot].length;
		*count = poll_last[slot].count;
		*age_ms = (uint32_t)(k_uptime_get() - poll_last[slot].uptime_ms);
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

/* Bench A/B switch for the external receive LNA (QM33120W GPIO5). */
int receptec_uwb_radio_set_lna(bool enable)
{
	int ret = 0;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready) {
		ret = -EAGAIN;
	} else {
		dwt_setlnapamode(enable ? DWT_LNA_ENABLE : 0);
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}
