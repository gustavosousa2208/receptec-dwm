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

enum receptec_uwb_packet_type {
	RECEPTEC_UWB_DATA_REQ = 0x0,
	RECEPTEC_UWB_SYNC = 0x1,
	RECEPTEC_UWB_DATA = 0x2,
	RECEPTEC_UWB_ACK = 0x3,
	RECEPTEC_UWB_STOP = 0x4,
	RECEPTEC_UWB_STOP_ACK = 0x5,
	/* Reserved until the RANGE_PLAN/RANGE_RESULT payloads are specified. */
	RECEPTEC_UWB_RANGE_PLAN = 0x6,
	RECEPTEC_UWB_RANGE_RESULT = 0x7,
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

#endif /* RECEPTEC_UWB_PROTOCOL_H_ */
