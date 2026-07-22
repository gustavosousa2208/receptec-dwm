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

#include <receptec/coletec_sync.h>
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
#define DW3110_SAMPLING_PERIOD_US 1000000U
#define DW3110_SLOT_DURATION_US 20000U
#define DW3110_ATTEMPT_COUNT 3U
#define DW3110_UUS_TO_DTU 65536ULL
#define DW3110_TIMESTAMP_MASK ((1ULL << 40) - 1ULL)
#define DIAGNOSTIC_SAMPLE_OFFSET RECEPTEC_PROTOCOL_EUI64_SIZE
#define DIAGNOSTIC_FIXED_SIZE (RECEPTEC_PROTOCOL_EUI64_SIZE + sizeof(uint32_t))
#define FRAME_SEQUENCE_OFFSET 2U

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
	uint32_t invalid_payload;
	uint32_t duplicate_payload_mismatch;
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
static uint8_t protocol_rx_bytes[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
static uint8_t transaction_payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE];
static size_t payload_mismatch_offset;
static uint8_t payload_mismatch_expected;
static uint8_t payload_mismatch_received;

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

static void encode_u32_le(uint32_t value, uint8_t output[sizeof(uint32_t)])
{
	output[0] = (uint8_t)value;
	output[1] = (uint8_t)(value >> 8);
	output[2] = (uint8_t)(value >> 16);
	output[3] = (uint8_t)(value >> 24);
}

static uint32_t decode_u32_le(const uint8_t input[sizeof(uint32_t)])
{
	return (uint32_t)input[0] |
	       ((uint32_t)input[1] << 8) |
	       ((uint32_t)input[2] << 16) |
	       ((uint32_t)input[3] << 24);
}

static void initialize_diagnostic_payload(
	uint8_t payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE], uint32_t sample_counter)
{
	memcpy(payload, coletec_eui, sizeof(coletec_eui));
	encode_u32_le(sample_counter, &payload[DIAGNOSTIC_SAMPLE_OFFSET]);
	for (size_t i = DIAGNOSTIC_FIXED_SIZE; i <
	     RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE; ++i) {
		payload[i] = (uint8_t)i;
	}
}

static int preload_coletec_data(void)
{
	static uint8_t payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE];
	struct receptec_frame data = {
		.sequence = 0U,
		.type = RECEPTEC_PACKET_DATA,
		.payload = payload,
		.payload_length = sizeof(payload),
	};

	initialize_diagnostic_payload(payload, 1U);
	memcpy(data.destination, receptec_eui, sizeof(receptec_eui));
	memcpy(data.source, coletec_eui, sizeof(coletec_eui));
	return load_protocol_frame(&data);
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

