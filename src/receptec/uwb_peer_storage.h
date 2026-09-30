/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_PEER_STORAGE_H_
#define RECEPTEC_UWB_PEER_STORAGE_H_

#include <stdint.h>

#include "uwb_peer_registry.h"

int receptec_uwb_peer_storage_init(void);

int receptec_uwb_peer_storage_load(
	uint8_t slot,
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);

int receptec_uwb_peer_storage_save(
	uint8_t slot,
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);

int receptec_uwb_peer_storage_delete(uint8_t slot);

int receptec_uwb_identity_storage_load(
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_identity_storage_save(
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);

#endif
