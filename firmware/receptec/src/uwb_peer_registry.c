/* SPDX-License-Identifier: Apache-2.0 */

#include "uwb_peer_registry.h"

#include <errno.h>
#include <string.h>

int receptec_uwb_peer_registry_init(
	struct receptec_uwb_peer_registry *registry,
	const uint8_t local_eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	if (registry == NULL || local_eui64 == NULL) {
		return -EINVAL;
	}

	memset(registry, 0, sizeof(*registry));
	memcpy(registry->local_eui64, local_eui64,
	       RECEPTEC_UWB_EUI64_SIZE);
	return 0;
}

int receptec_uwb_peer_registry_register(
	struct receptec_uwb_peer_registry *registry, size_t slot,
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	size_t i;
	size_t history_slot = RECEPTEC_UWB_PEER_HISTORY_CAPACITY;
	size_t free_history_slot = RECEPTEC_UWB_PEER_HISTORY_CAPACITY;

	if (registry == NULL || eui64 == NULL ||
	    slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	if (memcmp(eui64, registry->local_eui64,
		   RECEPTEC_UWB_EUI64_SIZE) == 0) {
		return -EADDRINUSE;
	}
	if (registry->slots[slot].registered) {
		return -EEXIST;
	}
	for (i = 0U; i < RECEPTEC_UWB_MAX_PEERS; ++i) {
		if (registry->slots[i].registered &&
		    memcmp(eui64, registry->slots[i].eui64,
			   RECEPTEC_UWB_EUI64_SIZE) == 0) {
			return -EADDRINUSE;
		}
	}
	for (i = 0U; i < RECEPTEC_UWB_PEER_HISTORY_CAPACITY; ++i) {
		if (!registry->history[i].used) {
			if (free_history_slot == RECEPTEC_UWB_PEER_HISTORY_CAPACITY) {
				free_history_slot = i;
			}
			continue;
		}
		if (memcmp(eui64, registry->history[i].eui64,
			   RECEPTEC_UWB_EUI64_SIZE) == 0) {
			history_slot = i;
			break;
		}
	}
	if (history_slot == RECEPTEC_UWB_PEER_HISTORY_CAPACITY) {
		if (free_history_slot == RECEPTEC_UWB_PEER_HISTORY_CAPACITY) {
			return -ENOSPC;
		}
		history_slot = free_history_slot;
		registry->history[history_slot].used = true;
		registry->history[history_slot].next_sequence = 1U;
		memcpy(registry->history[history_slot].eui64, eui64,
		       RECEPTEC_UWB_EUI64_SIZE);
	}
	memcpy(registry->slots[slot].eui64, eui64,
	       RECEPTEC_UWB_EUI64_SIZE);
	registry->slots[slot].registered = true;
	return 0;
}

int receptec_uwb_peer_registry_unregister(
	struct receptec_uwb_peer_registry *registry, size_t slot)
{
	if (registry == NULL || slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	if (!registry->slots[slot].registered) {
		return -ENOENT;
	}

	memset(&registry->slots[slot], 0, sizeof(registry->slots[slot]));
	return 0;
}

int receptec_uwb_peer_registry_get(
	const struct receptec_uwb_peer_registry *registry, size_t slot,
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	if (registry == NULL || eui64 == NULL ||
	    slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	if (!registry->slots[slot].registered) {
		return -ENOENT;
	}

	memcpy(eui64, registry->slots[slot].eui64,
	       RECEPTEC_UWB_EUI64_SIZE);
	return 0;
}

int receptec_uwb_peer_registry_next_sequence(
	struct receptec_uwb_peer_registry *registry, size_t slot,
	uint8_t *sequence)
{
	size_t i;

	if (registry == NULL || sequence == NULL ||
	    slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	if (!registry->slots[slot].registered) {
		return -ENOENT;
	}
	for (i = 0U; i < RECEPTEC_UWB_PEER_HISTORY_CAPACITY; ++i) {
		if (registry->history[i].used &&
		    memcmp(registry->history[i].eui64,
			   registry->slots[slot].eui64,
			   RECEPTEC_UWB_EUI64_SIZE) == 0) {
			*sequence = registry->history[i].next_sequence++;
			return 0;
		}
	}
	return -EIO;
}
