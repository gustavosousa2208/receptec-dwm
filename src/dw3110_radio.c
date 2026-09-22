/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/dw3110_radio.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/random/random.h>
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
#define DW3110_REQUEST_SETUP_US 2000U
#define DW3110_TEST_SETUP_US 10000U
#define DW3110_RESPONSE_DELAY_US 2000U
#define DW3110_RESPONSE_TIMEOUT_US 3000U
#define DW3110_SAMPLING_PERIOD_US (CONFIG_RECEPTEC_TEST_SAMPLING_PERIOD_MS * 1000U)
#define DW3110_SLOT_DURATION_US 20000U
#define DW3110_OUT_OF_SLOT_PROBE_US 200000U

/*
 * protocol-v1.0 leaves "no retry may extend beyond the slot deadline" as OI-04.
 * The COLETEC keeps RX enabled for its whole slot, so the last attempt plus its
 * response timeout has to land inside DW3110_SLOT_DURATION_US or the tag stops
 * listening mid-schedule and the retries are wasted.
 */
BUILD_ASSERT(RECEPTEC_PROTOCOL_ATTEMPT_OFFSET_MAX_US +
		     DW3110_RESPONSE_TIMEOUT_US <=
	     DW3110_SLOT_DURATION_US,
	     "backoff schedule must finish inside the COLETEC slot");
#define DW3110_ATTEMPT_COUNT RECEPTEC_PROTOCOL_ATTEMPT_COUNT
#define RECEPTEC_PEER_COUNT DW3110_RADIO_PEER_COUNT

/* The four fixed slots have to fit inside one sampling period. */
BUILD_ASSERT(DW3110_SAMPLING_PERIOD_US >=
		     DW3110_RADIO_PEER_COUNT * DW3110_SLOT_DURATION_US,
	     "sampling period must hold every fixed slot");
/* These tests were written for the normal 1 s period. */
BUILD_ASSERT(CONFIG_RECEPTEC_TEST_SILENCE_AFTER == 0 ||
		     CONFIG_RECEPTEC_TEST_SAMPLING_PERIOD_MS == 1000,
	     "the Kconfig silence test assumes a 1 s sampling period");
BUILD_ASSERT(CONFIG_RECEPTEC_TEST_OUT_OF_SLOT_AFTER == 0 ||
		     DW3110_SAMPLING_PERIOD_US > DW3110_OUT_OF_SLOT_PROBE_US,
	     "the out-of-slot probe must land before the next cycle");
#define DW3110_TIMESTAMP_MASK ((1ULL << 40) - 1ULL)
#define DW3110_IRQ_EVENT_MASK \
	(DWT_INT_TXFRS_BIT_MASK | DWT_INT_RXFCG_BIT_MASK | \
	 SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO)
#define DW3110_UWB_THREAD_STACK_SIZE 4096U
#define DW3110_UWB_THREAD_PRIORITY 0
#define DIAGNOSTIC_SAMPLE_OFFSET RECEPTEC_PROTOCOL_EUI64_SIZE
#define DIAGNOSTIC_FIXED_SIZE (RECEPTEC_PROTOCOL_EUI64_SIZE + sizeof(uint32_t))
#define FRAME_SEQUENCE_OFFSET 2U
#define FRAME_TYPE_OFFSET 19U
#define COLETEC_EMPTY_RESPONSE_RADIO_LENGTH \
	(RECEPTEC_PROTOCOL_HEADER_SIZE + RECEPTEC_PROTOCOL_FCS_SIZE)
#define COLETEC_DATA_RADIO_LENGTH \
	(RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE + RECEPTEC_PROTOCOL_FCS_SIZE)

#if defined(CONFIG_RECEPTEC_SLOTTED_AUTOSTART)
#define RECEPTEC_SLOTTED_AUTOSTART 1
#else
#define RECEPTEC_SLOTTED_AUTOSTART 0
#endif

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

struct receptec_peer_runtime {
	const char *name;
	uint8_t eui[RECEPTEC_PROTOCOL_EUI64_SIZE];
	struct protocol_counters counters;
	uint32_t successful_exchanges;
	uint8_t sequence;
	bool expected_present;
	bool diagnostic_payload;
};

enum receptec_command_kind {
	RECEPTEC_COMMAND_SLOTTED_SET,
	RECEPTEC_COMMAND_PERIODIC_SYNC_SET,
	RECEPTEC_COMMAND_DATA_REQ,
	RECEPTEC_COMMAND_TEST_SYNC,
	RECEPTEC_COMMAND_TEST_STOP,
	RECEPTEC_COMMAND_TEST_WRONG_EUI,
	RECEPTEC_COMMAND_TEST_OUT_OF_SLOT,
	RECEPTEC_COMMAND_TEST_SILENCE,
	RECEPTEC_COMMAND_TEST_REJECTION,
	RECEPTEC_COMMAND_TEST_SEQUENCE_WRAP,
	RECEPTEC_COMMAND_TEST_WINDOW,
	RECEPTEC_COMMAND_ABORT,
};

struct receptec_command {
	enum receptec_command_kind kind;
	uint32_t argument;
	uint8_t peer_slot;
	bool enable;
};

#define RECEPTEC_COMMAND_QUEUE_DEPTH 8U

K_MSGQ_DEFINE(receptec_command_queue, sizeof(struct receptec_command),
	      RECEPTEC_COMMAND_QUEUE_DEPTH, sizeof(uint32_t));

static atomic_t control_slotted_running =
	ATOMIC_INIT(RECEPTEC_SLOTTED_AUTOSTART);
static atomic_t control_periodic_sync_running = ATOMIC_INIT(0);
static atomic_t control_periodic_sync_peer_slot = ATOMIC_INIT(0);
static atomic_t control_operation_pending = ATOMIC_INIT(0);
static atomic_t control_operation =
	ATOMIC_INIT(DW3110_RADIO_OPERATION_NONE);
static atomic_t control_cycle = ATOMIC_INIT(0);

/*
 * The backoff schedule is drawn per transaction (protocol-v1.0 §4.2), so there
 * is no fixed offset table any more. Each run_* function builds its own and
 * passes it to whoever prints the result.
 */
static void draw_attempt_offsets(uint16_t offsets_us[DW3110_ATTEMPT_COUNT])
{
	uint32_t random_words[DW3110_ATTEMPT_COUNT];

	for (size_t i = 0U; i < ARRAY_SIZE(random_words); ++i) {
		random_words[i] = sys_rand32_get();
	}
	receptec_protocol_backoff_schedule(random_words, offsets_us);
}

static const uint8_t receptec_eui[RECEPTEC_PROTOCOL_EUI64_SIZE] = {
	0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E,
};
static const uint8_t coletec_eui[RECEPTEC_PROTOCOL_EUI64_SIZE] = {
	0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40,
};

/* DW3xxx time is based on 499.2 MHz: 1 us = 319488 / 5 DTU. */
static uint64_t us_to_dtu(uint64_t us)
{
	return (us * 319488ULL + 2ULL) / 5ULL;
}

static uint32_t us_to_dx_time(uint32_t us)
{
	return (uint32_t)(((uint64_t)us * 1248ULL + 2ULL) / 5ULL);
}

/* Qorvo RX timeout arguments use 512 / 499.2 MHz = 40 / 39 us units. */
static uint32_t us_to_rx_timeout(uint32_t us)
{
	return (uint32_t)(((uint64_t)us * 39ULL + 39ULL) / 40ULL);
}

static uint32_t dx_time_to_us(uint32_t dx_time)
{
	return (uint32_t)(((uint64_t)dx_time * 5ULL) / 1248ULL);
}

static int configure_coletec_frame_filter(void)
{
#if defined(CONFIG_RECEPTEC_COLETEC_HW_FRAME_FILTER)
	uint8_t programmed_eui[RECEPTEC_PROTOCOL_EUI64_SIZE];
	uint8_t readback_eui[RECEPTEC_PROTOCOL_EUI64_SIZE];

	memcpy(programmed_eui, coletec_eui, sizeof(programmed_eui));
	dwt_seteui(programmed_eui);
	dwt_geteui(readback_eui);
	if (memcmp(readback_eui, coletec_eui, sizeof(readback_eui)) != 0) {
		printk("COLETEC hardware frame filter EUI readback failed\n");
		return -EIO;
	}
	dwt_configureframefilter(DWT_FF_ENABLE_802_15_4, DWT_FF_DATA_EN);
	printk("COLETEC hardware frame filter enabled: "
	       "destination=31:5D:02:D5:15:8C:FA:40 type=DATA\n");
#endif
	return 0;
}

static struct receptec_peer_runtime receptec_peers[RECEPTEC_PEER_COUNT] = {
	{
		.name = "dwm-test",
		.eui = { 0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40 },
		.expected_present = true,
		.diagnostic_payload = true,
	},
	{
		.name = "production-coletec",
		.eui = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 },
		.expected_present = true,
		.diagnostic_payload = true,
	},
	{
		.name = "missing-2",
		.eui = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03 },
	},
	{
		.name = "missing-3",
		.eui = { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 },
	},
};
static uint8_t protocol_rx_bytes[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
static uint8_t transaction_payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE];
static size_t payload_mismatch_offset;
static uint8_t payload_mismatch_expected;
static uint8_t payload_mismatch_received;
static uint8_t last_stop_response_type;
static volatile uint32_t coletec_frame_filter_rejections;
static volatile uint32_t coletec_slot_first_event_filter_rejections;
static K_THREAD_STACK_DEFINE(uwb_thread_stack,
			     DW3110_UWB_THREAD_STACK_SIZE);
static struct k_thread uwb_thread;
static bool uwb_protocol_configured;
static bool uwb_thread_started;
static bool uwb_thread_is_receptec;

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

static void clear_radio_events(void)
{
	dwt_writesysstatuslo(DW3110_IRQ_EVENT_MASK);
	dw3110_port_irq_clear();
}

static uint32_t wait_for_radio_event(uint32_t requested_events,
				     k_timeout_t timeout)
{
	for (;;) {
		uint32_t radio_status = dwt_readsysstatuslo();
		uint32_t unrelated_events =
			radio_status & DW3110_IRQ_EVENT_MASK & ~requested_events;

		if ((radio_status & requested_events) != 0U) {
			return radio_status;
		}
		if (unrelated_events != 0U) {
			dwt_writesysstatuslo(unrelated_events);
		}
		if (dw3110_port_wait_irq(timeout) < 0) {
			return 0U;
		}
	}
}

