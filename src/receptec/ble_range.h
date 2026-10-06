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
 * call from the range thread every round; reads use a consistent snapshot.
 */
void receptec_ble_range_publish(uint8_t pair_index, uint8_t status,
                                uint32_t distance_mm);

enum receptec_ble_range_source {
  RECEPTEC_BLE_RANGE_MEASURED = 0,
  RECEPTEC_BLE_RANGE_FIXED = 1,
  RECEPTEC_BLE_RANGE_UNAVAILABLE = 2,
};

/* Metadata 0x3104: version, pair, source, status, round u64, mm u32,
 * age_ms u32 (little-endian); read returns all three records, notify one.
 * Status != 0 always means invalid distance. Initial records are unavailable.
 */
void receptec_ble_range_publish_extended(uint8_t pair_index, uint8_t source,
                                         uint8_t status, uint64_t round_id,
                                         uint32_t distance_mm);

#endif /* RECEPTEC_BLE_RANGE_H_ */
