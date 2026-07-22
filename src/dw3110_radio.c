/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/dw3110_radio.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <deca_device_api.h>
#include <deca_interface.h>

#include <receptec/dw3110_port.h>
#include <receptec/protocol.h>

#define DW3110_IDLE_RC_POLL_US 10U
#define DW3110_IDLE_RC_POLLS 1000U
#define DW3110_TX_ANTENNA_DELAY 16385U
#define DW3110_RX_ANTENNA_DELAY 16385U
#define DW3110_UUS_TO_DX_TIME 256U
#define DW3110_REQUEST_SETUP_US 2000U
#define DW3110_RESPONSE_DELAY_US 1000U
#define DW3110_RESPONSE_TIMEOUT_US 3000U
#define DW3110_SYNC_PERIOD_MS 1000U
#define DW3110_ATTEMPT_COUNT 3U
#define DW3110_TIMESTAMP_MASK ((1ULL << 40) - 1ULL)

struct protocol_counters {
	uint32_t transactions_started;
	uint32_t transactions_completed;
	uint32_t attempts_transmitted;
	uint32_t response_timeouts;
	uint32_t retry_exhaustion;
	uint32_t invalid_length;
	uint32_t invalid_fcf;
	uint32_t invalid_type;
	uint32_t invalid_address;
	uint32_t invalid_sequence;
	uint32_t radio_rx_errors;
	uint32_t late_schedules;
};

static const uint16_t attempt_offsets_us[DW3110_ATTEMPT_COUNT] = {
	0U, 5000U, 10000U,
};

static const uint8_t receptec_eui[RECEPTEC_PROTOCOL_EUI64_SIZE] = {
	0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E,
};
static const uint8_t coletec_eui[RECEPTEC_PROTOCOL_EUI64_SIZE] = {
	0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40,
};

extern const struct dwt_driver_s dw3000_driver;

static const struct dwt_driver_s *driver_list[] = {
	&dw3000_driver,
};

static struct dwt_probe_s probe = {
	.dw = NULL,
	.spi = NULL,
	.wakeup_device_with_io = dw3110_port_wakeup,
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
	.power = 0xFDFDFDFDUL,
	.PGcount = 0U,
};

static uint64_t timestamp_to_u64(const uint8_t timestamp[5])
{
	uint64_t value = 0U;

	for (int i = 4; i >= 0; --i) {
		value = (value << 8) | timestamp[i];
	}
	return value;
}

static uint64_t read_rx_timestamp(void)
{
	uint8_t timestamp[5];

	dwt_readrxtimestamp(timestamp, DWT_COMPAT_NONE);
	return timestamp_to_u64(timestamp);
}

static uint64_t read_tx_timestamp(void)
{
	uint8_t timestamp[5];

	dwt_readtxtimestamp(timestamp);
	return timestamp_to_u64(timestamp);
}