static int enable_protocol_irq(void)
{
	clear_radio_events();
	dwt_setinterrupt(DW3110_IRQ_EVENT_MASK, 0U, DWT_ENABLE_INT_ONLY);
	return dw3110_port_irq_enable(true);
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

static int load_raw_frame(const uint8_t *bytes, size_t logical_length)
{
	uint16_t radio_length = (uint16_t)(logical_length +
					   RECEPTEC_PROTOCOL_FCS_SIZE);

	if (logical_length > RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE ||
	    dwt_writetxdata(radio_length, bytes, 0U) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl(radio_length, 0U, 0U);
	return 0;
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
		.sequence = 1U,
		.type = RECEPTEC_PACKET_DATA,
		.payload = payload,
		.payload_length = sizeof(payload),
	};

	initialize_diagnostic_payload(payload, 1U);
	memcpy(data.destination, receptec_eui, sizeof(receptec_eui));
	memcpy(data.source, coletec_eui, sizeof(coletec_eui));
	return load_protocol_frame(&data);
}

static int set_preloaded_response_sequence(uint8_t sequence)
{
	return dwt_writetxdata(1U, &sequence, FRAME_SEQUENCE_OFFSET) ==
		       DWT_SUCCESS ? 0 : -EIO;
}

static int set_preloaded_response_type(enum receptec_packet_type type)
{
	uint8_t encoded_type = (uint8_t)type;

	return dwt_writetxdata(1U, &encoded_type, FRAME_TYPE_OFFSET) ==
		       DWT_SUCCESS ? 0 : -EIO;
}

static uint8_t next_prepared_response_sequence(
	uint8_t request_sequence, uint8_t response_sequence,
	uint8_t fault_bad_response_count)
{
	if (response_sequence != request_sequence) {
		return fault_bad_response_count < CONFIG_RECEPTEC_TEST_BAD_ACKS ?
			(uint8_t)(request_sequence + 1U) : request_sequence;
	}
	return CONFIG_RECEPTEC_TEST_BAD_ACKS > 0 ?
		(uint8_t)(request_sequence + 2U) :
		(uint8_t)(request_sequence + 1U);
}


static int64_t update_nominal_slot(uint64_t request_rx_timestamp,
				   uint16_t dt_sampling_us,
				   bool duplicate_request,
				   uint64_t *previous_nominal_slot)
{
	uint64_t next_nominal_slot = (request_rx_timestamp +
		us_to_dtu(DW3110_SAMPLING_PERIOD_US) -
		us_to_dtu(dt_sampling_us)) &
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

static int send_delayed_timed_request_attempt(
	enum receptec_packet_type type,
	uint8_t sequence,
	const uint8_t destination[RECEPTEC_PROTOCOL_EUI64_SIZE],
	uint16_t dt_sampling_us, uint32_t slot_epoch,
	struct protocol_counters *counters)
{
	uint8_t dt_sampling[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE];
	const struct receptec_frame request = {
		.sequence = sequence,
		.destination = { 0 },
		.source = { 0 },
		.type = type,
		.payload = dt_sampling,
		.payload_length = sizeof(dt_sampling),
	};
	struct receptec_frame addressed_request = request;
	int status;

	memcpy(addressed_request.destination, destination,
	       RECEPTEC_PROTOCOL_EUI64_SIZE);
	memcpy(addressed_request.source, receptec_eui, sizeof(receptec_eui));
	receptec_protocol_encode_dt_sampling(dt_sampling_us, dt_sampling);
	status = load_protocol_frame(&addressed_request);
	if (status < 0) {
		return status;
	}

	dwt_forcetrxoff();
	clear_radio_events();
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(DW3110_RESPONSE_TIMEOUT_US));
	dwt_setdelayedtrxtime(slot_epoch +
			     us_to_dx_time(dt_sampling_us));
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) !=
	    DWT_SUCCESS) {
		counters->late_schedules++;
		return -ETIME;
	}
	counters->attempts_transmitted++;
	return 0;
}

static int send_delayed_stop_attempt(
				     uint8_t sequence,
				     const uint8_t destination[
					     RECEPTEC_PROTOCOL_EUI64_SIZE],
				     uint16_t attempt_offset_us,
				     uint32_t slot_epoch,
				     struct protocol_counters *counters)
{
	struct receptec_frame stop = {
		.sequence = sequence,
		.type = RECEPTEC_PACKET_STOP,
		.payload = NULL,
		.payload_length = 0U,
	};
	int status;

	memcpy(stop.destination, destination, RECEPTEC_PROTOCOL_EUI64_SIZE);
	memcpy(stop.source, receptec_eui, sizeof(receptec_eui));
	status = load_protocol_frame(&stop);
	if (status < 0) {
		return status;
	}
	dwt_forcetrxoff();
	clear_radio_events();
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(DW3110_RESPONSE_TIMEOUT_US));
	dwt_setdelayedtrxtime(slot_epoch +
			     us_to_dx_time(attempt_offset_us));
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) !=
	    DWT_SUCCESS) {
		counters->late_schedules++;
		return -ETIME;
	}
	counters->attempts_transmitted++;
	return 0;
}

static int poll_for_matching_empty_response(
				      uint8_t sequence,
				      const uint8_t expected_source[
					      RECEPTEC_PROTOCOL_EUI64_SIZE],
				      enum receptec_packet_type expected_type,
				      struct protocol_counters *counters,
				      uint32_t *final_radio_status)
{
	struct receptec_frame stop_ack;
	uint32_t radio_status;
	uint16_t frame_length;
	uint16_t logical_length;
	uint8_t ranging;
	int status;

	radio_status = wait_for_radio_event(DWT_INT_RXFCG_BIT_MASK |
					    SYS_STATUS_ALL_RX_ERR |
					    SYS_STATUS_ALL_RX_TO,
					    K_FOREVER);
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
	if (frame_length != RECEPTEC_PROTOCOL_HEADER_SIZE +
			    RECEPTEC_PROTOCOL_FCS_SIZE) {
		counters->invalid_length++;
		status = -EMSGSIZE;
		goto out;
	}
	logical_length = frame_length - RECEPTEC_PROTOCOL_FCS_SIZE;
	dwt_readrxdata(protocol_rx_bytes, logical_length, 0U);
	status = receptec_protocol_decode(protocol_rx_bytes, logical_length,
					  &stop_ack);
	if (status != RECEPTEC_PROTOCOL_OK) {
		count_codec_rejection(counters, status);
		status = -EBADMSG;
		goto out;
	}
	if (stop_ack.type != expected_type) {
		if (expected_type == RECEPTEC_PACKET_STOP_ACK) {
			last_stop_response_type = (uint8_t)stop_ack.type;
		}
		counters->invalid_type++;
		status = -EBADMSG;
		goto out;
	}
	if (memcmp(stop_ack.destination, receptec_eui, sizeof(receptec_eui)) != 0 ||
	    memcmp(stop_ack.source, expected_source,
		   RECEPTEC_PROTOCOL_EUI64_SIZE) != 0) {
		counters->invalid_address++;
		status = -EBADMSG;
		goto out;
	}
	if (stop_ack.sequence != sequence) {
		counters->invalid_sequence++;
		status = -EBADMSG;
		goto out;
	}
	status = 0;
out:
	dwt_forcetrxoff();
	clear_radio_events();
	return status;
}

