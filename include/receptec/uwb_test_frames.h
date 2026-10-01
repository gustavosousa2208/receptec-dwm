/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_TEST_FRAMES_H_
#define RECEPTEC_UWB_TEST_FRAMES_H_

#include <stddef.h>
#include <stdint.h>

#include <receptec/uwb_protocol.h>

/*
 * Fixed, named mutations of an otherwise valid DATA_REQ, used to check that the
 * production COLETEC rejects malformed requests in software and stays healthy.
 * There is deliberately no free-form frame builder: every mutation is one case
 * documented here and asserted byte for byte by the host tests.
 *
 * All of them are sent with a hardware-generated valid FCS, so the COLETEC hardware
 * filter accepts them and the rejection has to come from its software checks.
 */

enum receptec_uwb_test_mutation {
	RECEPTEC_UWB_MUTATION_WRONG_SOURCE = 0,
	RECEPTEC_UWB_MUTATION_BAD_TYPE,
	RECEPTEC_UWB_MUTATION_BAD_RESERVED,
	RECEPTEC_UWB_MUTATION_BAD_LENGTH,
	RECEPTEC_UWB_MUTATION_BAD_DT,
	RECEPTEC_UWB_MUTATION_COUNT,
};

/* Source EUI-64 no COLETEC knows (wrong_source). Fixed, locally administered. */
extern const uint8_t receptec_uwb_test_unknown_source[RECEPTEC_UWB_EUI64_SIZE];

/*
 * Type byte with the high nibble zero that is not a defined packet type (bad_type).
 * 0x0-0xA are defined through RECEPTEC_UWB_RANGE_FINAL; 0xB-0xF remain reserved.
 */
#define RECEPTEC_UWB_TEST_RESERVED_TYPE 0x0BU
/* Reserved high-nibble bit set on an otherwise valid DATA_REQ type byte (bad_reserved). */
#define RECEPTEC_UWB_TEST_RESERVED_BIT 0x10U
/*
 * DT_sampling far outside any assigned slot (bad_dt). The production COLETEC slot
 * is 30 ms, so 60 ms cannot be a valid offset inside it; the bench RECEPTEC slot is
 * 20 ms. It is a named constant, not a shell argument.
 */
#define RECEPTEC_UWB_TEST_BAD_DT_US 60000U

/* Stable name used in the UWB_TEST line and on the shell: "wrong_source"... */
const char *receptec_uwb_test_mutation_name(enum receptec_uwb_test_mutation mutation);
/* -1 when the name is not one of the five mutations. */
int receptec_uwb_test_mutation_from_name(const char *name);

/*
 * Build the mutated request into output. destination is the target peer, source is
 * the RECEPTEC EUI-64 (ignored by wrong_source), dt_sampling_us is the offset a
 * valid request would carry. Returns 0 and the logical length (without FCS), or
 * a negative errno.
 */
int receptec_uwb_test_build_mutation(enum receptec_uwb_test_mutation mutation,
				     const uint8_t destination[RECEPTEC_UWB_EUI64_SIZE],
				     const uint8_t source[RECEPTEC_UWB_EUI64_SIZE],
				     uint8_t sequence, uint16_t dt_sampling_us,
				     uint8_t *output, size_t capacity,
				     size_t *logical_length);

/*
 * Request sequence numbers of the three staged transactions of sequence_wrap: 254, 255, 0.
 * index is taken modulo 3. The test restores the peer runtime's own next sequence
 * afterwards, so it never leaves the following ordinary cycle out of step.
 */
uint8_t receptec_uwb_test_wrap_sequence(size_t index);

#endif /* RECEPTEC_UWB_TEST_FRAMES_H_ */
