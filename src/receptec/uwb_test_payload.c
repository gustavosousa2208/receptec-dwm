/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/uwb_test_payload.h>

#include <stdbool.h>
#include <string.h>

#define OFFSET_FLAGS 1U
#define OFFSET_SEQUENCE 2U
#define OFFSET_AGE 4U
#define OFFSET_SUMMARY RECEPTEC_UWB_PAYLOAD_V1_HEADER_SIZE
#define OFFSET_PADDING \
	(RECEPTEC_UWB_PAYLOAD_V1_HEADER_SIZE + RECEPTEC_UWB_PAYLOAD_V1_SUMMARY_SIZE)
#define RESERVED_FLAGS_MASK 0xFCU

static bool all_zero(const uint8_t *data, size_t length)
{
	size_t i;

	for (i = 0U; i < length; i++) {
		if (data[i] != 0U) {
			return false;
		}
	}
	return true;
}

const char *receptec_uwb_payload_reason(enum receptec_uwb_payload_status status)
{
	switch (status) {
	case RECEPTEC_UWB_PAYLOAD_OK:
		return "ok";
	case RECEPTEC_UWB_PAYLOAD_WRONG_LENGTH:
		return "wrong-length";
	case RECEPTEC_UWB_PAYLOAD_WRONG_VERSION:
		return "wrong-version";
	case RECEPTEC_UWB_PAYLOAD_RESERVED_FLAGS:
		return "reserved-flags";
	case RECEPTEC_UWB_PAYLOAD_STALE_WITHOUT_SUMMARY:
		return "stale-without-summary";
	case RECEPTEC_UWB_PAYLOAD_SUMMARY_NOT_ZERO:
		return "summary-not-zero";
	case RECEPTEC_UWB_PAYLOAD_SUMMARY_HEADER:
		return "summary-header";
	case RECEPTEC_UWB_PAYLOAD_PADDING:
		return "padding-not-zero";
	default:
		return "unknown";
	}
}

enum receptec_uwb_payload_status receptec_uwb_payload_v1_check(
	const uint8_t *payload, size_t length, struct receptec_uwb_payload_info *info)
{
	uint8_t flags;

	if (payload == NULL || length != RECEPTEC_UWB_PAYLOAD_V1_SIZE) {
		return RECEPTEC_UWB_PAYLOAD_WRONG_LENGTH;
	}
	flags = payload[OFFSET_FLAGS];
	if (info != NULL) {
		info->flags = flags;
		info->sequence = (uint16_t)(payload[OFFSET_SEQUENCE] |
					    (payload[OFFSET_SEQUENCE + 1U] << 8));
		info->age_ms = (uint32_t)payload[OFFSET_AGE] |
			       ((uint32_t)payload[OFFSET_AGE + 1U] << 8) |
			       ((uint32_t)payload[OFFSET_AGE + 2U] << 16) |
			       ((uint32_t)payload[OFFSET_AGE + 3U] << 24);
	}
	if (payload[0] != RECEPTEC_UWB_PAYLOAD_V1_VERSION) {
		return RECEPTEC_UWB_PAYLOAD_WRONG_VERSION;
	}
	if ((flags & RESERVED_FLAGS_MASK) != 0U) {
		return RECEPTEC_UWB_PAYLOAD_RESERVED_FLAGS;
	}
	if ((flags & RECEPTEC_UWB_PAYLOAD_V1_FLAG_SUMMARY_VALID) == 0U) {
		if ((flags & RECEPTEC_UWB_PAYLOAD_V1_FLAG_SUMMARY_STALE) != 0U) {
			return RECEPTEC_UWB_PAYLOAD_STALE_WITHOUT_SUMMARY;
		}
		if (!all_zero(&payload[OFFSET_SUMMARY],
			      RECEPTEC_UWB_PAYLOAD_V1_SUMMARY_SIZE)) {
			return RECEPTEC_UWB_PAYLOAD_SUMMARY_NOT_ZERO;
		}
	} else if (payload[OFFSET_SUMMARY] == 0U) {
		/* ipc_summary_data_t starts with its own non-zero packet version. */
		return RECEPTEC_UWB_PAYLOAD_SUMMARY_HEADER;
	}
	if (!all_zero(&payload[OFFSET_PADDING],
		      RECEPTEC_UWB_PAYLOAD_V1_SIZE - OFFSET_PADDING)) {
		return RECEPTEC_UWB_PAYLOAD_PADDING;
	}
	return RECEPTEC_UWB_PAYLOAD_OK;
}

enum receptec_uwb_payload_duplicate receptec_uwb_payload_compare(
	const uint8_t *first, size_t first_length,
	const uint8_t *second, size_t second_length)
{
	if (first_length != second_length) {
		return RECEPTEC_UWB_DUPLICATE_LENGTH_CHANGED;
	}
	if (first_length == 0U || memcmp(first, second, first_length) == 0) {
		return RECEPTEC_UWB_DUPLICATE_SAME;
	}
	if (first_length > OFFSET_AGE + 3U) {
		if (memcmp(&first[OFFSET_SEQUENCE], &second[OFFSET_SEQUENCE], 2U) != 0) {
			return RECEPTEC_UWB_DUPLICATE_SEQUENCE_CHANGED;
		}
		if (memcmp(&first[OFFSET_AGE], &second[OFFSET_AGE], 4U) != 0) {
			return RECEPTEC_UWB_DUPLICATE_AGE_CHANGED;
		}
	}
	return RECEPTEC_UWB_DUPLICATE_PAYLOAD_CHANGED;
}

const char *receptec_uwb_payload_duplicate_reason(
	enum receptec_uwb_payload_duplicate result)
{
	switch (result) {
	case RECEPTEC_UWB_DUPLICATE_SAME:
		return "ok";
	case RECEPTEC_UWB_DUPLICATE_SEQUENCE_CHANGED:
		return "duplicate-sequence-changed";
	case RECEPTEC_UWB_DUPLICATE_AGE_CHANGED:
		return "duplicate-age-changed";
	case RECEPTEC_UWB_DUPLICATE_PAYLOAD_CHANGED:
		return "duplicate-payload-changed";
	case RECEPTEC_UWB_DUPLICATE_LENGTH_CHANGED:
		return "duplicate-length-changed";
	default:
		return "unknown";
	}
}