static int poll_for_matching_data(
	uint8_t sequence,
	const uint8_t expected_source[RECEPTEC_PROTOCOL_EUI64_SIZE],
	bool diagnostic_payload, struct protocol_counters *counters,
	uint32_t *final_radio_status,
	uint8_t frozen_payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE],
	size_t *frozen_payload_length, bool *payload_frozen,
	uint32_t *sample_counter)
{
	struct receptec_frame data;
	uint32_t radio_status;
	uint16_t frame_length;
	uint16_t logical_length;
	uint8_t ranging;
	int status;

	radio_status = wait_for_radio_event(DWT_INT_RXFCG_BIT_MASK |
					    SYS_STATUS_ALL_RX_ERR |
					    SYS_STATUS_ALL_RX_TO,
					    K_FOREVER);
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
	    memcmp(data.source, expected_source,
		   RECEPTEC_PROTOCOL_EUI64_SIZE) != 0) {
		counters->invalid_address++;
		status = -EBADMSG;
		goto out;
	}
	if (diagnostic_payload &&
	    (data.payload_length != RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE ||
	     memcmp(data.payload, expected_source,
		    RECEPTEC_PROTOCOL_EUI64_SIZE) != 0)) {
		counters->invalid_payload++;
		status = -EBADMSG;
		goto out;
	}
	if (diagnostic_payload) {
		for (size_t i = DIAGNOSTIC_FIXED_SIZE;
		     i < data.payload_length; ++i) {
			if (data.payload[i] != (uint8_t)i) {
				counters->invalid_payload++;
				status = -EBADMSG;
				goto out;
			}
		}
	}
	if (*payload_frozen &&
	    (data.payload_length != *frozen_payload_length ||
	     memcmp(data.payload, frozen_payload, data.payload_length) != 0)) {
		for (size_t i = 0U; i < data.payload_length; ++i) {
			if (i >= *frozen_payload_length) {
				payload_mismatch_offset = i;
				payload_mismatch_expected = 0U;
				payload_mismatch_received = data.payload[i];
				break;
			}
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
		*frozen_payload_length = data.payload_length;
		*payload_frozen = true;
	}
	if (diagnostic_payload) {
		*sample_counter = decode_u32_le(
			&data.payload[DIAGNOSTIC_SAMPLE_OFFSET]);
	}
	if (data.sequence != sequence) {
		counters->invalid_sequence++;
		status = -EBADMSG;
		goto out;
	}
	status = 0;

out:
	dwt_forcetrxoff();
	clear_radio_events();
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

static int run_receptec_stop(struct receptec_peer_runtime *peer,
			     uint32_t slot_epoch)
{
	uint32_t attempt_radio_status[DW3110_ATTEMPT_COUNT] = { 0 };
	int attempt_status[DW3110_ATTEMPT_COUNT] = { 0 };
	uint16_t attempt_offsets_us[DW3110_ATTEMPT_COUNT];
	uint32_t attempts_run = 0U;
	int status = -ETIMEDOUT;

	draw_attempt_offsets(attempt_offsets_us);
	for (size_t attempt = 0U; attempt < ARRAY_SIZE(attempt_offsets_us);
	     ++attempt) {
		attempts_run = (uint32_t)attempt + 1U;
		attempt_status[attempt] = send_delayed_stop_attempt(
			peer->sequence, peer->eui, attempt_offsets_us[attempt],
			slot_epoch, &peer->counters);
		if (attempt_status[attempt] == 0) {
			attempt_status[attempt] =
				poll_for_matching_empty_response(
				peer->sequence, peer->eui,
				RECEPTEC_PACKET_STOP_ACK, &peer->counters,
				&attempt_radio_status[attempt]);
		}
		status = attempt_status[attempt];
		if (status == 0) {
			for (uint32_t failed = 0U; failed < attempt; ++failed) {
				printk("STOP attempt failed: seq=%u attempt=%u "
				       "offset=%u error=%d radio=0x%08X retrying\n",
				       peer->sequence, failed + 1U,
				       attempt_offsets_us[failed],
				       attempt_status[failed],
				       attempt_radio_status[failed]);
			}
			printk("STOP/STOP_ACK OK: seq=%u attempt=%u offset=%u "
			       "FCS=valid addressed=ok\n",
			       peer->sequence, attempt + 1U,
			       attempt_offsets_us[attempt]);
			return 0;
		}
	}
	for (uint32_t attempt = 0U; attempt < attempts_run; ++attempt) {
		printk("STOP attempt failed: seq=%u attempt=%u offset=%u "
		       "error=%d radio=0x%08X%s\n",
		       peer->sequence, attempt + 1U,
		       attempt_offsets_us[attempt],
		       attempt_status[attempt], attempt_radio_status[attempt],
		       attempt + 1U < attempts_run ? " retrying" : " exhausted");
	}
	printk("STOP/STOP_ACK failed: seq=%u error=%d\n",
	       peer->sequence, status);
	return status;
}

static int run_receptec_sync(struct receptec_peer_runtime *peer,
			     uint32_t slot_epoch)
{
	uint32_t attempt_radio_status[DW3110_ATTEMPT_COUNT] = { 0 };
	int attempt_status[DW3110_ATTEMPT_COUNT] = { 0 };
	uint16_t attempt_offsets_us[DW3110_ATTEMPT_COUNT];
	int status = -ETIMEDOUT;

	peer->counters.transactions_started++;
	draw_attempt_offsets(attempt_offsets_us);
	for (size_t attempt = 0U; attempt < ARRAY_SIZE(attempt_offsets_us);
	     ++attempt) {
		attempt_status[attempt] = send_delayed_timed_request_attempt(
			RECEPTEC_PACKET_SYNC, peer->sequence, peer->eui,
			attempt_offsets_us[attempt], slot_epoch,
			&peer->counters);
		if (attempt_status[attempt] == 0) {
			attempt_status[attempt] =
				poll_for_matching_empty_response(
				peer->sequence, peer->eui, RECEPTEC_PACKET_ACK,
				&peer->counters,
				&attempt_radio_status[attempt]);
		}
		status = attempt_status[attempt];
		if (status == 0) {
			for (size_t failed = 0U; failed < attempt; ++failed) {
				printk("SYNC attempt failed: seq=%u attempt=%u "
				       "offset=%u error=%d radio=0x%08X "
				       "retrying\n",
				       peer->sequence, (uint32_t)failed + 1U,
				       attempt_offsets_us[failed],
				       attempt_status[failed],
				       attempt_radio_status[failed]);
			}
			peer->counters.transactions_completed++;
			printk("SYNC/ACK OK: seq=%u attempt=%u offset=%u "
			       "FCS=valid addressed=ok\n",
			       peer->sequence, (uint32_t)attempt + 1U,
			       attempt_offsets_us[attempt]);
			return 0;
		}
	}
	peer->counters.retry_exhaustion++;
	for (size_t attempt = 0U; attempt < ARRAY_SIZE(attempt_offsets_us);
	     ++attempt) {
		printk("SYNC attempt failed: seq=%u attempt=%u offset=%u "
		       "error=%d radio=0x%08X%s\n",
		       peer->sequence, (uint32_t)attempt + 1U,
		       attempt_offsets_us[attempt], attempt_status[attempt],
		       attempt_radio_status[attempt],
		       attempt + 1U < ARRAY_SIZE(attempt_offsets_us) ?
		       " retrying" : " exhausted");
	}
	printk("SYNC/ACK failed: seq=%u error=%d\n", peer->sequence, status);
	return status;
}

struct receptec_slot_result {
	uint32_t attempt_radio_status[DW3110_ATTEMPT_COUNT];
	int attempt_status[DW3110_ATTEMPT_COUNT];
	/* The schedule this transaction drew, so the printer can report it. */
	uint16_t attempt_offsets_us[DW3110_ATTEMPT_COUNT];
	uint32_t sample_counter;
	size_t payload_length;
	uint8_t sequence;
	uint8_t attempts_run;
	uint8_t successful_attempt;
	int status;
};

static void run_receptec_peer_data(
	struct receptec_peer_runtime *peer, uint32_t slot_epoch,
	struct receptec_slot_result *result)
{
	bool payload_frozen = false;

	memset(result, 0, sizeof(*result));
	result->status = -ETIMEDOUT;
	peer->sequence++;
	result->sequence = peer->sequence;
	peer->counters.transactions_started++;
	draw_attempt_offsets(result->attempt_offsets_us);

	for (size_t attempt = 0U;
	     attempt < ARRAY_SIZE(result->attempt_offsets_us); ++attempt) {
		result->attempts_run = (uint8_t)attempt + 1U;
		result->attempt_status[attempt] =
			send_delayed_timed_request_attempt(
				RECEPTEC_PACKET_DATA_REQ,
				peer->sequence, peer->eui,
				result->attempt_offsets_us[attempt], slot_epoch,
				&peer->counters);
		if (result->attempt_status[attempt] == 0) {
			result->attempt_status[attempt] =
				poll_for_matching_data(
					peer->sequence, peer->eui,
					peer->diagnostic_payload,
					&peer->counters,
					&result->attempt_radio_status[attempt],
					transaction_payload,
					&result->payload_length,
					&payload_frozen,
					&result->sample_counter);
		}
		result->status = result->attempt_status[attempt];
		if (result->status == 0) {
			result->successful_attempt = (uint8_t)attempt + 1U;
			peer->counters.transactions_completed++;
			peer->successful_exchanges++;
			return;
		}
	}

	peer->counters.retry_exhaustion++;
	if (peer->expected_present) {
		peer->successful_exchanges = 0U;
	}
}

static void print_receptec_slot_result(
	const struct receptec_peer_runtime *peer, uint32_t cycle,
	uint8_t slot, const struct receptec_slot_result *result)
{
	if (result->status == 0) {
		for (uint8_t attempt = 0U;
		     attempt + 1U < result->successful_attempt; ++attempt) {
			printk("DATA_REQ attempt failed: seq=%u attempt=%u "
			       "offset=%u error=%d radio=0x%08X retrying\n",
			       result->sequence, attempt + 1U,
			       result->attempt_offsets_us[attempt],
			       result->attempt_status[attempt],
			       result->attempt_radio_status[attempt]);
		}
		if (!peer->expected_present) {
			printk("RWD unexpected peer response: cycle=%u slot=%u "
			       "peer=%s seq=%u\n",
			       cycle, slot, peer->name, result->sequence);
			return;
		}
		printk("DATA_REQ/DATA OK: cycle=%u slot=%u peer=%s seq=%u "
		       "sample=%u bytes=%u attempt=%u offset=%u "
		       "FCS=valid addressed=ok\n",
		       cycle, slot, peer->name, result->sequence,
		       result->sample_counter, (uint32_t)result->payload_length,
		       result->successful_attempt,
		       result->attempt_offsets_us[result->successful_attempt - 1U]);
		return;
	}

	if (!peer->expected_present) {
		printk("RWD missing peer: cycle=%u slot=%u peer=%s seq=%u "
		       "attempts=%u result=timeout\n",
		       cycle, slot, peer->name, result->sequence,
		       result->attempts_run);
		return;
	}

	for (uint8_t attempt = 0U; attempt < result->attempts_run; ++attempt) {
		printk("DATA_REQ attempt failed: seq=%u attempt=%u offset=%u "
		       "error=%d radio=0x%08X%s\n",
		       result->sequence, attempt + 1U,
		       result->attempt_offsets_us[attempt],
		       result->attempt_status[attempt],
		       result->attempt_radio_status[attempt],
		       attempt + 1U < result->attempts_run ?
		       " retrying" : " exhausted");
	}
	printk("DATA_REQ/DATA failed: seq=%u error=%d\n",
	       result->sequence, result->status);
	printk("M6 qualification reset: consecutive successes=0\n");
	print_protocol_counters(&peer->counters);
	if (peer->counters.duplicate_payload_mismatch > 0U) {
		printk("Last duplicate payload mismatch: offset=%u "
		       "expected=0x%02X received=0x%02X\n",
		       (uint32_t)payload_mismatch_offset,
		       payload_mismatch_expected,
		       payload_mismatch_received);
	}
}

static void run_out_of_slot_probe(struct receptec_peer_runtime *peer,
				  uint32_t probe_epoch)
{
	struct protocol_counters counters = { 0 };
	uint8_t payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE];
	size_t payload_length = 0U;
	uint32_t radio_status = 0U;
	uint32_t sample_counter = 0U;
	bool payload_frozen = false;
	int status;

	printk("OUT-OF-SLOT probe: peer=%s seq=%u cycle-offset=%u us\n",
	       peer->name, peer->sequence, DW3110_OUT_OF_SLOT_PROBE_US);
	status = send_delayed_timed_request_attempt(
		RECEPTEC_PACKET_DATA_REQ, peer->sequence, peer->eui, 0U,
		probe_epoch, &counters);
	if (status == 0) {
		status = poll_for_matching_data(
			peer->sequence, peer->eui, peer->diagnostic_payload,
			&counters, &radio_status, payload, &payload_length,
			&payload_frozen, &sample_counter);
	}
	if (status == -ETIMEDOUT) {
		printk("OUT-OF-SLOT PASS: no response from peer=%s "
		       "radio=0x%08X\n",
		       peer->name, radio_status);
		return;
	}
	if (status == 0) {
		printk("OUT-OF-SLOT FAIL: peer=%s responded outside its "
		       "assigned slot\n",
		       peer->name);
		return;
	}
	printk("OUT-OF-SLOT INCONCLUSIVE: peer=%s error=%d "
	       "radio=0x%08X\n",
	       peer->name, status, radio_status);
}

struct wrong_destination_result {
	uint8_t destination[RECEPTEC_PROTOCOL_EUI64_SIZE];
	uint8_t sequence;
	uint32_t radio_status;
	int status;
};

static void run_wrong_destination_probe(
	struct receptec_peer_runtime *peer, uint32_t slot_epoch,
	struct wrong_destination_result *result)
{
	struct protocol_counters counters = { 0 };
	uint8_t payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE];
	size_t payload_length = 0U;
	uint32_t sample_counter = 0U;
	bool payload_frozen = false;

	memset(result, 0, sizeof(*result));
	memcpy(result->destination, peer->eui, sizeof(result->destination));
	result->destination[0] ^= 0x80U;
	peer->sequence++;
	result->sequence = peer->sequence;
	result->status = send_delayed_timed_request_attempt(
		RECEPTEC_PACKET_DATA_REQ, result->sequence,
		result->destination, 0U, slot_epoch, &counters);
	if (result->status == 0) {
		result->status = poll_for_matching_data(
			result->sequence, peer->eui, peer->diagnostic_payload,
			&counters, &result->radio_status, payload,
			&payload_length, &payload_frozen, &sample_counter);
	}
}

static void print_wrong_destination_result(
	const struct receptec_peer_runtime *peer, uint8_t slot,
	const struct wrong_destination_result *result)
{
	if (result->status == -ETIMEDOUT) {
		printk("WRONG-EUI PASS: slot=%u peer=%s seq=%u "
		       "destination=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X "
		       "response=none\n",
		       slot, peer->name, result->sequence,
		       result->destination[0], result->destination[1],
		       result->destination[2], result->destination[3],
		       result->destination[4], result->destination[5],
		       result->destination[6], result->destination[7]);
		return;
	}
	if (result->status == 0) {
		printk("WRONG-EUI FAIL: slot=%u peer=%s seq=%u responded\n",
		       slot, peer->name, result->sequence);
		return;
	}
	printk("WRONG-EUI INCONCLUSIVE: slot=%u peer=%s seq=%u error=%d "
	       "radio=0x%08X\n",
	       slot, peer->name, result->sequence, result->status,
	       result->radio_status);
}

static const char *rejection_case_name(enum dw3110_radio_rejection_case test)
{
	switch (test) {
	case DW3110_RADIO_REJECT_WRONG_SOURCE:
		return "wrong-source";
	case DW3110_RADIO_REJECT_WRONG_TYPE:
		return "wrong-type";
	case DW3110_RADIO_REJECT_RESERVED_BITS:
		return "reserved-bits";
	case DW3110_RADIO_REJECT_SHORT_PAYLOAD:
		return "short-payload";
	case DW3110_RADIO_REJECT_DT_SAMPLING:
		return "dt-sampling-range";
	default:
		return "unknown";
	}
}

