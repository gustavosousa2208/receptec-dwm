/*
 * Host tests for the runtime-test helpers: the UWB_TEST result line, the fixed
 * request mutations and the DATA payload v1 checks. No Zephyr, no hardware.
 */
#include <receptec/uwb_test_frames.h>
#include <receptec/uwb_test_payload.h>
#include <receptec/uwb_test_result.h>

#include <assert.h>
#include <errno.h>
#include <string.h>

static const uint8_t peer[RECEPTEC_UWB_EUI64_SIZE] = {
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
};
static const uint8_t local[RECEPTEC_UWB_EUI64_SIZE] = {
	0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E,
};

/* ---------------------------------------------------------------- result line */

static int line(const struct receptec_uwb_test_result *r, char *out, size_t cap)
{
	return receptec_uwb_test_result_format(r, out, cap);
}

static void test_result_pass_line(void)
{
	struct receptec_uwb_test_result r;
	char out[RECEPTEC_UWB_TEST_LINE_SIZE];

	receptec_uwb_test_result_init(&r, "duplicate", 1U);
	assert(receptec_uwb_test_result_add_u32(&r, "seq", 42U));
	assert(receptec_uwb_test_result_add_i32(&r, "offset-us", -13));
	assert(line(&r, out, sizeof(out)) > 0);
	assert(strcmp(out, "UWB_TEST result=PASS test=duplicate slot=1 seq=42 offset-us=-13") == 0);
}

static void test_result_fail_timeout_abort_mapping(void)
{
	struct receptec_uwb_test_result r;
	char out[RECEPTEC_UWB_TEST_LINE_SIZE];

	receptec_uwb_test_result_init(&r, "retry", 0U);
	r.outcome = RECEPTEC_UWB_TEST_FAIL;
	r.reason = "late-response";
	assert(line(&r, out, sizeof(out)) > 0);
	assert(strcmp(out, "UWB_TEST result=FAIL test=retry slot=0 reason=late-response") == 0);

	r.outcome = RECEPTEC_UWB_TEST_TIMEOUT;
	r.reason = "ignored-for-timeout";
	assert(line(&r, out, sizeof(out)) > 0);
	assert(strcmp(out, "UWB_TEST result=FAIL test=retry slot=0 reason=timeout") == 0);

	r.outcome = RECEPTEC_UWB_TEST_ABORT;
	assert(line(&r, out, sizeof(out)) > 0);
	assert(strcmp(out, "UWB_TEST result=FAIL test=retry slot=0 reason=aborted") == 0);
}

static void test_result_contract_is_enforced(void)
{
	struct receptec_uwb_test_result r;
	char out[RECEPTEC_UWB_TEST_LINE_SIZE];

	receptec_uwb_test_result_init(&r, "wrong_source", 3U);
	assert(line(&r, out, sizeof(out)) > 0);
	/* a name with a space, an upper-case letter or nothing at all breaks the line */
	receptec_uwb_test_result_init(&r, "bad name", 0U);
	assert(line(&r, out, sizeof(out)) == -EINVAL);
	receptec_uwb_test_result_init(&r, "Retry", 0U);
	assert(line(&r, out, sizeof(out)) == -EINVAL);
	receptec_uwb_test_result_init(&r, NULL, 0U);
	assert(line(&r, out, sizeof(out)) == -EINVAL);
	/* slot above 3 */
	receptec_uwb_test_result_init(&r, "retry", 4U);
	assert(line(&r, out, sizeof(out)) == -EINVAL);
	/* FAIL without a reason, or with a reason that is not a token */
	receptec_uwb_test_result_init(&r, "retry", 0U);
	r.outcome = RECEPTEC_UWB_TEST_FAIL;
	assert(line(&r, out, sizeof(out)) == -EINVAL);
	r.reason = "two words";
	assert(line(&r, out, sizeof(out)) == -EINVAL);
	/* a field with a space in its value */
	receptec_uwb_test_result_init(&r, "retry", 0U);
	assert(receptec_uwb_test_result_add_token(&r, "note", "a b"));
	assert(line(&r, out, sizeof(out)) == -EINVAL);
}

