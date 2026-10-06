/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_PROTOCOL_H_
#define RECEPTEC_UWB_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECEPTEC_UWB_FCF_LSB 0x41U
#define RECEPTEC_UWB_FCF_MSB 0xECU
#define RECEPTEC_UWB_EUI64_SIZE 8U
#define RECEPTEC_UWB_HEADER_SIZE 20U
#define RECEPTEC_UWB_FCS_SIZE 2U
#define RECEPTEC_UWB_MAX_PSDU_SIZE 127U
#define RECEPTEC_UWB_MAX_LOGICAL_SIZE \
	(RECEPTEC_UWB_MAX_PSDU_SIZE - RECEPTEC_UWB_FCS_SIZE)
#define RECEPTEC_UWB_MAX_PAYLOAD_SIZE \
	(RECEPTEC_UWB_MAX_LOGICAL_SIZE - RECEPTEC_UWB_HEADER_SIZE)
#define RECEPTEC_UWB_DT_SAMPLING_SIZE 2U
#define RECEPTEC_UWB_PLAN_SIZE 28U
#define RECEPTEC_UWB_PLAN_V2_SIZE 30U
#define RECEPTEC_UWB_POLL_SIZE 9U
#define RECEPTEC_UWB_RESPONSE_SIZE 19U
#define RECEPTEC_UWB_FINAL_SIZE 24U
#define RECEPTEC_UWB_RESULT_HEADER_SIZE 11U
#define RECEPTEC_UWB_RESULT_RECORD_SIZE 26U
#define RECEPTEC_UWB_RESULT_MAX_RECORDS 3U
#define RECEPTEC_UWB_DISTANCE_INVALID UINT32_MAX

enum receptec_uwb_packet_type {
	RECEPTEC_UWB_DATA_REQ = 0x0,
	RECEPTEC_UWB_SYNC = 0x1,
	RECEPTEC_UWB_DATA = 0x2,
	RECEPTEC_UWB_ACK = 0x3,
	RECEPTEC_UWB_STOP = 0x4,
	RECEPTEC_UWB_STOP_ACK = 0x5,
	RECEPTEC_UWB_RANGE_PLAN = 0x6,
	RECEPTEC_UWB_RANGE_RESULT = 0x7,
	RECEPTEC_UWB_RANGE_POLL = 0x8,
	RECEPTEC_UWB_RANGE_RESPONSE = 0x9,
	RECEPTEC_UWB_RANGE_FINAL = 0xA,
};

enum receptec_uwb_plan_phase {
	RECEPTEC_UWB_PLAN_ARM = 0,
	RECEPTEC_UWB_PLAN_START = 1,
	RECEPTEC_UWB_PLAN_ROUND_ARM = 2,
};

enum receptec_uwb_report_mode {
	RECEPTEC_UWB_REPORT_STREAM = 0,
	RECEPTEC_UWB_REPORT_AGGREGATE = 1,
};

enum receptec_uwb_range_status {
	RECEPTEC_UWB_RANGE_OK = 0,
	RECEPTEC_UWB_RANGE_TIMEOUT = 1,
	RECEPTEC_UWB_RANGE_RADIO_LATE = 2,
	RECEPTEC_UWB_RANGE_BAD_MEASUREMENT = 3,
	RECEPTEC_UWB_RANGE_PLAN_TIMEOUT = 4,
	RECEPTEC_UWB_RANGE_NO_RESULT = 5,
	RECEPTEC_UWB_RANGE_NOT_RUN_DEADLINE = 6,
	RECEPTEC_UWB_RANGE_NOT_SCHEDULED = 7,
};

struct receptec_uwb_plan {
	/* Zero encodes legacy schema 1; schema 2 ARM/START carry 20..200 ms. */
	uint16_t authorization_ms;
	enum receptec_uwb_plan_phase phase;
	uint64_t round_id;
	uint8_t pair_index;
	enum receptec_uwb_report_mode report_mode;
	uint8_t initiator[RECEPTEC_UWB_EUI64_SIZE];
	uint8_t responder[RECEPTEC_UWB_EUI64_SIZE];
};

struct receptec_uwb_range_record {
	uint8_t pair_index;
	uint8_t initiator[RECEPTEC_UWB_EUI64_SIZE];
	uint8_t responder[RECEPTEC_UWB_EUI64_SIZE];
	enum receptec_uwb_range_status status;
	uint32_t distance_mm;
	uint32_t master_rx_time_ms;
};