static void run_rejection_probe(struct receptec_peer_runtime *peer,
				uint32_t slot_epoch,
				enum dw3110_radio_rejection_case test)
{
	uint8_t bytes[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
	uint8_t dt_sampling[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE];
	struct protocol_counters counters = { 0 };
	struct receptec_slot_result baseline;
	struct receptec_slot_result recovery;
	struct receptec_frame request = {
		.type = RECEPTEC_PACKET_DATA_REQ,
		.payload = dt_sampling,
		.payload_length = sizeof(dt_sampling),
	};
	uint8_t payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE];
	size_t payload_length = 0U;
	size_t logical_length = 0U;
	uint32_t radio_status = 0U;
	uint32_t sample_counter = 0U;
	bool payload_frozen = false;
	uint32_t malformed_epoch;
	int status;

	/* Test preparation is intentionally verbose; keep delayed TX in future. */
	slot_epoch += us_to_dx_time(DW3110_TEST_SETUP_US);

	/* Establish the peer's nominal schedule before testing rejection. */
	run_receptec_peer_data(peer, slot_epoch, &baseline);
	if (baseline.status != 0) {
		printk("P-02 %s INCONCLUSIVE: baseline=%d\n",
		       rejection_case_name(test), baseline.status);
		return;
	}
	malformed_epoch = slot_epoch +
		us_to_dx_time(DW3110_SAMPLING_PERIOD_US);
	request.sequence = ++peer->sequence;
	memcpy(request.destination, peer->eui, sizeof(request.destination));
	memcpy(request.source, receptec_eui, sizeof(request.source));
	receptec_protocol_encode_dt_sampling(0U, dt_sampling);
	status = receptec_protocol_encode(bytes, sizeof(bytes), &request,
					  &logical_length);
	if (status != RECEPTEC_PROTOCOL_OK) {
		printk("P-02 %s INCONCLUSIVE: encode=%d\n",
		       rejection_case_name(test), status);
		return;
	}

	switch (test) {
	case DW3110_RADIO_REJECT_WRONG_SOURCE:
		bytes[11] ^= 0x80U;
		break;
	case DW3110_RADIO_REJECT_WRONG_TYPE:
		bytes[FRAME_TYPE_OFFSET] = RECEPTEC_PACKET_DATA;
		break;
	case DW3110_RADIO_REJECT_RESERVED_BITS:
		bytes[FRAME_TYPE_OFFSET] |= 0x10U;
		break;
	case DW3110_RADIO_REJECT_SHORT_PAYLOAD:
		logical_length--;
		break;
	case DW3110_RADIO_REJECT_DT_SAMPLING:
		receptec_protocol_encode_dt_sampling(
			RECEPTEC_PROTOCOL_ATTEMPT_OFFSET_MAX_US + 1U,
			&bytes[RECEPTEC_PROTOCOL_HEADER_SIZE]);
		break;
	default:
		printk("P-02 rejection INCONCLUSIVE: invalid case\n");
		return;
	}

	status = load_raw_frame(bytes, logical_length);
	if (status == 0) {
		dwt_forcetrxoff();
		clear_radio_events();
		dwt_setrxaftertxdelay(0U);
		dwt_setrxtimeout(us_to_rx_timeout(DW3110_RESPONSE_TIMEOUT_US));
		dwt_setdelayedtrxtime(malformed_epoch);
		status = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) ==
			 DWT_SUCCESS ? 0 : -ETIME;
	}
	if (status == 0) {
		status = poll_for_matching_data(
			request.sequence, peer->eui, peer->diagnostic_payload,
			&counters, &radio_status, payload, &payload_length,
			&payload_frozen, &sample_counter);
	}
	if (status != -ETIMEDOUT) {
		printk("P-02 %s FAIL: malformed request status=%d radio=0x%08X\n",
		       rejection_case_name(test), status, radio_status);
		return;
	}

	/* A valid request in the following nominal window must still work. */
	run_receptec_peer_data(peer,
			       slot_epoch +
				       2U * us_to_dx_time(DW3110_SAMPLING_PERIOD_US),
			       &recovery);
	if (recovery.status == 0) {
		printk("P-02 %s PASS: response=none recovery=ok seq=%u\n",
		       rejection_case_name(test), recovery.sequence);
	} else {
		printk("P-02 %s FAIL: response=none recovery=%d\n",
		       rejection_case_name(test), recovery.status);
	}
}

static void run_sequence_wrap_probe(struct receptec_peer_runtime *peer,
				    uint32_t slot_epoch)
{
	static const uint8_t sequence[] = { 254U, 255U, 0U, 1U };
	int status[ARRAY_SIZE(sequence)];

	slot_epoch += us_to_dx_time(DW3110_TEST_SETUP_US);
	for (size_t i = 0U; i < ARRAY_SIZE(sequence); ++i) {
		peer->sequence = sequence[i];
		status[i] = run_receptec_sync(
			peer, slot_epoch +
				      (uint32_t)i *
					      us_to_dx_time(DW3110_SAMPLING_PERIOD_US));
	}

	for (size_t i = 0U; i < ARRAY_SIZE(sequence); ++i) {
		if (status[i] != 0) {
			printk("P-04 FAIL: seq=%u status=%d\n", sequence[i],
			       status[i]);
			return;
		}
	}
	printk("P-04 PASS: sequence=254,255,0 recovery=1 responses=matching\n");
}

static const char *window_case_name(enum dw3110_radio_window_case test)
{
	switch (test) {
	case DW3110_RADIO_WINDOW_BEFORE:
		return "before";
	case DW3110_RADIO_WINDOW_INSIDE:
		return "inside";
	case DW3110_RADIO_WINDOW_END:
		return "end";
	case DW3110_RADIO_WINDOW_AFTER:
		return "after";
	default:
		return "unknown";
	}
}

static int32_t window_case_offset_us(enum dw3110_radio_window_case test)
{
	switch (test) {
	case DW3110_RADIO_WINDOW_BEFORE:
		return -(int32_t)(COLETEC_BASE_GUARD_US + 100U);
	case DW3110_RADIO_WINDOW_INSIDE:
		return 0;
	case DW3110_RADIO_WINDOW_END:
		return (int32_t)(DW3110_SLOT_DURATION_US +
				 COLETEC_BASE_GUARD_US);
	case DW3110_RADIO_WINDOW_AFTER:
		return (int32_t)(DW3110_SLOT_DURATION_US +
				 COLETEC_BASE_GUARD_US + 100U);
	default:
		return 0;
	}
}

static uint32_t add_signed_us_to_dx(uint32_t epoch, int32_t offset_us)
{
	if (offset_us < 0) {
		return epoch - us_to_dx_time((uint32_t)-offset_us);
	}
	return epoch + us_to_dx_time((uint32_t)offset_us);
}

static void run_window_probe(struct receptec_peer_runtime *peer,
			     uint32_t slot_epoch,
			     enum dw3110_radio_window_case test)
{
	struct protocol_counters counters = { 0 };
	struct receptec_slot_result baseline;
	struct receptec_slot_result recovery;
	uint8_t dt_sampling[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE];
	struct receptec_frame request = {
		.type = RECEPTEC_PACKET_DATA_REQ,
		.payload = dt_sampling,
		.payload_length = sizeof(dt_sampling),
	};
	uint8_t payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE];
	size_t payload_length = 0U;
	uint32_t radio_status = 0U;
	uint32_t sample_counter = 0U;
	uint32_t nominal_epoch;
	uint32_t probe_epoch;
	int32_t offset_us = window_case_offset_us(test);
	bool payload_frozen = false;
	bool expect_response = test == DW3110_RADIO_WINDOW_INSIDE;
	int status;

	slot_epoch += us_to_dx_time(DW3110_TEST_SETUP_US);
	run_receptec_peer_data(peer, slot_epoch, &baseline);
	if (baseline.status != 0) {
		printk("P-05 %s INCONCLUSIVE: baseline=%d\n",
		       window_case_name(test), baseline.status);
		return;
	}

	nominal_epoch = slot_epoch + us_to_dx_time(DW3110_SAMPLING_PERIOD_US);
	probe_epoch = add_signed_us_to_dx(nominal_epoch, offset_us);
	request.sequence = ++peer->sequence;
	memcpy(request.destination, peer->eui, sizeof(request.destination));
	memcpy(request.source, receptec_eui, sizeof(request.source));
	receptec_protocol_encode_dt_sampling(0U, dt_sampling);
	status = load_protocol_frame(&request);
	if (status == 0) {
		dwt_forcetrxoff();
		clear_radio_events();
		dwt_setrxaftertxdelay(0U);
		dwt_setrxtimeout(us_to_rx_timeout(DW3110_RESPONSE_TIMEOUT_US));
		dwt_setdelayedtrxtime(probe_epoch);
		status = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) ==
			 DWT_SUCCESS ? 0 : -ETIME;
	}
	if (status == 0) {
		status = poll_for_matching_data(
			request.sequence, peer->eui, peer->diagnostic_payload,
			&counters, &radio_status, payload, &payload_length,
			&payload_frozen, &sample_counter);
	}

	run_receptec_peer_data(peer,
			       slot_epoch +
				       2U * us_to_dx_time(DW3110_SAMPLING_PERIOD_US),
			       &recovery);
	if (((expect_response && status == 0) ||
	     (!expect_response && status == -ETIMEDOUT)) &&
	    recovery.status == 0) {
		printk("P-05 %s PASS: offset=%d us response=%s recovery=ok\n",
		       window_case_name(test), offset_us,
		       expect_response ? "yes" : "none");
		return;
	}
	printk("P-05 %s FAIL: offset=%d us expected=%s status=%d "
	       "radio=0x%08X recovery=%d\n",
	       window_case_name(test), offset_us,
	       expect_response ? "response" : "silence", status,
	       radio_status, recovery.status);
}

static bool wrong_destination_probe_ready(void)
{
	uint32_t selected =
		(uint32_t)CONFIG_RECEPTEC_TEST_WRONG_DESTINATION_PEER_MASK;

	if (CONFIG_RECEPTEC_TEST_WRONG_DESTINATION_AFTER == 0 ||
	    selected == 0U) {
		return false;
	}
	for (uint8_t slot = 0U; slot < RECEPTEC_PEER_COUNT; ++slot) {
		if ((selected & (1U << slot)) == 0U) {
			continue;
		}
		if (!receptec_peers[slot].expected_present ||
		    receptec_peers[slot].successful_exchanges <
			    CONFIG_RECEPTEC_TEST_WRONG_DESTINATION_AFTER) {
			return false;
		}
	}
	return true;
}

struct receptec_control_runtime {
	struct receptec_command action;
	uint32_t silence_remaining;
	bool slotted_running;
	bool periodic_sync_running;
	bool action_ready;
	bool epoch_reset;
	uint8_t periodic_sync_peer_slot;
};

enum receptec_cycle_slot_operation {
	RECEPTEC_SLOT_NONE,
	RECEPTEC_SLOT_DATA,
	RECEPTEC_SLOT_SYNC,
	RECEPTEC_SLOT_STOP,
	RECEPTEC_SLOT_WRONG_EUI,
	RECEPTEC_SLOT_REJECTION,
	RECEPTEC_SLOT_SEQUENCE_WRAP,
	RECEPTEC_SLOT_WINDOW,
};

static bool command_is_action(enum receptec_command_kind kind)
{
	return kind == RECEPTEC_COMMAND_DATA_REQ || kind == RECEPTEC_COMMAND_TEST_SYNC ||
	       kind == RECEPTEC_COMMAND_TEST_STOP || kind == RECEPTEC_COMMAND_TEST_WRONG_EUI ||
	       kind == RECEPTEC_COMMAND_TEST_OUT_OF_SLOT || kind == RECEPTEC_COMMAND_TEST_SILENCE ||
	       kind == RECEPTEC_COMMAND_TEST_REJECTION ||
	       kind == RECEPTEC_COMMAND_TEST_SEQUENCE_WRAP ||
	       kind == RECEPTEC_COMMAND_TEST_WINDOW;
}

