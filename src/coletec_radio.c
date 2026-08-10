/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/coletec_radio.h>

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
#define DW3110_RESPONSE_DELAY_US 2000U
#define DW3110_SAMPLING_PERIOD_US 1000000U
#define DW3110_SLOT_DURATION_US 20000U
#define DW3110_TIMESTAMP_MASK ((1ULL << 40) - 1ULL)
#define DW3110_IRQ_EVENT_MASK \
	(DWT_INT_TXFRS_BIT_MASK | DWT_INT_RXFCG_BIT_MASK | \
	 SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO)
#define DW3110_UWB_THREAD_STACK_SIZE 4096U
#define DW3110_UWB_THREAD_PRIORITY 0
#define FRAME_SEQUENCE_OFFSET 2U
#define FRAME_TYPE_OFFSET 19U
#define COLETEC_EMPTY_RESPONSE_RADIO_LENGTH \
	(RECEPTEC_PROTOCOL_HEADER_SIZE + RECEPTEC_PROTOCOL_FCS_SIZE)
#define COLETEC_DATA_RADIO_LENGTH \
	(RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE + RECEPTEC_PROTOCOL_FCS_SIZE)

/* Addresses are IEEE 802.15.4 on-air byte order (least-significant octet
 * first).  Change them only with the matching RECEPTEC configuration. */
static const uint8_t receptec_eui[RECEPTEC_PROTOCOL_EUI64_SIZE] = {
	0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E,
};
static const uint8_t coletec_eui[RECEPTEC_PROTOCOL_EUI64_SIZE] = {
	0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40,
};

struct coletec_runtime {
	uint64_t previous_nominal_slot;
	uint32_t next_slot_dx;
	uint8_t previous_sequence;
	uint32_t requests;
	uint32_t data_responses;
	uint32_t sync_responses;
	uint32_t stop_responses;
	uint32_t late_schedules;
	uint32_t rejected_frames;
	uint16_t last_irq_to_arm_us;
	uint16_t max_irq_to_arm_us;
	bool have_previous_request;
	bool stopped;
	uint8_t stop_sequence;
};