static int load_protocol_frame(const struct receptec_frame *frame)
{
	uint8_t bytes[RECEPTEC_PROTOCOL_MAX_PSDU_SIZE] = { 0 };
	size_t logical_length;
	uint16_t radio_length;
	int status;

	status = receptec_protocol_encode(bytes,
					RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE,
					frame, &logical_length);
	if (status != RECEPTEC_PROTOCOL_OK) {
		return -EINVAL;
	}

	radio_length = (uint16_t)(logical_length + RECEPTEC_PROTOCOL_FCS_SIZE);
	if (dwt_writetxdata(radio_length, bytes, 0U) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl(radio_length, 0U, 0U);
	return 0;
}

static bool frame_is_for(const struct receptec_frame *frame,
			 const uint8_t destination[RECEPTEC_PROTOCOL_EUI64_SIZE],
			 const uint8_t source[RECEPTEC_PROTOCOL_EUI64_SIZE],
			 enum receptec_packet_type type)
{
	return frame->type == type &&
	       memcmp(frame->destination, destination,
		      RECEPTEC_PROTOCOL_EUI64_SIZE) == 0 &&
	       memcmp(frame->source, source, RECEPTEC_PROTOCOL_EUI64_SIZE) == 0;
}

static int preload_coletec_ack(void)
{
	struct receptec_frame ack = {
		.sequence = 0U,
		.type = RECEPTEC_PACKET_ACK,
		.payload = NULL,
		.payload_length = 0U,
	};

	memcpy(ack.destination, receptec_eui, sizeof(receptec_eui));
	memcpy(ack.source, coletec_eui, sizeof(coletec_eui));
	return load_protocol_frame(&ack);
}

static bool is_valid_attempt_offset(uint16_t dt_sampling_us)
{
	for (size_t i = 0U; i < ARRAY_SIZE(attempt_offsets_us); ++i) {
		if (dt_sampling_us == attempt_offsets_us[i]) {
			return true;
		}
	}
	return false;
}

static void count_codec_rejection(struct protocol_counters *counters,
				  int codec_status)
{
	if (codec_status == RECEPTEC_PROTOCOL_ERR_LENGTH) {
		counters->invalid_length++;
	} else if (codec_status == RECEPTEC_PROTOCOL_ERR_FCF) {
		counters->invalid_fcf++;
	} else {
		counters->invalid_type++;
	}
}

static int send_delayed_sync_attempt(uint8_t sequence,
				     uint16_t dt_sampling_us,
				     uint32_t cycle_epoch,
				     struct protocol_counters *counters)
{
	uint8_t dt_sampling[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE];
	const struct receptec_frame sync = {
		.sequence = sequence,
		.destination = { 0 },
		.source = { 0 },
		.type = RECEPTEC_PACKET_SYNC,
		.payload = dt_sampling,
		.payload_length = sizeof(dt_sampling),
	};
	struct receptec_frame addressed_sync = sync;
	int status;

	memcpy(addressed_sync.destination, coletec_eui, sizeof(coletec_eui));
	memcpy(addressed_sync.source, receptec_eui, sizeof(receptec_eui));
	receptec_protocol_encode_dt_sampling(dt_sampling_us, dt_sampling);
	status = load_protocol_frame(&addressed_sync);
	if (status < 0) {
		return status;
	}

	dwt_forcetrxoff();
	dwt_writesysstatuslo(SYS_STATUS_ALL_RX_GOOD |
			     SYS_STATUS_ALL_RX_ERR |
			     SYS_STATUS_ALL_RX_TO |
			     DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(DW3110_RESPONSE_TIMEOUT_US);
	dwt_setdelayedtrxtime(cycle_epoch +
			     (uint32_t)dt_sampling_us * DW3110_UUS_TO_DX_TIME);
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) !=
	    DWT_SUCCESS) {
		counters->late_schedules++;
		return -ETIME;
	}
	counters->attempts_transmitted++;
	return 0;
}

