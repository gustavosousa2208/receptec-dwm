/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_PROTOCOL_H_
#define RECEPTEC_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECEPTEC_PROTOCOL_FCF_LSB 0x41U
#define RECEPTEC_PROTOCOL_FCF_MSB 0xECU
#define RECEPTEC_PROTOCOL_EUI64_SIZE 8U
#define RECEPTEC_PROTOCOL_HEADER_SIZE 20U
#define RECEPTEC_PROTOCOL_FCS_SIZE 2U
#define RECEPTEC_PROTOCOL_MAX_PSDU_SIZE 127U
#define RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE \
	(RECEPTEC_PROTOCOL_MAX_PSDU_SIZE - RECEPTEC_PROTOCOL_FCS_SIZE)
#define RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE \
	(RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE - RECEPTEC_PROTOCOL_HEADER_SIZE)
#define RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE 2U

enum receptec_packet_type {
	RECEPTEC_PACKET_DATA_REQ = 0x0,
	RECEPTEC_PACKET_SYNC = 0x1,
	RECEPTEC_PACKET_DATA = 0x2,
	RECEPTEC_PACKET_ACK = 0x3,
	RECEPTEC_PACKET_STOP = 0x4,
	RECEPTEC_PACKET_STOP_ACK = 0x5,
};

enum receptec_protocol_status {
	RECEPTEC_PROTOCOL_OK = 0,
	RECEPTEC_PROTOCOL_ERR_ARGUMENT = -1,
	RECEPTEC_PROTOCOL_ERR_BUFFER_TOO_SMALL = -2,
	RECEPTEC_PROTOCOL_ERR_LENGTH = -3,
	RECEPTEC_PROTOCOL_ERR_FCF = -4,
	RECEPTEC_PROTOCOL_ERR_RESERVED = -5,
	RECEPTEC_PROTOCOL_ERR_TYPE = -6,
};

struct receptec_frame {
	uint8_t sequence;
	uint8_t destination[RECEPTEC_PROTOCOL_EUI64_SIZE];
	uint8_t source[RECEPTEC_PROTOCOL_EUI64_SIZE];
	enum receptec_packet_type type;
	const uint8_t *payload;
	size_t payload_length;
};

/* EUI-64 arrays are always stored in IEEE 802.15.4 on-air order. */
int receptec_protocol_encode(uint8_t *output, size_t output_capacity,
			     const struct receptec_frame *frame,
			     size_t *encoded_length);

/* The decoded payload points into input and is valid only while input is. */
int receptec_protocol_decode(const uint8_t *input, size_t input_length,
			     struct receptec_frame *frame);

bool receptec_protocol_type_is_request(enum receptec_packet_type type);
bool receptec_protocol_type_is_response(enum receptec_packet_type type);
int receptec_protocol_expected_response(enum receptec_packet_type request,
					enum receptec_packet_type *response);
void receptec_protocol_encode_dt_sampling(
	uint16_t dt_sampling_us,
	uint8_t output[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE]);
uint16_t receptec_protocol_decode_dt_sampling(
	const uint8_t input[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE]);

#endif /* RECEPTEC_PROTOCOL_H_ */