static void clear_runtime_operation(struct receptec_control_runtime *control)
{
	control->action_ready = false;
	atomic_set(&control_operation, DW3110_RADIO_OPERATION_NONE);
	atomic_set(&control_operation_pending, 0);
}

static void apply_receptec_command(struct receptec_control_runtime *control,
				   const struct receptec_command *command)
{
	switch (command->kind) {
	case RECEPTEC_COMMAND_SLOTTED_SET:
		if (control->slotted_running != command->enable) {
			control->slotted_running = command->enable;
			control->epoch_reset = true;
		}
		atomic_set(&control_slotted_running, control->slotted_running);
		printk("UWB shell: slotted schedule %s\n",
		       control->slotted_running ? "started" : "stopped");
		break;
	case RECEPTEC_COMMAND_PERIODIC_SYNC_SET:
		if (control->periodic_sync_running != command->enable ||
		    control->periodic_sync_peer_slot != command->peer_slot) {
			control->epoch_reset = true;
		}
		control->periodic_sync_running = command->enable;
		control->periodic_sync_peer_slot = command->peer_slot;
		atomic_set(&control_periodic_sync_running, control->periodic_sync_running);
		atomic_set(&control_periodic_sync_peer_slot, control->periodic_sync_peer_slot);
		if (control->periodic_sync_running) {
			printk("UWB shell: periodic SYNC started on slot=%u "
			       "peer=%s\n",
			       control->periodic_sync_peer_slot,
			       receptec_peers[control->periodic_sync_peer_slot].name);
		} else {
			printk("UWB shell: periodic SYNC stopped\n");
		}
		break;
	case RECEPTEC_COMMAND_ABORT:
		if (control->action_ready || control->silence_remaining > 0U ||
		    atomic_get(&control_operation_pending) != 0) {
			control->silence_remaining = 0U;
			clear_runtime_operation(control);
			control->epoch_reset = true;
			printk("UWB shell: test aborted at a safe boundary\n");
		} else {
			printk("UWB shell: no queued or active test to abort\n");
		}
		break;
	default:
		if (command_is_action(command->kind)) {
			control->action = *command;
			control->action_ready = true;
			control->epoch_reset = true;
		}
		break;
	}
}

static void drain_receptec_commands(struct receptec_control_runtime *control)
{
	struct receptec_command command;

	while (k_msgq_get(&receptec_command_queue, &command, K_NO_WAIT) == 0) {
		apply_receptec_command(control, &command);
	}
}

static void run_runtime_silence_cycle(struct receptec_control_runtime *control)
{
	struct receptec_command command;

	dwt_forcetrxoff();
	if (k_msgq_get(&receptec_command_queue, &command, K_SECONDS(1)) == 0) {
		apply_receptec_command(control, &command);
		drain_receptec_commands(control);
		return;
	}

	if (control->silence_remaining > 0U) {
		control->silence_remaining--;
	}
	if (control->silence_remaining == 0U) {
		printk("SILENCE test complete; normal timing epoch will restart\n");
		clear_runtime_operation(control);
		control->epoch_reset = true;
	} else {
		printk("SILENCE test: %u one-second cycles remaining\n",
		       control->silence_remaining);
	}
}

