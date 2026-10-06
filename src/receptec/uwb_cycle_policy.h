/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RECEPTEC_UWB_CYCLE_POLICY_H_
#define RECEPTEC_UWB_CYCLE_POLICY_H_

#include <stdint.h>
#include <stdbool.h>

#define RECEPTEC_UWB_CYCLE_PERIOD_MS 1000
#define RECEPTEC_UWB_CYCLE_ARM_MS 80
#define RECEPTEC_UWB_CYCLE_SLOT_MS 275

static inline bool receptec_uwb_cycle_period_valid(uint32_t period)
{
	return period >= 300U && period <= 1000U;
}

static inline int64_t receptec_uwb_cycle_slot_ms(uint32_t period)
{
	return receptec_uwb_cycle_period_valid(period) ? (int64_t)((period - 175U) / 3U) : -1;
}

static inline int64_t receptec_uwb_cycle_offset_for(uint8_t pair, uint32_t period)
{
	int64_t slot = receptec_uwb_cycle_slot_ms(period);
	return pair < 3U && slot > 0 ? 100 + slot * pair : -1;
}

/* The master serializes all pairs; the qualified default remains one Hz. */
static inline int64_t receptec_uwb_cycle_offset(uint8_t pair)
{
	return receptec_uwb_cycle_offset_for(pair, RECEPTEC_UWB_CYCLE_PERIOD_MS);
}

static inline int64_t receptec_uwb_cycle_slot_end(int64_t epoch, uint8_t pair)
{
	int64_t offset = receptec_uwb_cycle_offset(pair);

	return offset < 0 ? -1 : epoch + offset + RECEPTEC_UWB_CYCLE_SLOT_MS;
}

static inline int64_t receptec_uwb_cycle_next_epoch_for(int64_t epoch, int64_t now,
						 uint32_t period, bool *overrun)
{
	int64_t next = epoch + period;
	*overrun = now > next;
	return *overrun ? now + period : next;
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

/* Distinct actions make timer installation and ACK-only duplicates explicit.
 * phase values match the established RANGE_PLAN encoding: ARM=0, START=1. */
enum receptec_uwb_delegate_action {
	RECEPTEC_UWB_DELEGATE_IGNORE,
	RECEPTEC_UWB_DELEGATE_ARM,
	RECEPTEC_UWB_DELEGATE_ACK,
	RECEPTEC_UWB_DELEGATE_RUN,
};

static inline enum receptec_uwb_delegate_action receptec_uwb_delegate_decide(
	bool identity_match, bool have_round, uint8_t pair, uint8_t phase,
	uint64_t current_round, uint64_t incoming_round, bool armed,
	bool completed, int64_t arm_ms, int64_t now_ms)
{
	if (!identity_match || !have_round || pair != 1U ||
	    current_round != incoming_round || phase > 1U) {
		return RECEPTEC_UWB_DELEGATE_IGNORE;
	}
	if (!armed) {
		/* A late repeated ARM must not reopen an already executed exchange. */
		return phase == 0U && !completed ? RECEPTEC_UWB_DELEGATE_ARM :
			RECEPTEC_UWB_DELEGATE_IGNORE;
	}
	if (now_ms < arm_ms) {
		return RECEPTEC_UWB_DELEGATE_IGNORE;
	}
	int64_t elapsed = now_ms - arm_ms;
	if (phase == 0U) {
		return elapsed <= 200 ? RECEPTEC_UWB_DELEGATE_ACK :
			RECEPTEC_UWB_DELEGATE_IGNORE;
	}
	if (elapsed > 150) {
		return RECEPTEC_UWB_DELEGATE_IGNORE;
	}
	return completed ? RECEPTEC_UWB_DELEGATE_ACK : RECEPTEC_UWB_DELEGATE_RUN;
}

#endif
