/* SPDX-License-Identifier: Apache-2.0 */

#include "uwb_peer_storage.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/settings/settings.h>

struct mock_settings {
	uint8_t values[RECEPTEC_UWB_MAX_PEERS][RECEPTEC_UWB_EUI64_SIZE];
	size_t lengths[RECEPTEC_UWB_MAX_PEERS];
	bool present[RECEPTEC_UWB_MAX_PEERS];
	char nicknames[RECEPTEC_UWB_MAX_PEERS]
		      [RECEPTEC_UWB_PEER_NICKNAME_MAX_LEN - 1U];
	size_t nick_lengths[RECEPTEC_UWB_MAX_PEERS];
	bool nick_present[RECEPTEC_UWB_MAX_PEERS];
	uint8_t cycle[16];
	size_t cycle_len;
	uint8_t round_limit[13];
	size_t round_limit_len;
	bool corrupt_save;
	uint8_t range_auto;
	size_t range_auto_len;
	bool range_auto_present;
	uint8_t range_autostart[6];
	size_t range_autostart_len;
	bool range_autostart_present;
	int init_result;
	ssize_t get_len_result;
	ssize_t load_result;
	int save_result;
	int delete_result;
	char last_key[20];
};

static struct mock_settings mock;

enum mock_key_ns {
	MOCK_KEY_NONE,
	MOCK_KEY_PEER,
	MOCK_KEY_NICKNAME,
};

static int key_parse(const char *key, enum mock_key_ns *ns)
{
	unsigned int slot;
	char trailing;

	if (sscanf(key, "uwb/peer/%u%c", &slot, &trailing) == 1 &&
	    slot < RECEPTEC_UWB_MAX_PEERS) {
		*ns = MOCK_KEY_PEER;
		return (int)slot;
	}
	if (sscanf(key, "uwb/nickname/%u%c", &slot, &trailing) == 1 &&
	    slot < RECEPTEC_UWB_MAX_PEERS) {
		*ns = MOCK_KEY_NICKNAME;
		return (int)slot;
	}

	*ns = MOCK_KEY_NONE;
	return -1;
}

static int slot_for_key(const char *key)
{
	enum mock_key_ns ns;
	int slot = key_parse(key, &ns);

	return ns == MOCK_KEY_PEER ? slot : -1;
}

static int nickname_slot_for_key(const char *key)
{
	enum mock_key_ns ns;
	int slot = key_parse(key, &ns);

	return ns == MOCK_KEY_NICKNAME ? slot : -1;
}

static void reset_mock(void)
{
	memset(&mock, 0, sizeof(mock));
	mock.get_len_result = -1;
	mock.load_result = -1;
}

int settings_subsys_init(void)
{
	return mock.init_result;
}

ssize_t settings_get_val_len(const char *key)
{
	int slot = slot_for_key(key);
	int nick_slot = nickname_slot_for_key(key);

	(void)snprintf(mock.last_key, sizeof(mock.last_key), "%s", key);
	if (mock.get_len_result != -1) {
		return mock.get_len_result;
	}
	if (strcmp(key, "uwb/cycle") == 0) { return (ssize_t)mock.cycle_len; }
	if (strcmp(key, "uwb/round_limit") == 0) { return (ssize_t)mock.round_limit_len; }
	if (strcmp(key, "uwb/range_auto") == 0) {
		if (!mock.range_auto_present) {
			return 0;
		}
		return (ssize_t)mock.range_auto_len;
	}
	if (strcmp(key, "uwb/range_autostart") == 0) {
		if (!mock.range_autostart_present) {
			return 0;
		}
		return (ssize_t)mock.range_autostart_len;
	}
	if (nick_slot >= 0) {
		if (!mock.nick_present[nick_slot]) {
			return 0;
		}
		return (ssize_t)mock.nick_lengths[nick_slot];
	}
	if (slot < 0 || !mock.present[slot]) {
		return 0;
	}

	return (ssize_t)mock.lengths[slot];
}