static int run_receptec_data(void)
{
	struct receptec_peer_runtime *live_peer = &receptec_peers[0];
	struct receptec_peer_runtime *stop_peer =
		&receptec_peers[CONFIG_RECEPTEC_TEST_STOP_PEER_SLOT];
	struct receptec_peer_runtime *sync_peer =
		&receptec_peers[CONFIG_RECEPTEC_TEST_SYNC_PEER_SLOT];
	struct receptec_peer_runtime *out_of_slot_peer =
		&receptec_peers[CONFIG_RECEPTEC_TEST_OUT_OF_SLOT_PEER_SLOT];
	struct receptec_control_runtime control = {
		.slotted_running = RECEPTEC_SLOTTED_AUTOSTART != 0,
		.epoch_reset = true,
	};
	uint32_t cycle_epoch = 0U;
	uint32_t cycle = 0U;
	bool test_silence_done = false;
	bool out_of_slot_probe_done = false;
	bool wrong_destination_probe_done = false;
	int status;

	printk("Protocol test: RECEPTEC RWD schedule peers=%u slot=%u us "
	       "sampling=%u us IRQ-driven\n",
	       RECEPTEC_PEER_COUNT, DW3110_SLOT_DURATION_US, DW3110_SAMPLING_PERIOD_US);
	status = enable_protocol_irq();
	if (status < 0) {
		return status;
	}
	k_msleep(250U);
	for (;;) {
		struct receptec_slot_result results[RECEPTEC_PEER_COUNT];
		struct wrong_destination_result wrong_results[RECEPTEC_PEER_COUNT];
		enum receptec_cycle_slot_operation slot_operations[RECEPTEC_PEER_COUNT] = {
			RECEPTEC_SLOT_NONE,
		};
		bool runtime_action;
		bool wrong_destination_cycle =
			!wrong_destination_probe_done && wrong_destination_probe_ready();

		if (control.silence_remaining > 0U) {
			run_runtime_silence_cycle(&control);
			continue;
		}

		if (!control.slotted_running && !control.periodic_sync_running &&
		    !control.action_ready) {
			struct receptec_command command;

			dwt_forcetrxoff();
			printk("UWB shell: radio idle; waiting for a command\n");
			if (k_msgq_get(&receptec_command_queue, &command, K_FOREVER) == 0) {
				apply_receptec_command(&control, &command);
				drain_receptec_commands(&control);
			}
			continue;
		}

		drain_receptec_commands(&control);
		if (control.action_ready && control.action.kind == RECEPTEC_COMMAND_TEST_SILENCE) {
			control.silence_remaining = control.action.argument;
			control.action_ready = false;
			control.epoch_reset = true;
			printk("SILENCE test started: cycles=%u; all UWB "
			       "requests suppressed\n",
			       control.silence_remaining);
			continue;
		}
		if (!control.slotted_running && !control.periodic_sync_running &&
		    !control.action_ready) {
			control.epoch_reset = true;
			continue;
		}
		if (control.action_ready &&
		    control.action.kind == RECEPTEC_COMMAND_TEST_OUT_OF_SLOT &&
		    !control.slotted_running) {
			printk("OUT-OF-SLOT test rejected: slotted schedule "
			       "is not running\n");
			clear_runtime_operation(&control);
			continue;
		}

		if (control.epoch_reset) {
			cycle_epoch = dwt_readsystimestamphi32() +
				      us_to_dx_time(DW3110_REQUEST_SETUP_US);
			control.epoch_reset = false;
		}
		runtime_action = control.action_ready;
		if (runtime_action) {
			wrong_destination_cycle = false;
		}

		if (CONFIG_RECEPTEC_TEST_SYNC_AFTER > 0 &&
		    sync_peer->successful_exchanges >= CONFIG_RECEPTEC_TEST_SYNC_AFTER) {
			uint32_t sync_slot_epoch =
				cycle_epoch + us_to_dx_time(
					(uint32_t)CONFIG_RECEPTEC_TEST_SYNC_PEER_SLOT *
					DW3110_SLOT_DURATION_US);

			sync_peer->sequence++;
			printk("RECEPTEC initiating SYNC for slot=%u peer=%s "
			       "after %u DATA transactions\n",
			       CONFIG_RECEPTEC_TEST_SYNC_PEER_SLOT, sync_peer->name,
			       sync_peer->successful_exchanges);
			status = run_receptec_sync(sync_peer, sync_slot_epoch);
			dwt_forcetrxoff();
			print_protocol_counters(&sync_peer->counters);
			return status;
		}

		if (CONFIG_RECEPTEC_TEST_STOP_AFTER > 0 &&
		    stop_peer->successful_exchanges >= CONFIG_RECEPTEC_TEST_STOP_AFTER) {
			uint32_t stop_slot_epoch =
				cycle_epoch + us_to_dx_time(
					(uint32_t)CONFIG_RECEPTEC_TEST_STOP_PEER_SLOT *
					DW3110_SLOT_DURATION_US);

			stop_peer->sequence++;
			printk("RECEPTEC initiating STOP for slot=%u peer=%s "
			       "after %u DATA transactions\n",
			       CONFIG_RECEPTEC_TEST_STOP_PEER_SLOT, stop_peer->name,
			       stop_peer->successful_exchanges);
			status = run_receptec_stop(stop_peer, stop_slot_epoch);
			dwt_forcetrxoff();
			if (status == 0) {
				printk("RECEPTEC STOPPED: UWB disabled\n");
			} else {
				print_protocol_counters(&stop_peer->counters);
				printk("Last STOP response type: %u\n", last_stop_response_type);
			}
			return status;
		}

		if (!test_silence_done && CONFIG_RECEPTEC_TEST_SILENCE_AFTER > 0 &&
		    (CONFIG_RECEPTEC_TEST_OUT_OF_SLOT_AFTER == 0 || out_of_slot_probe_done) &&
		    live_peer->successful_exchanges >= CONFIG_RECEPTEC_TEST_SILENCE_AFTER) {
			printk("RECEPTEC test silence: slots=%u\n",
			       CONFIG_RECEPTEC_TEST_SILENT_SLOTS);
			dwt_forcetrxoff();
			k_sleep(K_MSEC((uint32_t)CONFIG_RECEPTEC_TEST_SILENT_SLOTS * 1000U));
			cycle_epoch += (uint32_t)CONFIG_RECEPTEC_TEST_SILENT_SLOTS *
				       us_to_dx_time(DW3110_SAMPLING_PERIOD_US);
			test_silence_done = true;
			printk("RECEPTEC test silence complete\n");
		}

		cycle++;
		atomic_set(&control_cycle, (atomic_val_t)cycle);
		for (uint8_t slot = 0U; slot < RECEPTEC_PEER_COUNT; ++slot) {
			uint32_t slot_epoch = cycle_epoch +
				us_to_dx_time((uint32_t)slot *
					      DW3110_SLOT_DURATION_US);
			struct receptec_peer_runtime *peer = &receptec_peers[slot];

			if (runtime_action && control.action.peer_slot == slot) {
				switch (control.action.kind) {
				case RECEPTEC_COMMAND_DATA_REQ:
					printk("MANUAL DATA_REQ: cycle=%u slot=%u "
					       "peer=%s\n",
					       cycle, slot, peer->name);
					slot_operations[slot] = RECEPTEC_SLOT_DATA;
					run_receptec_peer_data(peer, slot_epoch, &results[slot]);
					continue;
				case RECEPTEC_COMMAND_TEST_SYNC:
					peer->sequence++;
					printk("ONE-SHOT SYNC: cycle=%u slot=%u "
					       "peer=%s\n",
					       cycle, slot, peer->name);
					slot_operations[slot] = RECEPTEC_SLOT_SYNC;
					(void)run_receptec_sync(peer, slot_epoch);
					continue;
				case RECEPTEC_COMMAND_TEST_STOP:
					peer->sequence++;
					printk("ONE-SHOT STOP: cycle=%u slot=%u "
					       "peer=%s\n",
					       cycle, slot, peer->name);
					slot_operations[slot] = RECEPTEC_SLOT_STOP;
					status = run_receptec_stop(peer, slot_epoch);
					dwt_forcetrxoff();
					if (status == 0) {
						printk("STOP test complete: peer=%s "
						       "requires reset\n",
						       peer->name);
					} else {
						print_protocol_counters(&peer->counters);
					}
					continue;
				case RECEPTEC_COMMAND_TEST_WRONG_EUI:
					printk("ONE-SHOT WRONG-EUI: cycle=%u "
					       "slot=%u peer=%s\n",
					       cycle, slot, peer->name);
					slot_operations[slot] = RECEPTEC_SLOT_WRONG_EUI;
					run_wrong_destination_probe(peer, slot_epoch,
								    &wrong_results[slot]);
					continue;
				case RECEPTEC_COMMAND_TEST_REJECTION:
					printk("ONE-SHOT P-02: cycle=%u slot=%u peer=%s case=%s\n",
					       cycle, slot, peer->name,
					       rejection_case_name(control.action.argument));
					slot_operations[slot] = RECEPTEC_SLOT_REJECTION;
					run_rejection_probe(peer, slot_epoch,
						(enum dw3110_radio_rejection_case)
							control.action.argument);
					continue;
				case RECEPTEC_COMMAND_TEST_SEQUENCE_WRAP:
					printk("ONE-SHOT P-04: cycle=%u slot=%u peer=%s\n",
					       cycle, slot, peer->name);
					slot_operations[slot] = RECEPTEC_SLOT_SEQUENCE_WRAP;
					run_sequence_wrap_probe(peer, slot_epoch);
					continue;
				case RECEPTEC_COMMAND_TEST_WINDOW:
					printk("ONE-SHOT P-05: cycle=%u slot=%u peer=%s case=%s\n",
					       cycle, slot, peer->name,
					       window_case_name(control.action.argument));
					slot_operations[slot] = RECEPTEC_SLOT_WINDOW;
					run_window_probe(peer, slot_epoch,
						(enum dw3110_radio_window_case)
							control.action.argument);
					continue;
				default:
					break;
				}
			}

			if (wrong_destination_cycle &&
			    (CONFIG_RECEPTEC_TEST_WRONG_DESTINATION_PEER_MASK & (1U << slot)) !=
				    0U) {
				slot_operations[slot] = RECEPTEC_SLOT_WRONG_EUI;
				run_wrong_destination_probe(peer, slot_epoch, &wrong_results[slot]);
				continue;
			}
			if (control.periodic_sync_running &&
			    control.periodic_sync_peer_slot == slot) {
				peer->sequence++;
				printk("PERIODIC SYNC: cycle=%u slot=%u peer=%s\n", cycle, slot,
				       peer->name);
				slot_operations[slot] = RECEPTEC_SLOT_SYNC;
				(void)run_receptec_sync(peer, slot_epoch);
				continue;
			}
			if (control.slotted_running &&
			    (CONFIG_RECEPTEC_TEST_POLL_PEER_MASK & (1U << slot)) != 0U) {
				slot_operations[slot] = RECEPTEC_SLOT_DATA;
				run_receptec_peer_data(peer, slot_epoch, &results[slot]);
			}
		}

		/* UART output waits until every fixed slot in the cycle is complete. */
		for (uint8_t slot = 0U; slot < RECEPTEC_PEER_COUNT; ++slot) {
			if (slot_operations[slot] == RECEPTEC_SLOT_WRONG_EUI) {
				print_wrong_destination_result(&receptec_peers[slot], slot,
							       &wrong_results[slot]);
			} else if (slot_operations[slot] == RECEPTEC_SLOT_DATA) {
				print_receptec_slot_result(&receptec_peers[slot], cycle, slot,
							   &results[slot]);
			}
		}
		if (wrong_destination_cycle) {
			wrong_destination_probe_done = true;
		}
		if ((live_peer->successful_exchanges % 100U) == 0U &&
		    live_peer->successful_exchanges != 0U) {
			printk("RWD qualification progress: live=%u cycles=%u\n",
			       live_peer->successful_exchanges, cycle);
			print_protocol_counters(&live_peer->counters);
		}
		if (runtime_action && control.action.kind == RECEPTEC_COMMAND_TEST_OUT_OF_SLOT) {
			uint32_t probe_epoch =
				cycle_epoch +
				us_to_dx_time(DW3110_OUT_OF_SLOT_PROBE_US);

			run_out_of_slot_probe(&receptec_peers[control.action.peer_slot],
					      probe_epoch);
		}
		if (runtime_action) {
			clear_runtime_operation(&control);
		}
		if (!out_of_slot_probe_done && CONFIG_RECEPTEC_TEST_OUT_OF_SLOT_AFTER > 0 &&
		    out_of_slot_peer->successful_exchanges >=
			    CONFIG_RECEPTEC_TEST_OUT_OF_SLOT_AFTER) {
			uint32_t probe_epoch =
				cycle_epoch +
				us_to_dx_time(DW3110_OUT_OF_SLOT_PROBE_US);

			run_out_of_slot_probe(out_of_slot_peer, probe_epoch);
			out_of_slot_probe_done = true;
		}
		cycle_epoch += us_to_dx_time(DW3110_SAMPLING_PERIOD_US);
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
	uint8_t stop_sequence;
	uint8_t prepared_response_sequence;
	uint8_t prepared_response_type;
	uint8_t stop_requests;
	uint8_t stop_responses;
	uint8_t stop_buffer_write_failures;
	uint8_t stop_late_schedules;
	uint16_t last_irq_to_arm_us;
	uint16_t max_irq_to_arm_us;
	uint32_t sync_requests;
	uint32_t sync_responses;
	uint32_t sync_late_schedules;
	uint32_t data_responses;
	uint32_t data_late_schedules;
	bool have_previous_request;
	bool stop_requested;
	bool stop_tx_selected;
};

static void count_coletec_filter_rejection(
	struct coletec_radio_runtime *runtime, uint32_t radio_status)
{
	ARG_UNUSED(runtime);
	if ((radio_status & DWT_INT_ARFE_BIT_MASK) != 0U) {
		coletec_frame_filter_rejections++;
	}
}

static uint32_t remaining_rx_us(uint32_t deadline_dx)
{
	int32_t remaining_dx = (int32_t)(deadline_dx -
					 dwt_readsystimestamphi32());

	if (remaining_dx <= 0) {
		return 0U;
	}
	return dx_time_to_us((uint32_t)remaining_dx);
}

static int arm_immediate_rx(uint32_t timeout_us)
{
	dwt_forcetrxoff();
	clear_radio_events();
	dwt_setrxtimeout(us_to_rx_timeout(timeout_us));
	return dwt_rxenable(DWT_START_RX_IMMEDIATE) == DWT_SUCCESS ? 0 : -EIO;
}

static int arm_delayed_rx(uint32_t start_dx, uint32_t timeout_us)
{
	dwt_forcetrxoff();
	clear_radio_events();
	dwt_setrxtimeout(us_to_rx_timeout(timeout_us));
	dwt_setdelayedtrxtime(start_dx);
	return dwt_rxenable(DWT_START_RX_DELAYED | DWT_IDLE_ON_DLY_ERR) ==
	       DWT_SUCCESS ? 0 : -ETIME;
}

static uint32_t wait_for_rx_event(void)
{
	return wait_for_radio_event(DWT_INT_RXFCG_BIT_MASK |
				    SYS_STATUS_ALL_RX_ERR |
				    SYS_STATUS_ALL_RX_TO,
				    K_FOREVER);
}

static uint32_t wait_for_rx_event_until(int64_t end_ms)
{
	const uint32_t requested_events = DWT_INT_RXFCG_BIT_MASK |
		SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO;

	for (;;) {
		uint32_t radio_status = dwt_readsysstatuslo();
		uint32_t unrelated_events =
			radio_status & DW3110_IRQ_EVENT_MASK & ~requested_events;
		int64_t remaining_ms;

		if ((radio_status & requested_events) != 0U) {
			return radio_status;
		}
		if (unrelated_events != 0U) {
			dwt_writesysstatuslo(unrelated_events);
		}
		remaining_ms = end_ms - k_uptime_get();
		if (remaining_ms <= 0 ||
		    dw3110_port_wait_irq(K_MSEC(remaining_ms)) < 0) {
			return 0U;
		}
	}
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
	if (status != RECEPTEC_PROTOCOL_OK) {
		return -EBADMSG;
	}
	status = receptec_protocol_validate_request(
		request, coletec_eui, receptec_eui, dt_sampling_us);
	if (status != RECEPTEC_PROTOCOL_OK) {
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
		us_to_dtu(DW3110_SAMPLING_PERIOD_US) -
		us_to_dtu(dt_sampling_us)) &
		DW3110_TIMESTAMP_MASK;
	uint64_t response_tx_timestamp;
	uint64_t response_delay_dtu;
	uint64_t scheduled_tx_timestamp;
	uint32_t request_irq_cycles = dw3110_port_last_irq_cycles();
	uint8_t next_response_sequence;
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
	if (runtime->prepared_response_sequence != response_sequence) {
		if (set_preloaded_response_sequence(response_sequence) < 0) {
			return;
		}
		runtime->prepared_response_sequence = response_sequence;
	}
	if (runtime->prepared_response_type != RECEPTEC_PACKET_DATA) {
		if (set_preloaded_response_type(RECEPTEC_PACKET_DATA) < 0) {
			return;
		}
		runtime->prepared_response_type = RECEPTEC_PACKET_DATA;
	}
	scheduled_tx_timestamp = request_rx_timestamp +
		us_to_dtu(DW3110_RESPONSE_DELAY_US) -
		DW3110_TX_ANTENNA_DELAY;
	dwt_setdelayedtrxtime((uint32_t)(scheduled_tx_timestamp >> 8));
	runtime->last_irq_to_arm_us = (uint16_t)MIN(
		k_cyc_to_us_floor32(k_cycle_get_32() - request_irq_cycles),
		UINT16_MAX);
	if (runtime->last_irq_to_arm_us > runtime->max_irq_to_arm_us) {
		runtime->max_irq_to_arm_us = runtime->last_irq_to_arm_us;
	}
	if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
		runtime->data_late_schedules++;
		clear_radio_events();
		return;
	}
	(void)wait_for_radio_event(DWT_INT_TXFRS_BIT_MASK, K_FOREVER);
	dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
	if (set_preloaded_response_type(RECEPTEC_PACKET_STOP_ACK) == 0) {
		runtime->prepared_response_type = RECEPTEC_PACKET_STOP_ACK;
	}
	response_tx_timestamp = read_tx_timestamp();
	response_delay_dtu = (response_tx_timestamp - request_rx_timestamp) &
			     DW3110_TIMESTAMP_MASK;
	runtime->data_responses++;
	if (dt_sampling_us >= RECEPTEC_PROTOCOL_ATTEMPT_OFFSET_FINAL_MIN_US) {
		printk("Final-slot DATA: request-seq=%u response-seq=%u "
		       "sample=1 bytes=%u dt=%u RX-to-TX=%llu us "
		       "duplicate=%u nominal-delta=%lld dtu FCS=valid\n",
		       request->sequence, response_sequence,
		       RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE, dt_sampling_us,
		       (response_delay_dtu * 5ULL + 159744ULL) / 319488ULL,
		       duplicate_request, runtime->nominal_slot_delta);
	}
	next_response_sequence = next_prepared_response_sequence(
		request->sequence, response_sequence,
		runtime->fault_bad_response_count);
	if (runtime->prepared_response_sequence != next_response_sequence) {
		if (set_preloaded_response_sequence(next_response_sequence) == 0) {
			runtime->prepared_response_sequence =
				next_response_sequence;
		}
	}
}

