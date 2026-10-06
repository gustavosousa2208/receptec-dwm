/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/uwb_protocol.h>

#include <string.h>
#include <receptec/uwb_remote_config.h>

#define DESTINATION_OFFSET 3U
#define SOURCE_OFFSET 11U
#define TYPE_OFFSET 19U

static int validate_payload(enum receptec_uwb_packet_type type,
			    const uint8_t *payload, size_t payload_length)
{
	struct receptec_uwb_plan plan;
	struct receptec_uwb_result result;
	struct receptec_uwb_twr_response response;
	struct receptec_uwb_twr_final final;

	switch (type) {
	case RECEPTEC_UWB_DATA_REQ:
	case RECEPTEC_UWB_SYNC:
		return payload_length == RECEPTEC_UWB_DT_SAMPLING_SIZE ?
			RECEPTEC_UWB_OK : RECEPTEC_UWB_ERR_LENGTH;
	case RECEPTEC_UWB_DATA:
		return payload_length <= RECEPTEC_UWB_MAX_PAYLOAD_SIZE ?
			RECEPTEC_UWB_OK : RECEPTEC_UWB_ERR_LENGTH;
	case RECEPTEC_UWB_ACK:
	case RECEPTEC_UWB_STOP:
	case RECEPTEC_UWB_STOP_ACK:
		return payload_length == 0U ? RECEPTEC_UWB_OK :
			RECEPTEC_UWB_ERR_LENGTH;
	case RECEPTEC_UWB_CONFIG_REQUEST:
	case RECEPTEC_UWB_CONFIG_RESPONSE:
		return payload != NULL && payload_length >= UWB_REMOTE_CONFIG_HEADER_SIZE &&
		       payload[0] == UWB_REMOTE_CONFIG_SCHEMA &&
		       payload[1] <= UWB_REMOTE_CONFIG_REBOOT &&
		       payload[10] <= UWB_REMOTE_CONFIG_MAX_DATA &&
		       payload_length == UWB_REMOTE_CONFIG_HEADER_SIZE + payload[10] ?
		       RECEPTEC_UWB_OK : RECEPTEC_UWB_ERR_LENGTH;
	case RECEPTEC_UWB_RANGE_PLAN:
		return receptec_uwb_plan_decode(payload, payload_length, &plan);
	case RECEPTEC_UWB_RANGE_RESULT:
		return receptec_uwb_result_decode(payload, payload_length, &result);
	case RECEPTEC_UWB_RANGE_POLL:
		return payload_length == RECEPTEC_UWB_POLL_SIZE &&
			payload != NULL && payload[8] < 3U ?
			RECEPTEC_UWB_OK : RECEPTEC_UWB_ERR_LENGTH;
	case RECEPTEC_UWB_RANGE_RESPONSE:
		return receptec_uwb_twr_response_decode(payload, payload_length,
						  &response);
	case RECEPTEC_UWB_RANGE_FINAL:
		return receptec_uwb_twr_final_decode(payload, payload_length, &final);
	default:
		return RECEPTEC_UWB_ERR_TYPE;
	}
}