ssize_t settings_load_one(const char *key, void *value, size_t value_size)
{
	int slot = slot_for_key(key);
	int nick_slot = nickname_slot_for_key(key);

	(void)snprintf(mock.last_key, sizeof(mock.last_key), "%s", key);
	if (mock.load_result != -1) {
		return mock.load_result;
	}
	if (strcmp(key, "uwb/cycle") == 0 || strcmp(key, "uwb/round_limit") == 0) {
		bool cycle = strcmp(key, "uwb/cycle") == 0;
		size_t len = cycle ? mock.cycle_len : mock.round_limit_len;
		if (len == 0) { return -ENOENT; }
		if (len > value_size) { return -ENOMEM; }
		memcpy(value, cycle ? mock.cycle : mock.round_limit, len);
		return (ssize_t)len;
	}
	if (strcmp(key, "uwb/range_auto") == 0) {
		if (!mock.range_auto_present) {
			return -ENOENT;
		}
		if (mock.range_auto_len > value_size) {
			return -ENOMEM;
		}
		memcpy(value, &mock.range_auto, mock.range_auto_len);

		return (ssize_t)mock.range_auto_len;
	}
	if (strcmp(key, "uwb/range_autostart") == 0) {
		if (!mock.range_autostart_present) {
			return -ENOENT;
		}
		if (mock.range_autostart_len > value_size) {
			return -ENOMEM;
		}
		memcpy(value, mock.range_autostart, mock.range_autostart_len);

		return (ssize_t)mock.range_autostart_len;
	}
	if (nick_slot >= 0) {
		if (!mock.nick_present[nick_slot]) {
			return -ENOENT;
		}
		if (mock.nick_lengths[nick_slot] > value_size) {
			return -ENOMEM;
		}
		memcpy(value, mock.nicknames[nick_slot],
		       mock.nick_lengths[nick_slot]);

		return (ssize_t)mock.nick_lengths[nick_slot];
	}
	if (slot < 0 || !mock.present[slot]) {
		return -ENOENT;
	}
	if (mock.lengths[slot] > value_size) {
		return -ENOMEM;
	}
	memcpy(value, mock.values[slot], mock.lengths[slot]);

	return (ssize_t)mock.lengths[slot];
}

int settings_save_one(const char *key, const void *value, size_t value_size)
{
	int slot = slot_for_key(key);
	int nick_slot = nickname_slot_for_key(key);

	(void)snprintf(mock.last_key, sizeof(mock.last_key), "%s", key);
	if (mock.save_result != 0) {
		return mock.save_result;
	}
	if (strcmp(key, "uwb/cycle") == 0 || strcmp(key, "uwb/round_limit") == 0) {
		bool cycle = strcmp(key, "uwb/cycle") == 0;
		size_t size = cycle ? sizeof(mock.cycle) : sizeof(mock.round_limit);
		uint8_t *dest = cycle ? mock.cycle : mock.round_limit;
		if (size != value_size) { return -EINVAL; }
		memcpy(dest, value, size);
		if (mock.corrupt_save) { dest[0] ^= 1U; }
		if (cycle) { mock.cycle_len = size; } else { mock.round_limit_len = size; }
		return 0;
	}
	if (strcmp(key, "uwb/range_auto") == 0) {
		if (value_size != sizeof(mock.range_auto)) {
			return -EINVAL;
		}
		memcpy(&mock.range_auto, value, value_size);
		mock.range_auto_len = value_size;
		mock.range_auto_present = true;

		return 0;
	}
	if (strcmp(key, "uwb/range_autostart") == 0) {
		if (value_size != sizeof(mock.range_autostart)) {
			return -EINVAL;
		}
		memcpy(mock.range_autostart, value, value_size);
		mock.range_autostart_len = value_size;
		mock.range_autostart_present = true;

		return 0;
	}
	if (nick_slot >= 0) {
		if (value_size > sizeof(mock.nicknames[nick_slot])) {
			return -EINVAL;
		}
		memcpy(mock.nicknames[nick_slot], value, value_size);
		mock.nick_lengths[nick_slot] = value_size;
		mock.nick_present[nick_slot] = true;

		return 0;
	}
	if (slot < 0 || value_size > sizeof(mock.values[slot])) {
		return -EINVAL;
	}
	memcpy(mock.values[slot], value, value_size);
	mock.lengths[slot] = value_size;
	mock.present[slot] = true;

	return 0;
}

