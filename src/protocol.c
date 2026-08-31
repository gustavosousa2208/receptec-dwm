/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/protocol.h>

#include <string.h>

#define DESTINATION_OFFSET 3U
#define SOURCE_OFFSET 11U
#define TYPE_OFFSET 19U

static int validate_payload(enum receptec_packet_type type,
			    size_t payload_length)
{
	switch (type) {
	case RECEPTEC_PACKET_DATA_REQ:
	case RECEPTEC_PACKET_SYNC:
		return payload_length == RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE
			       ? RECEPTEC_PROTOCOL_OK
			       : RECEPTEC_PROTOCOL_ERR_LENGTH;
	case RECEPTEC_PACKET_DATA:
		return payload_length <= RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE
			       ? RECEPTEC_PROTOCOL_OK
			       : RECEPTEC_PROTOCOL_ERR_LENGTH;
	case RECEPTEC_PACKET_ACK:
	case RECEPTEC_PACKET_STOP:
	case RECEPTEC_PACKET_STOP_ACK:
		return payload_length == 0U ? RECEPTEC_PROTOCOL_OK
					    : RECEPTEC_PROTOCOL_ERR_LENGTH;
	default:
		return RECEPTEC_PROTOCOL_ERR_TYPE;
	}
}

int receptec_protocol_encode(uint8_t *output, size_t output_capacity,
			     const struct receptec_frame *frame,
			     size_t *encoded_length)
{
	size_t logical_length;
	int status;

	if (output == NULL || frame == NULL || encoded_length == NULL) {
		return RECEPTEC_PROTOCOL_ERR_ARGUMENT;
	}

	status = validate_payload(frame->type, frame->payload_length);
	if (status != RECEPTEC_PROTOCOL_OK) {
		return status;
	}
	if (frame->payload_length > 0U && frame->payload == NULL) {
		return RECEPTEC_PROTOCOL_ERR_ARGUMENT;
	}

	logical_length = RECEPTEC_PROTOCOL_HEADER_SIZE + frame->payload_length;
	if (output_capacity < logical_length) {
		return RECEPTEC_PROTOCOL_ERR_BUFFER_TOO_SMALL;
	}

	output[0] = RECEPTEC_PROTOCOL_FCF_LSB;
	output[1] = RECEPTEC_PROTOCOL_FCF_MSB;
	output[2] = frame->sequence;
	memcpy(&output[DESTINATION_OFFSET], frame->destination,
	       RECEPTEC_PROTOCOL_EUI64_SIZE);
	memcpy(&output[SOURCE_OFFSET], frame->source,
	       RECEPTEC_PROTOCOL_EUI64_SIZE);
	output[TYPE_OFFSET] = (uint8_t)frame->type;
	if (frame->payload_length > 0U) {
		memcpy(&output[RECEPTEC_PROTOCOL_HEADER_SIZE], frame->payload,
		       frame->payload_length);
	}

	*encoded_length = logical_length;
	return RECEPTEC_PROTOCOL_OK;
}

int receptec_protocol_decode(const uint8_t *input, size_t input_length,
			     struct receptec_frame *frame)
{
	uint8_t type_byte;
	size_t payload_length;
	int status;

	if (input == NULL || frame == NULL) {
		return RECEPTEC_PROTOCOL_ERR_ARGUMENT;
	}
	if (input_length < RECEPTEC_PROTOCOL_HEADER_SIZE ||
	    input_length > RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE) {
		return RECEPTEC_PROTOCOL_ERR_LENGTH;
	}
	if (input[0] != RECEPTEC_PROTOCOL_FCF_LSB ||
	    input[1] != RECEPTEC_PROTOCOL_FCF_MSB) {
		return RECEPTEC_PROTOCOL_ERR_FCF;
	}

	type_byte = input[TYPE_OFFSET];
	if ((type_byte & 0xF0U) != 0U) {
		return RECEPTEC_PROTOCOL_ERR_RESERVED;
	}

	payload_length = input_length - RECEPTEC_PROTOCOL_HEADER_SIZE;
	status = validate_payload((enum receptec_packet_type)type_byte,
				  payload_length);
	if (status != RECEPTEC_PROTOCOL_OK) {
		return status;
	}

	frame->sequence = input[2];
	memcpy(frame->destination, &input[DESTINATION_OFFSET],
	       RECEPTEC_PROTOCOL_EUI64_SIZE);
	memcpy(frame->source, &input[SOURCE_OFFSET],
	       RECEPTEC_PROTOCOL_EUI64_SIZE);
	frame->type = (enum receptec_packet_type)type_byte;
	frame->payload = &input[RECEPTEC_PROTOCOL_HEADER_SIZE];
	frame->payload_length = payload_length;
	return RECEPTEC_PROTOCOL_OK;
}

bool receptec_protocol_type_is_request(enum receptec_packet_type type)
{
	return type == RECEPTEC_PACKET_DATA_REQ || type == RECEPTEC_PACKET_SYNC ||
	       type == RECEPTEC_PACKET_STOP;
}

bool receptec_protocol_type_is_response(enum receptec_packet_type type)
{
	return type == RECEPTEC_PACKET_DATA || type == RECEPTEC_PACKET_ACK ||
	       type == RECEPTEC_PACKET_STOP_ACK;
}

int receptec_protocol_expected_response(enum receptec_packet_type request,
					enum receptec_packet_type *response)
{
	if (response == NULL) {
		return RECEPTEC_PROTOCOL_ERR_ARGUMENT;
	}

	switch (request) {
	case RECEPTEC_PACKET_DATA_REQ:
		*response = RECEPTEC_PACKET_DATA;
		return RECEPTEC_PROTOCOL_OK;
	case RECEPTEC_PACKET_SYNC:
		*response = RECEPTEC_PACKET_ACK;
		return RECEPTEC_PROTOCOL_OK;
	case RECEPTEC_PACKET_STOP:
		*response = RECEPTEC_PACKET_STOP_ACK;
		return RECEPTEC_PROTOCOL_OK;
	default:
		return RECEPTEC_PROTOCOL_ERR_TYPE;
	}
}

void receptec_protocol_encode_dt_sampling(
	uint16_t dt_sampling_us,
	uint8_t output[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE])
{
	output[0] = (uint8_t)(dt_sampling_us & 0xFFU);
	output[1] = (uint8_t)(dt_sampling_us >> 8);
}

uint16_t receptec_protocol_decode_dt_sampling(
	const uint8_t input[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE])
{
	return (uint16_t)input[0] | ((uint16_t)input[1] << 8);
}

void receptec_protocol_backoff_schedule(
	const uint32_t random_words[RECEPTEC_PROTOCOL_ATTEMPT_COUNT],
	uint16_t offsets_us[RECEPTEC_PROTOCOL_ATTEMPT_COUNT])
{
	uint32_t offset = 0U;

	offsets_us[0] = 0U;
	for (size_t attempt = 1U; attempt < RECEPTEC_PROTOCOL_ATTEMPT_COUNT;
	     ++attempt) {
		uint32_t wait = RECEPTEC_PROTOCOL_BACKOFF_BASE_US
				<< (attempt - 1U);
		uint32_t jitter = random_words[attempt] %
				  (RECEPTEC_PROTOCOL_BACKOFF_JITTER_MAX_US + 1U);

		offset += wait + jitter;
		offsets_us[attempt] = (uint16_t)offset;
	}
}

bool receptec_protocol_attempt_offset_valid(uint16_t dt_sampling_us)
{
	return dt_sampling_us <= RECEPTEC_PROTOCOL_ATTEMPT_OFFSET_MAX_US;
}
