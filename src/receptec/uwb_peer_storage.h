/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_PEER_STORAGE_H_
#define RECEPTEC_UWB_PEER_STORAGE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "uwb_peer_registry.h"

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

int receptec_uwb_schedule_action_load(uint8_t slot, uint8_t *action);
int receptec_uwb_schedule_action_save(uint8_t slot, uint8_t action);
int receptec_uwb_schedule_action_delete(uint8_t slot);
int receptec_uwb_schedule_period_load(uint32_t *period_ms);
int receptec_uwb_schedule_period_save(uint32_t period_ms);

int receptec_uwb_identity_storage_load(
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_identity_storage_save(
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);

#endif
