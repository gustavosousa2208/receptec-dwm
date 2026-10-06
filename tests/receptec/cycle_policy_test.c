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
	assert(tag_end < base_due && base_end < epoch + RECEPTEC_UWB_CYCLE_PERIOD_MS);
	assert(receptec_uwb_cycle_offset(1) == -1);
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

int main(void)
{
	test_profile_slots_are_disjoint();
	test_missing_tag_does_not_shift_base_slot();
	test_stall_never_queues_catchup_rounds();
	puts("RECEPTEC cycle policy tests passed");
	return 0;
}
