/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_TEST_RESULT_H_
#define RECEPTEC_UWB_TEST_RESULT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * One terminal line per runtime test, parsed by scripts/uwb-test-runner.py:
 *
 *   UWB_TEST result=PASS test=<name> slot=<0-3> key=value ...
 *   UWB_TEST result=FAIL test=<name> slot=<0-3> reason=<token> key=value ...
 *
 * result, test and slot are always present. Everything is a lowercase token with no
 * spaces: names and keys use [a-z0-9_-], values also allow '.', and a reason is
 * hyphenated [a-z0-9-]. The line is a compatibility surface for the host runner.
 * A timeout is FAIL with reason=timeout and an abort is FAIL with
 * reason=aborted, so a runner has exactly two results to tell apart.
 */

#define RECEPTEC_UWB_TEST_LINE_SIZE 160U
#define RECEPTEC_UWB_TEST_MAX_FIELDS 8U

enum receptec_uwb_test_outcome {
	RECEPTEC_UWB_TEST_PASS = 0,
	RECEPTEC_UWB_TEST_FAIL,
	RECEPTEC_UWB_TEST_TIMEOUT,
	RECEPTEC_UWB_TEST_ABORT,
};

struct receptec_uwb_test_field {
	const char *key;
	char value[24];
};

struct receptec_uwb_test_result {
	const char *name;
	uint8_t slot;
	enum receptec_uwb_test_outcome outcome;
	const char *reason; /* required for FAIL, ignored for PASS, fixed for TIMEOUT/ABORT */
	struct receptec_uwb_test_field fields[RECEPTEC_UWB_TEST_MAX_FIELDS];
	size_t field_count;
};

void receptec_uwb_test_result_init(struct receptec_uwb_test_result *result,
				   const char *name, uint8_t slot);
/* Append key=<unsigned> or key=<signed>; false when the field table is full. */
bool receptec_uwb_test_result_add_u32(struct receptec_uwb_test_result *result,
				      const char *key, uint32_t value);
bool receptec_uwb_test_result_add_i32(struct receptec_uwb_test_result *result,
				      const char *key, int32_t value);
bool receptec_uwb_test_result_add_token(struct receptec_uwb_test_result *result,
					const char *key, const char *value);
/*
 * Format the terminal line (no newline). Returns the length, or a negative
 * errno: -EINVAL for a result that would break the line contract (bad token,
 * missing reason on FAIL, slot above 3), -ENOSPC when the buffer is too small.
 */
int receptec_uwb_test_result_format(const struct receptec_uwb_test_result *result,
				    char *output, size_t capacity);

#endif /* RECEPTEC_UWB_TEST_RESULT_H_ */
