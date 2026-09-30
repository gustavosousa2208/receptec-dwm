/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_RADIO_H_
#define RECEPTEC_UWB_RADIO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uwb_peer_registry.h"

int receptec_uwb_radio_init(void);
int receptec_uwb_local_eui_get(
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_local_eui_set(
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_peer_register(uint8_t slot,
			       const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_peer_unregister(uint8_t slot);
int receptec_uwb_peer_get(uint8_t slot,
			  uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_radio_send_sync(uint8_t peer_slot);
/* Send one STOP and wait for the matching STOP_ACK before reporting success. */
int receptec_uwb_radio_send_stop(uint8_t peer_slot);
/* Bench A/B: switch the external receive LNA (GPIO5) on or off. */
int receptec_uwb_radio_set_lna(bool enable);

enum receptec_uwb_peer_action {
	RECEPTEC_UWB_PEER_ACTION_SYNC = 0,
	RECEPTEC_UWB_PEER_ACTION_DATA_REQ,
};

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
	bool sync_mode;
	bool schedule_mode;
	uint8_t peer_slot;
	uint32_t period_us;
	uint32_t cycles;
	uint32_t overruns;
	enum receptec_uwb_peer_action action[RECEPTEC_UWB_MAX_PEERS];
	int64_t started_ms;
	int64_t stopped_ms;
	struct receptec_uwb_poll_peer_stats peer[RECEPTEC_UWB_MAX_PEERS];
};

/* Poll every registered peer with DATA_REQ once per period_ms. */
int receptec_uwb_poll_start(uint32_t period_ms);
/* Send one SYNC per period to a single registered peer; retry in later epochs. */
int receptec_uwb_sync_start(uint8_t peer_slot, uint32_t period_ms);
/* Configure a volatile per-peer action; unconfigured slots default to SYNC. */
int receptec_uwb_schedule_set(uint8_t peer_slot,
			      enum receptec_uwb_peer_action action);
int receptec_uwb_schedule_get(uint8_t peer_slot,
			      enum receptec_uwb_peer_action *action);
int receptec_uwb_schedule_period_set(uint32_t period_ms);
int receptec_uwb_schedule_period_get(uint32_t *period_ms);
/* Run each registered peer's configured action at the persisted period. */
int receptec_uwb_schedule_start(void);
int receptec_uwb_sync_stop(void);
int receptec_uwb_poll_stop(void);
void receptec_uwb_poll_get(struct receptec_uwb_poll_status *out);
/* Print every received DATA payload as one `DATA slot=..` line. */
void receptec_uwb_poll_stream(bool enable);
/* Latest DATA payload of a peer; -ENODATA until one has arrived. */
int receptec_uwb_poll_last(uint8_t slot, uint8_t *payload, size_t capacity,
			   size_t *length, uint32_t *count, uint32_t *age_ms);

/*
 * Runtime UWB test (see docs/uwb-runtime-tests.md): arm `name` on a registered peer's slot
 * and start a 1 s poll that runs it between ordinary transactions. Returns at once; the
 * outcome is one `UWB_TEST ...` line on the console. -EBUSY while a poll or a test runs,
 * -ENOENT for an unregistered slot, -ENOTSUP for a name that is not implemented,
 * -EINVAL for a bad argument (retry takes 1 or 2 ignored responses).
 */
int receptec_uwb_test_start(const char *name, uint8_t arg, uint8_t slot);
/* Ask the running test to end at its next cycle boundary (result: FAIL reason=aborted). */
int receptec_uwb_test_abort(void);

#endif /* RECEPTEC_UWB_RADIO_H_ */
