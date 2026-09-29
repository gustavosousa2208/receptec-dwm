/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/uwb_test_result.h>

#include <errno.h>
#include <string.h>

/* No printf: this file is also built on the host, and the line is tiny. */
struct out {
	char *buf;
	size_t cap;
	size_t len;
	bool overflow;
};

static void put_char(struct out *o, char c)
{
	if (o->len + 1U >= o->cap) {
		o->overflow = true;
		return;
	}
	o->buf[o->len++] = c;
}

static void put_str(struct out *o, const char *s)
{
	while (*s != '\0') {
		put_char(o, *s++);
	}
}

static void put_u32(struct out *o, uint32_t value)
{
	char digits[10];
	size_t n = 0U;

	do {
		digits[n++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0U);
	while (n != 0U) {
		put_char(o, digits[--n]);
	}
}

/* Names and keys: [a-z0-9_-]. Values also allow ".". Reasons: [a-z0-9-] only. */
enum token_kind { TOKEN_REASON, TOKEN_NAME, TOKEN_VALUE };

static bool token_ok(const char *token, enum token_kind kind)
{
	size_t i;

	if (token == NULL || token[0] == '\0') {
		return false;
	}
	for (i = 0U; token[i] != '\0'; i++) {
		char c = token[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';

		if (kind != TOKEN_REASON) {
			ok = ok || c == '_';
		}
		if (kind == TOKEN_VALUE) {
			ok = ok || c == '.';
		}
		if (!ok) {
			return false;
		}
	}
	return true;
}

void receptec_uwb_test_result_init(struct receptec_uwb_test_result *result,
				   const char *name, uint8_t slot)
{
	memset(result, 0, sizeof(*result));
	result->name = name;
	result->slot = slot;
	result->outcome = RECEPTEC_UWB_TEST_PASS;
}

static struct receptec_uwb_test_field *next_field(
	struct receptec_uwb_test_result *result, const char *key)
{
	struct receptec_uwb_test_field *field;

	if (result->field_count >= RECEPTEC_UWB_TEST_MAX_FIELDS) {
		return NULL;
	}
	field = &result->fields[result->field_count++];
	field->key = key;
	field->value[0] = '\0';
	return field;
}

bool receptec_uwb_test_result_add_u32(struct receptec_uwb_test_result *result,
				      const char *key, uint32_t value)
{
	struct receptec_uwb_test_field *field = next_field(result, key);
	struct out o;

	if (field == NULL) {
		return false;
	}
	o = (struct out){ .buf = field->value, .cap = sizeof(field->value) };
	put_u32(&o, value);
	field->value[o.len] = '\0';
	return !o.overflow;
}

bool receptec_uwb_test_result_add_i32(struct receptec_uwb_test_result *result,
				      const char *key, int32_t value)
{
	struct receptec_uwb_test_field *field = next_field(result, key);
	struct out o;

	if (field == NULL) {
		return false;
	}
	o = (struct out){ .buf = field->value, .cap = sizeof(field->value) };
	if (value < 0) {
		put_char(&o, '-');
		put_u32(&o, (uint32_t)(-(int64_t)value));
	} else {
		put_u32(&o, (uint32_t)value);
	}
	field->value[o.len] = '\0';
	return !o.overflow;
}

bool receptec_uwb_test_result_add_token(struct receptec_uwb_test_result *result,
					const char *key, const char *value)
{
	struct receptec_uwb_test_field *field = next_field(result, key);

	if (field == NULL || value == NULL || strlen(value) >= sizeof(field->value)) {
		return false;
	}
	strcpy(field->value, value);
	return true;
}

int receptec_uwb_test_result_format(const struct receptec_uwb_test_result *result,
				    char *output, size_t capacity)
{
	struct out o = { .buf = output, .cap = capacity };
	const char *reason = NULL;
	bool failed = result->outcome != RECEPTEC_UWB_TEST_PASS;
	size_t i;

	if (output == NULL || capacity == 0U) {
		return -EINVAL;
	}
	if (!token_ok(result->name, TOKEN_NAME) || result->slot > 3U) {
		return -EINVAL;
	}
	switch (result->outcome) {
	case RECEPTEC_UWB_TEST_PASS:
		break;
	case RECEPTEC_UWB_TEST_FAIL:
		reason = result->reason;
		break;
	case RECEPTEC_UWB_TEST_TIMEOUT:
		reason = "timeout";
		break;
	case RECEPTEC_UWB_TEST_ABORT:
		reason = "aborted";
		break;
	default:
		return -EINVAL;
	}
	if (failed && !token_ok(reason, TOKEN_REASON)) {
		return -EINVAL;
	}
	for (i = 0U; i < result->field_count; i++) {
		if (!token_ok(result->fields[i].key, TOKEN_NAME) ||
		    !token_ok(result->fields[i].value, TOKEN_VALUE)) {
			return -EINVAL;
		}
	}

	put_str(&o, "UWB_TEST result=");
	put_str(&o, failed ? "FAIL" : "PASS");
	put_str(&o, " test=");
	put_str(&o, result->name);
	put_str(&o, " slot=");
	put_u32(&o, result->slot);
	if (failed) {
		put_str(&o, " reason=");
		put_str(&o, reason);
	}
	for (i = 0U; i < result->field_count; i++) {
		put_char(&o, ' ');
		put_str(&o, result->fields[i].key);
		put_char(&o, '=');
		put_str(&o, result->fields[i].value);
	}
	if (o.overflow) {
		return -ENOSPC;
	}
	o.buf[o.len] = '\0';
	return (int)o.len;
}
