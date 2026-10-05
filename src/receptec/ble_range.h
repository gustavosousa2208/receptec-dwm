/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_BLE_RANGE_H_
#define RECEPTEC_BLE_RANGE_H_

#include <stdbool.h>
#include <stdint.h>

/* Bring up BLE (own thread; never fails the boot) and advertise. */
void receptec_ble_range_init(void);

/*
 * Publish one pair's latest round: status 0 carries a valid distance_mm,
 * anything else means "no valid result" (distance ignored). Cheap enough to
 * call from the range thread every round; a torn read only ever affects one
 * notification.
 */
void receptec_ble_range_publish(uint8_t pair_index, uint8_t status,
				 uint32_t distance_mm);

#endif /* RECEPTEC_BLE_RANGE_H_ */
