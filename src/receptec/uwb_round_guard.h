/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RECEPTEC_UWB_ROUND_GUARD_H
#define RECEPTEC_UWB_ROUND_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#define RECEPTEC_UWB_ARM_WINDOW_MS 110

enum receptec_round_arm_action {
	RECEPTEC_ROUND_ADOPT,
	RECEPTEC_ROUND_ACK_DUPLICATE,
	RECEPTEC_ROUND_IGNORE_BUSY,
};

static inline enum receptec_round_arm_action receptec_round_arm_action(
	bool have_round, bool armed, uint64_t current, uint64_t incoming)
{
	if (have_round && current == incoming) {
		return RECEPTEC_ROUND_ACK_DUPLICATE;
	}
	return armed ? RECEPTEC_ROUND_IGNORE_BUSY : RECEPTEC_ROUND_ADOPT;
}

static inline bool receptec_round_arm_expired(bool armed, int64_t arm_ms,
					    int64_t now_ms)
{
	return armed && now_ms - arm_ms > RECEPTEC_UWB_ARM_WINDOW_MS;
}

#endif
