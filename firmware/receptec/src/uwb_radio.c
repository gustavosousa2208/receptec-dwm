/* SPDX-License-Identifier: Apache-2.0 */

#include "uwb_radio.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "dw3720_spi.h"
#include "uwb_peer_registry.h"
#include <receptec/uwb_protocol.h>

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

static int load_sync_frame(const uint8_t destination[RECEPTEC_UWB_EUI64_SIZE],
			   uint8_t sequence)
{
	uint8_t dt_sampling[RECEPTEC_UWB_DT_SAMPLING_SIZE] = { 0U, 0U };
	const struct receptec_uwb_frame frame = {
		.sequence = sequence,
		.type = RECEPTEC_UWB_SYNC,
		.payload = dt_sampling,
		.payload_length = sizeof(dt_sampling),
	};
	struct receptec_uwb_frame addressed = frame;
	size_t logical_length;
	uint16_t radio_length;

	memcpy(addressed.destination, destination, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(addressed.source, receptec_eui, RECEPTEC_UWB_EUI64_SIZE);
	if (receptec_uwb_encode(tx_bytes, sizeof(tx_bytes), &addressed,
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