static bool handle_coletec_stop(struct coletec_radio_runtime *runtime,
				const struct receptec_frame *request,
				uint64_t request_rx_timestamp)
{
	uint64_t scheduled_tx_timestamp;
	uint8_t response_sequence;
	bool duplicate_request = runtime->have_previous_request &&
				 request->sequence == runtime->previous_sequence;

	if (runtime->stop_requested && request->sequence != runtime->stop_sequence) {
		return false;
	}
	if (!duplicate_request) {
		runtime->previous_sequence = request->sequence;
		runtime->fault_drop_count = 0U;
		runtime->fault_bad_response_count = 0U;
		runtime->have_previous_request = true;
	}
	if (!runtime->stop_requested) {
		runtime->stop_requested = true;
		runtime->stop_sequence = request->sequence;
	}
	runtime->stop_requests++;
	if (runtime->fault_drop_count < CONFIG_RECEPTEC_TEST_DROP_ACKS) {
		runtime->fault_drop_count++;
		return true;
	}
	response_sequence = request->sequence;
	if (runtime->fault_bad_response_count < CONFIG_RECEPTEC_TEST_BAD_ACKS) {
		response_sequence++;
		runtime->fault_bad_response_count++;
	}
	if (runtime->prepared_response_sequence != response_sequence) {
		if (set_preloaded_response_sequence(response_sequence) < 0) {
			runtime->stop_buffer_write_failures++;
			return true;
		}
		runtime->prepared_response_sequence = response_sequence;
	}
	if (runtime->prepared_response_type != RECEPTEC_PACKET_STOP_ACK) {
		/*
		 * DATA and STOP_ACK share the same FCF and addresses, so only the
		 * protocol type byte needs restoring if a prior DATA start was late.
		 */
		if (set_preloaded_response_type(RECEPTEC_PACKET_STOP_ACK) < 0) {
			runtime->stop_buffer_write_failures++;
			return true;
		}
		runtime->prepared_response_type = RECEPTEC_PACKET_STOP_ACK;
	}
	if (!runtime->stop_tx_selected) {
		/*
		 * The buffer is staged as STOP_ACK during idle time. Shorten the
		 * existing frame prefix; stale DATA payload bytes stay in TX RAM.
		 */
		dwt_writetxfctrl(COLETEC_EMPTY_RESPONSE_RADIO_LENGTH, 0U, 0U);
		runtime->stop_tx_selected = true;
	}
	scheduled_tx_timestamp = request_rx_timestamp +
		us_to_dtu(DW3110_RESPONSE_DELAY_US) -
		DW3110_TX_ANTENNA_DELAY;
	dwt_setdelayedtrxtime((uint32_t)(scheduled_tx_timestamp >> 8));
	if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
		runtime->stop_late_schedules++;
		clear_radio_events();
		return true;
	}
	(void)wait_for_radio_event(DWT_INT_TXFRS_BIT_MASK, K_FOREVER);
	dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
	if (response_sequence != request->sequence) {
		uint8_t next_response_sequence =
			next_prepared_response_sequence(
				request->sequence, response_sequence,
				runtime->fault_bad_response_count);

		if (set_preloaded_response_sequence(next_response_sequence) == 0) {
			runtime->prepared_response_sequence =
				next_response_sequence;
		}
	}
	runtime->stop_responses++;
	return true;
}

static void handle_coletec_sync(struct coletec_radio_runtime *runtime,
				const struct receptec_frame *request,
				uint64_t request_rx_timestamp,
				uint16_t dt_sampling_us)
{
	uint64_t next_nominal_slot = (request_rx_timestamp +
		us_to_dtu(DW3110_SAMPLING_PERIOD_US) -
		us_to_dtu(dt_sampling_us)) &
		DW3110_TIMESTAMP_MASK;
	uint64_t scheduled_tx_timestamp;
	uint8_t response_sequence = request->sequence;
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
	runtime->sync_requests++;
	if (runtime->fault_drop_count < CONFIG_RECEPTEC_TEST_DROP_ACKS) {
		runtime->fault_drop_count++;
		return;
	}
	if (runtime->fault_bad_response_count < CONFIG_RECEPTEC_TEST_BAD_ACKS) {
		response_sequence++;
		runtime->fault_bad_response_count++;
	}
	if (runtime->prepared_response_sequence != response_sequence) {
		if (set_preloaded_response_sequence(response_sequence) < 0) {
			return;
		}
		runtime->prepared_response_sequence = response_sequence;
	}
	if (runtime->prepared_response_type != RECEPTEC_PACKET_ACK) {
		if (set_preloaded_response_type(RECEPTEC_PACKET_ACK) < 0) {
			return;
		}
		runtime->prepared_response_type = RECEPTEC_PACKET_ACK;
	}
	dwt_writetxfctrl(COLETEC_EMPTY_RESPONSE_RADIO_LENGTH, 0U, 0U);
	scheduled_tx_timestamp = request_rx_timestamp +
		us_to_dtu(DW3110_RESPONSE_DELAY_US) -
		DW3110_TX_ANTENNA_DELAY;
	dwt_setdelayedtrxtime((uint32_t)(scheduled_tx_timestamp >> 8));
	if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
		runtime->sync_late_schedules++;
		clear_radio_events();
		dwt_writetxfctrl(COLETEC_DATA_RADIO_LENGTH, 0U, 0U);
		return;
	}
	(void)wait_for_radio_event(DWT_INT_TXFRS_BIT_MASK, K_FOREVER);
	dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
	runtime->sync_responses++;
	printk("COLETEC SYNC/ACK: request-seq=%u response-seq=%u "
	       "dt=%u duplicate=%u responses=%u late=%u\n",
	       request->sequence, response_sequence, dt_sampling_us,
	       duplicate_request, runtime->sync_responses,
	       runtime->sync_late_schedules);

	if (set_preloaded_response_type(RECEPTEC_PACKET_STOP_ACK) == 0) {
		runtime->prepared_response_type = RECEPTEC_PACKET_STOP_ACK;
	}
	dwt_writetxfctrl(COLETEC_DATA_RADIO_LENGTH, 0U, 0U);
	if (response_sequence != request->sequence) {
		uint8_t next_response_sequence =
			next_prepared_response_sequence(
				request->sequence, response_sequence,
				runtime->fault_bad_response_count);

		if (set_preloaded_response_sequence(next_response_sequence) == 0) {
			runtime->prepared_response_sequence =
				next_response_sequence;
		}
	}
}

