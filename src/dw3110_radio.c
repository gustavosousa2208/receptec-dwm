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
#define DW3110_EVENT_TIMEOUT_MS 100U
#define DW3110_TIMESTAMP_MASK ((1ULL << 40) - 1ULL)

enum dw3110_event {
	DW3110_EVENT_TX_DONE,
	DW3110_EVENT_RX_GOOD,
	DW3110_EVENT_RX_TIMEOUT,
	DW3110_EVENT_RX_ERROR,
};

struct dw3110_event_data {
	enum dw3110_event type;
	uint32_t status;
	uint16_t frame_length;
};

struct received_protocol_frame {
	uint8_t bytes[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
	struct receptec_frame frame;
	uint64_t rx_timestamp;
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

K_MSGQ_DEFINE(event_queue, sizeof(struct dw3110_event_data), 4U, 4U);

static void queue_event(enum dw3110_event type,
			const dwt_cb_data_t *callback_data)
{
	const struct dw3110_event_data event = {
		.type = type,
		.status = callback_data->status,
		.frame_length = callback_data->datalength,
	};

	(void)k_msgq_put(&event_queue, &event, K_NO_WAIT);
}

static void tx_done_callback(const dwt_cb_data_t *callback_data)
{
	queue_event(DW3110_EVENT_TX_DONE, callback_data);
}

static void rx_good_callback(const dwt_cb_data_t *callback_data)
{
	queue_event(DW3110_EVENT_RX_GOOD, callback_data);
}

static void rx_timeout_callback(const dwt_cb_data_t *callback_data)
{
	queue_event(DW3110_EVENT_RX_TIMEOUT, callback_data);
}

static void rx_error_callback(const dwt_cb_data_t *callback_data)
{
	queue_event(DW3110_EVENT_RX_ERROR, callback_data);
}

static dwt_callbacks_s radio_callbacks = {
	.cbTxDone = tx_done_callback,
	.cbRxOk = rx_good_callback,
	.cbRxTo = rx_timeout_callback,
	.cbRxErr = rx_error_callback,
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

static void clear_events(void)
{
	k_msgq_purge(&event_queue);
}

static int wait_for_event(struct dw3110_event_data *event,
			  k_timeout_t timeout)
{
	return k_msgq_get(&event_queue, event, timeout) == 0 ? 0 : -ETIMEDOUT;
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

static int decode_received_frame(const struct dw3110_event_data *event,
				 struct received_protocol_frame *received)
{
	uint16_t logical_length;
	int status;

	if (event->frame_length < RECEPTEC_PROTOCOL_HEADER_SIZE +
				  RECEPTEC_PROTOCOL_FCS_SIZE ||
	    event->frame_length > RECEPTEC_PROTOCOL_MAX_PSDU_SIZE) {
		printk("Protocol RX invalid PSDU length: %u\n", event->frame_length);
		return -EMSGSIZE;
	}

	logical_length = event->frame_length - RECEPTEC_PROTOCOL_FCS_SIZE;
	received->rx_timestamp = read_rx_timestamp();
	dwt_readrxdata(received->bytes, logical_length, 0U);
	status = receptec_protocol_decode(received->bytes, logical_length,
					  &received->frame);
	if (status != RECEPTEC_PROTOCOL_OK) {
		printk("Protocol RX codec rejection: %d\n", status);
		return -EBADMSG;
	}
	return 0;
}

static int receive_protocol_frame(struct received_protocol_frame *received,
				  bool enable_rx, uint32_t timeout_uus)
{
	struct dw3110_event_data event;
	int status;

	if (enable_rx) {
		clear_events();
		dwt_setrxtimeout(timeout_uus);
		if (dwt_rxenable(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
			return -EIO;
		}
	}

	for (;;) {
		status = wait_for_event(&event, timeout_uus == 0U ? K_FOREVER :
						K_MSEC(DW3110_EVENT_TIMEOUT_MS));
		if (status < 0) {
			printk("Protocol RX IRQ timeout\n");
			dwt_forcetrxoff();
			return status;
		}
		if (event.type == DW3110_EVENT_TX_DONE) {
			continue;
		}
		if (event.type == DW3110_EVENT_RX_TIMEOUT) {
			return -ETIMEDOUT;
		}
		if (event.type == DW3110_EVENT_RX_ERROR) {
			printk("Protocol RX hardware error: 0x%08X\n", event.status);
			return -EIO;
		}
		return decode_received_frame(&event, received);
	}
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

static int send_delayed_sync(uint8_t sequence)
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
	uint32_t transmit_time;
	int status;

	memcpy(addressed_sync.destination, coletec_eui, sizeof(coletec_eui));
	memcpy(addressed_sync.source, receptec_eui, sizeof(receptec_eui));
	receptec_protocol_encode_dt_sampling(0U, dt_sampling);
	status = load_protocol_frame(&addressed_sync);
	if (status < 0) {
		return status;
	}

	clear_events();
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(DW3110_RESPONSE_TIMEOUT_US);
	transmit_time = dwt_readsystimestamphi32() +
			DW3110_REQUEST_SETUP_US * DW3110_UUS_TO_DX_TIME;
	dwt_setdelayedtrxtime(transmit_time);
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) !=
	    DWT_SUCCESS) {
		printk("SYNC delayed TX was scheduled too late\n");
		return -ETIME;
	}
	return 0;
}

static int run_receptec_sync(void)
{
	struct received_protocol_frame received;
	uint8_t sequence = 0U;
	uint32_t successful_exchanges = 0U;
	int status;

	printk("Protocol test: RECEPTEC delayed SYNC initiator\n");
	k_msleep(250U);
	for (;;) {
		sequence++;
		status = send_delayed_sync(sequence);
		if (status == 0) {
			status = receive_protocol_frame(&received, false,
						DW3110_RESPONSE_TIMEOUT_US);
		}
		if (status == 0 &&
		    (!frame_is_for(&received.frame, receptec_eui, coletec_eui,
				   RECEPTEC_PACKET_ACK) ||
		     received.frame.sequence != sequence)) {
			printk("SYNC rejected ACK: type=%u seq=%u expected=%u\n",
			       received.frame.type, received.frame.sequence, sequence);
			status = -EBADMSG;
		}

		if (status == 0) {
			successful_exchanges++;
			printk("SYNC/ACK OK: seq=%u FCS=valid addressed=ok\n",
			       sequence);
			if ((successful_exchanges % 100U) == 0U) {
				printk("M5 qualification progress: %u/1000\n",
				       successful_exchanges < 1000U ?
				       successful_exchanges : 1000U);
			}
			if (successful_exchanges == 1000U) {
				printk("M5 qualification complete: 1000 consecutive SYNC/ACK\n");
			}
		} else {
			successful_exchanges = 0U;
			printk("SYNC/ACK failed: seq=%u error=%d\n", sequence,
			       status);
			printk("M5 qualification reset: consecutive successes=0\n");
			dwt_forcetrxoff();
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
		    receptec_protocol_decode_dt_sampling(request.payload) != 0U) {
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
	dwt_setcallbacks(&radio_callbacks);
	dwt_setinterrupt(DWT_INT_TXFRS_BIT_MASK | DWT_INT_RXFCG_BIT_MASK |
			 DWT_INT_RXFTO_BIT_MASK | DWT_INT_RXPTO_BIT_MASK |
			 DWT_INT_RXPHE_BIT_MASK | DWT_INT_RXFCE_BIT_MASK |
			 DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXSTO_BIT_MASK |
			 DWT_INT_ARFE_BIT_MASK | DWT_INT_CIAERR_BIT_MASK,
			 0U, DWT_ENABLE_INT_ONLY);
	status = dw3110_port_irq_enable(true);
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