static int64_t update_nominal_slot(uint64_t request_rx_timestamp,
				   uint16_t dt_sampling_us,
				   bool duplicate_request,
				   uint64_t *previous_nominal_slot)
{
	uint64_t next_nominal_slot = (request_rx_timestamp +
		(uint64_t)DW3110_SAMPLING_PERIOD_US * DW3110_UUS_TO_DTU -
		(uint64_t)dt_sampling_us * DW3110_UUS_TO_DTU) &
		DW3110_TIMESTAMP_MASK;
	uint64_t delta;

	if (!duplicate_request) {
		*previous_nominal_slot = next_nominal_slot;
		return 0;
	}
	delta = (next_nominal_slot - *previous_nominal_slot) &
		DW3110_TIMESTAMP_MASK;
	return delta > (DW3110_TIMESTAMP_MASK >> 1) ?
		(int64_t)(delta - (DW3110_TIMESTAMP_MASK + 1ULL)) :
		(int64_t)delta;
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

static int send_delayed_data_request_attempt(
	uint8_t sequence, uint16_t dt_sampling_us, uint32_t cycle_epoch,
	struct protocol_counters *counters)
{
	uint8_t dt_sampling[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE];
	const struct receptec_frame request = {
		.sequence = sequence,
		.destination = { 0 },
		.source = { 0 },
		.type = RECEPTEC_PACKET_DATA_REQ,
		.payload = dt_sampling,
		.payload_length = sizeof(dt_sampling),
	};
	struct receptec_frame addressed_request = request;
	int status;

	memcpy(addressed_request.destination, coletec_eui, sizeof(coletec_eui));
	memcpy(addressed_request.source, receptec_eui, sizeof(receptec_eui));
	receptec_protocol_encode_dt_sampling(dt_sampling_us, dt_sampling);
	status = load_protocol_frame(&addressed_request);
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

static int poll_for_matching_data(
	uint8_t sequence, struct protocol_counters *counters,
	uint32_t *final_radio_status,
	uint8_t frozen_payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE],
	bool *payload_frozen, uint32_t *sample_counter)
{
	struct receptec_frame data;
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
	dwt_readrxdata(protocol_rx_bytes, logical_length, 0U);
	status = receptec_protocol_decode(protocol_rx_bytes, logical_length, &data);
	if (status != RECEPTEC_PROTOCOL_OK) {
		count_codec_rejection(counters, status);
		status = -EBADMSG;
		goto out;
	}
	if (data.type != RECEPTEC_PACKET_DATA) {
		counters->invalid_type++;
		status = -EBADMSG;
		goto out;
	}
	if (memcmp(data.destination, receptec_eui, sizeof(receptec_eui)) != 0 ||
	    memcmp(data.source, coletec_eui, sizeof(coletec_eui)) != 0) {
		counters->invalid_address++;
		status = -EBADMSG;
		goto out;
	}
	if (data.payload_length != RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE ||
	    memcmp(data.payload, coletec_eui, sizeof(coletec_eui)) != 0) {
		counters->invalid_payload++;
		status = -EBADMSG;
		goto out;
	}
	for (size_t i = DIAGNOSTIC_FIXED_SIZE; i < data.payload_length; ++i) {
		if (data.payload[i] != (uint8_t)i) {
			counters->invalid_payload++;
			status = -EBADMSG;
			goto out;
		}
	}
	if (*payload_frozen &&
	    memcmp(data.payload, frozen_payload, data.payload_length) != 0) {
		for (size_t i = 0U; i < data.payload_length; ++i) {
			if (data.payload[i] != frozen_payload[i]) {
				payload_mismatch_offset = i;
				payload_mismatch_expected = frozen_payload[i];
				payload_mismatch_received = data.payload[i];
				break;
			}
		}
		counters->duplicate_payload_mismatch++;
		status = -EBADMSG;
		goto out;
	}
	if (!*payload_frozen) {
		memcpy(frozen_payload, data.payload, data.payload_length);
		*payload_frozen = true;
	}
	*sample_counter = decode_u32_le(
		&data.payload[DIAGNOSTIC_SAMPLE_OFFSET]);
	if (data.sequence != sequence) {
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
	       "rx_errors=%u exhausted=%u late=%u "
	       "invalid=%u/%u/%u/%u/%u/%u duplicate_payload=%u\n",
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
	       counters->invalid_sequence,
	       counters->invalid_payload,
	       counters->duplicate_payload_mismatch);
}

static int run_receptec_data(void)
{
	struct protocol_counters counters = { 0 };
	uint8_t sequence = 0U;
	uint32_t successful_exchanges = 0U;
	uint32_t cycle_epoch;
	bool test_silence_done = false;
	int status = 0;

	printk("Protocol test: RECEPTEC polled DATA_REQ initiator "
	       "with deterministic retries\n");
	status = dw3110_port_irq_enable(false);
	if (status < 0) {
		return status;
	}
	dwt_setinterrupt(0U, 0U, DWT_ENABLE_INT_ONLY);
	k_msleep(250U);
	cycle_epoch = dwt_readsystimestamphi32() +
		      DW3110_REQUEST_SETUP_US * DW3110_UUS_TO_DX_TIME;
	for (;;) {
		uint32_t successful_attempt = 0U;
		uint32_t attempts_run = 0U;
		uint32_t attempt_radio_status[DW3110_ATTEMPT_COUNT] = { 0 };
		uint32_t sample_counter = 0U;
		bool payload_frozen = false;
		int attempt_status[DW3110_ATTEMPT_COUNT] = { 0 };

		if (!test_silence_done && CONFIG_RECEPTEC_TEST_SILENCE_AFTER > 0 &&
		    successful_exchanges >= CONFIG_RECEPTEC_TEST_SILENCE_AFTER) {
			printk("RECEPTEC test silence: slots=%u\n",
			       CONFIG_RECEPTEC_TEST_SILENT_SLOTS);
			dwt_forcetrxoff();
			k_sleep(K_MSEC((uint32_t)CONFIG_RECEPTEC_TEST_SILENT_SLOTS *
					 1000U));
			cycle_epoch += (uint32_t)CONFIG_RECEPTEC_TEST_SILENT_SLOTS *
				       DW3110_SAMPLING_PERIOD_US *
				       DW3110_UUS_TO_DX_TIME;
			test_silence_done = true;
			printk("RECEPTEC test silence complete\n");
		}

		sequence++;
		counters.transactions_started++;

		for (size_t attempt = 0U; attempt < ARRAY_SIZE(attempt_offsets_us);
		     ++attempt) {
			attempts_run = (uint32_t)attempt + 1U;
			attempt_status[attempt] = send_delayed_data_request_attempt(sequence,
					attempt_offsets_us[attempt], cycle_epoch,
					&counters);
			if (attempt_status[attempt] == 0) {
				attempt_status[attempt] = poll_for_matching_data(
					sequence, &counters,
					&attempt_radio_status[attempt], transaction_payload,
					&payload_frozen, &sample_counter);
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
			printk("DATA_REQ attempt failed: seq=%u attempt=%u offset=%u "
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
			printk("DATA_REQ/DATA OK: seq=%u sample=%u bytes=%u "
			       "attempt=%u offset=%u FCS=valid addressed=ok\n",
			       sequence, sample_counter,
			       RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE, successful_attempt,
			       attempt_offsets_us[successful_attempt - 1U]);
			if ((successful_exchanges % 100U) == 0U) {
				printk("M6 qualification progress: %u\n",
				       successful_exchanges);
			}
			if (successful_exchanges == 1000U) {
				printk("M6 qualification checkpoint: 1000 consecutive DATA transactions\n");
			}
		} else {
			counters.retry_exhaustion++;
			successful_exchanges = 0U;
			printk("DATA_REQ/DATA failed: seq=%u error=%d\n", sequence,
			       status);
			printk("M6 qualification reset: consecutive successes=0\n");
			print_protocol_counters(&counters);
			if (counters.duplicate_payload_mismatch > 0U) {
				printk("Last duplicate payload mismatch: offset=%u "
				       "expected=0x%02X received=0x%02X\n",
				       (uint32_t)payload_mismatch_offset,
				       payload_mismatch_expected,
				       payload_mismatch_received);
			}
		}
		if ((counters.transactions_started % 100U) == 0U) {
			print_protocol_counters(&counters);
		}
		cycle_epoch += DW3110_SAMPLING_PERIOD_US *
			       DW3110_UUS_TO_DX_TIME;
	}
	return 0;
}

struct coletec_radio_runtime {
	uint64_t previous_nominal_slot;
	uint32_t next_slot_dx;
	int64_t nominal_slot_delta;
	uint8_t previous_sequence;
	uint8_t fault_drop_count;
	uint8_t fault_bad_response_count;
	bool have_previous_request;
};

static uint32_t remaining_rx_us(uint32_t deadline_dx)
{
	int32_t remaining_dx = (int32_t)(deadline_dx -
					 dwt_readsystimestamphi32());

	if (remaining_dx <= 0) {
		return 0U;
	}
	return (uint32_t)remaining_dx / DW3110_UUS_TO_DX_TIME;
}

static void clear_rx_status(void)
{
	dwt_writesysstatuslo(SYS_STATUS_ALL_RX_GOOD |
			     SYS_STATUS_ALL_RX_ERR |
			     SYS_STATUS_ALL_RX_TO |
			     DWT_INT_TXFRS_BIT_MASK);
}

static int arm_immediate_rx(uint32_t timeout_us)
{
	dwt_forcetrxoff();
	clear_rx_status();
	dwt_setrxtimeout(timeout_us);
	return dwt_rxenable(DWT_START_RX_IMMEDIATE) == DWT_SUCCESS ? 0 : -EIO;
}

static int arm_delayed_rx(uint32_t start_dx, uint32_t timeout_us)
{
	dwt_forcetrxoff();
	clear_rx_status();
	dwt_setrxtimeout(timeout_us);
	dwt_setdelayedtrxtime(start_dx);
	return dwt_rxenable(DWT_START_RX_DELAYED | DWT_IDLE_ON_DLY_ERR) ==
	       DWT_SUCCESS ? 0 : -ETIME;
}

static uint32_t wait_for_rx_event(void)
{
	uint32_t radio_status;

	do {
		radio_status = dwt_readsysstatuslo();
	} while ((radio_status & (DWT_INT_RXFCG_BIT_MASK |
				 SYS_STATUS_ALL_RX_ERR |
				 SYS_STATUS_ALL_RX_TO)) == 0U);
	return radio_status;
}

static uint32_t wait_for_rx_event_until(int64_t end_ms)
{
	uint32_t radio_status;

	do {
		radio_status = dwt_readsysstatuslo();
		if ((radio_status & (DWT_INT_RXFCG_BIT_MASK |
				    SYS_STATUS_ALL_RX_ERR |
				    SYS_STATUS_ALL_RX_TO)) != 0U) {
			return radio_status;
		}
	} while (k_uptime_get() < end_ms);
	return 0U;
}

static int read_coletec_request(uint32_t radio_status,
				 struct receptec_frame *request,
				 uint64_t *request_rx_timestamp,
				 uint16_t *dt_sampling_us)
{
	uint16_t frame_length;
	uint16_t logical_length;
	uint8_t ranging;
	int status;

	if ((radio_status & DWT_INT_RXFCG_BIT_MASK) == 0U) {
		return -EIO;
	}
	frame_length = dwt_getframelength(&ranging);
	if (frame_length < RECEPTEC_PROTOCOL_HEADER_SIZE +
			   RECEPTEC_PROTOCOL_FCS_SIZE ||
	    frame_length > RECEPTEC_PROTOCOL_MAX_PSDU_SIZE) {
		return -EMSGSIZE;
	}
	logical_length = frame_length - RECEPTEC_PROTOCOL_FCS_SIZE;
	dwt_readrxdata(protocol_rx_bytes, logical_length, 0U);
	status = receptec_protocol_decode(protocol_rx_bytes, logical_length, request);
	if (status != RECEPTEC_PROTOCOL_OK ||
	    !frame_is_for(request, coletec_eui, receptec_eui,
			  RECEPTEC_PACKET_DATA_REQ)) {
		return -EBADMSG;
	}
	*dt_sampling_us = receptec_protocol_decode_dt_sampling(request->payload);
	if (!is_valid_attempt_offset(*dt_sampling_us)) {
		return -EBADMSG;
	}
	*request_rx_timestamp = read_rx_timestamp();
	return 0;
}

static void handle_coletec_request(
	struct coletec_radio_runtime *runtime,
	const struct receptec_frame *request, uint64_t request_rx_timestamp,
	uint16_t dt_sampling_us)
{
	uint64_t next_nominal_slot = (request_rx_timestamp +
		(uint64_t)DW3110_SAMPLING_PERIOD_US * DW3110_UUS_TO_DTU -
		(uint64_t)dt_sampling_us * DW3110_UUS_TO_DTU) &
		DW3110_TIMESTAMP_MASK;
	uint64_t response_tx_timestamp;
	uint64_t response_delay_dtu;
	uint64_t scheduled_tx_timestamp;
	uint32_t radio_status;
	uint8_t response_sequence;
	bool duplicate_request = runtime->have_previous_request &&
				 request->sequence == runtime->previous_sequence;

	runtime->nominal_slot_delta = update_nominal_slot(
		request_rx_timestamp, dt_sampling_us, duplicate_request,
		&runtime->previous_nominal_slot);
	runtime->next_slot_dx = (uint32_t)(next_nominal_slot >> 8);
	if (!duplicate_request) {
		runtime->previous_sequence = request->sequence;
		runtime->fault_drop_count = 0U;
		runtime->fault_bad_response_count = 0U;
		runtime->have_previous_request = true;
	}
	if (runtime->fault_drop_count < CONFIG_RECEPTEC_TEST_DROP_ACKS) {
		runtime->fault_drop_count++;
		return;
	}

	response_sequence = request->sequence;
	if (runtime->fault_bad_response_count < CONFIG_RECEPTEC_TEST_BAD_ACKS) {
		response_sequence++;
		runtime->fault_bad_response_count++;
	}
	if (dwt_writetxdata(1U, &response_sequence,
			    FRAME_SEQUENCE_OFFSET) != DWT_SUCCESS) {
		return;
	}
	scheduled_tx_timestamp = request_rx_timestamp +
		(uint64_t)DW3110_RESPONSE_DELAY_US * DW3110_UUS_TO_DTU -
		DW3110_TX_ANTENNA_DELAY;
	dwt_setdelayedtrxtime((uint32_t)(scheduled_tx_timestamp >> 8));
	if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
		return;
	}
	do {
		radio_status = dwt_readsysstatuslo();
	} while ((radio_status & DWT_INT_TXFRS_BIT_MASK) == 0U);
	dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
	response_tx_timestamp = read_tx_timestamp();
	response_delay_dtu = (response_tx_timestamp - request_rx_timestamp) &
			     DW3110_TIMESTAMP_MASK;
	if (dt_sampling_us == attempt_offsets_us[DW3110_ATTEMPT_COUNT - 1U]) {
		printk("Final-slot DATA: request-seq=%u response-seq=%u "
		       "sample=1 bytes=%u dt=%u RX-to-TX=%llu us "
		       "duplicate=%u nominal-delta=%lld dtu FCS=valid\n",
		       request->sequence, response_sequence,
		       RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE, dt_sampling_us,
		       (response_delay_dtu + 32768U) / DW3110_UUS_TO_DTU,
		       duplicate_request, runtime->nominal_slot_delta);
	}
}

static bool listen_until_slot_deadline(
	struct coletec_radio_runtime *runtime, struct coletec_sync *sync,
	uint32_t deadline_dx)
{
	bool valid_request_seen = false;

	for (;;) {
		struct receptec_frame request;
		uint64_t request_rx_timestamp;
		uint32_t remaining_us = remaining_rx_us(deadline_dx);
		uint32_t radio_status;
		uint16_t dt_sampling_us;

		if (remaining_us == 0U || arm_immediate_rx(remaining_us) < 0) {
			return valid_request_seen;
		}
		radio_status = wait_for_rx_event();
		if (read_coletec_request(radio_status, &request,
					 &request_rx_timestamp,
					 &dt_sampling_us) == 0) {
			handle_coletec_request(runtime, &request,
					       request_rx_timestamp, dt_sampling_us);
			coletec_sync_accept_request(sync);
			valid_request_seen = true;
		}
	}
}

static bool listen_open_period(struct coletec_radio_runtime *runtime,
			       struct coletec_sync *sync, uint32_t duration_ms)
{
	int64_t end_ms = k_uptime_get() + duration_ms;

	if (arm_immediate_rx(0U) < 0) {
		return false;
	}
	while (k_uptime_get() < end_ms) {
		struct receptec_frame request;
		uint64_t request_rx_timestamp;
		uint32_t radio_status = wait_for_rx_event_until(end_ms);
		uint32_t current_slot_deadline;
		uint16_t dt_sampling_us;

		if (radio_status == 0U) {
			break;
		}
		if (read_coletec_request(radio_status, &request,
					 &request_rx_timestamp,
					 &dt_sampling_us) == 0) {
			handle_coletec_request(runtime, &request,
					       request_rx_timestamp, dt_sampling_us);
			coletec_sync_accept_request(sync);
			current_slot_deadline = runtime->next_slot_dx -
				DW3110_SAMPLING_PERIOD_US * DW3110_UUS_TO_DX_TIME +
				(DW3110_SLOT_DURATION_US + COLETEC_BASE_GUARD_US) *
				DW3110_UUS_TO_DX_TIME;
			(void)listen_until_slot_deadline(runtime, sync,
						 current_slot_deadline);
			return true;
		}
		if (arm_immediate_rx(0U) < 0) {
			break;
		}
	}
	dwt_forcetrxoff();
	return false;
}

static void listen_synchronized_slot(struct coletec_radio_runtime *runtime,
				     struct coletec_sync *sync)
{
	uint32_t guard_us = coletec_sync_guard_us(sync);
	uint32_t window_start = runtime->next_slot_dx -
		guard_us * DW3110_UUS_TO_DX_TIME;
	uint32_t window_deadline = runtime->next_slot_dx +
		(DW3110_SLOT_DURATION_US + guard_us) * DW3110_UUS_TO_DX_TIME;
	uint32_t window_duration = DW3110_SLOT_DURATION_US + 2U * guard_us;
	bool valid_request_seen = false;

	if (arm_delayed_rx(window_start, window_duration) == 0) {
		struct receptec_frame request;
		uint64_t request_rx_timestamp;
		uint32_t radio_status = wait_for_rx_event();
		uint16_t dt_sampling_us;

		if (read_coletec_request(radio_status, &request,
					 &request_rx_timestamp,
					 &dt_sampling_us) == 0) {
			handle_coletec_request(runtime, &request,
					       request_rx_timestamp, dt_sampling_us);
			coletec_sync_accept_request(sync);
			valid_request_seen = true;
		}
		valid_request_seen |= listen_until_slot_deadline(
			runtime, sync, window_deadline);
	}
	if (valid_request_seen) {
		return;
	}
	runtime->next_slot_dx +=
		DW3110_SAMPLING_PERIOD_US * DW3110_UUS_TO_DX_TIME;
	coletec_sync_miss_slot(sync);
	if (sync->state == COLETEC_SYNC_DISCOVERY) {
		printk("COLETEC synchronization lost after 5 missed slots\n");
	} else {
		printk("COLETEC slot missed: count=%u next-guard=%u us\n",
		       sync->missed_slots, coletec_sync_guard_us(sync));
	}
}

static int run_coletec_data(void)
{
	struct coletec_radio_runtime runtime = { 0 };
	struct coletec_sync sync;
	int status;

	printk("Protocol test: COLETEC synchronized DATA responder\n");
	printk("Protocol fault injection: drop responses=%u bad responses=%u\n",
	       CONFIG_RECEPTEC_TEST_DROP_ACKS, CONFIG_RECEPTEC_TEST_BAD_ACKS);
	status = preload_coletec_data();
	if (status < 0) {
		return status;
	}
	status = dw3110_port_irq_enable(false);
	if (status < 0) {
		return status;
	}
	dwt_setinterrupt(0U, 0U, DWT_ENABLE_INT_ONLY);
	coletec_sync_init(&sync);
	printk("COLETEC initial continuous RX: 10000 ms\n");
	if (listen_open_period(&runtime, &sync, COLETEC_INITIAL_RX_MS)) {
		printk("COLETEC synchronized during initial RX\n");
	} else {
		coletec_sync_initial_rx_expired(&sync);
		printk("COLETEC initial RX expired; entering discovery\n");
	}

	for (;;) {
		if (sync.state == COLETEC_SYNC_SYNCHRONIZED) {
			listen_synchronized_slot(&runtime, &sync);
			continue;
		}
		printk("COLETEC discovery ON: 2000 ms cycle=%u multiplier=%u\n",
		       sync.discovery_cycle_count + 1U,
		       sync.discovery_off_multiplier);
		if (listen_open_period(&runtime, &sync,
				       COLETEC_DISCOVERY_ON_MS)) {
			printk("COLETEC discovery reentry: synchronized\n");
			continue;
		}
		coletec_sync_discovery_on_expired(&sync);
		printk("COLETEC discovery OFF: %u ms next-cycle=%u multiplier=%u\n",
		       coletec_sync_discovery_off_ms(&sync),
		       sync.discovery_cycle_count + 1U,
		       sync.discovery_off_multiplier);
		k_sleep(K_MSEC(coletec_sync_discovery_off_ms(&sync)));
	}
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

int dw3110_radio_run_protocol_test(bool is_receptec)
{
	return is_receptec ? run_receptec_data() : run_coletec_data();
}
