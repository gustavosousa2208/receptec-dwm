#include <receptec/timing.h>

#include <limits.h>

const struct receptec_timing_profile receptec_default_timing = {
	.sampling_period_us = 1000000U,
	.slot_duration_us = 20000U,
	.response_delay_us = 1000U,
	.response_timeout_us = 3000U,
	.guard_us = 500U,
	.guard_step_us = 100U,
	.guard_miss_limit = 5U,
	.attempt_count = 3U,
	.attempt_offset_us = { 0U, 5000U, 10000U },
};

int receptec_timing_validate(const struct receptec_timing_profile *timing,
			     size_t slot_count)
{
	uint8_t attempt;
	uint32_t last_offset;

	if (timing == NULL || slot_count == 0U ||
	    timing->sampling_period_us == 0U || timing->slot_duration_us == 0U ||
	    timing->response_timeout_us == 0U) {
		return RECEPTEC_TIMING_ERR_ARGUMENT;
	}

	if (timing->attempt_count == 0U ||
	    timing->attempt_count > RECEPTEC_TIMING_MAX_ATTEMPTS) {
		return RECEPTEC_TIMING_ERR_ATTEMPTS;
	}

	if (timing->response_delay_us >= timing->response_timeout_us) {
		return RECEPTEC_TIMING_ERR_RESPONSE_BUDGET;
	}

	for (attempt = 0U; attempt < timing->attempt_count; ++attempt) {
		if (timing->attempt_offset_us[attempt] > UINT16_MAX) {
			return RECEPTEC_TIMING_ERR_DT_RANGE;
		}

		if (attempt > 0U &&
		    timing->attempt_offset_us[attempt] <=
			    timing->attempt_offset_us[attempt - 1U]) {
			return RECEPTEC_TIMING_ERR_ATTEMPT_ORDER;
		}
	}

	last_offset = timing->attempt_offset_us[timing->attempt_count - 1U];
	if ((uint64_t)last_offset + timing->response_timeout_us >=
	    timing->slot_duration_us) {
		return RECEPTEC_TIMING_ERR_SLOT_BUDGET;
	}

	if ((uint64_t)slot_count * timing->slot_duration_us >
	    timing->sampling_period_us) {
		return RECEPTEC_TIMING_ERR_CYCLE_BUDGET;
	}

	return RECEPTEC_TIMING_OK;
}

int receptec_timing_dt_sampling(const struct receptec_timing_profile *timing,
				uint8_t attempt, uint16_t *dt_sampling_us)
{
	uint32_t offset;

	if (timing == NULL || dt_sampling_us == NULL) {
		return RECEPTEC_TIMING_ERR_ARGUMENT;
	}

	if (attempt >= timing->attempt_count ||
	    attempt >= RECEPTEC_TIMING_MAX_ATTEMPTS) {
		return RECEPTEC_TIMING_ERR_ATTEMPTS;
	}

	offset = timing->attempt_offset_us[attempt];
	if (offset > UINT16_MAX) {
		return RECEPTEC_TIMING_ERR_DT_RANGE;
	}

	*dt_sampling_us = (uint16_t)offset;
	return RECEPTEC_TIMING_OK;
}

uint32_t receptec_timing_expanded_guard(
	const struct receptec_timing_profile *timing, uint8_t missed_slots)
{
	uint64_t expanded;
	uint8_t bounded_misses;

	if (timing == NULL) {
		return 0U;
	}

	bounded_misses = missed_slots;
	if (bounded_misses > timing->guard_miss_limit) {
		bounded_misses = timing->guard_miss_limit;
	}

	expanded = (uint64_t)timing->guard_us +
		   (uint64_t)bounded_misses * timing->guard_step_us;
	return expanded > UINT32_MAX ? UINT32_MAX : (uint32_t)expanded;
}
