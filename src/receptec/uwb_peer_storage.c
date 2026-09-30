/* SPDX-License-Identifier: Apache-2.0 */

#include "uwb_peer_storage.h"

#include <errno.h>
#include <stddef.h>

#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

#define PEER_KEY_MAX_LEN 20U

static int make_peer_key(uint8_t slot, char key[PEER_KEY_MAX_LEN])
{
	int len;

	if (slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}

	len = snprintk(key, PEER_KEY_MAX_LEN,
		      "uwb/peer/%u", slot);

	if (len <= 0 || len >= (int)PEER_KEY_MAX_LEN) {
		return -ENOSPC;
	}

	return 0;
}

int receptec_uwb_peer_storage_init(void)
{
	return settings_subsys_init();
}

int receptec_uwb_peer_storage_load(
	uint8_t slot,
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	char key[PEER_KEY_MAX_LEN];
	ssize_t len;
	int ret;

	if (eui64 == NULL) {
		return -EINVAL;
	}

	ret = make_peer_key(slot, key);
	if (ret < 0) {
		return ret;
	}

	/*
	 * settings_get_val_len() retorna 0 quando a chave
	 * não existe.
	 */
	len = settings_get_val_len(key);

	if (len < 0) {
		return (int)len;
	}

	if (len == 0) {
		return -ENOENT;
	}

	if (len != RECEPTEC_UWB_EUI64_SIZE) {
		return -EINVAL;
	}

	len = settings_load_one(
		key,
		eui64,
		RECEPTEC_UWB_EUI64_SIZE);

	if (len < 0) {
		return (int)len;
	}

	if (len != RECEPTEC_UWB_EUI64_SIZE) {
		return -EIO;
	}

	return 0;
}

int receptec_uwb_peer_storage_save(
	uint8_t slot,
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	char key[PEER_KEY_MAX_LEN];
	int ret;

	if (eui64 == NULL) {
		return -EINVAL;
	}

	ret = make_peer_key(slot, key);
	if (ret < 0) {
		return ret;
	}

	return settings_save_one(
		key,
		eui64,
		RECEPTEC_UWB_EUI64_SIZE);
}

int receptec_uwb_peer_storage_delete(uint8_t slot)
{
	char key[PEER_KEY_MAX_LEN];
	int ret;

	ret = make_peer_key(slot, key);
	if (ret < 0) {
		return ret;
	}

	return settings_delete(key);
}

int receptec_uwb_identity_storage_load(
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	ssize_t len;

	if (eui64 == NULL) {
		return -EINVAL;
	}
	len = settings_get_val_len("uwb/identity");
	if (len == 0) {
		return -ENOENT;
	}
	if (len < 0) {
		return (int)len;
	}
	if (len != RECEPTEC_UWB_EUI64_SIZE) {
		return -EINVAL;
	}
	len = settings_load_one("uwb/identity", eui64,
				RECEPTEC_UWB_EUI64_SIZE);
	return len == RECEPTEC_UWB_EUI64_SIZE ? 0 :
		(len < 0 ? (int)len : -EIO);
}

int receptec_uwb_identity_storage_save(
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	if (eui64 == NULL) {
		return -EINVAL;
	}
	return settings_save_one("uwb/identity", eui64,
				 RECEPTEC_UWB_EUI64_SIZE);
}
