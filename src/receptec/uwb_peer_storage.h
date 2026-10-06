/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_PEER_STORAGE_H_
#define RECEPTEC_UWB_PEER_STORAGE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "uwb_peer_registry.h"

/* Nickname buffer size including the NUL terminator (31 usable characters). */
#define RECEPTEC_UWB_PEER_NICKNAME_MAX_LEN 32U

int receptec_uwb_peer_storage_init(void);

int receptec_uwb_peer_storage_load(
	uint8_t slot,
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);

int receptec_uwb_peer_storage_save(
	uint8_t slot,
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);

int receptec_uwb_peer_storage_delete(uint8_t slot);
int receptec_uwb_peer_enabled_load(uint8_t slot, bool *enabled);
int receptec_uwb_peer_enabled_save(uint8_t slot, bool enabled);
int receptec_uwb_peer_enabled_delete(uint8_t slot);
int receptec_uwb_peer_nickname_load(uint8_t slot, char *nickname,
				    size_t capacity);
int receptec_uwb_peer_nickname_save(uint8_t slot, const char *nickname);
int receptec_uwb_peer_nickname_delete(uint8_t slot);

/* Global range auto-responder flag (1 byte, 0/1); -ENOENT when never saved. */
int receptec_uwb_range_auto_load(bool *enabled);
int receptec_uwb_range_auto_save(bool enabled);

/*
 * Range autostart: which round to begin at boot (slot, pair, period_ms).
 * -ENOENT when never configured; invalid stored values read as -EINVAL.
 */
struct receptec_uwb_range_autostart {
	uint8_t slot;
	uint8_t pair_index;
	uint32_t period_ms;
};

int receptec_uwb_range_autostart_load(
	struct receptec_uwb_range_autostart *out);
int receptec_uwb_range_autostart_save(
	const struct receptec_uwb_range_autostart *cfg);
int receptec_uwb_range_autostart_delete(void);

int receptec_uwb_schedule_action_load(uint8_t slot, uint8_t *action);
int receptec_uwb_schedule_action_save(uint8_t slot, uint8_t action);
int receptec_uwb_schedule_action_delete(uint8_t slot);
int receptec_uwb_schedule_period_load(uint32_t *period_ms);
int receptec_uwb_schedule_period_save(uint32_t period_ms);

int receptec_uwb_identity_storage_load(
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_identity_storage_save(
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);

/* A single versioned settings record; malformed records fail closed. */
struct receptec_uwb_cycle_config {
	uint8_t tag_slot;
	uint8_t base_slot;
	bool autostart;
	bool fixed;
	uint32_t baseline_mm;
};
int receptec_uwb_cycle_load(struct receptec_uwb_cycle_config *out);
int receptec_uwb_cycle_save(const struct receptec_uwb_cycle_config *cfg);

/* Persist a 1024-ID reservation before returning [first, limit). */
int receptec_uwb_round_reserve(uint64_t *first, uint64_t *limit);

#endif
