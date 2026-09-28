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
	int init_result;
	ssize_t get_len_result;
	ssize_t load_result;
	int save_result;
	int delete_result;
	char last_key[20];
};

static struct mock_settings mock;

static int slot_for_key(const char *key)
{
	unsigned int slot;
	char trailing;

	if (sscanf(key, "uwb/peer/%u%c", &slot, &trailing) != 1 ||
	    slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -1;
	}

	return (int)slot;
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

	(void)snprintf(mock.last_key, sizeof(mock.last_key), "%s", key);
	if (mock.get_len_result != -1) {
		return mock.get_len_result;
	}
	if (slot < 0 || !mock.present[slot]) {
		return 0;
	}

	return (ssize_t)mock.lengths[slot];
}

ssize_t settings_load_one(const char *key, void *value, size_t value_size)
{
	int slot = slot_for_key(key);

	(void)snprintf(mock.last_key, sizeof(mock.last_key), "%s", key);
	if (mock.load_result != -1) {
		return mock.load_result;
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

	(void)snprintf(mock.last_key, sizeof(mock.last_key), "%s", key);
	if (mock.save_result != 0) {
		return mock.save_result;
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

	(void)snprintf(mock.last_key, sizeof(mock.last_key), "%s", key);
	if (mock.delete_result != 0) {
		return mock.delete_result;
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

int main(void)
{
	test_init_and_argument_errors();
	test_save_load_delete_round_trip();
	test_load_rejects_corrupt_sizes_and_propagates_errors();
	test_save_and_delete_propagate_errors();
	puts("RECEPTEC UWB peer storage tests passed");

	return 0;
}
