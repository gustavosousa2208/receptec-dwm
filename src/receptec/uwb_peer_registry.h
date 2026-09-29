/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_PEER_REGISTRY_H_
#define RECEPTEC_UWB_PEER_REGISTRY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <receptec/uwb_protocol.h>

#define RECEPTEC_UWB_MAX_PEERS 4U
#define RECEPTEC_UWB_PEER_HISTORY_CAPACITY 256U

struct receptec_uwb_peer_slot {
	bool registered;
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
};

struct receptec_uwb_peer_history {
	bool used;
	uint8_t next_sequence;
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
};

struct receptec_uwb_peer_registry {
	uint8_t local_eui64[RECEPTEC_UWB_EUI64_SIZE];
	struct receptec_uwb_peer_slot slots[RECEPTEC_UWB_MAX_PEERS];
	struct receptec_uwb_peer_history history[
		RECEPTEC_UWB_PEER_HISTORY_CAPACITY];
};

int receptec_uwb_peer_registry_init(
	struct receptec_uwb_peer_registry *registry,
	const uint8_t local_eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_peer_registry_register(
	struct receptec_uwb_peer_registry *registry, size_t slot,
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_peer_registry_unregister(
	struct receptec_uwb_peer_registry *registry, size_t slot);
int receptec_uwb_peer_registry_get(
	const struct receptec_uwb_peer_registry *registry, size_t slot,
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_peer_registry_next_sequence(
	struct receptec_uwb_peer_registry *registry, size_t slot,
	uint8_t *sequence);

/*
 * Set the sequence number the next transaction with this peer will use. The runtime
 * sequence_wrap test uses it to put the counter back where it was after it has
 * sent 254, 255 and 0, so the next ordinary transaction is not out of step.
 */
int receptec_uwb_peer_registry_set_next_sequence(
	struct receptec_uwb_peer_registry *registry, size_t slot,
	uint8_t sequence);

#endif /* RECEPTEC_UWB_PEER_REGISTRY_H_ */