static bool handle_valid_coletec_request(
	struct coletec_radio_runtime *runtime, struct coletec_sync *sync,
	const struct receptec_frame *request, uint64_t request_rx_timestamp,
	uint16_t dt_sampling_us)
{
	if (request->type == RECEPTEC_PACKET_STOP) {
		if (!handle_coletec_stop(runtime, request, request_rx_timestamp)) {
			return false;
		}
	} else if (request->type == RECEPTEC_PACKET_SYNC) {
		if (runtime->stop_requested) {
			return false;
		}
		handle_coletec_sync(runtime, request, request_rx_timestamp,
				    dt_sampling_us);
	} else {
		if (runtime->stop_requested) {
			return false;
		}
		handle_coletec_request(runtime, request, request_rx_timestamp,
					 dt_sampling_us);
	}
	coletec_sync_accept_request(sync);
	return true;
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
		count_coletec_filter_rejection(runtime, radio_status);
		if (read_coletec_request(radio_status, &request,
					 &request_rx_timestamp,
					 &dt_sampling_us) == 0) {
			valid_request_seen |= handle_valid_coletec_request(
				runtime, sync, &request, request_rx_timestamp,
				dt_sampling_us);
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
		count_coletec_filter_rejection(runtime, radio_status);
		if (read_coletec_request(radio_status, &request,
					 &request_rx_timestamp,
					 &dt_sampling_us) == 0) {
			if (!handle_valid_coletec_request(runtime, sync, &request,
						  request_rx_timestamp,
						  dt_sampling_us)) {
				continue;
			}
			current_slot_deadline = request.type == RECEPTEC_PACKET_STOP ?
				(uint32_t)(request_rx_timestamp >> 8) +
				us_to_dx_time(DW3110_SLOT_DURATION_US) :
				runtime->next_slot_dx -
				us_to_dx_time(DW3110_SAMPLING_PERIOD_US) +
				us_to_dx_time(DW3110_SLOT_DURATION_US +
					      COLETEC_BASE_GUARD_US);
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
		us_to_dx_time(guard_us);
	uint32_t window_deadline = runtime->next_slot_dx +
		us_to_dx_time(DW3110_SLOT_DURATION_US + guard_us);
	uint32_t window_duration = DW3110_SLOT_DURATION_US + 2U * guard_us;
	bool valid_request_seen = false;

	if (arm_delayed_rx(window_start, window_duration) == 0) {
		struct receptec_frame request;
		uint64_t request_rx_timestamp;
		uint32_t radio_status = wait_for_rx_event();
		uint16_t dt_sampling_us;

		if ((radio_status & DWT_INT_ARFE_BIT_MASK) != 0U) {
			coletec_slot_first_event_filter_rejections++;
		}
		count_coletec_filter_rejection(runtime, radio_status);
		if (read_coletec_request(radio_status, &request,
					 &request_rx_timestamp,
					 &dt_sampling_us) == 0) {
			valid_request_seen = handle_valid_coletec_request(
				runtime, sync, &request, request_rx_timestamp,
				dt_sampling_us);
		}
		valid_request_seen |= listen_until_slot_deadline(
			runtime, sync, window_deadline);
	}
	if (valid_request_seen) {
		return;
	}
	runtime->next_slot_dx +=
		us_to_dx_time(DW3110_SAMPLING_PERIOD_US);
	coletec_sync_miss_slot(sync);
	if (sync->state == COLETEC_SYNC_DISCOVERY) {
		printk("COLETEC synchronization lost after 5 missed slots "
		       "hw-filter-rejections=%u first-event-filter=%u\n",
		       coletec_frame_filter_rejections,
		       coletec_slot_first_event_filter_rejections);
	} else {
		printk("COLETEC slot missed: count=%u next-guard=%u us "
		       "hw-filter-rejections=%u first-event-filter=%u\n",
		       sync->missed_slots, coletec_sync_guard_us(sync),
		       coletec_frame_filter_rejections,
		       coletec_slot_first_event_filter_rejections);
	}
}

static int run_coletec_data(void)
{
	struct coletec_radio_runtime runtime = {
		.prepared_response_sequence =
			CONFIG_RECEPTEC_TEST_BAD_ACKS > 0 ? 2U : 1U,
		.prepared_response_type = RECEPTEC_PACKET_STOP_ACK,
	};
	struct coletec_sync sync;
	int status;

	printk("Protocol test: COLETEC synchronized DATA responder\n");
	printk("Protocol fault injection: drop responses=%u bad responses=%u\n",
	       CONFIG_RECEPTEC_TEST_DROP_ACKS, CONFIG_RECEPTEC_TEST_BAD_ACKS);
	coletec_frame_filter_rejections = 0U;
	coletec_slot_first_event_filter_rejections = 0U;
	status = configure_coletec_frame_filter();
	if (status < 0) {
		return status;
	}
	status = preload_coletec_data();
	if (status < 0) {
		return status;
	}
	if (runtime.prepared_response_sequence != 1U) {
		status = set_preloaded_response_sequence(
			runtime.prepared_response_sequence);
		if (status < 0) {
			return status;
		}
	}
	status = set_preloaded_response_type(RECEPTEC_PACKET_STOP_ACK);
	if (status < 0) {
		return status;
	}
	status = enable_protocol_irq();
	if (status < 0) {
		return status;
	}
	coletec_sync_init(&sync);
	printk("COLETEC initial continuous RX: 10000 ms\n");
	if (listen_open_period(&runtime, &sync, COLETEC_INITIAL_RX_MS)) {
		printk("COLETEC synchronized during initial RX: responses=%u "
		       "IRQ-to-arm=%u us late=%u\n",
		       runtime.data_responses,
		       runtime.last_irq_to_arm_us,
		       runtime.data_late_schedules);
	} else {
		coletec_sync_initial_rx_expired(&sync);
		printk("COLETEC initial RX expired; entering discovery\n");
	}

	for (;;) {
		if (runtime.stop_requested) {
			dwt_forcetrxoff();
			printk("COLETEC STOPPED: seq=%u requests=%u responses=%u "
			       "buffer-write-fail=%u late=%u "
			       "UWB disabled until reset\n",
			       runtime.stop_sequence, runtime.stop_requests,
			       runtime.stop_responses,
			       runtime.stop_buffer_write_failures,
			       runtime.stop_late_schedules);
			return 0;
		}
		if (sync.state == COLETEC_SYNC_SYNCHRONIZED) {
			uint32_t responses_before = runtime.data_responses;
			uint32_t late_before = runtime.data_late_schedules;

			listen_synchronized_slot(&runtime, &sync);
			if (runtime.data_late_schedules != late_before) {
				printk("COLETEC response late: IRQ-to-arm=%u us "
				       "late=%u\n",
				       runtime.last_irq_to_arm_us,
				       runtime.data_late_schedules);
			}
			if (runtime.data_responses != responses_before &&
			    (runtime.data_responses % 10U) == 0U) {
				printk("COLETEC timing: responses=%u "
				       "IRQ-to-arm=%u us max-IRQ-to-arm=%u us "
				       "late=%u\n",
				       runtime.data_responses,
				       runtime.last_irq_to_arm_us,
				       runtime.max_irq_to_arm_us,
				       runtime.data_late_schedules);
			}
			continue;
		}
		/*
		 * Discovery may skip several request sequences. Stage the DATA
		 * type while the radio is idle so reentry only has to patch the
		 * newly observed sequence before arming the 1000 us response.
		 */
		if (runtime.prepared_response_type != RECEPTEC_PACKET_DATA) {
			if (set_preloaded_response_type(RECEPTEC_PACKET_DATA) < 0) {
				return -EIO;
			}
			runtime.prepared_response_type = RECEPTEC_PACKET_DATA;
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

static void uwb_thread_entry(void *unused_a, void *unused_b, void *unused_c)
{
	int status;

	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	printk("UWB protocol thread started: priority=%d IRQ-driven\n",
	       DW3110_UWB_THREAD_PRIORITY);
	status = uwb_thread_is_receptec ?
		run_receptec_data() : run_coletec_data();
	printk("UWB protocol thread exited: status=%d\n", status);
}

static void start_uwb_thread(void)
{
	uwb_thread_started = true;
	k_thread_create(&uwb_thread, uwb_thread_stack, K_THREAD_STACK_SIZEOF(uwb_thread_stack),
			uwb_thread_entry, NULL, NULL, NULL,
			K_PRIO_PREEMPT(DW3110_UWB_THREAD_PRIORITY), 0U, K_NO_WAIT);
	(void)k_thread_name_set(&uwb_thread, "uwb_protocol");
}

int dw3110_radio_start_protocol(bool is_receptec)
{
	if (uwb_protocol_configured) {
		return -EALREADY;
	}

	k_msgq_purge(&receptec_command_queue);
	atomic_set(&control_slotted_running,
		   is_receptec ? RECEPTEC_SLOTTED_AUTOSTART : 0);
	atomic_set(&control_periodic_sync_running, 0);
	atomic_set(&control_periodic_sync_peer_slot, 0);
	atomic_set(&control_operation_pending, 0);
	atomic_set(&control_operation, DW3110_RADIO_OPERATION_NONE);
	atomic_set(&control_cycle, 0);
	uwb_thread_is_receptec = is_receptec;
	uwb_protocol_configured = true;

	if (is_receptec) {
		start_uwb_thread();
	} else {
		printk("COLETEC protocol idle; run 'uwb coletec start' to wait for SYNC\n");
	}
	return 0;
}

int dw3110_radio_enable_coletec(void)
{
	if (!uwb_protocol_configured) {
		return -EAGAIN;
	}
	if (uwb_thread_is_receptec) {
		return -ENOTSUP;
	}
	if (uwb_thread_started) {
		return -EALREADY;
	}

	start_uwb_thread();
	return 0;
}

static int submit_receptec_command(const struct receptec_command *command,
				   enum dw3110_radio_operation operation)
{
	bool action = command_is_action(command->kind);

	if (!uwb_protocol_configured) {
		return -EAGAIN;
	}
	if (!uwb_thread_is_receptec) {
		return -ENOTSUP;
	}
	if (command->peer_slot >= RECEPTEC_PEER_COUNT &&
	    command->kind != RECEPTEC_COMMAND_TEST_SILENCE &&
	    command->kind != RECEPTEC_COMMAND_SLOTTED_SET &&
	    command->kind != RECEPTEC_COMMAND_PERIODIC_SYNC_SET &&
	    command->kind != RECEPTEC_COMMAND_ABORT) {
		return -EINVAL;
	}
	if (action && !atomic_cas(&control_operation_pending, 0, 1)) {
		return -EBUSY;
	}
	if (action) {
		atomic_set(&control_operation, operation);
	}
	if (k_msgq_put(&receptec_command_queue, command, K_NO_WAIT) < 0) {
		if (action) {
			atomic_set(&control_operation, DW3110_RADIO_OPERATION_NONE);
			atomic_set(&control_operation_pending, 0);
		}
		return -ENOMSG;
	}
	return 0;
}

int dw3110_radio_set_slotted(bool enable)
{
	const struct receptec_command command = {
		.kind = RECEPTEC_COMMAND_SLOTTED_SET,
		.enable = enable,
	};

	return submit_receptec_command(&command, DW3110_RADIO_OPERATION_NONE);
}

int dw3110_radio_set_periodic_sync(bool enable, uint8_t peer_slot)
{
	const struct receptec_command command = {
		.kind = RECEPTEC_COMMAND_PERIODIC_SYNC_SET,
		.peer_slot = peer_slot,
		.enable = enable,
	};

	if (enable && peer_slot >= RECEPTEC_PEER_COUNT) {
		return -EINVAL;
	}
	return submit_receptec_command(&command, DW3110_RADIO_OPERATION_NONE);
}

int dw3110_radio_request_data(uint8_t peer_slot)
{
	const struct receptec_command command = {
		.kind = RECEPTEC_COMMAND_DATA_REQ,
		.peer_slot = peer_slot,
	};

	return submit_receptec_command(&command, DW3110_RADIO_OPERATION_DATA_REQ);
}

int dw3110_radio_run_test(enum dw3110_radio_test test, uint8_t peer_slot, uint32_t argument)
{
	struct receptec_command command = {
		.peer_slot = peer_slot,
		.argument = argument,
	};
	enum dw3110_radio_operation operation;

	switch (test) {
	case DW3110_RADIO_TEST_SYNC:
		command.kind = RECEPTEC_COMMAND_TEST_SYNC;
		operation = DW3110_RADIO_OPERATION_TEST_SYNC;
		break;
	case DW3110_RADIO_TEST_STOP:
		command.kind = RECEPTEC_COMMAND_TEST_STOP;
		operation = DW3110_RADIO_OPERATION_TEST_STOP;
		break;
	case DW3110_RADIO_TEST_WRONG_EUI:
		command.kind = RECEPTEC_COMMAND_TEST_WRONG_EUI;
		operation = DW3110_RADIO_OPERATION_TEST_WRONG_EUI;
		break;
	case DW3110_RADIO_TEST_OUT_OF_SLOT:
		if (atomic_get(&control_slotted_running) == 0) {
			return -EACCES;
		}
		command.kind = RECEPTEC_COMMAND_TEST_OUT_OF_SLOT;
		operation = DW3110_RADIO_OPERATION_TEST_OUT_OF_SLOT;
		break;
	case DW3110_RADIO_TEST_SILENCE:
		if (argument == 0U || argument > 1000U) {
			return -EINVAL;
		}
		command.kind = RECEPTEC_COMMAND_TEST_SILENCE;
		command.peer_slot = 0U;
		operation = DW3110_RADIO_OPERATION_TEST_SILENCE;
		break;
	case DW3110_RADIO_TEST_REJECTION:
		if (argument >= DW3110_RADIO_REJECT_CASE_COUNT) {
			return -EINVAL;
		}
		command.kind = RECEPTEC_COMMAND_TEST_REJECTION;
		operation = DW3110_RADIO_OPERATION_TEST_REJECTION;
		break;
	case DW3110_RADIO_TEST_SEQUENCE_WRAP:
		command.kind = RECEPTEC_COMMAND_TEST_SEQUENCE_WRAP;
		operation = DW3110_RADIO_OPERATION_TEST_SEQUENCE_WRAP;
		break;
	case DW3110_RADIO_TEST_WINDOW:
		if (argument >= DW3110_RADIO_WINDOW_CASE_COUNT) {
			return -EINVAL;
		}
		command.kind = RECEPTEC_COMMAND_TEST_WINDOW;
		operation = DW3110_RADIO_OPERATION_TEST_WINDOW;
		break;
	default:
		return -EINVAL;
	}
	return submit_receptec_command(&command, operation);
}

int dw3110_radio_abort_test(void)
{
	const struct receptec_command command = {
		.kind = RECEPTEC_COMMAND_ABORT,
	};

	return submit_receptec_command(&command, DW3110_RADIO_OPERATION_NONE);
}

int dw3110_radio_get_control_status(struct dw3110_radio_control_status *status)
{
	if (status == NULL) {
		return -EINVAL;
	}
	if (!uwb_protocol_configured) {
		return -EAGAIN;
	}

	status->receptec_role = uwb_thread_is_receptec;
	status->protocol_running = uwb_thread_started;
	status->slotted_running = atomic_get(&control_slotted_running) != 0;
	status->periodic_sync_running = atomic_get(&control_periodic_sync_running) != 0;
	status->periodic_sync_peer_slot = (uint8_t)atomic_get(&control_periodic_sync_peer_slot);
	status->operation_pending = atomic_get(&control_operation_pending) != 0;
	status->operation = (enum dw3110_radio_operation)atomic_get(&control_operation);
	status->cycle = (uint32_t)atomic_get(&control_cycle);
	return 0;
}

int dw3110_radio_get_peer_info(uint8_t peer_slot, struct dw3110_radio_peer_info *info)
{
	if (info == NULL || peer_slot >= RECEPTEC_PEER_COUNT) {
		return -EINVAL;
	}

	info->name = receptec_peers[peer_slot].name;
	info->eui = receptec_peers[peer_slot].eui;
	info->expected_present = receptec_peers[peer_slot].expected_present;
	return 0;
}
