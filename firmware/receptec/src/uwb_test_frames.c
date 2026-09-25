/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/uwb_test_frames.h>

#include <errno.h>
#include <string.h>

#define TYPE_OFFSET 19U
#define SOURCE_OFFSET 11U

const uint8_t receptec_uwb_test_unknown_source[RECEPTEC_UWB_EUI64_SIZE] = {
	0x02, 0xA7, 0x51, 0xDE, 0xAD, 0xBE, 0xEF, 0x01,
};

static const char *const mutation_names[RECEPTEC_UWB_MUTATION_COUNT] = {
	[RECEPTEC_UWB_MUTATION_WRONG_SOURCE] = "wrong_source",
	[RECEPTEC_UWB_MUTATION_BAD_TYPE] = "bad_type",
	[RECEPTEC_UWB_MUTATION_BAD_RESERVED] = "bad_reserved",
	[RECEPTEC_UWB_MUTATION_BAD_LENGTH] = "bad_length",
	[RECEPTEC_UWB_MUTATION_BAD_DT] = "bad_dt",
};

const char *receptec_uwb_test_mutation_name(enum receptec_uwb_test_mutation mutation)
{
	if ((unsigned int)mutation >= RECEPTEC_UWB_MUTATION_COUNT) {
		return NULL;
	}
	return mutation_names[mutation];
}

int receptec_uwb_test_mutation_from_name(const char *name)
{
	int i;

	if (name == NULL) {
		return -1;
	}
	for (i = 0; i < (int)RECEPTEC_UWB_MUTATION_COUNT; i++) {
		if (strcmp(name, mutation_names[i]) == 0) {
			return i;
		}
	}
	return -1;
}

uint8_t receptec_uwb_test_wrap_sequence(size_t index)
{
	static const uint8_t wrap[3] = { 254U, 255U, 0U };

	return wrap[index % 3U];
}

int receptec_uwb_test_build_mutation(enum receptec_uwb_test_mutation mutation,
				     const uint8_t destination[RECEPTEC_UWB_EUI64_SIZE],
				     const uint8_t source[RECEPTEC_UWB_EUI64_SIZE],
				     uint8_t sequence, uint16_t dt_sampling_us,
				     uint8_t *output, size_t capacity,
				     size_t *logical_length)
{
	uint8_t dt[RECEPTEC_UWB_DT_SAMPLING_SIZE];
	struct receptec_uwb_frame frame = {
		.sequence = sequence,
		.type = RECEPTEC_UWB_DATA_REQ,
		.payload = dt,
		.payload_length = sizeof(dt),
	};
	size_t length;
	int ret;

	if (output == NULL || logical_length == NULL || destination == NULL ||
	    source == NULL || (unsigned int)mutation >= RECEPTEC_UWB_MUTATION_COUNT) {
		return -EINVAL;
	}
	if (mutation == RECEPTEC_UWB_MUTATION_BAD_DT) {
		dt_sampling_us = RECEPTEC_UWB_TEST_BAD_DT_US;
	}
	receptec_uwb_encode_dt_sampling(dt_sampling_us, dt);
	memcpy(frame.destination, destination, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(frame.source, mutation == RECEPTEC_UWB_MUTATION_WRONG_SOURCE ?
	       receptec_uwb_test_unknown_source : source, RECEPTEC_UWB_EUI64_SIZE);
	/* Start from a valid DATA_REQ: the codec refuses to build a malformed one. */
	ret = receptec_uwb_encode(output, capacity, &frame, &length);
	if (ret != RECEPTEC_UWB_OK) {
		return -EINVAL;
	}

	switch (mutation) {
	case RECEPTEC_UWB_MUTATION_BAD_TYPE:
		output[TYPE_OFFSET] = RECEPTEC_UWB_TEST_RESERVED_TYPE;
		break;
	case RECEPTEC_UWB_MUTATION_BAD_RESERVED:
		output[TYPE_OFFSET] = (uint8_t)(RECEPTEC_UWB_DATA_REQ |
						RECEPTEC_UWB_TEST_RESERVED_BIT);
		break;
	case RECEPTEC_UWB_MUTATION_BAD_LENGTH:
		/* DT_sampling truncated to one byte: 21 logical bytes instead of 22. */
		length = RECEPTEC_UWB_HEADER_SIZE + 1U;
		break;
	default:
		break;
	}
	*logical_length = length;
	return 0;
}
