/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RECEPTEC_UWB_CYCLE_POLICY_H_
#define RECEPTEC_UWB_CYCLE_POLICY_H_

#include <stdint.h>
#include <stdbool.h>

#define RECEPTEC_UWB_CYCLE_PERIOD_MS 1000
#define RECEPTEC_UWB_CYCLE_ARM_MS 80
#define RECEPTEC_UWB_CYCLE_SLOT_MS 275

/* Only active pairs have slots; A-T is explicitly absent in this profile. */
static inline int64_t receptec_uwb_cycle_offset(uint8_t pair)
{
	return pair == 0U ? 100 : (pair == 2U ? 650 : -1);
}

static inline int64_t receptec_uwb_cycle_slot_end(int64_t epoch, uint8_t pair)
{
	int64_t offset = receptec_uwb_cycle_offset(pair);

	return offset < 0 ? -1 : epoch + offset + RECEPTEC_UWB_CYCLE_SLOT_MS;
}

/* No catch-up traffic: a missed next epoch starts a fresh period from now. */
static inline int64_t receptec_uwb_cycle_next_epoch(int64_t epoch, int64_t now,
						 bool *overrun)
{
	int64_t next = epoch + RECEPTEC_UWB_CYCLE_PERIOD_MS;

	*overrun = now > next;
	return *overrun ? now + RECEPTEC_UWB_CYCLE_PERIOD_MS : next;
}

/* Re-read once, then bound waits; late callbacks cannot become huge sleeps. */
static inline int32_t receptec_uwb_cycle_wait_ms(int64_t target, int64_t now)
{
	int64_t remaining = target - now;

	return remaining <= 0 ? 0 : (remaining > 20 ? 20 : (int32_t)remaining);
}

#endif