int settings_delete(const char *key)
{
	int slot = slot_for_key(key);
	int nick_slot = nickname_slot_for_key(key);

	(void)snprintf(mock.last_key, sizeof(mock.last_key), "%s", key);
	if (mock.delete_result != 0) {
		return mock.delete_result;
	}
	if (strcmp(key, "uwb/range_auto") == 0) {
		if (!mock.range_auto_present) {
			return -ENOENT;
		}
		mock.range_auto_present = false;
		mock.range_auto_len = 0U;

		return 0;
	}
	if (strcmp(key, "uwb/range_autostart") == 0) {
		if (!mock.range_autostart_present) {
			return -ENOENT;
		}
		mock.range_autostart_present = false;
		mock.range_autostart_len = 0U;

		return 0;
	}
	if (nick_slot >= 0) {
		if (!mock.nick_present[nick_slot]) {
			return -ENOENT;
		}
		mock.nick_present[nick_slot] = false;
		mock.nick_lengths[nick_slot] = 0U;

		return 0;
	}
	if (slot < 0 || !mock.present[slot]) {
		return -ENOENT;
	}
	mock.present[slot] = false;
	mock.lengths[slot] = 0U;

	return 0;
}

static void test_init_and_argument_errors(void)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE] = {0};

	reset_mock();
	mock.init_result = -EIO;
	assert(receptec_uwb_peer_storage_init() == -EIO);
	assert(receptec_uwb_peer_storage_load(0U, NULL) == -EINVAL);
	assert(receptec_uwb_peer_storage_load(RECEPTEC_UWB_MAX_PEERS, eui64) ==
	       -EINVAL);
	assert(receptec_uwb_peer_storage_save(0U, NULL) == -EINVAL);
	assert(receptec_uwb_peer_storage_save(RECEPTEC_UWB_MAX_PEERS, eui64) ==
	       -EINVAL);
	assert(receptec_uwb_peer_storage_delete(RECEPTEC_UWB_MAX_PEERS) ==
	       -EINVAL);
}

static void test_save_load_delete_round_trip(void)
{
	const uint8_t expected[RECEPTEC_UWB_EUI64_SIZE] = {
		0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
	};
	uint8_t actual[RECEPTEC_UWB_EUI64_SIZE] = {0};

	reset_mock();
	assert(receptec_uwb_peer_storage_save(2U, expected) == 0);
	assert(strcmp(mock.last_key, "uwb/peer/2") == 0);
	assert(receptec_uwb_peer_storage_load(2U, actual) == 0);
	assert(memcmp(actual, expected, sizeof(expected)) == 0);
	assert(receptec_uwb_peer_storage_delete(2U) == 0);
	assert(receptec_uwb_peer_storage_load(2U, actual) == -ENOENT);
}

static void test_load_rejects_corrupt_sizes_and_propagates_errors(void)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE] = {0};

	reset_mock();
	mock.present[0] = true;
	mock.lengths[0] = RECEPTEC_UWB_EUI64_SIZE - 1U;
	assert(receptec_uwb_peer_storage_load(0U, eui64) == -EINVAL);

	mock.lengths[0] = RECEPTEC_UWB_EUI64_SIZE;
	mock.load_result = (ssize_t)RECEPTEC_UWB_EUI64_SIZE - 1;
	assert(receptec_uwb_peer_storage_load(0U, eui64) == -EIO);

	mock.get_len_result = -EACCES;
	assert(receptec_uwb_peer_storage_load(0U, eui64) == -EACCES);

	mock.get_len_result = (ssize_t)RECEPTEC_UWB_EUI64_SIZE;
	mock.load_result = -EIO;
	assert(receptec_uwb_peer_storage_load(0U, eui64) == -EIO);
}

