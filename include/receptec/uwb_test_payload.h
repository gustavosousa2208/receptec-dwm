/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_TEST_PAYLOAD_H_
#define RECEPTEC_UWB_TEST_PAYLOAD_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Structural check of the COLETEC DATA payload v1 (contracts/protocol/
 * uwb-receptec-coletec/payload-v1.md, draft): 105 bytes = 8-byte header, the 88-byte
 * STM32 SUMMARY_DATA verbatim, 9 bytes of zero padding.
 *
 * It validates the layout the contract fixes and nothing about sensor values. It is
 * an implementation check, not a stable application format, and it lives here, not in
 * the frame codec, so the codec keeps knowing nothing about payload contents.
 */

#define RECEPTEC_UWB_PAYLOAD_V1_SIZE 105U
#define RECEPTEC_UWB_PAYLOAD_V1_HEADER_SIZE 8U
#define RECEPTEC_UWB_PAYLOAD_V1_SUMMARY_SIZE 88U
#define RECEPTEC_UWB_PAYLOAD_V1_VERSION 1U
#define RECEPTEC_UWB_PAYLOAD_V1_FLAG_SUMMARY_VALID 0x01U
#define RECEPTEC_UWB_PAYLOAD_V1_FLAG_SUMMARY_STALE 0x02U

enum receptec_uwb_payload_status {
	RECEPTEC_UWB_PAYLOAD_OK = 0,
	RECEPTEC_UWB_PAYLOAD_WRONG_LENGTH,
	RECEPTEC_UWB_PAYLOAD_WRONG_VERSION,
	RECEPTEC_UWB_PAYLOAD_RESERVED_FLAGS,
	RECEPTEC_UWB_PAYLOAD_STALE_WITHOUT_SUMMARY,
	RECEPTEC_UWB_PAYLOAD_SUMMARY_NOT_ZERO,
	RECEPTEC_UWB_PAYLOAD_SUMMARY_HEADER,
	RECEPTEC_UWB_PAYLOAD_PADDING,
};

struct receptec_uwb_payload_info {
	uint8_t flags;
	uint16_t sequence;
	uint32_t age_ms;
};

enum receptec_uwb_payload_duplicate {
	RECEPTEC_UWB_DUPLICATE_SAME = 0,
	RECEPTEC_UWB_DUPLICATE_SEQUENCE_CHANGED,
	RECEPTEC_UWB_DUPLICATE_AGE_CHANGED,
	RECEPTEC_UWB_DUPLICATE_PAYLOAD_CHANGED,
	RECEPTEC_UWB_DUPLICATE_LENGTH_CHANGED,
};

/* Stable failure token for the UWB_TEST reason field ("wrong-length", ...). */
const char *receptec_uwb_payload_reason(enum receptec_uwb_payload_status status);

/* Validate one payload; info is filled when the header could be read. */
enum receptec_uwb_payload_status receptec_uwb_payload_v1_check(
	const uint8_t *payload, size_t length, struct receptec_uwb_payload_info *info);

/*
 * Compare two DATA payloads from one transaction. A retransmitted request must be
 * answered from the same staged payload, byte for byte, including sequence and age.
 */
enum receptec_uwb_payload_duplicate receptec_uwb_payload_compare(
	const uint8_t *first, size_t first_length,
	const uint8_t *second, size_t second_length);

const char *receptec_uwb_payload_duplicate_reason(
	enum receptec_uwb_payload_duplicate result);

#endif /* RECEPTEC_UWB_TEST_PAYLOAD_H_ */
