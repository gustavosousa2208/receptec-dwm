/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_BLE_RANGE_H_
#define RECEPTEC_BLE_RANGE_H_

#include <stdbool.h>
#include <stdint.h>

/* Bring up BLE (own thread; never fails the boot) and advertise. */
void receptec_ble_range_init(void);

/* Best-effort notification transport. Accepted means queued to BLE, not
 * delivered to the PC. Reads retain the newest value regardless of delivery. */
struct receptec_ble_range_stats {
  uint32_t published[3], coalesced[3], notify_accepted[3], notify_errors[3];
  uint32_t legacy_errors[3], no_subscriber[3], not_ready[3];
  int last_notify_error[3];
  uint32_t submit_errors;
};
void receptec_ble_range_stats_get(struct receptec_ble_range_stats *out);

/*
 * Publish one pair's latest round: status 0 carries a valid distance_mm,
 * anything else means "no valid result" (distance ignored). Only copies to a bounded
 * latest-per-pair mailbox and submits work; never calls GATT or waits for BLE
 * buffers from the range thread. Congestion may discard older notifications.
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
