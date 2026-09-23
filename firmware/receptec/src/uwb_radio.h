/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_RADIO_H_
#define RECEPTEC_UWB_RADIO_H_

#include <stdint.h>

#include "uwb_peer_registry.h"

int receptec_uwb_radio_init(void);
int receptec_uwb_peer_register(uint8_t slot,
			       const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_peer_unregister(uint8_t slot);
int receptec_uwb_peer_get(uint8_t slot,
			  uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_radio_send_sync(uint8_t peer_slot);

#endif /* RECEPTEC_UWB_RADIO_H_ */
