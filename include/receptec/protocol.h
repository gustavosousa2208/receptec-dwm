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

/*
 * Retransmission backoff — protocol-v1.0 §4.2.
 *
 *     T_wait(n) = T_base × 2^n + random(0, T_jitter_max)
 *
 * T_wait(n) is the wait *after* attempt n fails, so the schedule this produces
 * is cumulative from the slot epoch: attempt 0 always leaves at offset 0, and
 * every later attempt sits at the previous offset plus its own T_wait. That
 * cumulative offset is exactly what the RECEPTEC reports as DT_sampling, which
 * is why the schedule lives here beside the DT_sampling codec.
 *
 * The values are bench values, not qualified ones: the spec leaves T_base,
 * T_jitter_max and N_retry_max as TBD (OI-04, OI-05). They are sized so the
 * whole schedule stays well inside the 16-bit DT_sampling field.
 */
#define RECEPTEC_PROTOCOL_ATTEMPT_COUNT 3U
#define RECEPTEC_PROTOCOL_BACKOFF_BASE_US 3000U
#define RECEPTEC_PROTOCOL_BACKOFF_JITTER_MAX_US 500U

/*
 * Largest offset the schedule can produce, with every jitter draw maximal:
 * T_base × (2^(N-1) − 1) accumulated wait, plus (N−1) jitter draws. The COLETEC
 * range-checks a received DT_sampling against this — it cannot match against a
 * fixed table any more, because the jitter makes each schedule unique.
 */
#define RECEPTEC_PROTOCOL_ATTEMPT_OFFSET_MAX_US                                \
	(RECEPTEC_PROTOCOL_BACKOFF_BASE_US *                                   \
		 ((1U << (RECEPTEC_PROTOCOL_ATTEMPT_COUNT - 1U)) - 1U) +       \
	 (RECEPTEC_PROTOCOL_ATTEMPT_COUNT - 1U) *                              \
		 RECEPTEC_PROTOCOL_BACKOFF_JITTER_MAX_US)

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

/*
 * Fill offsets_us with one transaction's backoff schedule. random_words carries
 * one raw random draw per attempt; entry 0 is ignored, since attempt 0 always
 * leaves at offset 0. Keeping the randomness an input makes the schedule pure
 * and testable on the host — the caller supplies sys_rand32_get().
 */
void receptec_protocol_backoff_schedule(
	const uint32_t random_words[RECEPTEC_PROTOCOL_ATTEMPT_COUNT],
	uint16_t offsets_us[RECEPTEC_PROTOCOL_ATTEMPT_COUNT]);

/*
 * Smallest offset the last attempt can carry — its accumulated wait with every
 * jitter draw zero. No earlier attempt can reach it, because the largest offset
 * attempt N-2 can draw is strictly below this, so a COLETEC can still recognise
 * a final-attempt request without knowing the schedule that produced it.
 */
#define RECEPTEC_PROTOCOL_ATTEMPT_OFFSET_FINAL_MIN_US                          \
	(RECEPTEC_PROTOCOL_BACKOFF_BASE_US *                                   \
	 ((1U << (RECEPTEC_PROTOCOL_ATTEMPT_COUNT - 1U)) - 1U))

/* True when dt_sampling_us could have come from a valid backoff schedule. */
bool receptec_protocol_attempt_offset_valid(uint16_t dt_sampling_us);

#endif /* RECEPTEC_PROTOCOL_H_ */