static void test_save_and_delete_propagate_errors(void)
{
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE] = {0};

	reset_mock();
	mock.save_result = -ENOSPC;
	assert(receptec_uwb_peer_storage_save(1U, eui64) == -ENOSPC);

	mock.delete_result = -EIO;
	assert(receptec_uwb_peer_storage_delete(1U) == -EIO);
}

static void test_nickname_round_trip(void)
{
	char loaded[RECEPTEC_UWB_PEER_NICKNAME_MAX_LEN];

	reset_mock();
	assert(receptec_uwb_peer_nickname_load(1U, loaded, sizeof(loaded)) ==
	       -ENOENT);
	assert(receptec_uwb_peer_nickname_save(1U, "coletec-a") == 0);
	assert(strcmp(mock.last_key, "uwb/nickname/1") == 0);
	assert(receptec_uwb_peer_nickname_load(1U, loaded, sizeof(loaded)) ==
	       0);
	assert(strcmp(loaded, "coletec-a") == 0);
	assert(receptec_uwb_peer_nickname_save(1U, "coletec-b") == 0);
	assert(receptec_uwb_peer_nickname_load(1U, loaded, sizeof(loaded)) ==
	       0);
	assert(strcmp(loaded, "coletec-b") == 0);
	assert(receptec_uwb_peer_nickname_delete(1U) == 0);
	assert(receptec_uwb_peer_nickname_load(1U, loaded, sizeof(loaded)) ==
	       -ENOENT);
}

static void test_nickname_rejects_bad_arguments(void)
{
	char loaded[RECEPTEC_UWB_PEER_NICKNAME_MAX_LEN];
	char too_long[RECEPTEC_UWB_PEER_NICKNAME_MAX_LEN + 1U];

	memset(too_long, 'a', sizeof(too_long) - 1U);
	too_long[sizeof(too_long) - 1U] = '\0';

	reset_mock();
	assert(receptec_uwb_peer_nickname_load(0U, NULL, sizeof(loaded)) ==
	       -EINVAL);
	assert(receptec_uwb_peer_nickname_load(0U, loaded, 0U) == -EINVAL);
	assert(receptec_uwb_peer_nickname_load(RECEPTEC_UWB_MAX_PEERS, loaded,
					       sizeof(loaded)) == -EINVAL);
	assert(receptec_uwb_peer_nickname_save(0U, NULL) == -EINVAL);
	assert(receptec_uwb_peer_nickname_save(RECEPTEC_UWB_MAX_PEERS, "x") ==
	       -EINVAL);
	assert(receptec_uwb_peer_nickname_save(0U, "") == -EINVAL);
	assert(receptec_uwb_peer_nickname_save(0U, too_long) == -EINVAL);
	assert(receptec_uwb_peer_nickname_delete(RECEPTEC_UWB_MAX_PEERS) ==
	       -EINVAL);

	/* A buffer that cannot hold the stored value plus NUL is rejected. */
	assert(receptec_uwb_peer_nickname_save(2U, "coletec-a") == 0);
	assert(receptec_uwb_peer_nickname_load(2U, loaded, 4U) == -EINVAL);
	assert(receptec_uwb_peer_nickname_load(2U, loaded, sizeof(loaded)) ==
	       0);
	assert(strcmp(loaded, "coletec-a") == 0);
}

