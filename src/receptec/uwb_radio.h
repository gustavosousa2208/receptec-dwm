/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_RADIO_H_
#define RECEPTEC_UWB_RADIO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "uwb_peer_registry.h"
#include "uwb_peer_storage.h"

int receptec_uwb_radio_init(void);
int receptec_uwb_local_eui_get(uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_local_eui_set(const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_peer_register(uint8_t slot, const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_peer_unregister(uint8_t slot);
int receptec_uwb_peer_get(uint8_t slot, uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE]);
int receptec_uwb_peer_enabled_get(uint8_t slot, bool *enabled);
int receptec_uwb_peer_enabled_set(uint8_t slot, bool enabled);
/*
 * Human-readable per-peer label (e.g. "coletec-a"). Persisted in settings,
 * independent of the EUI, and shown by `uwb peer list`. get() copies the
 * label and returns -ENOENT when the slot has none; set() validates the
 * charset ([A-Za-z0-9_.-], 1..31 chars) and persists; clear() removes it.
 */
int receptec_uwb_peer_nickname_get(uint8_t slot, char *nickname, size_t capacity);
int receptec_uwb_peer_nickname_set(uint8_t slot, const char *nickname);
int receptec_uwb_peer_nickname_clear(uint8_t slot);
/*
 * Background range auto-responder: while set, this board answers DS-TWR
 * rounds addressed to it (pair A-B) from any commissioned, enabled peer,
 * without a manual `uwb range respond` per round. Persisted in settings and
 * restored at boot. Initiator commands (`range start`, poll, sync) still take
 * the radio first; an in-progress answer fails them with -EBUSY.
 */
int receptec_uwb_range_auto_set(bool enable);
int receptec_uwb_range_auto_get(bool *enable);
/*
 * Range autostart: begin this round automatically at boot (power-up-and-go
 * master). Saved persistently; cleared with
 * receptec_uwb_range_autostart_clear(). A manual `range stop` stays stopped
 * until the next boot or `range start`.
 */
int receptec_uwb_range_autostart_set(uint8_t peer_slot, uint32_t period_ms, uint8_t pair_index);
int receptec_uwb_range_autostart_clear(void);
int receptec_uwb_range_autostart_get(struct receptec_uwb_range_autostart *out);
int receptec_uwb_radio_send_sync(uint8_t peer_slot);
/* Send one STOP when Ttimeout is configured; otherwise return -ENOTSUP. */
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
/* Send one SYNC per period to a single registered peer; retry in later epochs.
 */
int receptec_uwb_sync_start(uint8_t peer_slot, uint32_t period_ms);
/* Configure a volatile per-peer action; unconfigured slots default to SYNC. */
int receptec_uwb_schedule_set(uint8_t peer_slot, enum receptec_uwb_peer_action action);
int receptec_uwb_schedule_get(uint8_t peer_slot, enum receptec_uwb_peer_action *action);
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
int receptec_uwb_poll_last(uint8_t slot, uint8_t *payload, size_t capacity, size_t *length,
			   uint32_t *count, uint32_t *age_ms);

/*
 * Runtime UWB test (see docs/uwb-runtime-tests.md): arm `name` on a registered
 * peer's slot and start a 1 s poll that runs it between ordinary transactions.
 * Returns at once; the outcome is one `UWB_TEST ...` line on the console.
 * -EBUSY while a poll or a test runs, -ENOENT for an unregistered slot,
 * -ENOTSUP for a name that is not implemented, -EINVAL for a bad argument
 * (retry takes 1 or 2 ignored responses).
 */
int receptec_uwb_test_start(const char *name, uint8_t arg, uint8_t slot);
/* Ask the running test to end at its next cycle boundary (result: FAIL
 * reason=aborted). */
int receptec_uwb_test_abort(void);

struct receptec_uwb_range_round_status {
	bool running;
	bool auto_responder;
	uint8_t peer_slot;
	uint8_t pair_index;
	uint32_t period_us;
	uint64_t round_id;
	uint32_t rounds;
	uint32_t successes;
	uint32_t plan_timeouts;
	uint32_t round_arm_timeouts;
	int32_t last_arm_send_status;
	uint32_t range_failures;
	bool last_valid;
	uint8_t last_status;
	uint32_t last_distance_mm;
	int64_t last_update_ms;
};

/*
 * Minimal DS-TWR-as-initiator proof of concept (contract UWB v1.2 S5-S9):
 * each period, send ROUND_ARM then a RANGE_PLAN ARM for `pair_index` to the
 * peer in `peer_slot`, run one DS-TWR exchange with this RECEPTEC as
 * initiator (POLL -> RESPONSE -> FINAL), and read back the responder's
 * RANGE_RESULT uplink. `pair_index` must match what the responder expects
 * for this peer (0=A-T and 1=B-T are initiated by whichever anchor this
 * board's own CONFIG_RECEPTEC_FIXED_ANCHOR_ID names; 2=A-B is initiated by
 * the master against the base, regardless of the master's own anchor id).
 * Deliberately not the full master state machine in S9: no simultaneous
 * multi-pair scheduling, no retry on a dropped ARM/ACK/result, and round_id
 * uses an in-RAM counter (boot_generation is always 0, not NVS-persisted).
 * Shares the single radio-owner thread with poll/sync/schedule: -EBUSY
 * while any of those run, and vice versa.
 */
struct receptec_uwb_cycle_config;
int receptec_uwb_cycle_configure(const struct receptec_uwb_cycle_config *cfg);
int receptec_uwb_cycle_start(void);
int receptec_uwb_cycle_get(struct receptec_uwb_cycle_config *out, bool *running,
			   uint32_t success[3], uint32_t failure[3], uint32_t max_ms[3],
			   uint32_t *overruns);
int receptec_uwb_range_start(uint8_t peer_slot, uint32_t period_ms, uint8_t pair_index);
int receptec_uwb_range_stop(void);
void receptec_uwb_range_get(struct receptec_uwb_range_round_status *out);
/*
 * Follow-mode display: while quiet is set, the range thread still records
 * every round in the status above but skips its per-round console line, so a
 * shell follower can redraw one updating line instead of scrolling the log.
 */
void receptec_uwb_range_set_quiet(bool quiet);

/*
 * One-shot DS-TWR responder (contract UWB v1.2 S5-S9): listens up to
 * `timeout_ms` for ROUND_ARM, then ARM for `pair_index`, then POLL/FINAL
 * from the registered master in `master_slot`, computes the distance and
 * uplinks RANGE_RESULT to that master. Used today for pair_index=2 (A-B),
 * the only pair where a RECEPTEC-family board plays responder; returns 0
 * with *out_status==RECEPTEC_UWB_RANGE_OK on a successful measurement, a
 * negative errno otherwise (-EBUSY/-EAGAIN as receptec_uwb_range_start()).
 * Blocks the calling thread for up to timeout_ms; call it from the shell,
 * not from range_thread.
 */
int receptec_uwb_range_respond(uint8_t master_slot, uint8_t pair_index, uint32_t timeout_ms,
			       enum receptec_uwb_range_status *out_status,
			       uint32_t *out_distance_mm);

/*
 * Passively wait up to `timeout_ms` for a RANGE_RESULT for `pair_index`
 * from the peer in `peer_slot`, without initiating anything. Contract UWB
 * v1.2 S2.1 always addresses RANGE_RESULT to the master, even for pairs
 * the master did not initiate (B-T: B initiates, T reports to the master).
 * On the master, this is how to see a pair's distance when some other
 * node drove that round -- e.g. `receptec_uwb_range_listen(<T's slot>, 1,
 * ...)` while B runs its own `uwb range start` for pair 1. No round_id is
 * checked (the master never armed that round), so *out_round_id reports
 * whichever round the result named. Same busy/-EBUSY rules as the other
 * range_* entry points; blocks the calling thread, call from the shell.
 */
int receptec_uwb_range_listen(uint8_t peer_slot, uint8_t pair_index, uint32_t timeout_ms,
			      enum receptec_uwb_range_status *out_status, uint32_t *out_distance_mm,
			      uint64_t *out_round_id);

#endif /* RECEPTEC_UWB_RADIO_H_ */
