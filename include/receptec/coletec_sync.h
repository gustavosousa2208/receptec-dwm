/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_COLETEC_SYNC_H_
#define RECEPTEC_COLETEC_SYNC_H_

#include <stdbool.h>
#include <stdint.h>

#define COLETEC_INITIAL_RX_MS 10000U
#define COLETEC_BASE_GUARD_US 500U
#define COLETEC_GUARD_STEP_US 100U
#define COLETEC_MISSES_BEFORE_DISCOVERY 5U
#define COLETEC_DISCOVERY_ON_MS 2000U
#define COLETEC_DISCOVERY_OFF_MS 2000U
#define COLETEC_DISCOVERY_CYCLES_PER_STEP 3U
#define COLETEC_DISCOVERY_MAX_OFF_MULTIPLIER 5U

enum coletec_sync_state {
	COLETEC_SYNC_INITIAL_RX,
	COLETEC_SYNC_SYNCHRONIZED,
	COLETEC_SYNC_DISCOVERY,
};

struct coletec_sync {
	enum coletec_sync_state state;
	uint8_t missed_slots;
	uint8_t discovery_cycle_count;
	uint8_t discovery_off_multiplier;
};

void coletec_sync_init(struct coletec_sync *sync);
void coletec_sync_accept_request(struct coletec_sync *sync);
void coletec_sync_initial_rx_expired(struct coletec_sync *sync);
void coletec_sync_miss_slot(struct coletec_sync *sync);
void coletec_sync_discovery_on_expired(struct coletec_sync *sync);
uint16_t coletec_sync_guard_us(const struct coletec_sync *sync);
uint32_t coletec_sync_discovery_off_ms(const struct coletec_sync *sync);

#endif /* RECEPTEC_COLETEC_SYNC_H_ */
