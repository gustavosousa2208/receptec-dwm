/* SPDX-License-Identifier: Apache-2.0 */
#include "uwb_cycle_policy.h"
#include <assert.h>
#include <stdio.h>

static void test_profile_slots_are_disjoint(void)
{
	const int64_t epoch = 4294967296LL;
	int64_t tag_due = epoch + receptec_uwb_cycle_offset(0);
	int64_t tag_end = receptec_uwb_cycle_slot_end(epoch, 0);
	int64_t base_due = epoch + receptec_uwb_cycle_offset(2);
	int64_t base_end = receptec_uwb_cycle_slot_end(epoch, 2);

	assert(tag_due == epoch + 100 && tag_end == epoch + 375);
	assert(base_due == epoch + 650 && base_end == epoch + 925);
	assert(tag_due >= epoch + RECEPTEC_UWB_CYCLE_ARM_MS);
	assert(tag_end == epoch + receptec_uwb_cycle_offset(1));
	assert(receptec_uwb_cycle_slot_end(epoch, 1) == base_due);
	assert(base_end < epoch + RECEPTEC_UWB_CYCLE_PERIOD_MS);
	assert(receptec_uwb_cycle_offset(1) == 375);
	assert(receptec_uwb_cycle_slot_end(epoch, 255) == -1);
}

static void test_missing_tag_does_not_shift_base_slot(void)
{
	/* The tag may consume its whole deadline with no ACK. The base still
	 * has its own fixed slot; relative-after-completion scheduling would fail. */
	int64_t epoch = 123000;
	int64_t now = receptec_uwb_cycle_slot_end(epoch, 0);
	int64_t base_due = epoch + receptec_uwb_cycle_offset(2);

	assert(base_due - now == 275);
	assert(receptec_uwb_cycle_wait_ms(base_due, now) == 20);
	assert(receptec_uwb_cycle_wait_ms(base_due, base_due - 1) == 1);
	assert(receptec_uwb_cycle_wait_ms(base_due, base_due) == 0);
	assert(receptec_uwb_cycle_wait_ms(base_due, base_due + 1) == 0);
	assert(receptec_uwb_cycle_wait_ms(base_due, base_due + 86400000) == 0);
}

static void test_stall_never_queues_catchup_rounds(void)
{
	bool overrun;
	assert(receptec_uwb_cycle_next_epoch(10000, 10925, &overrun) == 11000 && !overrun);
	assert(receptec_uwb_cycle_next_epoch(10000, 11000, &overrun) == 11000 && !overrun);
	assert(receptec_uwb_cycle_next_epoch(10000, 11001, &overrun) == 12001 && overrun);
	assert(receptec_uwb_cycle_next_epoch(10000, 50000, &overrun) == 51000 && overrun);
}

static void test_delegation_authorization_and_duplicates(void)
{
	const uint64_t round = (1ULL << 32) + 1024;
	#define DECIDE(phase, armed, done, now) receptec_uwb_delegate_decide( \
		true, true, 1, phase, round, round, armed, done, 1000, now)
	assert(DECIDE(0, false, false, 1000) == RECEPTEC_UWB_DELEGATE_ARM);
	assert(DECIDE(1, false, false, 1000) == RECEPTEC_UWB_DELEGATE_IGNORE);
	assert(DECIDE(0, true, false, 1199) == RECEPTEC_UWB_DELEGATE_ACK);
	assert(DECIDE(0, true, false, 1200) == RECEPTEC_UWB_DELEGATE_ACK);
	assert(DECIDE(0, true, false, 1201) == RECEPTEC_UWB_DELEGATE_IGNORE);
	assert(DECIDE(1, true, false, 1150) == RECEPTEC_UWB_DELEGATE_RUN);
	assert(DECIDE(1, true, false, 1151) == RECEPTEC_UWB_DELEGATE_IGNORE);
	assert(DECIDE(1, true, true, 1150) == RECEPTEC_UWB_DELEGATE_ACK);
	assert(DECIDE(1, true, true, 1151) == RECEPTEC_UWB_DELEGATE_IGNORE);
	assert(DECIDE(0, true, true, 1200) == RECEPTEC_UWB_DELEGATE_ACK);
	assert(DECIDE(0, false, true, 1100) == RECEPTEC_UWB_DELEGATE_IGNORE);
	assert(DECIDE(1, true, false, 999) == RECEPTEC_UWB_DELEGATE_IGNORE);
	#undef DECIDE
	assert(receptec_uwb_delegate_decide(false, true, 1, 0, round, round,
		false, false, 1000, 1000) == RECEPTEC_UWB_DELEGATE_IGNORE);
	assert(receptec_uwb_delegate_decide(true, false, 1, 0, round, round,
		false, false, 1000, 1000) == RECEPTEC_UWB_DELEGATE_IGNORE);
	assert(receptec_uwb_delegate_decide(true, true, 2, 0, round, round,
		false, false, 1000, 1000) == RECEPTEC_UWB_DELEGATE_IGNORE);
	assert(receptec_uwb_delegate_decide(true, true, 1, 2, round, round,
		false, false, 1000, 1000) == RECEPTEC_UWB_DELEGATE_IGNORE);
	assert(receptec_uwb_delegate_decide(true, true, 1, 0, round, round + 1,
		false, false, 1000, 1000) == RECEPTEC_UWB_DELEGATE_IGNORE);
	assert(receptec_uwb_delegate_decide(true, true, 1, 1, round, round - 1,
		true, false, 1000, 1100) == RECEPTEC_UWB_DELEGATE_IGNORE);
}

static void test_accelerated_slots(void)
{
	for (uint32_t period = 300; period <= 1000; period++) {
		int64_t slot = receptec_uwb_cycle_slot_ms(period);
		assert(slot > 0);
		assert(receptec_uwb_cycle_offset_for(0, period) >= RECEPTEC_UWB_CYCLE_ARM_MS);
		for (uint8_t pair = 0; pair < 2; pair++)
			assert(receptec_uwb_cycle_offset_for(pair, period) + slot ==
			       receptec_uwb_cycle_offset_for(pair + 1, period));
		assert(receptec_uwb_cycle_offset_for(2, period) + slot <= period - 75);
		bool overrun;
		assert(receptec_uwb_cycle_next_epoch_for(10000, 10000 + period - 1,
			period, &overrun) == 10000 + period && !overrun);
		assert(receptec_uwb_cycle_next_epoch_for(10000, 10000 + period + 1,
			period, &overrun) == 10000 + 2 * period + 1 && overrun);
	}
	assert(receptec_uwb_cycle_slot_ms(1000) == 275);
	assert(receptec_uwb_cycle_slot_ms(299) == -1);
	assert(receptec_uwb_cycle_slot_ms(1001) == -1);
}

int main(void)
{
	test_accelerated_slots();
	test_delegation_authorization_and_duplicates();
	test_profile_slots_are_disjoint();
	test_missing_tag_does_not_shift_base_slot();
	test_stall_never_queues_catchup_rounds();
	puts("RECEPTEC cycle policy tests passed");
	return 0;
}