static void test_result_buffer_and_field_limits(void)
{
	struct receptec_uwb_test_result r;
	char out[RECEPTEC_UWB_TEST_LINE_SIZE];
	char small[16];
	size_t i;

	receptec_uwb_test_result_init(&r, "retry", 0U);
	assert(line(&r, small, sizeof(small)) == -ENOSPC);
	for (i = 0U; i < RECEPTEC_UWB_TEST_MAX_FIELDS; i++) {
		assert(receptec_uwb_test_result_add_u32(&r, "k", (uint32_t)i));
	}
	assert(!receptec_uwb_test_result_add_u32(&r, "k", 9U));
	assert(line(&r, out, sizeof(out)) > 0);
	assert(receptec_uwb_test_result_add_u32(&r, "k", 1U) == false);
	receptec_uwb_test_result_init(&r, "retry", 0U);
	assert(!receptec_uwb_test_result_add_token(&r, "long", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

static void test_result_numbers(void)
{
	struct receptec_uwb_test_result r;
	char out[RECEPTEC_UWB_TEST_LINE_SIZE];

	receptec_uwb_test_result_init(&r, "rx_boundary", 2U);
	assert(receptec_uwb_test_result_add_u32(&r, "max", 4294967295U));
	assert(receptec_uwb_test_result_add_i32(&r, "min", -2147483647 - 1));
	assert(receptec_uwb_test_result_add_u32(&r, "zero", 0U));
	assert(line(&r, out, sizeof(out)) > 0);
	assert(strstr(out, "max=4294967295") != NULL);
	assert(strstr(out, "min=-2147483648") != NULL);
	assert(strstr(out, "zero=0") != NULL);
}

/* ---------------------------------------------------------------- mutations */

static void expect_valid_header(const uint8_t *f, const uint8_t *src, uint8_t seq)
{
	assert(f[0] == 0x41U && f[1] == 0xECU);
	assert(f[2] == seq);
	assert(memcmp(&f[3], peer, RECEPTEC_UWB_EUI64_SIZE) == 0);
	assert(memcmp(&f[11], src, RECEPTEC_UWB_EUI64_SIZE) == 0);
}

static void test_mutation_wrong_source(void)
{
	uint8_t f[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
	size_t n = 0U;

	assert(receptec_uwb_test_build_mutation(RECEPTEC_UWB_MUTATION_WRONG_SOURCE, peer, local,
						7U, 3000U, f, sizeof(f), &n) == 0);
	assert(n == 22U);
	expect_valid_header(f, receptec_uwb_test_unknown_source, 7U);
	assert(memcmp(&f[11], local, RECEPTEC_UWB_EUI64_SIZE) != 0);
	assert(f[19] == RECEPTEC_UWB_DATA_REQ);
	assert(f[20] == 0xB8U && f[21] == 0x0BU); /* 3000 us, little endian */
}

static void test_mutation_bad_type(void)
{
	uint8_t f[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
	struct receptec_uwb_frame decoded;
	size_t n = 0U;

	assert(receptec_uwb_test_build_mutation(RECEPTEC_UWB_MUTATION_BAD_TYPE, peer, local, 9U, 0U,
						f, sizeof(f), &n) == 0);
	assert(n == 22U);
	expect_valid_header(f, local, 9U);
	assert(f[19] == 0x06U && (f[19] & 0xF0U) == 0U);
	assert(receptec_uwb_decode(f, n, &decoded) == RECEPTEC_UWB_ERR_TYPE);
}

static void test_mutation_bad_reserved(void)
{
	uint8_t f[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
	struct receptec_uwb_frame decoded;
	size_t n = 0U;

	assert(receptec_uwb_test_build_mutation(RECEPTEC_UWB_MUTATION_BAD_RESERVED, peer, local,
						10U, 0U, f, sizeof(f), &n) == 0);
	assert(n == 22U);
	expect_valid_header(f, local, 10U);
	assert(f[19] == 0x10U);
	assert(receptec_uwb_decode(f, n, &decoded) == RECEPTEC_UWB_ERR_RESERVED);
}

static void test_mutation_bad_length(void)
{
	uint8_t f[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
	struct receptec_uwb_frame decoded;
	size_t n = 0U;

	assert(receptec_uwb_test_build_mutation(RECEPTEC_UWB_MUTATION_BAD_LENGTH, peer, local, 11U,
						0U, f, sizeof(f), &n) == 0);
	assert(n == 21U); /* header + one byte of the two-byte DT_sampling */
	expect_valid_header(f, local, 11U);
	assert(f[19] == RECEPTEC_UWB_DATA_REQ);
	assert(receptec_uwb_decode(f, n, &decoded) == RECEPTEC_UWB_ERR_LENGTH);
}

static void test_mutation_bad_dt(void)
{
	uint8_t f[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
	struct receptec_uwb_frame decoded;
	size_t n = 0U;

	/* the requested offset is ignored: the mutation always encodes the named constant */
	assert(receptec_uwb_test_build_mutation(RECEPTEC_UWB_MUTATION_BAD_DT, peer, local, 12U, 0U,
						f, sizeof(f), &n) == 0);
	assert(n == 22U);
	expect_valid_header(f, local, 12U);
	assert(receptec_uwb_decode(f, n, &decoded) == RECEPTEC_UWB_OK);
	assert(receptec_uwb_decode_dt_sampling(decoded.payload) == RECEPTEC_UWB_TEST_BAD_DT_US);
	assert(RECEPTEC_UWB_TEST_BAD_DT_US > 30000U);
}

static void test_mutation_arguments_and_names(void)
{
	uint8_t f[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
	uint8_t tiny[10];
	size_t n = 0U;
	int i;

	assert(receptec_uwb_test_build_mutation(RECEPTEC_UWB_MUTATION_BAD_TYPE, peer, local, 0U, 0U,
						tiny, sizeof(tiny), &n) == -EINVAL);
	assert(receptec_uwb_test_build_mutation(RECEPTEC_UWB_MUTATION_COUNT, peer, local, 0U, 0U,
						f, sizeof(f), &n) == -EINVAL);
	assert(receptec_uwb_test_build_mutation(RECEPTEC_UWB_MUTATION_BAD_TYPE, NULL, local, 0U, 0U,
						f, sizeof(f), &n) == -EINVAL);
	for (i = 0; i < (int)RECEPTEC_UWB_MUTATION_COUNT; i++) {
		const char *name = receptec_uwb_test_mutation_name((enum receptec_uwb_test_mutation)i);

		assert(name != NULL);
		assert(receptec_uwb_test_mutation_from_name(name) == i);
	}
	assert(receptec_uwb_test_mutation_name(RECEPTEC_UWB_MUTATION_COUNT) == NULL);
	assert(receptec_uwb_test_mutation_from_name("raw") == -1);
	assert(receptec_uwb_test_mutation_from_name(NULL) == -1);
}

static void test_wrap_sequence_order(void)
{
	assert(receptec_uwb_test_wrap_sequence(0U) == 254U);
	assert(receptec_uwb_test_wrap_sequence(1U) == 255U);
	assert(receptec_uwb_test_wrap_sequence(2U) == 0U);
	assert((uint8_t)(receptec_uwb_test_wrap_sequence(1U) + 1U) ==
	       receptec_uwb_test_wrap_sequence(2U));
	assert(receptec_uwb_test_wrap_sequence(3U) == 254U);
}

/* ---------------------------------------------------------------- payload v1 */

static void make_payload(uint8_t p[RECEPTEC_UWB_PAYLOAD_V1_SIZE], bool with_summary)
{
	memset(p, 0, RECEPTEC_UWB_PAYLOAD_V1_SIZE);
	p[0] = 1U;
	p[2] = 0x34U;
	p[3] = 0x12U;
	p[4] = 0x0AU;
	if (with_summary) {
		p[1] = RECEPTEC_UWB_PAYLOAD_V1_FLAG_SUMMARY_VALID;
		p[8] = 4U; /* ipc_summary_data_t packet version */
		p[9] = 0x3FU;
		p[20] = 0x77U;
	}
}

static void test_payload_valid_cases(void)
{
	uint8_t p[RECEPTEC_UWB_PAYLOAD_V1_SIZE];
	struct receptec_uwb_payload_info info;

	make_payload(p, false);
	assert(receptec_uwb_payload_v1_check(p, sizeof(p), &info) == RECEPTEC_UWB_PAYLOAD_OK);
	assert(info.flags == 0U && info.sequence == 0x1234U && info.age_ms == 10U);
	make_payload(p, true);
	assert(receptec_uwb_payload_v1_check(p, sizeof(p), &info) == RECEPTEC_UWB_PAYLOAD_OK);
	assert(info.flags == 1U);
	p[1] = 3U; /* valid and stale */
	assert(receptec_uwb_payload_v1_check(p, sizeof(p), NULL) == RECEPTEC_UWB_PAYLOAD_OK);
}

static void test_payload_rejections(void)
{
	uint8_t p[RECEPTEC_UWB_PAYLOAD_V1_SIZE];

	make_payload(p, true);
	assert(receptec_uwb_payload_v1_check(p, sizeof(p) - 1U, NULL) ==
	       RECEPTEC_UWB_PAYLOAD_WRONG_LENGTH);
	assert(receptec_uwb_payload_v1_check(p, 0U, NULL) == RECEPTEC_UWB_PAYLOAD_WRONG_LENGTH);
	assert(receptec_uwb_payload_v1_check(NULL, sizeof(p), NULL) ==
	       RECEPTEC_UWB_PAYLOAD_WRONG_LENGTH);
	p[0] = 2U;
	assert(receptec_uwb_payload_v1_check(p, sizeof(p), NULL) ==
	       RECEPTEC_UWB_PAYLOAD_WRONG_VERSION);
	make_payload(p, true);
	p[1] |= 0x04U;
	assert(receptec_uwb_payload_v1_check(p, sizeof(p), NULL) ==
	       RECEPTEC_UWB_PAYLOAD_RESERVED_FLAGS);
	make_payload(p, false);
	p[1] = RECEPTEC_UWB_PAYLOAD_V1_FLAG_SUMMARY_STALE;
	assert(receptec_uwb_payload_v1_check(p, sizeof(p), NULL) ==
	       RECEPTEC_UWB_PAYLOAD_STALE_WITHOUT_SUMMARY);
	make_payload(p, false);
	p[40] = 1U;
	assert(receptec_uwb_payload_v1_check(p, sizeof(p), NULL) ==
	       RECEPTEC_UWB_PAYLOAD_SUMMARY_NOT_ZERO);
	make_payload(p, true);
	p[8] = 0U;
	assert(receptec_uwb_payload_v1_check(p, sizeof(p), NULL) ==
	       RECEPTEC_UWB_PAYLOAD_SUMMARY_HEADER);
	make_payload(p, true);
	p[104] = 0xFFU;
	assert(receptec_uwb_payload_v1_check(p, sizeof(p), NULL) == RECEPTEC_UWB_PAYLOAD_PADDING);
	make_payload(p, true);
	p[96] = 1U;
	assert(receptec_uwb_payload_v1_check(p, sizeof(p), NULL) == RECEPTEC_UWB_PAYLOAD_PADDING);
}

static void test_payload_reason_tokens_are_valid_result_reasons(void)
{
	struct receptec_uwb_test_result r;
	char out[RECEPTEC_UWB_TEST_LINE_SIZE];
	int s;

	for (s = RECEPTEC_UWB_PAYLOAD_WRONG_LENGTH; s <= RECEPTEC_UWB_PAYLOAD_PADDING; s++) {
		receptec_uwb_test_result_init(&r, "data_payload", 0U);
		r.outcome = RECEPTEC_UWB_TEST_FAIL;
		r.reason = receptec_uwb_payload_reason((enum receptec_uwb_payload_status)s);
		assert(strcmp(r.reason, "unknown") != 0);
		assert(line(&r, out, sizeof(out)) > 0);
	}
	for (s = RECEPTEC_UWB_DUPLICATE_SEQUENCE_CHANGED; s <= RECEPTEC_UWB_DUPLICATE_LENGTH_CHANGED; s++) {
		receptec_uwb_test_result_init(&r, "duplicate", 0U);
		r.outcome = RECEPTEC_UWB_TEST_FAIL;
		r.reason = receptec_uwb_payload_duplicate_reason((enum receptec_uwb_payload_duplicate)s);
		assert(strcmp(r.reason, "unknown") != 0);
		assert(line(&r, out, sizeof(out)) > 0);
	}
}

static void test_duplicate_comparison(void)
{
	uint8_t a[RECEPTEC_UWB_PAYLOAD_V1_SIZE];
	uint8_t b[RECEPTEC_UWB_PAYLOAD_V1_SIZE];

	make_payload(a, true);
	memcpy(b, a, sizeof(a));
	assert(receptec_uwb_payload_compare(a, sizeof(a), b, sizeof(b)) == RECEPTEC_UWB_DUPLICATE_SAME);
	b[2] ^= 1U;
	assert(receptec_uwb_payload_compare(a, sizeof(a), b, sizeof(b)) ==
	       RECEPTEC_UWB_DUPLICATE_SEQUENCE_CHANGED);
	memcpy(b, a, sizeof(a));
	b[5] ^= 1U;
	assert(receptec_uwb_payload_compare(a, sizeof(a), b, sizeof(b)) ==
	       RECEPTEC_UWB_DUPLICATE_AGE_CHANGED);
	memcpy(b, a, sizeof(a));
	b[60] ^= 0x80U;
	assert(receptec_uwb_payload_compare(a, sizeof(a), b, sizeof(b)) ==
	       RECEPTEC_UWB_DUPLICATE_PAYLOAD_CHANGED);
	assert(receptec_uwb_payload_compare(a, sizeof(a), b, sizeof(b) - 1U) ==
	       RECEPTEC_UWB_DUPLICATE_LENGTH_CHANGED);
}

int main(void)
{
	test_result_pass_line();
	test_result_fail_timeout_abort_mapping();
	test_result_contract_is_enforced();
	test_result_buffer_and_field_limits();
	test_result_numbers();
	test_mutation_wrong_source();
	test_mutation_bad_type();
	test_mutation_bad_reserved();
	test_mutation_bad_length();
	test_mutation_bad_dt();
	test_mutation_arguments_and_names();
	test_wrap_sequence_order();
	test_payload_valid_cases();
	test_payload_rejections();
	test_payload_reason_tokens_are_valid_result_reasons();
	test_duplicate_comparison();
	return 0;
}