static int poll_for_matching_ack(uint8_t sequence,
				 struct protocol_counters *counters,
				 uint32_t *final_radio_status)
{
	uint8_t bytes[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
	struct receptec_frame ack;
	uint32_t radio_status;
	uint16_t frame_length;
	uint16_t logical_length;
	uint8_t ranging;
	int status;

	do {
		radio_status = dwt_readsysstatuslo();
	} while ((radio_status & (DWT_INT_RXFCG_BIT_MASK |
				 SYS_STATUS_ALL_RX_ERR |
				 SYS_STATUS_ALL_RX_TO)) == 0U);
	*final_radio_status = radio_status;

	if ((radio_status & DWT_INT_RXFCG_BIT_MASK) == 0U) {
		if ((radio_status & SYS_STATUS_ALL_RX_TO) != 0U) {
			counters->response_timeouts++;
			status = -ETIMEDOUT;
		} else {
			counters->radio_rx_errors++;
			status = -EIO;
		}
		goto out;
	}

	frame_length = dwt_getframelength(&ranging);
	if (frame_length < RECEPTEC_PROTOCOL_HEADER_SIZE +
			   RECEPTEC_PROTOCOL_FCS_SIZE ||
	    frame_length > RECEPTEC_PROTOCOL_MAX_PSDU_SIZE) {
		counters->invalid_length++;
		status = -EMSGSIZE;
		goto out;
	}

	logical_length = frame_length - RECEPTEC_PROTOCOL_FCS_SIZE;
	dwt_readrxdata(bytes, logical_length, 0U);
	status = receptec_protocol_decode(bytes, logical_length, &ack);
	if (status != RECEPTEC_PROTOCOL_OK) {
		count_codec_rejection(counters, status);
		status = -EBADMSG;
		goto out;
	}
	if (ack.type != RECEPTEC_PACKET_ACK) {
		counters->invalid_type++;
		status = -EBADMSG;
		goto out;
	}
	if (memcmp(ack.destination, receptec_eui, sizeof(receptec_eui)) != 0 ||
	    memcmp(ack.source, coletec_eui, sizeof(coletec_eui)) != 0) {
		counters->invalid_address++;
		status = -EBADMSG;
		goto out;
	}
	if (ack.sequence != sequence) {
		counters->invalid_sequence++;
		status = -EBADMSG;
		goto out;
	}
	status = 0;

out:
	dwt_forcetrxoff();
	dwt_writesysstatuslo(SYS_STATUS_ALL_RX_GOOD |
			     SYS_STATUS_ALL_RX_ERR |
			     SYS_STATUS_ALL_RX_TO |
			     DWT_INT_TXFRS_BIT_MASK);
	return status;
}

static void print_protocol_counters(const struct protocol_counters *counters)
{
	printk("Protocol counters: txns=%u/%u attempts=%u timeouts=%u "
	       "rx_errors=%u exhausted=%u late=%u invalid=%u/%u/%u/%u/%u\n",
	       counters->transactions_completed,
	       counters->transactions_started,
	       counters->attempts_transmitted,
	       counters->response_timeouts,
	       counters->radio_rx_errors,
	       counters->retry_exhaustion,
	       counters->late_schedules,
	       counters->invalid_length,
	       counters->invalid_fcf,
	       counters->invalid_type,
	       counters->invalid_address,
	       counters->invalid_sequence);
}

static int run_receptec_sync(void)
{
	struct protocol_counters counters = { 0 };
	uint8_t sequence = 0U;
	uint32_t successful_exchanges = 0U;
	int status = 0;

	printk("Protocol test: RECEPTEC polled SYNC initiator "
	       "with deterministic retries\n");
	status = dw3110_port_irq_enable(false);
	if (status < 0) {
		return status;
	}
	dwt_setinterrupt(0U, 0U, DWT_ENABLE_INT_ONLY);
	k_msleep(250U);
	for (;;) {
		uint32_t cycle_epoch;
		uint32_t successful_attempt = 0U;
		uint32_t attempts_run = 0U;
		uint32_t attempt_radio_status[DW3110_ATTEMPT_COUNT] = { 0 };
		int attempt_status[DW3110_ATTEMPT_COUNT] = { 0 };

		sequence++;
		counters.transactions_started++;
		cycle_epoch = dwt_readsystimestamphi32() +
			      DW3110_REQUEST_SETUP_US * DW3110_UUS_TO_DX_TIME;

		for (size_t attempt = 0U; attempt < ARRAY_SIZE(attempt_offsets_us);
		     ++attempt) {
			attempts_run = (uint32_t)attempt + 1U;
			attempt_status[attempt] = send_delayed_sync_attempt(sequence,
					attempt_offsets_us[attempt], cycle_epoch,
					&counters);
			if (attempt_status[attempt] == 0) {
				attempt_status[attempt] = poll_for_matching_ack(
					sequence, &counters,
					&attempt_radio_status[attempt]);
			}
			status = attempt_status[attempt];
			if (attempt_status[attempt] == 0) {
				successful_attempt = (uint32_t)attempt + 1U;
				break;
			}
		}

		/* UART output is intentionally deferred until no retry deadline remains. */
		for (uint32_t attempt = 0U; attempt < attempts_run; ++attempt) {
			if (attempt_status[attempt] == 0) {
				continue;
			}
			printk("SYNC attempt failed: seq=%u attempt=%u offset=%u "
			       "error=%d radio=0x%08X%s\n",
			       sequence, attempt + 1U,
			       attempt_offsets_us[attempt], attempt_status[attempt],
			       attempt_radio_status[attempt],
			       attempt + 1U < attempts_run ?
			       " retrying" : " exhausted");
		}

		if (status == 0) {
			counters.transactions_completed++;
			successful_exchanges++;
			printk("SYNC/ACK OK: seq=%u attempt=%u offset=%u "
			       "FCS=valid addressed=ok\n",
			       sequence, successful_attempt,
			       attempt_offsets_us[successful_attempt - 1U]);
			if ((successful_exchanges % 100U) == 0U) {
				printk("M5 qualification progress: %u/1000\n",
				       successful_exchanges < 1000U ?
				       successful_exchanges : 1000U);
			}
			if (successful_exchanges == 1000U) {
				printk("M5 qualification complete: 1000 consecutive SYNC/ACK\n");
			}
		} else {
			counters.retry_exhaustion++;
			successful_exchanges = 0U;
			printk("SYNC/ACK failed: seq=%u error=%d\n", sequence,
			       status);
			printk("M5 qualification reset: consecutive successes=0\n");
		}
		if ((counters.transactions_started % 100U) == 0U) {
			print_protocol_counters(&counters);
		}
		k_msleep(DW3110_SYNC_PERIOD_MS);
	}
	return 0;
}

static int run_coletec_sync(void)
{
	uint8_t bytes[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
	struct receptec_frame request;
	uint64_t request_rx_timestamp;
	uint64_t response_tx_timestamp;
	uint64_t response_delay_dtu;
	uint64_t scheduled_tx_timestamp;
	uint32_t radio_status;
	uint16_t frame_length;
	uint16_t logical_length;
	uint8_t ranging;
	int status;

	printk("Protocol test: COLETEC polled addressed SYNC responder\n");
	status = preload_coletec_ack();
	if (status < 0) {
		return status;
	}
	status = dw3110_port_irq_enable(false);
	if (status < 0) {
		return status;
	}
	dwt_setinterrupt(0U, 0U, DWT_ENABLE_INT_ONLY);
	dwt_setrxtimeout(0U);

	for (;;) {
		dwt_writesysstatuslo(SYS_STATUS_ALL_RX_GOOD |
				     SYS_STATUS_ALL_RX_ERR |
				     SYS_STATUS_ALL_RX_TO |
				     DWT_INT_TXFRS_BIT_MASK);
		if (dwt_rxenable(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
			printk("COLETEC RX enable failed\n");
			continue;
		}

		do {
			radio_status = dwt_readsysstatuslo();
		} while ((radio_status & (DWT_INT_RXFCG_BIT_MASK |
					 SYS_STATUS_ALL_RX_ERR |
					 SYS_STATUS_ALL_RX_TO)) == 0U);

		if ((radio_status & DWT_INT_RXFCG_BIT_MASK) == 0U) {
			printk("COLETEC RX hardware error: 0x%08X\n", radio_status);
			continue;
		}

		frame_length = dwt_getframelength(&ranging);
		if (frame_length < RECEPTEC_PROTOCOL_HEADER_SIZE +
				   RECEPTEC_PROTOCOL_FCS_SIZE ||
		    frame_length > RECEPTEC_PROTOCOL_MAX_PSDU_SIZE) {
			printk("COLETEC invalid PSDU length: %u\n", frame_length);
			continue;
		}
		logical_length = frame_length - RECEPTEC_PROTOCOL_FCS_SIZE;
		dwt_readrxdata(bytes, logical_length, 0U);
		status = receptec_protocol_decode(bytes, logical_length, &request);
		if (status != RECEPTEC_PROTOCOL_OK ||
		    !frame_is_for(&request, coletec_eui, receptec_eui,
				  RECEPTEC_PACKET_SYNC) ||
		    !is_valid_attempt_offset(
			receptec_protocol_decode_dt_sampling(request.payload))) {
			printk("COLETEC rejected request: codec=%d\n", status);
			continue;
		}

		if (dwt_writetxdata(1U, &request.sequence, 2U) != DWT_SUCCESS) {
			printk("COLETEC ACK sequence write failed\n");
			continue;
		}
		request_rx_timestamp = read_rx_timestamp();
		scheduled_tx_timestamp = request_rx_timestamp +
			(uint64_t)DW3110_RESPONSE_DELAY_US * 65536U -
			DW3110_TX_ANTENNA_DELAY;
		dwt_setdelayedtrxtime((uint32_t)(scheduled_tx_timestamp >> 8));
		if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
			printk("ACK delayed TX was scheduled too late\n");
			continue;
		}

		do {
			radio_status = dwt_readsysstatuslo();
		} while ((radio_status & DWT_INT_TXFRS_BIT_MASK) == 0U);
		dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
		response_tx_timestamp = read_tx_timestamp();
		response_delay_dtu =
			(response_tx_timestamp - request_rx_timestamp) &
			DW3110_TIMESTAMP_MASK;
		printk("ACK sent: seq=%u RX-to-TX=%llu us FCS=valid\n",
		       request.sequence,
		       (response_delay_dtu + 32768U) / 65536U);
	}
	return 0;
}

int dw3110_radio_init(void)
{
	uint32_t poll;
	uint32_t device_id;
	int status;

	status = dw3110_port_init();
	if (status < 0) {
		printk("DW3110 port initialization failed: %d\n", status);
		return status;
	}
	status = dw3110_port_reset();
	if (status < 0) {
		printk("DW3110 reset failed: %d\n", status);
		return status;
	}

	probe.spi = (void *)dw3110_port_spi();
	if (dwt_probe(&probe) != DWT_SUCCESS) {
		printk("DW3110 probe failed\n");
		return -ENODEV;
	}
	for (poll = 0U; poll < DW3110_IDLE_RC_POLLS; ++poll) {
		if (dwt_checkidlerc() != 0U) {
			break;
		}
		k_busy_wait(DW3110_IDLE_RC_POLL_US);
	}
	if (poll == DW3110_IDLE_RC_POLLS) {
		printk("DW3110 did not enter IDLE_RC\n");
		return -ETIMEDOUT;
	}
	if (dwt_initialise(DWT_READ_OTP_ALL) != DWT_SUCCESS) {
		printk("DW3110 driver initialization failed\n");
		return -EIO;
	}

	device_id = dwt_readdevid();
	printk("Qorvo driver: %s\n", dwt_version_string());
	printk("DW3110 device ID: 0x%08X\n", device_id);
	printk("DW3110 part ID: 0x%08X\n", dwt_getpartid());
	printk("DW3110 lot ID: 0x%08X%08X\n",
	       (uint32_t)(dwt_getlotid() >> 32), (uint32_t)dwt_getlotid());
	if (dwt_check_dev_id() != DWT_SUCCESS) {
		printk("DW3110 device ID does not match selected driver\n");
		return -ENODEV;
	}
	if (dwt_configure(&radio_config) != DWT_SUCCESS) {
		printk("DW3110 PHY configuration failed\n");
		return -EIO;
	}

	dwt_configuretxrf(&tx_config);
	dwt_settxantennadelay(DW3110_TX_ANTENNA_DELAY);
	dwt_setrxantennadelay(DW3110_RX_ANTENNA_DELAY);
	dwt_setinterrupt(0U, 0U, DWT_ENABLE_INT_ONLY);
	status = dw3110_port_irq_enable(false);
	if (status < 0) {
		printk("DW3110 IRQ enable failed: %d\n", status);
		return status;
	}

	printk("DW3110 PHY ready: ch9 6.8M plen128 code9 STS-off\n");
	return 0;
}

int dw3110_radio_run_sync_test(bool is_receptec)
{
	return is_receptec ? run_receptec_sync() : run_coletec_sync();
}
