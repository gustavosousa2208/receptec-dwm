/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_RADIO_H_
#define RECEPTEC_UWB_RADIO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uwb_peer_registry.h"

int receptec_uwb_radio_init(void);
int receptec_uwb_peer_register(uint8_t slot,
			       const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_peer_unregister(uint8_t slot);
int receptec_uwb_peer_get(uint8_t slot,
			  uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_radio_send_sync(uint8_t peer_slot);

struct receptec_uwb_poll_peer_stats {
	uint32_t transactions;
	uint32_t completed;
	uint32_t completed_on_attempt[3];
	uint32_t attempts;
	uint32_t timeouts;
	uint32_t rx_errors;
	uint32_t bad_frames;
	uint32_t late_schedules;
	uint32_t exhausted;
	uint64_t payload_bytes;
	uint32_t sample_gaps;
	uint32_t last_sample;
	bool have_sample;
};

struct receptec_uwb_poll_status {
	bool running;
	uint32_t period_us;
	uint32_t cycles;
	uint32_t overruns;
	int64_t started_ms;
	int64_t stopped_ms;
	struct receptec_uwb_poll_peer_stats peer[RECEPTEC_UWB_MAX_PEERS];
};

/* Poll every registered peer with DATA_REQ once per period_ms. */
int receptec_uwb_poll_start(uint32_t period_ms);
int receptec_uwb_poll_stop(void);
void receptec_uwb_poll_get(struct receptec_uwb_poll_status *out);
/* Print every received DATA payload as one `DATA slot=..` line. */
void receptec_uwb_poll_stream(bool enable);
/* Latest DATA payload of a peer; -ENODATA until one has arrived. */
int receptec_uwb_poll_last(uint8_t slot, uint8_t *payload, size_t capacity,
			   size_t *length, uint32_t *count, uint32_t *age_ms);

#endif /* RECEPTEC_UWB_RADIO_H_ */
