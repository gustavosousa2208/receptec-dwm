/* SPDX-License-Identifier: Apache-2.0 */
#include "uwb_round_guard.h"
#include <assert.h>

int main(void)
{
	const uint64_t round = UINT64_C(0x100000001);

	assert(receptec_round_arm_action(false, false, 0, 0) ==
	       RECEPTEC_ROUND_ADOPT);
	/* Duplicate ROUND_ARM must not clear an active exchange. */
	assert(receptec_round_arm_action(true, true, round, round) ==
	       RECEPTEC_ROUND_ACK_DUPLICATE);
	assert(receptec_round_arm_action(true, false, round, round) ==
	       RECEPTEC_ROUND_ACK_DUPLICATE);
	/* A newer round at 100 ms cannot replace the 94 ms exchange. */
	assert(receptec_round_arm_action(true, true, round, round + 1) ==
	       RECEPTEC_ROUND_IGNORE_BUSY);
	assert(receptec_round_arm_action(true, true, round, round - 1) ==
	       RECEPTEC_ROUND_IGNORE_BUSY);
	assert(!receptec_round_arm_expired(true, 1000, 1094));
	assert(!receptec_round_arm_expired(true, 1000, 1110));
	assert(receptec_round_arm_expired(true, 1000, 1111));
	/* Dead half-exchanges expire, allowing the next round to recover. */
	bool armed = !receptec_round_arm_expired(true, 1000, 1111);
	assert(receptec_round_arm_action(true, armed, round, round + 1) ==
	       RECEPTEC_ROUND_ADOPT);
	assert(!receptec_round_arm_expired(false, 1000, 9000));
	return 0;
}