static void test_range_auto_round_trip(void)
{
	bool enabled = false;

	reset_mock();
	assert(receptec_uwb_range_auto_load(NULL) == -EINVAL);
	assert(receptec_uwb_range_auto_load(&enabled) == -ENOENT);
	assert(receptec_uwb_range_auto_save(true) == 0);
	assert(strcmp(mock.last_key, "uwb/range_auto") == 0);
	assert(receptec_uwb_range_auto_load(&enabled) == 0);
	assert(enabled);
	assert(receptec_uwb_range_auto_save(false) == 0);
	assert(receptec_uwb_range_auto_load(&enabled) == 0);
	assert(!enabled);
}

static void test_range_auto_rejects_corrupt_values(void)
{
	bool enabled = false;

	reset_mock();
	mock.range_auto_present = true;
	mock.range_auto_len = 0U;
	assert(receptec_uwb_range_auto_load(&enabled) == -ENOENT);

	mock.range_auto_len = 2U;
	assert(receptec_uwb_range_auto_load(&enabled) == -EINVAL);

	mock.range_auto_len = 1U;
	mock.range_auto = 7U;
	assert(receptec_uwb_range_auto_load(&enabled) == -EINVAL);
}

static void test_range_autostart_round_trip(void)
{
	struct receptec_uwb_range_autostart cfg = {
		.slot = 1U,
		.pair_index = 0U,
		.period_ms = 200U,
	};
	struct receptec_uwb_range_autostart loaded = {0};
	struct receptec_uwb_range_autostart bad = {
		.slot = RECEPTEC_UWB_MAX_PEERS,
		.pair_index = 0U,
		.period_ms = 200U,
	};

	reset_mock();
	assert(receptec_uwb_range_autostart_load(NULL) == -EINVAL);
	assert(receptec_uwb_range_autostart_load(&loaded) == -ENOENT);
	assert(receptec_uwb_range_autostart_save(NULL) == -EINVAL);
	assert(receptec_uwb_range_autostart_save(&bad) == -EINVAL);
	bad.slot = 1U;
	bad.pair_index = 3U;
	assert(receptec_uwb_range_autostart_save(&bad) == -EINVAL);
	bad.pair_index = 0U;
	bad.period_ms = 0U;
	assert(receptec_uwb_range_autostart_save(&bad) == -EINVAL);
	assert(receptec_uwb_range_autostart_save(&cfg) == 0);
	assert(strcmp(mock.last_key, "uwb/range_autostart") == 0);
	assert(receptec_uwb_range_autostart_load(&loaded) == 0);
	assert(loaded.slot == 1U && loaded.pair_index == 0U &&
	       loaded.period_ms == 200U);
	assert(receptec_uwb_range_autostart_delete() == 0);
	assert(receptec_uwb_range_autostart_load(&loaded) == -ENOENT);

	mock.range_autostart_present = true;
	mock.range_autostart_len = 3U;
	assert(receptec_uwb_range_autostart_load(&loaded) == -EINVAL);
}