static uint8_t protocol_rx_bytes[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
static K_THREAD_STACK_DEFINE(uwb_thread_stack, DW3110_UWB_THREAD_STACK_SIZE);
static struct k_thread uwb_thread;
static bool radio_started;

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

/* DW3xxx time is based on 499.2 MHz: 1 us = 319488 / 5 DTU. */
static uint64_t us_to_dtu(uint64_t us)
{
	return (us * 319488ULL + 2ULL) / 5ULL;
}

/* dwt_setdelayedtrxtime() consumes system-time bits 39:8. */
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

static int configure_frame_filter(void)
{
	uint8_t readback_eui[RECEPTEC_PROTOCOL_EUI64_SIZE];

	dwt_seteui((uint8_t *)coletec_eui);
	dwt_geteui(readback_eui);
	if (memcmp(readback_eui, coletec_eui, sizeof(readback_eui)) != 0) {
		return -EIO;
	}
	dwt_configureframefilter(DWT_FF_ENABLE_802_15_4, DWT_FF_DATA_EN);
	return 0;
}

static int enable_protocol_irq(void)
{
	clear_radio_events();
	dwt_setinterrupt(DW3110_IRQ_EVENT_MASK, 0U, DWT_ENABLE_INT_ONLY);
	return dw3110_port_irq_enable(true);
}

static int preload_data_response(void)
{
	uint8_t bytes[RECEPTEC_PROTOCOL_MAX_PSDU_SIZE] = { 0 };
	uint8_t payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE];
	struct receptec_frame data = {
		.sequence = 0U,
		.type = RECEPTEC_PACKET_DATA,
		.payload = payload,
		.payload_length = sizeof(payload),
	};
	size_t logical_length;

	memcpy(payload, coletec_eui, sizeof(coletec_eui));
	for (size_t i = sizeof(coletec_eui); i < sizeof(payload); ++i) {
		payload[i] = (uint8_t)i;
	}
	memcpy(data.destination, receptec_eui, sizeof(receptec_eui));
	memcpy(data.source, coletec_eui, sizeof(coletec_eui));
	if (receptec_protocol_encode(bytes, RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE,
				    &data, &logical_length) != RECEPTEC_PROTOCOL_OK) {
		return -EINVAL;
	}
	if (dwt_writetxdata((uint16_t)(logical_length + RECEPTEC_PROTOCOL_FCS_SIZE),
			    bytes, 0U) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl(COLETEC_DATA_RADIO_LENGTH, 0U, 0U);
	return 0;
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

static int read_request(uint32_t radio_status, struct receptec_frame *request,
			uint64_t *rx_timestamp, uint16_t *dt_sampling_us)
{
	uint16_t frame_length;
	uint16_t logical_length;
	uint8_t ranging;

	if ((radio_status & DWT_INT_RXFCG_BIT_MASK) == 0U) {
		return -EIO;
	}
	frame_length = dwt_getframelength(&ranging);
	if (frame_length < RECEPTEC_PROTOCOL_HEADER_SIZE + RECEPTEC_PROTOCOL_FCS_SIZE ||
	    frame_length > RECEPTEC_PROTOCOL_MAX_PSDU_SIZE) {
		return -EMSGSIZE;
	}
	logical_length = frame_length - RECEPTEC_PROTOCOL_FCS_SIZE;
	dwt_readrxdata(protocol_rx_bytes, logical_length, 0U);
	if (receptec_protocol_decode(protocol_rx_bytes, logical_length, request) !=
	    RECEPTEC_PROTOCOL_OK ||
	    memcmp(request->destination, coletec_eui, sizeof(coletec_eui)) != 0 ||
	    memcmp(request->source, receptec_eui, sizeof(receptec_eui)) != 0) {
		return -EBADMSG;
	}
	if (request->type == RECEPTEC_PACKET_STOP) {
		*dt_sampling_us = 0U;
	} else if (request->type == RECEPTEC_PACKET_DATA_REQ ||
		   request->type == RECEPTEC_PACKET_SYNC) {
		*dt_sampling_us = receptec_protocol_decode_dt_sampling(request->payload);
	} else {
		return -EBADMSG;
	}
	*rx_timestamp = read_rx_timestamp();
	return 0;
}

static void update_next_slot(struct coletec_runtime *runtime,
			     uint64_t request_rx_timestamp, uint16_t dt_sampling_us)
{
	uint64_t next_nominal_slot = (request_rx_timestamp +
		us_to_dtu(DW3110_SAMPLING_PERIOD_US) - us_to_dtu(dt_sampling_us)) &
		DW3110_TIMESTAMP_MASK;

	runtime->next_slot_dx = (uint32_t)(next_nominal_slot >> 8);
}

static int send_response(struct coletec_runtime *runtime,
			 enum receptec_packet_type type, uint8_t sequence,
			 uint64_t request_rx_timestamp)
{
	uint64_t scheduled_tx_timestamp = request_rx_timestamp +
		us_to_dtu(DW3110_RESPONSE_DELAY_US) - DW3110_TX_ANTENNA_DELAY;
	uint8_t type_byte = (uint8_t)type;
	uint32_t irq_cycles = dw3110_port_last_irq_cycles();

	if (dwt_writetxdata(1U, &sequence, FRAME_SEQUENCE_OFFSET) != DWT_SUCCESS ||
	    dwt_writetxdata(1U, &type_byte, FRAME_TYPE_OFFSET) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl(type == RECEPTEC_PACKET_DATA ? COLETEC_DATA_RADIO_LENGTH :
			COLETEC_EMPTY_RESPONSE_RADIO_LENGTH, 0U, 0U);
	dwt_setdelayedtrxtime((uint32_t)(scheduled_tx_timestamp >> 8));
	runtime->last_irq_to_arm_us = (uint16_t)MIN(
		k_cyc_to_us_floor32(k_cycle_get_32() - irq_cycles), UINT16_MAX);
	if (runtime->last_irq_to_arm_us > runtime->max_irq_to_arm_us) {
		runtime->max_irq_to_arm_us = runtime->last_irq_to_arm_us;
	}
	if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
		runtime->late_schedules++;
		clear_radio_events();
		return -ETIME;
	}
	(void)wait_for_radio_event(DWT_INT_TXFRS_BIT_MASK, K_FOREVER);
	dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
	return 0;
}

static bool handle_request(struct coletec_runtime *runtime,
			   struct coletec_sync *sync,
			   const struct receptec_frame *request,
			   uint64_t rx_timestamp, uint16_t dt_sampling_us)
{
	if (runtime->stopped && request->sequence != runtime->stop_sequence) {
		return false;
	}
	runtime->requests++;
	if (request->type == RECEPTEC_PACKET_STOP) {
		runtime->stopped = true;
		runtime->stop_sequence = request->sequence;
		if (send_response(runtime, RECEPTEC_PACKET_STOP_ACK, request->sequence,
				  rx_timestamp) == 0) {
			runtime->stop_responses++;
		}
		return true;
	}

	update_next_slot(runtime, rx_timestamp, dt_sampling_us);
	runtime->previous_sequence = request->sequence;
	runtime->have_previous_request = true;
	if (request->type == RECEPTEC_PACKET_SYNC) {
		if (send_response(runtime, RECEPTEC_PACKET_ACK, request->sequence,
				  rx_timestamp) == 0) {
			runtime->sync_responses++;
		}
	} else if (send_response(runtime, RECEPTEC_PACKET_DATA, request->sequence,
				 rx_timestamp) == 0) {
		runtime->data_responses++;
	}
	coletec_sync_accept_request(sync);
	return true;
}

static uint32_t remaining_rx_us(uint32_t deadline_dx)
{
	int32_t remaining_dx = (int32_t)(deadline_dx - dwt_readsystimestamphi32());

	return remaining_dx > 0 ? dx_time_to_us((uint32_t)remaining_dx) : 0U;
}

/* Keep listening for the rest of the slot after every response.  This is the
 * response-loss recovery model: an initiator retry can still be received. */
static bool listen_until_slot_deadline(struct coletec_runtime *runtime,
				       struct coletec_sync *sync, uint32_t deadline_dx)
{
	bool valid_request_seen = false;

	for (;;) {
		struct receptec_frame request;
		uint64_t rx_timestamp;
		uint16_t dt_sampling_us;
		uint32_t remaining_us = remaining_rx_us(deadline_dx);
		uint32_t radio_status;

		if (remaining_us == 0U || arm_immediate_rx(remaining_us) < 0) {
			return valid_request_seen;
		}
		radio_status = wait_for_radio_event(DWT_INT_RXFCG_BIT_MASK |
			SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO, K_FOREVER);
		if (read_request(radio_status, &request, &rx_timestamp,
				 &dt_sampling_us) == 0) {
			valid_request_seen |= handle_request(runtime, sync, &request,
							 rx_timestamp, dt_sampling_us);
		} else {
			runtime->rejected_frames++;
		}
	}
}

static bool listen_open_period(struct coletec_runtime *runtime,
			       struct coletec_sync *sync, uint32_t duration_ms)
{
	int64_t end_ms = k_uptime_get() + duration_ms;

	if (arm_immediate_rx(0U) < 0) {
		return false;
	}
	while (k_uptime_get() < end_ms) {
		struct receptec_frame request;
		uint64_t rx_timestamp;
		uint16_t dt_sampling_us;
		uint32_t radio_status = wait_for_radio_event(DWT_INT_RXFCG_BIT_MASK |
			SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO,
			K_MSEC(end_ms - k_uptime_get()));

		if (radio_status == 0U) {
			break;
		}
		if (read_request(radio_status, &request, &rx_timestamp,
				 &dt_sampling_us) == 0) {
			if (handle_request(runtime, sync, &request, rx_timestamp,
					   dt_sampling_us)) {
				uint32_t deadline = request.type == RECEPTEC_PACKET_STOP ?
					(uint32_t)(rx_timestamp >> 8) +
					us_to_dx_time(DW3110_SLOT_DURATION_US) :
					runtime->next_slot_dx -
					us_to_dx_time(DW3110_SAMPLING_PERIOD_US) +
					us_to_dx_time(DW3110_SLOT_DURATION_US +
						       COLETEC_BASE_GUARD_US);

				(void)listen_until_slot_deadline(runtime, sync, deadline);
				return true;
			}
		} else {
			runtime->rejected_frames++;
		}
		if (arm_immediate_rx(0U) < 0) {
			break;
		}
	}
	dwt_forcetrxoff();
	return false;
}

static void listen_synchronized_slot(struct coletec_runtime *runtime,
				     struct coletec_sync *sync)
{
	uint32_t guard_us = coletec_sync_guard_us(sync);
	uint32_t window_start = runtime->next_slot_dx - us_to_dx_time(guard_us);
	uint32_t window_deadline = runtime->next_slot_dx +
		us_to_dx_time(DW3110_SLOT_DURATION_US + guard_us);
	uint32_t window_duration = DW3110_SLOT_DURATION_US + 2U * guard_us;
	bool valid_request_seen = false;

	if (arm_delayed_rx(window_start, window_duration) == 0) {
		struct receptec_frame request;
		uint64_t rx_timestamp;
		uint16_t dt_sampling_us;
		uint32_t radio_status = wait_for_radio_event(DWT_INT_RXFCG_BIT_MASK |
			SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO, K_FOREVER);

		if (read_request(radio_status, &request, &rx_timestamp,
				 &dt_sampling_us) == 0) {
			valid_request_seen = handle_request(runtime, sync, &request,
							  rx_timestamp, dt_sampling_us);
		} else {
			runtime->rejected_frames++;
		}
		valid_request_seen |= listen_until_slot_deadline(runtime, sync,
							window_deadline);
	}
	if (valid_request_seen) {
		return;
	}
	runtime->next_slot_dx += us_to_dx_time(DW3110_SAMPLING_PERIOD_US);
	coletec_sync_miss_slot(sync);
}

static int run_coletec(void)
{
	struct coletec_runtime runtime = { 0 };
	struct coletec_sync sync;
	int status;

	status = configure_frame_filter();
	if (status < 0) {
		return status;
	}
	status = preload_data_response();
	if (status < 0) {
		return status;
	}
	status = enable_protocol_irq();
	if (status < 0) {
		return status;
	}
	coletec_sync_init(&sync);
	printk("COLETEC ready: initial RX=%u ms, response delay=%u us\n",
	       COLETEC_INITIAL_RX_MS, DW3110_RESPONSE_DELAY_US);
	if (!listen_open_period(&runtime, &sync, COLETEC_INITIAL_RX_MS)) {
		coletec_sync_initial_rx_expired(&sync);
		printk("COLETEC initial RX expired; entering discovery\n");
	}

	while (!runtime.stopped) {
		if (sync.state == COLETEC_SYNC_SYNCHRONIZED) {
			listen_synchronized_slot(&runtime, &sync);
			continue;
		}
		printk("COLETEC discovery ON: %u ms (off multiplier=%u)\n",
		       COLETEC_DISCOVERY_ON_MS, sync.discovery_off_multiplier);
		(void)listen_open_period(&runtime, &sync, COLETEC_DISCOVERY_ON_MS);
		if (sync.state != COLETEC_SYNC_SYNCHRONIZED) {
			coletec_sync_discovery_on_expired(&sync);
			k_sleep(K_MSEC(coletec_sync_discovery_off_ms(&sync)));
		}
	}

	dwt_forcetrxoff();
	printk("COLETEC stopped: requests=%u DATA=%u ACK=%u STOP_ACK=%u late=%u rejected=%u max-arm=%u us\n",
	       runtime.requests, runtime.data_responses, runtime.sync_responses,
	       runtime.stop_responses, runtime.late_schedules, runtime.rejected_frames,
	       runtime.max_irq_to_arm_us);
	return 0;
}

int coletec_radio_init(void)
{
	uint32_t poll;
	int status;

	status = dw3110_port_init();
	if (status < 0) {
		return status;
	}
	status = dw3110_port_reset();
	if (status < 0) {
		return status;
	}
	probe.spi = (void *)dw3110_port_spi();
	if (dwt_probe(&probe) != DWT_SUCCESS) {
		return -ENODEV;
	}
	for (poll = 0U; poll < DW3110_IDLE_RC_POLLS; ++poll) {
		if (dwt_checkidlerc() != 0U) {
			break;
		}
		k_busy_wait(DW3110_IDLE_RC_POLL_US);
	}
	if (poll == DW3110_IDLE_RC_POLLS) {
		return -ETIMEDOUT;
	}
	if (dwt_initialise(DWT_READ_OTP_ALL) != DWT_SUCCESS ||
	    dwt_check_dev_id() != DWT_SUCCESS ||
	    dwt_configure(&radio_config) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_configuretxrf(&tx_config);
	dwt_settxantennadelay(DW3110_TX_ANTENNA_DELAY);
	dwt_setrxantennadelay(DW3110_RX_ANTENNA_DELAY);
	dwt_setinterrupt(0U, 0U, DWT_ENABLE_INT_ONLY);
	return dw3110_port_irq_enable(false);
}

static void uwb_thread_entry(void *unused_a, void *unused_b, void *unused_c)
{
	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);
	printk("COLETEC UWB thread started: priority=%d\n",
	       DW3110_UWB_THREAD_PRIORITY);
	printk("COLETEC UWB thread exited: status=%d\n", run_coletec());
}

int coletec_radio_start(void)
{
	if (radio_started) {
		return -EALREADY;
	}
	radio_started = true;
	k_thread_create(&uwb_thread, uwb_thread_stack,
			K_THREAD_STACK_SIZEOF(uwb_thread_stack), uwb_thread_entry,
			NULL, NULL, NULL, K_PRIO_PREEMPT(DW3110_UWB_THREAD_PRIORITY),
			0U, K_NO_WAIT);
	(void)k_thread_name_set(&uwb_thread, "coletec_uwb");
	return 0;
}