struct receptec_uwb_result {
	uint64_t round_id;
	enum receptec_uwb_report_mode report_mode;
	uint8_t record_count;
	struct receptec_uwb_range_record records[RECEPTEC_UWB_RESULT_MAX_RECORDS];
};

struct receptec_uwb_twr_response {
	uint64_t round_id;
	uint8_t pair_index;
	uint64_t poll_rx_ts;
	uint64_t response_tx_ts;
};

struct receptec_uwb_twr_final {
	uint64_t round_id;
	uint8_t pair_index;
	uint64_t poll_tx_ts;
	uint64_t response_rx_ts;
	uint64_t final_tx_ts;
};

enum receptec_uwb_status {
	RECEPTEC_UWB_OK = 0,
	RECEPTEC_UWB_ERR_ARGUMENT = -1,
	RECEPTEC_UWB_ERR_BUFFER_TOO_SMALL = -2,
	RECEPTEC_UWB_ERR_LENGTH = -3,
	RECEPTEC_UWB_ERR_FCF = -4,
	RECEPTEC_UWB_ERR_RESERVED = -5,
	RECEPTEC_UWB_ERR_TYPE = -6,
};

struct receptec_uwb_frame {
	uint8_t sequence;
	uint8_t destination[RECEPTEC_UWB_EUI64_SIZE];
	uint8_t source[RECEPTEC_UWB_EUI64_SIZE];
	enum receptec_uwb_packet_type type;
	const uint8_t *payload;
	size_t payload_length;
};

int receptec_uwb_encode(uint8_t *output, size_t output_capacity,
			 const struct receptec_uwb_frame *frame,
			 size_t *encoded_length);
int receptec_uwb_decode(const uint8_t *input, size_t input_length,
			 struct receptec_uwb_frame *frame);
bool receptec_uwb_ack_matches(
	const struct receptec_uwb_frame *frame,
	const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
	const uint8_t local_eui64[RECEPTEC_UWB_EUI64_SIZE],
	uint8_t expected_sequence);
bool receptec_uwb_control_response_matches(
	const struct receptec_uwb_frame *frame,
	enum receptec_uwb_packet_type expected_type,
	const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
	const uint8_t local_eui64[RECEPTEC_UWB_EUI64_SIZE],
	uint8_t expected_sequence);
bool receptec_uwb_type_is_request(enum receptec_uwb_packet_type type);
int receptec_uwb_expected_response(enum receptec_uwb_packet_type request,
				   enum receptec_uwb_packet_type *response);
void receptec_uwb_encode_dt_sampling(
	uint16_t dt_sampling_us,
	uint8_t output[RECEPTEC_UWB_DT_SAMPLING_SIZE]);
uint16_t receptec_uwb_decode_dt_sampling(
	const uint8_t input[RECEPTEC_UWB_DT_SAMPLING_SIZE]);

int receptec_uwb_plan_encode(uint8_t *out, size_t capacity,
			     const struct receptec_uwb_plan *plan);
int receptec_uwb_plan_decode(const uint8_t *data, size_t length,
			     struct receptec_uwb_plan *plan);
int receptec_uwb_result_encode(uint8_t *out, size_t capacity,
			       const struct receptec_uwb_result *result,
			       size_t *length);
int receptec_uwb_result_decode(const uint8_t *data, size_t length,
			       struct receptec_uwb_result *result);
int receptec_uwb_twr_response_encode(uint8_t out[RECEPTEC_UWB_RESPONSE_SIZE],
				      const struct receptec_uwb_twr_response *response);
int receptec_uwb_twr_response_decode(const uint8_t *data, size_t length,
				      struct receptec_uwb_twr_response *response);
int receptec_uwb_twr_final_encode(uint8_t out[RECEPTEC_UWB_FINAL_SIZE],
				   const struct receptec_uwb_twr_final *final);
int receptec_uwb_twr_final_decode(const uint8_t *data, size_t length,
				   struct receptec_uwb_twr_final *final);
int receptec_uwb_distance_mm(const struct receptec_uwb_twr_response *response,
			      const struct receptec_uwb_twr_final *final,
			      uint64_t final_rx_ts, uint32_t *distance_mm);

#endif /* RECEPTEC_UWB_PROTOCOL_H_ */
