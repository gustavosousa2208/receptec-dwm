/* SPDX-License-Identifier: Apache-2.0 */

#include "uwb_peer_storage.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#define PEER_KEY_MAX_LEN 32U

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

static int make_action_key(uint8_t slot, char key[PEER_KEY_MAX_LEN])
{
	int len;

	if (slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	len = snprintk(key, PEER_KEY_MAX_LEN, "uwb/action/%u", slot);
	return len > 0 && len < (int)PEER_KEY_MAX_LEN ? 0 : -ENOSPC;
}

static int make_enabled_key(uint8_t slot, char key[PEER_KEY_MAX_LEN])
{
	int len;

	if (slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	len = snprintk(key, PEER_KEY_MAX_LEN, "uwb/enabled/%u", slot);
	return len > 0 && len < (int)PEER_KEY_MAX_LEN ? 0 : -ENOSPC;
}

static int make_nickname_key(uint8_t slot, char key[PEER_KEY_MAX_LEN])
{
	int len;

	if (slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	len = snprintk(key, PEER_KEY_MAX_LEN, "uwb/nickname/%u", slot);
	return len > 0 && len < (int)PEER_KEY_MAX_LEN ? 0 : -ENOSPC;
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

int receptec_uwb_peer_enabled_load(uint8_t slot, bool *enabled)
{
	char key[PEER_KEY_MAX_LEN];
	uint8_t stored;
	ssize_t len;
	int ret;

	if (enabled == NULL) {
		return -EINVAL;
	}
	ret = make_enabled_key(slot, key);
	if (ret < 0) {
		return ret;
	}
	len = settings_get_val_len(key);
	if (len < 0) {
		return (int)len;
	}
	if (len == 0) {
		return -ENOENT;
	}
	if (len != sizeof(stored)) {
		return -EINVAL;
	}
	len = settings_load_one(key, &stored, sizeof(stored));
	if (len != sizeof(stored)) {
		return len < 0 ? (int)len : -EIO;
	}
	if (stored > 1U) {
		return -EINVAL;
	}
	*enabled = stored != 0U;
	return 0;
}

int receptec_uwb_peer_enabled_save(uint8_t slot, bool enabled)
{
	char key[PEER_KEY_MAX_LEN];
	uint8_t stored = enabled ? 1U : 0U;
	int ret = make_enabled_key(slot, key);

	if (ret < 0) {
		return ret;
	}
	return settings_save_one(key, &stored, sizeof(stored));
}

int receptec_uwb_peer_enabled_delete(uint8_t slot)
{
	char key[PEER_KEY_MAX_LEN];
	int ret = make_enabled_key(slot, key);

	if (ret < 0) {
		return ret;
	}
	return settings_delete(key);
}

int receptec_uwb_peer_nickname_load(uint8_t slot, char *nickname,
				    size_t capacity)
{
	char key[PEER_KEY_MAX_LEN];
	ssize_t len;
	int ret;

	if (nickname == NULL || capacity == 0U) {
		return -EINVAL;
	}
	ret = make_nickname_key(slot, key);
	if (ret < 0) {
		return ret;
	}
	len = settings_get_val_len(key);
	if (len < 0) {
		return (int)len;
	}
	if (len == 0) {
		return -ENOENT;
	}
	if (len >= (ssize_t)capacity ||
	    (size_t)len >= RECEPTEC_UWB_PEER_NICKNAME_MAX_LEN) {
		return -EINVAL;
	}
	len = settings_load_one(key, nickname, (size_t)len);
	if (len < 0) {
		return (int)len;
	}
	if (len == 0 || len >= (ssize_t)capacity) {
		return -EIO;
	}
	nickname[len] = '\0';
	return 0;
}

int receptec_uwb_peer_nickname_save(uint8_t slot, const char *nickname)
{
	char key[PEER_KEY_MAX_LEN];
	size_t len;
	int ret;

	if (nickname == NULL) {
		return -EINVAL;
	}
	len = strlen(nickname);
	if (len == 0U || len >= RECEPTEC_UWB_PEER_NICKNAME_MAX_LEN) {
		return -EINVAL;
	}
	ret = make_nickname_key(slot, key);
	if (ret < 0) {
		return ret;
	}
	/* Stored without the NUL terminator; load() adds it back. */
	return settings_save_one(key, nickname, len);
}

int receptec_uwb_peer_nickname_delete(uint8_t slot)
{
	char key[PEER_KEY_MAX_LEN];
	int ret = make_nickname_key(slot, key);

	if (ret < 0) {
		return ret;
	}
	return settings_delete(key);
}

int receptec_uwb_range_auto_load(bool *enabled)
{
	uint8_t stored;
	ssize_t len;

	if (enabled == NULL) {
		return -EINVAL;
	}
	len = settings_get_val_len("uwb/range_auto");
	if (len < 0) {
		return (int)len;
	}
	if (len == 0) {
		return -ENOENT;
	}
	if (len != sizeof(stored)) {
		return -EINVAL;
	}
	len = settings_load_one("uwb/range_auto", &stored, sizeof(stored));
	if (len != sizeof(stored)) {
		return len < 0 ? (int)len : -EIO;
	}
	if (stored > 1U) {
		return -EINVAL;
	}
	*enabled = stored != 0U;
	return 0;
}

int receptec_uwb_range_auto_save(bool enabled)
{
	uint8_t stored = enabled ? 1U : 0U;

	return settings_save_one("uwb/range_auto", &stored, sizeof(stored));
}

#define RANGE_AUTOSTART_LEN 6U

int receptec_uwb_range_autostart_load(
	struct receptec_uwb_range_autostart *out)
{
	uint8_t encoded[RANGE_AUTOSTART_LEN];
	ssize_t len;

	if (out == NULL) {
		return -EINVAL;
	}
	len = settings_get_val_len("uwb/range_autostart");
	if (len < 0) {
		return (int)len;
	}
	if (len == 0) {
		return -ENOENT;
	}
	if (len != sizeof(encoded)) {
		return -EINVAL;
	}
	len = settings_load_one("uwb/range_autostart", encoded,
				sizeof(encoded));
	if (len != sizeof(encoded)) {
		return len < 0 ? (int)len : -EIO;
	}
	if (encoded[0] >= RECEPTEC_UWB_MAX_PEERS || encoded[1] > 2U) {
		return -EINVAL;
	}
	out->slot = encoded[0];
	out->pair_index = encoded[1];
	out->period_ms = (uint32_t)encoded[2] |
			 ((uint32_t)encoded[3] << 8) |
			 ((uint32_t)encoded[4] << 16) |
			 ((uint32_t)encoded[5] << 24);
	if (out->period_ms == 0U || out->period_ms > 60000U) {
		return -EINVAL;
	}
	return 0;
}

int receptec_uwb_range_autostart_save(
	const struct receptec_uwb_range_autostart *cfg)
{
	uint8_t encoded[RANGE_AUTOSTART_LEN];

	if (cfg == NULL || cfg->slot >= RECEPTEC_UWB_MAX_PEERS ||
	    cfg->pair_index > 2U || cfg->period_ms == 0U ||
	    cfg->period_ms > 60000U) {
		return -EINVAL;
	}
	encoded[0] = cfg->slot;
	encoded[1] = cfg->pair_index;
	encoded[2] = (uint8_t)(cfg->period_ms & 0xFFU);
	encoded[3] = (uint8_t)((cfg->period_ms >> 8) & 0xFFU);
	encoded[4] = (uint8_t)((cfg->period_ms >> 16) & 0xFFU);
	encoded[5] = (uint8_t)((cfg->period_ms >> 24) & 0xFFU);
	return settings_save_one("uwb/range_autostart", encoded,
				 sizeof(encoded));
}

int receptec_uwb_range_autostart_delete(void)
{
	return settings_delete("uwb/range_autostart");
}

int receptec_uwb_schedule_action_load(uint8_t slot, uint8_t *action)
{
	char key[PEER_KEY_MAX_LEN];
	ssize_t len;
	int ret;

	if (action == NULL) {
		return -EINVAL;
	}
	ret = make_action_key(slot, key);
	if (ret < 0) {
		return ret;
	}
	len = settings_get_val_len(key);
	if (len < 0) {
		return (int)len;
	}
	if (len == 0) {
		return -ENOENT;
	}
	if (len != sizeof(*action)) {
		return -EINVAL;
	}
	len = settings_load_one(key, action, sizeof(*action));
	return len == sizeof(*action) ? 0 : (len < 0 ? (int)len : -EIO);
}

int receptec_uwb_schedule_action_save(uint8_t slot, uint8_t action)
{
	char key[PEER_KEY_MAX_LEN];
	int ret = make_action_key(slot, key);

	if (ret < 0) {
		return ret;
	}
	return settings_save_one(key, &action, sizeof(action));
}

int receptec_uwb_schedule_action_delete(uint8_t slot)
{
	char key[PEER_KEY_MAX_LEN];
	int ret = make_action_key(slot, key);

	if (ret < 0) {
		return ret;
	}
	return settings_delete(key);
}

int receptec_uwb_schedule_period_load(uint32_t *period_ms)
{
	uint8_t encoded[sizeof(uint32_t)];
	ssize_t len;

	if (period_ms == NULL) {
		return -EINVAL;
	}
	len = settings_get_val_len("uwb/schedule_period_ms");
	if (len < 0) {
		return (int)len;
	}
	if (len == 0) {
		return -ENOENT;
	}
	if (len != sizeof(encoded)) {
		return -EINVAL;
	}
	len = settings_load_one("uwb/schedule_period_ms", encoded,
				sizeof(encoded));
	if (len != sizeof(encoded)) {
		return len < 0 ? (int)len : -EIO;
	}
	*period_ms = sys_get_le32(encoded);
	return 0;
}

int receptec_uwb_schedule_period_save(uint32_t period_ms)
{
	uint8_t encoded[sizeof(uint32_t)];

	sys_put_le32(period_ms, encoded);
	return settings_save_one("uwb/schedule_period_ms", encoded,
				 sizeof(encoded));
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