int receptec_uwb_encode(uint8_t *output, size_t output_capacity,
			 const struct receptec_uwb_frame *frame,
			 size_t *encoded_length)
{
	size_t logical_length;
	int status;

	if (output == NULL || frame == NULL || encoded_length == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	if (frame->payload_length != 0U && frame->payload == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	status = validate_payload(frame->type, frame->payload,
				  frame->payload_length);
	if (status != RECEPTEC_UWB_OK) {
		return status;
	}
	logical_length = RECEPTEC_UWB_HEADER_SIZE + frame->payload_length;
	if (output_capacity < logical_length) {
		return RECEPTEC_UWB_ERR_BUFFER_TOO_SMALL;
	}

	output[0] = RECEPTEC_UWB_FCF_LSB;
	output[1] = RECEPTEC_UWB_FCF_MSB;
	output[2] = frame->sequence;
	memcpy(&output[DESTINATION_OFFSET], frame->destination,
	       RECEPTEC_UWB_EUI64_SIZE);
	memcpy(&output[SOURCE_OFFSET], frame->source, RECEPTEC_UWB_EUI64_SIZE);
	output[TYPE_OFFSET] = (uint8_t)frame->type;
	if (frame->payload_length != 0U) {
		memcpy(&output[RECEPTEC_UWB_HEADER_SIZE], frame->payload,
		       frame->payload_length);
	}
	*encoded_length = logical_length;
	return RECEPTEC_UWB_OK;
}

int receptec_uwb_decode(const uint8_t *input, size_t input_length,
			 struct receptec_uwb_frame *frame)
{
	uint8_t type_byte;
	size_t payload_length;
	int status;

	if (input == NULL || frame == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	if (input_length < RECEPTEC_UWB_HEADER_SIZE ||
	    input_length > RECEPTEC_UWB_MAX_LOGICAL_SIZE) {
		return RECEPTEC_UWB_ERR_LENGTH;
	}
	if (input[0] != RECEPTEC_UWB_FCF_LSB || input[1] != RECEPTEC_UWB_FCF_MSB) {
		return RECEPTEC_UWB_ERR_FCF;
	}
	type_byte = input[TYPE_OFFSET];
	if ((type_byte & 0xF0U) != 0U) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	payload_length = input_length - RECEPTEC_UWB_HEADER_SIZE;
	status = validate_payload((enum receptec_uwb_packet_type)type_byte,
				  &input[RECEPTEC_UWB_HEADER_SIZE], payload_length);
	if (status != RECEPTEC_UWB_OK) {
		return status;
	}

	frame->sequence = input[2];
	memcpy(frame->destination, &input[DESTINATION_OFFSET],
	       RECEPTEC_UWB_EUI64_SIZE);
	memcpy(frame->source, &input[SOURCE_OFFSET], RECEPTEC_UWB_EUI64_SIZE);
	frame->type = (enum receptec_uwb_packet_type)type_byte;
	frame->payload = &input[RECEPTEC_UWB_HEADER_SIZE];
	frame->payload_length = payload_length;
	return RECEPTEC_UWB_OK;
}

bool receptec_uwb_ack_matches(
	const struct receptec_uwb_frame *frame,
	const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
	const uint8_t local_eui64[RECEPTEC_UWB_EUI64_SIZE],
	uint8_t expected_sequence)
{
	return receptec_uwb_control_response_matches(frame, RECEPTEC_UWB_ACK,
						peer_eui64, local_eui64,
						expected_sequence);
}

bool receptec_uwb_control_response_matches(
	const struct receptec_uwb_frame *frame,
	enum receptec_uwb_packet_type expected_type,
	const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
	const uint8_t local_eui64[RECEPTEC_UWB_EUI64_SIZE],
	uint8_t expected_sequence)
{
	return frame != NULL && peer_eui64 != NULL && local_eui64 != NULL &&
	       (expected_type == RECEPTEC_UWB_ACK ||
		expected_type == RECEPTEC_UWB_STOP_ACK) &&
	       frame->type == expected_type &&
	       frame->payload_length == 0U &&
	       frame->sequence == expected_sequence &&
	       memcmp(frame->source, peer_eui64, RECEPTEC_UWB_EUI64_SIZE) == 0 &&
	       memcmp(frame->destination, local_eui64,
		      RECEPTEC_UWB_EUI64_SIZE) == 0;
}

bool receptec_uwb_type_is_request(enum receptec_uwb_packet_type type)
{
	return type == RECEPTEC_UWB_DATA_REQ || type == RECEPTEC_UWB_SYNC ||
	       type == RECEPTEC_UWB_STOP;
}

int receptec_uwb_expected_response(enum receptec_uwb_packet_type request,
				   enum receptec_uwb_packet_type *response)
{
	if (response == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	switch (request) {
	case RECEPTEC_UWB_DATA_REQ:
		*response = RECEPTEC_UWB_DATA;
		return RECEPTEC_UWB_OK;
	case RECEPTEC_UWB_SYNC:
		*response = RECEPTEC_UWB_ACK;
		return RECEPTEC_UWB_OK;
	case RECEPTEC_UWB_STOP:
		*response = RECEPTEC_UWB_STOP_ACK;
		return RECEPTEC_UWB_OK;
	default:
		return RECEPTEC_UWB_ERR_TYPE;
	}
}

void receptec_uwb_encode_dt_sampling(
	uint16_t dt_sampling_us,
	uint8_t output[RECEPTEC_UWB_DT_SAMPLING_SIZE])
{
	output[0] = (uint8_t)(dt_sampling_us & 0xFFU);
	output[1] = (uint8_t)(dt_sampling_us >> 8);
}

uint16_t receptec_uwb_decode_dt_sampling(
	const uint8_t input[RECEPTEC_UWB_DT_SAMPLING_SIZE])
{
	return (uint16_t)input[0] | ((uint16_t)input[1] << 8);
}

static uint64_t get_le(const uint8_t *data, size_t width)
{
	uint64_t value = 0U;

	for (size_t i = 0; i < width; ++i) {
		value |= (uint64_t)data[i] << (8U * i);
	}
	return value;
}

static void put_le(uint8_t *data, uint64_t value, size_t width)
{
	for (size_t i = 0; i < width; ++i) {
		data[i] = (uint8_t)(value >> (8U * i));
	}
}

static bool valid_mode(enum receptec_uwb_report_mode mode)
{
	return mode == RECEPTEC_UWB_REPORT_STREAM ||
	       mode == RECEPTEC_UWB_REPORT_AGGREGATE;
}

static bool valid_plan(const struct receptec_uwb_plan *plan)
{
	if (plan == NULL || !valid_mode(plan->report_mode) ||
	    (plan->authorization_ms && (plan->authorization_ms < 20U || plan->authorization_ms > 200U))) {
		return false;
	}
	if (plan->phase == RECEPTEC_UWB_PLAN_ROUND_ARM) {
		return plan->pair_index == 0xFFU && plan->authorization_ms == 0U;
	}
	if (plan->phase != RECEPTEC_UWB_PLAN_ARM &&
	    plan->phase != RECEPTEC_UWB_PLAN_START) {
		return false;
	}
	return plan->pair_index < 3U &&
	       (plan->phase != RECEPTEC_UWB_PLAN_START ||
		plan->pair_index == 1U);
}

int receptec_uwb_plan_encode(uint8_t *out, size_t capacity,
			     const struct receptec_uwb_plan *plan)
{
	if (out == NULL || plan == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	if (!valid_plan(plan)) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	if (capacity < (plan->authorization_ms ? RECEPTEC_UWB_PLAN_V2_SIZE : RECEPTEC_UWB_PLAN_SIZE))
		return RECEPTEC_UWB_ERR_LENGTH;
	out[0] = plan->authorization_ms ? 2U : 1U;
	if (plan->authorization_ms) put_le(&out[28], plan->authorization_ms, 2U);
	out[1] = (uint8_t)plan->phase;
	put_le(&out[2], plan->round_id, 8U);
	out[10] = plan->pair_index;
	out[11] = (uint8_t)plan->report_mode;
	memcpy(&out[12], plan->initiator, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(&out[20], plan->responder, RECEPTEC_UWB_EUI64_SIZE);
	return RECEPTEC_UWB_OK;
}

int receptec_uwb_plan_decode(const uint8_t *data, size_t length,
			     struct receptec_uwb_plan *plan)
{
	if (data == NULL || plan == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	if (length != RECEPTEC_UWB_PLAN_SIZE && length != RECEPTEC_UWB_PLAN_V2_SIZE) {
		return RECEPTEC_UWB_ERR_LENGTH;
	}
	if (!((data[0] == 1U && length == RECEPTEC_UWB_PLAN_SIZE) ||
	      (data[0] == 2U && length == RECEPTEC_UWB_PLAN_V2_SIZE))) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	plan->authorization_ms = data[0] == 2U ? (uint16_t)get_le(&data[28], 2U) : 0U;
	if (data[0] == 2U && data[1] != RECEPTEC_UWB_PLAN_ROUND_ARM && plan->authorization_ms == 0U)
		return RECEPTEC_UWB_ERR_RESERVED;
	plan->phase = (enum receptec_uwb_plan_phase)data[1];
	plan->round_id = get_le(&data[2], 8U);
	plan->pair_index = data[10];
	plan->report_mode = (enum receptec_uwb_report_mode)data[11];
	memcpy(plan->initiator, &data[12], RECEPTEC_UWB_EUI64_SIZE);
	memcpy(plan->responder, &data[20], RECEPTEC_UWB_EUI64_SIZE);
	return valid_plan(plan) ? RECEPTEC_UWB_OK : RECEPTEC_UWB_ERR_RESERVED;
}

static bool valid_record(const struct receptec_uwb_range_record *record)
{
	return record->pair_index < 3U && record->status <=
		RECEPTEC_UWB_RANGE_NOT_SCHEDULED &&
	       ((record->status == RECEPTEC_UWB_RANGE_OK &&
		 record->distance_mm >= 1U && record->distance_mm <= 150000U) ||
		(record->status != RECEPTEC_UWB_RANGE_OK &&
		 record->distance_mm == RECEPTEC_UWB_DISTANCE_INVALID));
}

static bool valid_result(const struct receptec_uwb_result *result)
{
	uint8_t pairs_seen = 0U;

	if (result == NULL || !valid_mode(result->report_mode) ||
	    (result->record_count != 1U && result->record_count != 3U) ||
	    (result->report_mode == RECEPTEC_UWB_REPORT_STREAM &&
	     result->record_count != 1U)) {
		return false;
	}
	for (uint8_t i = 0U; i < result->record_count; ++i) {
		const struct receptec_uwb_range_record *record = &result->records[i];

		if (!valid_record(record) ||
		    (pairs_seen & (uint8_t)(1U << record->pair_index)) != 0U) {
			return false;
		}
		pairs_seen |= (uint8_t)(1U << record->pair_index);
	}
	return true;
}

int receptec_uwb_result_encode(uint8_t *out, size_t capacity,
			       const struct receptec_uwb_result *result,
			       size_t *length)
{
	size_t needed;

	if (out == NULL || result == NULL || length == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	if (!valid_result(result)) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	needed = RECEPTEC_UWB_RESULT_HEADER_SIZE +
		 (size_t)result->record_count * RECEPTEC_UWB_RESULT_RECORD_SIZE;
	if (capacity < needed) {
		return RECEPTEC_UWB_ERR_BUFFER_TOO_SMALL;
	}
	out[0] = 1U;
	put_le(&out[1], result->round_id, 8U);
	out[9] = (uint8_t)result->report_mode;
	out[10] = result->record_count;
	for (uint8_t i = 0U; i < result->record_count; ++i) {
		uint8_t *record_bytes = &out[11U + i * 26U];
		const struct receptec_uwb_range_record *record = &result->records[i];

		record_bytes[0] = record->pair_index;
		memcpy(&record_bytes[1], record->initiator, 8U);
		memcpy(&record_bytes[9], record->responder, 8U);
		record_bytes[17] = (uint8_t)record->status;
		put_le(&record_bytes[18], record->distance_mm, 4U);
		put_le(&record_bytes[22], record->master_rx_time_ms, 4U);
	}
	*length = needed;
	return RECEPTEC_UWB_OK;
}

int receptec_uwb_result_decode(const uint8_t *data, size_t length,
			       struct receptec_uwb_result *result)
{
	if (data == NULL || result == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	if (length < RECEPTEC_UWB_RESULT_HEADER_SIZE || data[0] != 1U) {
		return length < RECEPTEC_UWB_RESULT_HEADER_SIZE ?
			RECEPTEC_UWB_ERR_LENGTH : RECEPTEC_UWB_ERR_RESERVED;
	}
	result->round_id = get_le(&data[1], 8U);
	result->report_mode = (enum receptec_uwb_report_mode)data[9];
	result->record_count = data[10];
	if ((result->record_count != 1U && result->record_count != 3U) ||
	    length != RECEPTEC_UWB_RESULT_HEADER_SIZE +
		      (size_t)result->record_count * RECEPTEC_UWB_RESULT_RECORD_SIZE) {
		return RECEPTEC_UWB_ERR_LENGTH;
	}
	for (uint8_t i = 0U; i < result->record_count; ++i) {
		const uint8_t *record_bytes = &data[11U + i * 26U];
		struct receptec_uwb_range_record *record = &result->records[i];

		record->pair_index = record_bytes[0];
		memcpy(record->initiator, &record_bytes[1], 8U);
		memcpy(record->responder, &record_bytes[9], 8U);
		record->status = (enum receptec_uwb_range_status)record_bytes[17];
		record->distance_mm = (uint32_t)get_le(&record_bytes[18], 4U);
		record->master_rx_time_ms = (uint32_t)get_le(&record_bytes[22], 4U);
	}
	return valid_result(result) ? RECEPTEC_UWB_OK : RECEPTEC_UWB_ERR_RESERVED;
}

static bool valid_u40_tx(uint64_t timestamp)
{
	return timestamp < (1ULL << 40) && (timestamp & 0x1FFULL) == 0ULL;
}

int receptec_uwb_twr_response_encode(uint8_t out[RECEPTEC_UWB_RESPONSE_SIZE],
				      const struct receptec_uwb_twr_response *response)
{
	if (out == NULL || response == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	if (response->pair_index >= 3U || response->poll_rx_ts >= (1ULL << 40) ||
	    !valid_u40_tx(response->response_tx_ts)) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	put_le(out, response->round_id, 8U);
	out[8] = response->pair_index;
	put_le(&out[9], response->poll_rx_ts, 5U);
	put_le(&out[14], response->response_tx_ts, 5U);
	return RECEPTEC_UWB_OK;
}

int receptec_uwb_twr_response_decode(const uint8_t *data, size_t length,
				      struct receptec_uwb_twr_response *response)
{
	if (data == NULL || response == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	if (length != RECEPTEC_UWB_RESPONSE_SIZE) {
		return RECEPTEC_UWB_ERR_LENGTH;
	}
	response->round_id = get_le(data, 8U);
	response->pair_index = data[8];
	response->poll_rx_ts = get_le(&data[9], 5U);
	response->response_tx_ts = get_le(&data[14], 5U);
	return response->pair_index < 3U &&
	       valid_u40_tx(response->response_tx_ts) ?
		RECEPTEC_UWB_OK : RECEPTEC_UWB_ERR_RESERVED;
}

int receptec_uwb_twr_final_encode(uint8_t out[RECEPTEC_UWB_FINAL_SIZE],
				   const struct receptec_uwb_twr_final *final)
{
	if (out == NULL || final == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	if (final->pair_index >= 3U || !valid_u40_tx(final->poll_tx_ts) ||
	    final->response_rx_ts >= (1ULL << 40) ||
	    !valid_u40_tx(final->final_tx_ts)) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	put_le(out, final->round_id, 8U);
	out[8] = final->pair_index;
	put_le(&out[9], final->poll_tx_ts, 5U);
	put_le(&out[14], final->response_rx_ts, 5U);
	put_le(&out[19], final->final_tx_ts, 5U);
	return RECEPTEC_UWB_OK;
}

int receptec_uwb_twr_final_decode(const uint8_t *data, size_t length,
				   struct receptec_uwb_twr_final *final)
{
	if (data == NULL || final == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	if (length != RECEPTEC_UWB_FINAL_SIZE) {
		return RECEPTEC_UWB_ERR_LENGTH;
	}
	final->round_id = get_le(data, 8U);
	final->pair_index = data[8];
	final->poll_tx_ts = get_le(&data[9], 5U);
	final->response_rx_ts = get_le(&data[14], 5U);
	final->final_tx_ts = get_le(&data[19], 5U);
	return final->pair_index < 3U && valid_u40_tx(final->poll_tx_ts) &&
	       valid_u40_tx(final->final_tx_ts) ?
		RECEPTEC_UWB_OK : RECEPTEC_UWB_ERR_RESERVED;
}

int receptec_uwb_distance_mm(const struct receptec_uwb_twr_response *response,
			      const struct receptec_uwb_twr_final *final,
			      uint64_t final_rx_ts, uint32_t *distance_mm)
{
	const uint64_t mask = (1ULL << 40) - 1ULL;
	const uint64_t max_interval = 1277952000ULL;
	uint64_t ra, da, rb, db, denominator, remainder, ticks, millimeters;
	int64_t numerator;

	if (response == NULL || final == NULL || distance_mm == NULL ||
	    final_rx_ts > mask || response->round_id != final->round_id ||
	    response->pair_index != final->pair_index ||
	    !valid_u40_tx(response->response_tx_ts) ||
	    !valid_u40_tx(final->poll_tx_ts) || !valid_u40_tx(final->final_tx_ts)) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
	}
	ra = (final->response_rx_ts - final->poll_tx_ts) & mask;
	da = (final->final_tx_ts - final->response_rx_ts) & mask;
	rb = (final_rx_ts - response->response_tx_ts) & mask;
	db = (response->response_tx_ts - response->poll_rx_ts) & mask;
	if (ra > max_interval || da > max_interval || rb > max_interval ||
	    db > max_interval) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	denominator = ra + da + rb + db;
	if (denominator == 0U) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	numerator = (int64_t)(ra * rb) - (int64_t)(da * db);
	if (numerator <= 0) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	ticks = (uint64_t)numerator / denominator;
	remainder = (uint64_t)numerator % denominator;
	ticks += 2U * remainder >= denominator ? 1U : 0U;
	if (ticks < 1U || ticks > 31971U) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	numerator = (int64_t)(ticks * 299792458000ULL);
	millimeters = (uint64_t)numerator / 63897600000ULL;
	remainder = (uint64_t)numerator % 63897600000ULL;
	millimeters += 2U * remainder >= 63897600000ULL ? 1U : 0U;
	if (millimeters < 1U || millimeters > 150000U) {
		return RECEPTEC_UWB_ERR_RESERVED;
	}
	*distance_mm = (uint32_t)millimeters;
	return RECEPTEC_UWB_OK;
}
