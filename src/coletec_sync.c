/* SPDX-License-Identifier: Apache-2.0 */

#include <receptec/coletec_sync.h>

static void enter_discovery(struct coletec_sync *sync)
{
	sync->state = COLETEC_SYNC_DISCOVERY;
	sync->missed_slots = 0U;
	sync->discovery_cycle_count = 0U;
	sync->discovery_off_multiplier = 1U;
}

void coletec_sync_init(struct coletec_sync *sync)
{
	sync->state = COLETEC_SYNC_INITIAL_RX;
	sync->missed_slots = 0U;
	sync->discovery_cycle_count = 0U;
	sync->discovery_off_multiplier = 1U;
}

void coletec_sync_accept_request(struct coletec_sync *sync)
{
	sync->state = COLETEC_SYNC_SYNCHRONIZED;
	sync->missed_slots = 0U;
	sync->discovery_cycle_count = 0U;
	sync->discovery_off_multiplier = 1U;
}

void coletec_sync_initial_rx_expired(struct coletec_sync *sync)
{
	enter_discovery(sync);
}

void coletec_sync_miss_slot(struct coletec_sync *sync)
{
	if (sync->state != COLETEC_SYNC_SYNCHRONIZED) {
		return;
	}
	if (++sync->missed_slots >= COLETEC_MISSES_BEFORE_DISCOVERY) {
		enter_discovery(sync);
	}
}

void coletec_sync_discovery_on_expired(struct coletec_sync *sync)
{
	if (sync->state != COLETEC_SYNC_DISCOVERY) {
		return;
	}
	if (++sync->discovery_cycle_count >=
	    COLETEC_DISCOVERY_CYCLES_PER_STEP) {
		sync->discovery_cycle_count = 0U;
		if (sync->discovery_off_multiplier <
		    COLETEC_DISCOVERY_MAX_OFF_MULTIPLIER) {
			sync->discovery_off_multiplier++;
		}
	}
}

uint16_t coletec_sync_guard_us(const struct coletec_sync *sync)
{
	return COLETEC_BASE_GUARD_US +
	       (uint16_t)sync->missed_slots * COLETEC_GUARD_STEP_US;
}

uint32_t coletec_sync_discovery_off_ms(const struct coletec_sync *sync)
{
	return (uint32_t)sync->discovery_off_multiplier *
	       COLETEC_DISCOVERY_OFF_MS;
}
