#ifndef RECEPTEC_TIMING_H_
#define RECEPTEC_TIMING_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECEPTEC_TIMING_MAX_ATTEMPTS 3U

enum receptec_timing_status {
	RECEPTEC_TIMING_OK = 0,
	RECEPTEC_TIMING_ERR_ARGUMENT = -1,
	RECEPTEC_TIMING_ERR_ATTEMPTS = -2,
	RECEPTEC_TIMING_ERR_ATTEMPT_ORDER = -3,
	RECEPTEC_TIMING_ERR_SLOT_BUDGET = -4,
	RECEPTEC_TIMING_ERR_CYCLE_BUDGET = -5,
	RECEPTEC_TIMING_ERR_DT_RANGE = -6,
	RECEPTEC_TIMING_ERR_RESPONSE_BUDGET = -7,
};

struct receptec_timing_profile {
	uint32_t sampling_period_us;
	uint32_t slot_duration_us;
	uint32_t response_delay_us;
	uint32_t response_timeout_us;
	uint32_t guard_us;
	uint32_t guard_step_us;
	uint8_t guard_miss_limit;
	uint8_t attempt_count;
	uint32_t attempt_offset_us[RECEPTEC_TIMING_MAX_ATTEMPTS];
};

extern const struct receptec_timing_profile receptec_default_timing;

int receptec_timing_validate(const struct receptec_timing_profile *timing,
			     size_t slot_count);

int receptec_timing_dt_sampling(const struct receptec_timing_profile *timing,
				uint8_t attempt, uint16_t *dt_sampling_us);

uint32_t receptec_timing_expanded_guard(
	const struct receptec_timing_profile *timing, uint8_t missed_slots);

#ifdef __cplusplus
}
#endif

#endif /* RECEPTEC_TIMING_H_ */