static void test_cycle_integrity_and_validation(void)
{
	struct receptec_uwb_cycle_config cfg = {1U, 0U, true, false, 0U, 1000U};
	struct receptec_uwb_cycle_config out = {0};

	reset_mock();
	assert(receptec_uwb_cycle_load(&out) == -ENOENT);
	assert(receptec_uwb_cycle_save(&cfg) == 0);
	assert(receptec_uwb_cycle_load(&out) == 0);
	assert(out.tag_slot == 1 && out.base_slot == 0 && out.autostart && !out.fixed);
	assert(out.period_ms == 1000);
	/* Legacy settings migrate in memory at 1 Hz without rewriting the record. */
	uint32_t crc = UINT32_MAX;
	for (size_t i = 0; i < 8; i++) {
		crc ^= mock.cycle[i];
		for (unsigned int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
	}
	crc = ~crc;
	for (size_t i = 0; i < 4; i++) mock.cycle[8 + i] = (uint8_t)(crc >> (8 * i));
	mock.cycle_len = 12;
	assert(receptec_uwb_cycle_load(&out) == 0 && out.period_ms == 1000);
	assert(mock.cycle_len == 12);
	assert(receptec_uwb_cycle_save(&cfg) == 0);
	cfg.period_ms = 249;
	assert(receptec_uwb_cycle_save(&cfg) == -EINVAL);
	cfg.period_ms = 250;
	assert(receptec_uwb_cycle_save(&cfg) == 0);
	assert(receptec_uwb_cycle_load(&out) == 0 && out.period_ms == 250);
	cfg.period_ms = 500;
	assert(receptec_uwb_cycle_save(&cfg) == 0);
	assert(receptec_uwb_cycle_load(&out) == 0 && out.period_ms == 500);
	cfg.period_ms = 1001;
	assert(receptec_uwb_cycle_save(&cfg) == -EINVAL);
	cfg.period_ms = 1000;
	cfg.fixed = true;
	assert(receptec_uwb_cycle_save(&cfg) == -EINVAL);
	cfg.baseline_mm = 700;
	assert(receptec_uwb_cycle_save(&cfg) == 0);
	assert(receptec_uwb_cycle_load(&out) == 0 && out.baseline_mm == 700 && out.fixed);
	cfg.base_slot = cfg.tag_slot;
	assert(receptec_uwb_cycle_save(&cfg) == -EINVAL);
	cfg.base_slot = 0;
	mock.cycle[4] ^= 1U;
	assert(receptec_uwb_cycle_load(&out) == -EINVAL);
	mock.corrupt_save = true;
	assert(receptec_uwb_cycle_save(&cfg) == -EINVAL);
	mock.corrupt_save = false;
	assert(receptec_uwb_cycle_save(&cfg) == 0);
	mock.save_result = -ENOSPC;
	assert(receptec_uwb_cycle_save(&cfg) == -ENOSPC);
	assert(receptec_uwb_cycle_load(&out) == 0 && out.baseline_mm == 700);
	assert(receptec_uwb_cycle_load(NULL) == -EINVAL);
}

static void test_round_reservation_persistence(void)
{
	uint64_t first = 0, limit = 0;

	reset_mock();
	assert(receptec_uwb_round_reserve(&first, &limit) == 0);
	assert(first == (1ULL << 32) && limit == first + 1024U);
	/* Calling again models reboot: abandon any unused IDs in the old block. */
	assert(receptec_uwb_round_reserve(&first, &limit) == 0);
	assert(first == (1ULL << 32) + 1024U && limit == first + 1024U);
	mock.save_result = -EIO;
	assert(receptec_uwb_round_reserve(&first, &limit) == -EIO);
	assert(first == (1ULL << 32) + 1024U);
	mock.save_result = 0;
	assert(receptec_uwb_round_reserve(&first, &limit) == 0);
	assert(first == (1ULL << 32) + 2048U);
	mock.corrupt_save = true;
	assert(receptec_uwb_round_reserve(&first, &limit) == -EINVAL);
	assert(first == (1ULL << 32) + 2048U);
	mock.corrupt_save = false;
	mock.round_limit[1] ^= 1U;
	assert(receptec_uwb_round_reserve(&first, &limit) == -EINVAL);
	assert(receptec_uwb_round_reserve(NULL, &limit) == -EINVAL);
	assert(receptec_uwb_round_reserve(&first, &first) == -EINVAL);
}

int main(void)
{
	test_cycle_integrity_and_validation();
	test_round_reservation_persistence();
	test_init_and_argument_errors();
	test_save_load_delete_round_trip();
	test_load_rejects_corrupt_sizes_and_propagates_errors();
	test_save_and_delete_propagate_errors();
	test_nickname_round_trip();
	test_nickname_rejects_bad_arguments();
	test_range_auto_round_trip();
	test_range_auto_rejects_corrupt_values();
	test_range_autostart_round_trip();
	puts("RECEPTEC UWB peer storage tests passed");

	return 0;
}
