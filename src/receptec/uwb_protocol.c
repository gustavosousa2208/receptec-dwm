/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/uwb_protocol.h>

#include <string.h>

#define DESTINATION_OFFSET 3U
#define SOURCE_OFFSET 11U
#define TYPE_OFFSET 19U

static int validate_payload(enum receptec_uwb_packet_type type,
			    size_t payload_length)
{
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
	status = validate_payload(frame->type, frame->payload_length);
	if (status != RECEPTEC_UWB_OK) {
		return status;
	}
	if (frame->payload_length != 0U && frame->payload == NULL) {
		return RECEPTEC_UWB_ERR_ARGUMENT;
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
				  payload_length);
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
