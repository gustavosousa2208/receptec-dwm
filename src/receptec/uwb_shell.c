/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "uwb_radio.h"

#define UWB_DEFAULT_T_SAMPLING_MS 1000U

static int parse_slot(const struct shell *shell, const char *text,
		      uint8_t *slot)
{
	char *end;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' ||
	    parsed >= RECEPTEC_UWB_MAX_PEERS) {
		shell_error(shell, "slot must be in range 0..%u",
			    RECEPTEC_UWB_MAX_PEERS - 1U);
		return -EINVAL;
	}
	*slot = (uint8_t)parsed;
	return 0;
}

static int hex_nibble(char value)
{
	if (value >= '0' && value <= '9') {
		return value - '0';
	}
	if (value >= 'a' && value <= 'f') {
		return value - 'a' + 10;
	}
	if (value >= 'A' && value <= 'F') {
		return value - 'A' + 10;
	}
	return -1;
}

static int parse_eui64(const char *text,
		       uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	size_t i;

	if (strlen(text) != 23U) {
		return -EINVAL;
	}
	for (i = 0U; i < RECEPTEC_UWB_EUI64_SIZE; ++i) {
		size_t offset = i * 3U;
		int high = hex_nibble(text[offset]);
		int low = hex_nibble(text[offset + 1U]);

		if (high < 0 || low < 0 ||
		    (i + 1U < RECEPTEC_UWB_EUI64_SIZE &&
		     text[offset + 2U] != ':')) {
			return -EINVAL;
		}
		eui64[i] = (uint8_t)((high << 4) | low);
	}
	return 0;
}

static void format_eui64(const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE],
			 char text[24])
{
	snprintk(text, 24, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
		eui64[0], eui64[1], eui64[2], eui64[3], eui64[4], eui64[5],
		eui64[6], eui64[7]);
}

static void print_peer_slots(const struct shell *shell)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	char text[24];
	uint8_t slot;

	for (slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		if (receptec_uwb_peer_get(slot, eui64) == 0) {
			format_eui64(eui64, text);
			shell_print(shell, "slot %u: %s", slot, text);
		} else {
			shell_print(shell, "slot %u: empty", slot);
		}
	}
}

static int cmd_uwb_identity_show(const struct shell *shell, size_t argc,
				char **argv)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	char text[24];
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = receptec_uwb_local_eui_get(eui64);
	if (ret < 0) {
		shell_error(shell, "local identity unavailable: %d", ret);
		return ret;
	}
	format_eui64(eui64, text);
	shell_print(shell, "active local UWB EUI: %s", text);
	return 0;
}

static int cmd_uwb_identity_set(const struct shell *shell, size_t argc,
				char **argv)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	char text[24];
	int ret;

	ARG_UNUSED(argc);
	if (parse_eui64(argv[1], eui64) < 0) {
		shell_error(shell, "EUI-64 must be 8 colon-separated hex bytes");
		return -EINVAL;
	}
	ret = receptec_uwb_local_eui_set(eui64);
	if (ret < 0) {
		shell_error(shell, "identity save failed: %d", ret);
		return ret;
	}
	format_eui64(eui64, text);
	shell_print(shell, "saved local UWB EUI %s; reset the board to apply", text);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_identity,
	SHELL_CMD(show, NULL, "Show active local UWB EUI", cmd_uwb_identity_show),
	SHELL_CMD_ARG(set, NULL, "Persist local EUI: set <EUI-64>",
		     cmd_uwb_identity_set, 2, 0),
	SHELL_SUBCMD_SET_END);

static int cmd_uwb_status(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(shell, "QM33120W radio initialized at boot; RF is idle between tests");
	print_peer_slots(shell);
	return 0;
}

static int cmd_uwb_sync_once(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t slot;
	int ret;

	ARG_UNUSED(argc);
	ret = parse_slot(shell, argv[1], &slot);
	if (ret < 0) {
		shell_error(shell, "usage: uwb sync once <slot>");
		return ret;
	}

	shell_print(shell, "sending one SYNC to slot %u", slot);
	ret = receptec_uwb_radio_send_sync(slot);
	if (ret < 0) {
		shell_error(shell, "SYNC/ACK failed: %d", ret);
		return ret;
	}
	shell_print(shell, "SYNC/ACK passed with slot %u", slot);
	return 0;
}

static int cmd_uwb_stop(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t slot;
	int ret;

	ARG_UNUSED(argc);
	ret = parse_slot(shell, argv[1], &slot);
	if (ret < 0) {
		return ret;
	}
	ret = receptec_uwb_radio_send_stop(slot);
	if (ret == -ENOTSUP) {
		shell_error(shell, "STOP is disabled: configure RECEPTEC_STOP_TIMEOUT_US");
		return ret;
	}
	if (ret < 0) {
		shell_error(shell, "STOP/STOP_ACK failed for slot %u: %d", slot, ret);
		return ret;
	}
	shell_print(shell, "STOP/STOP_ACK passed with slot %u", slot);
	return 0;
}

static int cmd_uwb_sync_start(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t slot;
	unsigned long period_ms = UWB_DEFAULT_T_SAMPLING_MS;
	char *end;
	int ret;

	if (argc < 2U || argc > 3U || parse_slot(shell, argv[1], &slot) < 0) {
		shell_error(shell, "usage: uwb sync start <slot> [period_ms 1..60000]");
		return -EINVAL;
	}
	if (argc == 3U) {
		errno = 0;
		period_ms = strtoul(argv[2], &end, 10);
		if (errno != 0 || end == argv[2] || *end != '\0' ||
		    period_ms == 0UL || period_ms > 60000UL) {
			shell_error(shell, "period must be 1..60000 ms");
			return -EINVAL;
		}
	}
	ret = receptec_uwb_sync_start(slot, (uint32_t)period_ms);
	if (ret == -EALREADY) {
		shell_print(shell, "periodic SYNC already active for slot %u", slot);
		return 0;
	}
	if (ret < 0) {
		shell_error(shell, "periodic SYNC start failed: %d", ret);
		return ret;
	}
	shell_print(shell, "periodic SYNC started for slot %u every %lu ms; use `uwb sync stop`",
		    slot, period_ms);
	return 0;
}

static int cmd_uwb_sync(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t slot;
	int ret;

	if (argc != 2U || parse_slot(shell, argv[1], &slot) < 0) {
		shell_error(shell, "usage: uwb sync <slot> (or sync start|stop|once)");
		return -EINVAL;
	}
	ret = receptec_uwb_sync_start(slot, UWB_DEFAULT_T_SAMPLING_MS);
	if (ret == -EALREADY) {
		shell_print(shell, "periodic SYNC already active for slot %u", slot);
		return 0;
	}
	if (ret < 0) {
		shell_error(shell, "periodic SYNC start failed: %d", ret);
		return ret;
	}
	shell_print(shell, "periodic SYNC started for slot %u every %u ms; use `uwb sync stop`",
		    slot, UWB_DEFAULT_T_SAMPLING_MS);
	return 0;
}

static int cmd_uwb_sync_stop(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = receptec_uwb_sync_stop();
	if (ret < 0) {
		shell_error(shell, "periodic SYNC stop failed: %d", ret);
		return ret;
	}
	shell_print(shell, "periodic SYNC stopped");
	return 0;
}

static int cmd_uwb_peer_list(const struct shell *shell, size_t argc,
			     char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	print_peer_slots(shell);
	return 0;
}

static int cmd_uwb_peer_register(const struct shell *shell, size_t argc,
				 char **argv)
{
	uint8_t slot;
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	char text[24];
	int ret;

	ARG_UNUSED(argc);
	ret = parse_slot(shell, argv[1], &slot);
	if (ret < 0) {
		return ret;
	}
	ret = parse_eui64(argv[2], eui64);
	if (ret < 0) {
		shell_error(shell, "EUI-64 must be 8 colon-separated hex bytes");
		return ret;
	}
	ret = receptec_uwb_peer_register(slot, eui64);
	if (ret < 0) {
		if (ret == -EBUSY) {
			shell_error(shell,
				    "UWB is busy: stop polling or SYNC before registering a peer (%d)",
				    ret);
		} else {
			shell_error(shell, "peer registration failed: %d", ret);
		}
		return ret;
	}
	format_eui64(eui64, text);
	shell_print(shell, "registered slot %u: %s", slot, text);
	return 0;
}

static int cmd_uwb_peer_unregister(const struct shell *shell, size_t argc,
				   char **argv)
{
	uint8_t slot;
	int ret;

	ARG_UNUSED(argc);
	ret = parse_slot(shell, argv[1], &slot);
	if (ret < 0) {
		return ret;
	}
	ret = receptec_uwb_peer_unregister(slot);
	if (ret < 0) {
		shell_error(shell, "peer unregistration failed: %d", ret);
		return ret;
	}
	shell_print(shell, "unregistered slot %u", slot);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_peer,
	SHELL_CMD(list, NULL, "List registered peer slots and EUIs",
		 cmd_uwb_peer_list),
	SHELL_CMD_ARG(register, NULL,
		     "Register peer: register <slot> <EUI-64 in on-air byte order>",
		     cmd_uwb_peer_register, 3, 0),
	SHELL_CMD_ARG(set, NULL,
		     "Persist peer: set <slot> <EUI-64 in on-air byte order>",
		     cmd_uwb_peer_register, 3, 0),
	SHELL_CMD_ARG(unregister, NULL, "Unregister peer: unregister <slot>",
		     cmd_uwb_peer_unregister, 2, 0),
	SHELL_CMD_ARG(clear, NULL, "Remove peer: clear <slot>",
		     cmd_uwb_peer_unregister, 2, 0),
	SHELL_SUBCMD_SET_END);

static int cmd_uwb_poll_start(const struct shell *shell, size_t argc,
			      char **argv)
{
	unsigned long period_ms = 1000UL;
	char *end;
	int ret;

	if (argc > 1U) {
		errno = 0;
		period_ms = strtoul(argv[1], &end, 10);
		if (errno != 0 || end == argv[1] || *end != '\0' ||
		    period_ms == 0UL || period_ms > 60000UL) {
			shell_error(shell, "usage: uwb poll start [period_ms 1..60000]");
			return -EINVAL;
		}
	}
	ret = receptec_uwb_poll_start((uint32_t)period_ms);
	if (ret < 0) {
		shell_error(shell, "poll start failed: %d (-%d busy, -%d no peer, -%d "
			    "period shorter than the last slot)", ret, EBUSY, ENOENT,
			    EINVAL);
		return ret;
	}
	shell_print(shell, "polling every %lu ms; `uwb poll stats`, `uwb poll stop`",
		    period_ms);
	return 0;
}

static int cmd_uwb_schedule_show(const struct shell *shell, size_t argc,
				char **argv)
{
	struct receptec_uwb_poll_status status;
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	char text[24];
	uint32_t configured_period_ms;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	receptec_uwb_poll_get(&status);
	if (receptec_uwb_schedule_period_get(&configured_period_ms) < 0) {
		return -EAGAIN;
	}
	shell_print(shell, "schedule running=%u mode=%s period=%u ms cycles=%u overruns=%u",
		    status.running, status.schedule_mode ? "per-peer" : "inactive",
		    status.running ? status.period_us / 1000U : configured_period_ms,
		    status.cycles, status.overruns);
	for (uint8_t slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		enum receptec_uwb_peer_action configured_action;
		const char *action;

		if (receptec_uwb_peer_get(slot, eui64) == 0) {
			if (receptec_uwb_schedule_get(slot, &configured_action) < 0) {
				return -EAGAIN;
			}
			action = configured_action == RECEPTEC_UWB_PEER_ACTION_DATA_REQ ?
				"data_req" : "sync";
			format_eui64(eui64, text);
			shell_print(shell, "slot %u: %s peer=%s", slot, action, text);
		} else {
			shell_print(shell, "slot %u: sync (empty; skipped)", slot);
		}
	}
	return 0;
}

static int cmd_uwb_schedule_set(const struct shell *shell, size_t argc,
				char **argv)
{
	uint8_t slot;
	enum receptec_uwb_peer_action action;
	int ret;

	if (argc != 3U || parse_slot(shell, argv[1], &slot) < 0) {
		shell_error(shell, "usage: uwb schedule set <slot> sync|data_req");
		return -EINVAL;
	}
	if (strcmp(argv[2], "sync") == 0) {
		action = RECEPTEC_UWB_PEER_ACTION_SYNC;
	} else if (strcmp(argv[2], "data_req") == 0) {
		action = RECEPTEC_UWB_PEER_ACTION_DATA_REQ;
	} else {
		shell_error(shell, "action must be sync or data_req");
		return -EINVAL;
	}
	ret = receptec_uwb_schedule_set(slot, action);
	if (ret < 0) {
		shell_error(shell, "schedule update failed: %d", ret);
		return ret;
	}
	shell_print(shell, "slot %u action set to %s (saved)", slot, argv[2]);
	return 0;
}

static int cmd_uwb_schedule_start(const struct shell *shell, size_t argc,
				  char **argv)
{
	unsigned long period_ms = 0UL;
	uint32_t configured_period_ms;
	char *end;
	int ret;

	if (argc > 1U) {
		errno = 0;
		period_ms = strtoul(argv[1], &end, 10);
		if (errno != 0 || end == argv[1] || *end != '\0' ||
		    period_ms == 0UL || period_ms > 60000UL) {
			shell_error(shell, "usage: uwb schedule start [period_ms 1..60000]");
			return -EINVAL;
		}
		ret = receptec_uwb_schedule_period_set((uint32_t)period_ms);
		if (ret < 0) {
			shell_error(shell, "could not save schedule period: %d", ret);
			return ret;
		}
	}
	ret = receptec_uwb_schedule_period_get(&configured_period_ms);
	if (ret < 0) {
		shell_error(shell, "could not read schedule period: %d", ret);
		return ret;
	}
	ret = receptec_uwb_schedule_start();
	if (ret < 0) {
		shell_error(shell, "schedule start failed: %d (-%d busy, -%d no peer, -%d slot budget)",
			    ret, EBUSY, ENOENT, EINVAL);
		return ret;
	}
	shell_print(shell, "per-peer schedule started every %u ms (saved); SYNC is the default; use uwb schedule show/stats/stop",
		    configured_period_ms);
	return 0;
}

static int cmd_uwb_schedule_stop(const struct shell *shell, size_t argc,
				 char **argv)
{
	struct receptec_uwb_poll_status status;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	receptec_uwb_poll_get(&status);
	if (!status.running || !status.schedule_mode) {
		shell_error(shell, "per-peer schedule is not running");
		return -EALREADY;
	}
	ret = receptec_uwb_poll_stop();
	if (ret < 0) {
		shell_error(shell, "schedule did not stop: %d", ret);
		return ret;
	}
	shell_print(shell, "UWB schedule stopped");
	return 0;
}

static int cmd_uwb_poll_stop(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = receptec_uwb_poll_stop();
	if (ret < 0) {
		shell_error(shell, "poll thread did not stop: %d", ret);
		return ret;
	}
	shell_print(shell, "polling stopped");
	return 0;
}

static int cmd_uwb_poll_stats(const struct shell *shell, size_t argc, char **argv)
{
	struct receptec_uwb_poll_status status;
	int64_t elapsed_ms;
	uint8_t slot;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	receptec_uwb_poll_get(&status);
	elapsed_ms = status.started_ms == 0 ? 0 : status.stopped_ms - status.started_ms;
	if (status.schedule_mode) {
		shell_print(shell, "poll running=%u mode=PER_PEER period=%u ms elapsed=%u ms "
			    "cycles=%u overruns=%u", status.running,
			    status.period_us / 1000U, (uint32_t)elapsed_ms,
			    status.cycles, status.overruns);
	} else if (status.sync_mode) {
		shell_print(shell, "poll running=%u mode=SYNC slot=%u period=%u ms elapsed=%u ms "
			    "cycles=%u overruns=%u", status.running, status.peer_slot,
			    status.period_us / 1000U, (uint32_t)elapsed_ms,
			    status.cycles, status.overruns);
	} else {
		shell_print(shell, "poll running=%u mode=DATA_REQ period=%u ms elapsed=%u ms "
			    "cycles=%u overruns=%u", status.running, status.period_us / 1000U,
			    (uint32_t)elapsed_ms, status.cycles, status.overruns);
	}
	for (slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		const struct receptec_uwb_poll_peer_stats *p = &status.peer[slot];
		uint32_t permille = p->transactions == 0U ? 0U :
			(uint32_t)(((uint64_t)p->completed * 1000ULL) / p->transactions);

		if (p->transactions == 0U) {
			continue;
		}
		shell_print(shell,
			    "slot %u action=%s transactions=%u ok=%u (%u.%u%%) by_attempt=%u/%u/%u "
			    "attempts=%u timeouts=%u rx_err=%u bad=%u late=%u exhausted=%u "
			    "bytes=%u sample_gaps=%u",
			    slot, status.action[slot] == RECEPTEC_UWB_PEER_ACTION_DATA_REQ ?
			    "DATA_REQ" : "SYNC", p->transactions, p->completed, permille / 10U,
			    permille % 10U, p->completed_on_attempt[0],
			    p->completed_on_attempt[1], p->completed_on_attempt[2],
			    p->attempts, p->timeouts, p->rx_errors, p->bad_frames,
			    p->late_schedules, p->exhausted, (uint32_t)p->payload_bytes,
			    p->sample_gaps);
	}
	return 0;
}

static int cmd_uwb_poll_stream(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	if (strcmp(argv[1], "on") == 0) {
		receptec_uwb_poll_stream(true);
	} else if (strcmp(argv[1], "off") == 0) {
		receptec_uwb_poll_stream(false);
	} else {
		shell_error(shell, "usage: uwb poll stream on|off");
		return -EINVAL;
	}
	shell_print(shell, "stream %s", argv[1]);
	return 0;
}

static int cmd_uwb_poll_last(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t payload[RECEPTEC_UWB_MAX_PAYLOAD_SIZE];
	size_t length;
	uint32_t count;
	uint32_t age_ms;
	uint8_t slot;
	int ret;

	ARG_UNUSED(argc);
	ret = parse_slot(shell, argv[1], &slot);
	if (ret < 0) {
		return ret;
	}
	ret = receptec_uwb_poll_last(slot, payload, sizeof(payload), &length,
				     &count, &age_ms);
	if (ret < 0) {
		shell_error(shell, "no DATA payload for slot %u yet (%d)", slot, ret);
		return ret;
	}
	shell_print(shell, "slot %u: n=%u age=%u ms len=%u", slot, count, age_ms,
		    (uint32_t)length);
	shell_hexdump(shell, payload, length);
	return 0;
}

static int cmd_uwb_lna(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	if (strcmp(argv[1], "on") != 0 && strcmp(argv[1], "off") != 0) {
		shell_error(shell, "usage: uwb lna on|off");
		return -EINVAL;
	}
	ret = receptec_uwb_radio_set_lna(strcmp(argv[1], "on") == 0);
	if (ret < 0) {
		shell_error(shell, "lna failed: %d", ret);
		return ret;
	}
	shell_print(shell, "external LNA %s", argv[1]);
	return 0;
}

/*
 * uwb range: minimal DS-TWR-as-initiator proof of concept (contract UWB
 * v1.2 S5-S9). See the scope note on receptec_uwb_range_start() in
 * uwb_radio.h: no simultaneous multi-pair scheduling, no retry, in-RAM
 * round_id. Shares the radio with poll/sync/schedule, so it refuses to
 * start while any of those run. pair_index defaults to this board's own
 * CONFIG_RECEPTEC_FIXED_ANCHOR_ID (0=A-T on the master build, 1=B-T on the
 * fixed-anchor build); pass it explicitly to drive A-B (pair_index=2) from
 * the master against a base peer.
 */
static int cmd_uwb_range_start(const struct shell *shell, size_t argc,
			       char **argv)
{
	uint8_t slot;
	unsigned long period_ms = 1000UL;
	unsigned long pair_index = CONFIG_RECEPTEC_FIXED_ANCHOR_ID;
	char *end;
	int ret;

	if (parse_slot(shell, argv[1], &slot) < 0) {
		return -EINVAL;
	}
	if (argc > 2U) {
		errno = 0;
		period_ms = strtoul(argv[2], &end, 10);
		if (errno != 0 || end == argv[2] || *end != '\0' ||
		    period_ms == 0UL || period_ms > 60000UL) {
			shell_error(shell, "usage: uwb range start <slot> [period_ms 1..60000] [pair_index 0|1|2]");
			return -EINVAL;
		}
	}
	if (argc > 3U) {
		errno = 0;
		pair_index = strtoul(argv[3], &end, 10);
		if (errno != 0 || end == argv[3] || *end != '\0' || pair_index > 2UL) {
			shell_error(shell, "usage: uwb range start <slot> [period_ms 1..60000] [pair_index 0|1|2]");
			return -EINVAL;
		}
	}
	ret = receptec_uwb_range_start(slot, (uint32_t)period_ms, (uint8_t)pair_index);
	if (ret < 0) {
		shell_error(shell, "range start failed: %d (-%d busy, -%d no peer)",
			    ret, EBUSY, ENOENT);
		return ret;
	}
	shell_print(shell, "pair_index=%lu range round every %lu ms against slot %u; `uwb range status`, `uwb range stop`",
		    pair_index, period_ms, slot);
	return 0;
}

static int cmd_uwb_range_stop(const struct shell *shell, size_t argc,
			      char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = receptec_uwb_range_stop();
	if (ret < 0) {
		shell_error(shell, "range is not running: %d", ret);
		return ret;
	}
	shell_print(shell, "range round stopping");
	return 0;
}

static int cmd_uwb_range_respond(const struct shell *shell, size_t argc,
				 char **argv)
{
	uint8_t master_slot;
	unsigned long pair_index, timeout_ms = 5000UL;
	enum receptec_uwb_range_status out_status;
	uint32_t out_distance_mm;
	char *end;
	int ret;

	if (parse_slot(shell, argv[1], &master_slot) < 0) {
		return -EINVAL;
	}
	errno = 0;
	pair_index = strtoul(argv[2], &end, 10);
	if (errno != 0 || end == argv[2] || *end != '\0' || pair_index > 2UL) {
		shell_error(shell, "usage: uwb range respond <master_slot> <pair_index 0|1|2> [timeout_ms]");
		return -EINVAL;
	}
	if (argc > 3U) {
		errno = 0;
		timeout_ms = strtoul(argv[3], &end, 10);
		if (errno != 0 || end == argv[3] || *end != '\0' ||
		    timeout_ms == 0UL || timeout_ms > 60000UL) {
			shell_error(shell, "usage: uwb range respond <master_slot> <pair_index 0|1|2> [timeout_ms]");
			return -EINVAL;
		}
	}
	shell_print(shell, "listening up to %lu ms for pair_index=%lu against master slot %u",
		    timeout_ms, pair_index, master_slot);
	ret = receptec_uwb_range_respond(master_slot, (uint8_t)pair_index,
					 (uint32_t)timeout_ms, &out_status,
					 &out_distance_mm);
	if (ret < 0) {
		shell_error(shell, "respond failed: %d (-%d busy, -%d no peer)",
			    ret, EBUSY, ENOENT);
		return ret;
	}
	shell_print(shell, "respond status=%u distance_mm=%u", out_status,
		    out_distance_mm);
	return 0;
}

static int cmd_uwb_range_listen(const struct shell *shell, size_t argc,
				char **argv)
{
	uint8_t peer_slot;
	unsigned long pair_index, timeout_ms = 5000UL;
	enum receptec_uwb_range_status out_status;
	uint32_t out_distance_mm;
	uint64_t out_round_id;
	char *end;
	int ret;

	if (parse_slot(shell, argv[1], &peer_slot) < 0) {
		return -EINVAL;
	}
	errno = 0;
	pair_index = strtoul(argv[2], &end, 10);
	if (errno != 0 || end == argv[2] || *end != '\0' || pair_index > 2UL) {
		shell_error(shell, "usage: uwb range listen <peer_slot> <pair_index 0|1|2> [timeout_ms]");
		return -EINVAL;
	}
	if (argc > 3U) {
		errno = 0;
		timeout_ms = strtoul(argv[3], &end, 10);
		if (errno != 0 || end == argv[3] || *end != '\0' ||
		    timeout_ms == 0UL || timeout_ms > 60000UL) {
			shell_error(shell, "usage: uwb range listen <peer_slot> <pair_index 0|1|2> [timeout_ms]");
			return -EINVAL;
		}
	}
	shell_print(shell, "listening up to %lu ms for a RANGE_RESULT of pair_index=%lu from slot %u",
		    timeout_ms, pair_index, peer_slot);
	ret = receptec_uwb_range_listen(peer_slot, (uint8_t)pair_index,
					(uint32_t)timeout_ms, &out_status,
					&out_distance_mm, &out_round_id);
	if (ret < 0) {
		shell_error(shell, "listen failed: %d (-%d busy, -%d no peer)",
			    ret, EBUSY, ENOENT);
		return ret;
	}
	shell_print(shell, "listen round=%llu status=%u distance_mm=%u",
		    out_round_id, out_status, out_distance_mm);
	return 0;
}

static int cmd_uwb_range_status(const struct shell *shell, size_t argc,
				char **argv)
{
	struct receptec_uwb_range_round_status status;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	receptec_uwb_range_get(&status);
	shell_print(shell, "range running=%u slot=%u period=%u ms rounds=%u successes=%u "
		    "plan_timeouts=%u round_arm_timeouts=%u range_failures=%u last_arm_send_status=%d",
		    status.running, status.peer_slot, status.period_us / 1000U,
		    status.rounds, status.successes, status.plan_timeouts,
		    status.round_arm_timeouts, status.range_failures,
		    status.last_arm_send_status);
	if (status.last_valid) {
		shell_print(shell, "last round=%llu status=%u distance_mm=%u",
			    status.round_id, status.last_status,
			    status.last_distance_mm);
	} else {
		shell_print(shell, "last round=%llu: no valid result yet",
			    status.round_id);
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_range,
	SHELL_CMD_ARG(start, NULL,
		      "Start periodic DS-TWR round: start <slot> [period_ms] [pair_index 0|1|2]",
		      cmd_uwb_range_start, 2, 2),
	SHELL_CMD(stop, NULL, "Stop the range round", cmd_uwb_range_stop),
	SHELL_CMD(status, NULL, "Show round counters and last result",
		  cmd_uwb_range_status),
	SHELL_CMD_ARG(respond, NULL,
		      "One-shot DS-TWR responder: respond <master_slot> <pair_index 0|1|2> [timeout_ms]",
		      cmd_uwb_range_respond, 3, 1),
	SHELL_CMD_ARG(listen, NULL,
		      "Passively wait for another node's RANGE_RESULT: listen <peer_slot> <pair_index 0|1|2> [timeout_ms]",
		      cmd_uwb_range_listen, 3, 1),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_schedule,
	SHELL_CMD(show, NULL, "Show per-peer actions (default: SYNC)",
		  cmd_uwb_schedule_show),
	SHELL_CMD_ARG(set, NULL, "Set action: set <slot> sync|data_req",
		      cmd_uwb_schedule_set, 3, 0),
	SHELL_CMD_ARG(start, NULL, "Start per-peer schedule: start [period_ms]",
		      cmd_uwb_schedule_start, 1, 1),
	SHELL_CMD(stop, NULL, "Stop per-peer schedule", cmd_uwb_schedule_stop),
	SHELL_CMD(stats, NULL, "Show per-peer link counters", cmd_uwb_poll_stats),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_sync,
	SHELL_CMD_ARG(start, NULL, "Start periodic SYNC: start <slot> [period_ms]",
		      cmd_uwb_sync_start, 2, 1),
	SHELL_CMD(stop, NULL, "Stop periodic SYNC", cmd_uwb_sync_stop),
	SHELL_CMD_ARG(once, NULL, "Send one SYNC and validate ACK: once <slot>",
		      cmd_uwb_sync_once, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_poll,
	SHELL_CMD_ARG(start, NULL,
		      "Poll all registered peers with DATA_REQ: start [period_ms]",
		      cmd_uwb_poll_start, 1, 1),
	SHELL_CMD(stop, NULL, "Stop polling", cmd_uwb_poll_stop),
	SHELL_CMD(stats, NULL, "Show polling counters", cmd_uwb_poll_stats),
	SHELL_CMD_ARG(stream, NULL, "Print each received DATA payload: stream on|off",
		      cmd_uwb_poll_stream, 2, 0),
	SHELL_CMD_ARG(last, NULL, "Hexdump the latest DATA payload: last <slot>",
		      cmd_uwb_poll_last, 2, 0),
	SHELL_SUBCMD_SET_END);

/* uwb test <name> [1|2 for retry] <slot>: arms one runtime test; the result is one UWB_TEST line. */
static int cmd_uwb_test_run(const struct shell *shell, size_t argc, char **argv)
{
	const char *name = argv[0];
	bool takes_count = strcmp(name, "retry") == 0;
	unsigned long count = 0UL;
	uint8_t slot;
	char *end;
	int ret;

	if (takes_count) {
		if (argc != 3) {
			shell_error(shell, "usage: uwb test retry <1|2> <slot>");
			return -EINVAL;
		}
		errno = 0;
		count = strtoul(argv[1], &end, 10);
		if (errno != 0 || end == argv[1] || *end != '\0' ||
		    (count != 1UL && count != 2UL)) {
			shell_error(shell, "retry takes 1 or 2 ignored responses");
			return -EINVAL;
		}
	}
	ret = parse_slot(shell, argv[argc - 1], &slot);
	if (ret < 0) {
		return ret;
	}
	ret = receptec_uwb_test_start(name, (uint8_t)count, slot);
	switch (ret) {
	case 0:
		shell_print(shell, "test %s armed on slot %u: warm-up, test, verify; "
			    "the result is printed as one result line", name, slot);
		return 0;
	case -EBUSY:
		shell_error(shell, "a poll or a test is running: uwb test abort, or uwb poll stop");
		break;
	case -ENOENT:
		shell_error(shell, "slot %u has no registered peer", slot);
		break;
	case -ENOTSUP:
		shell_error(shell, "test %s is not implemented (see docs/uwb-runtime-tests.md)",
			    name);
		break;
	default:
		shell_error(shell, "test %s could not start: %d", name, ret);
		break;
	}
	return ret;
}

static int cmd_uwb_test_abort(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ret = receptec_uwb_test_abort();
	if (ret < 0) {
		shell_error(shell, "no test is running");
		return ret;
	}
	shell_print(shell, "abort requested; the test ends at its next cycle boundary");
	return 0;
}

static int cmd_uwb_test_list(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(shell, "implemented: data_payload, retry <1|2>, duplicate, sequence_wrap,");
	shell_print(shell, "             wrong_source, bad_type, bad_reserved, bad_length, bad_dt,");
	shell_print(shell, "             discovery");
	shell_print(shell, "not implemented: rx_boundary (the contract does not define its PASS/FAIL)");
	shell_print(shell, "usage: uwb test <name> <slot>   (needs a registered peer, no poll running)");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_test,
	SHELL_CMD(list, NULL, "List the runtime tests", cmd_uwb_test_list),
	SHELL_CMD(abort, NULL, "End the running test at its next cycle boundary",
		  cmd_uwb_test_abort),
	SHELL_CMD_ARG(data_payload, NULL, "DATA payload v1 layout and duplicate freeze: <slot>",
		      cmd_uwb_test_run, 2, 0),
	SHELL_CMD_ARG(retry, NULL, "Ignore 1 or 2 valid responses, then accept: <1|2> <slot>",
		      cmd_uwb_test_run, 3, 0),
	SHELL_CMD_ARG(duplicate, NULL, "Repeat a request, require the same response: <slot>",
		      cmd_uwb_test_run, 2, 0),
	SHELL_CMD_ARG(sequence_wrap, NULL, "Sequences 254, 255, 0: <slot>", cmd_uwb_test_run, 2, 0),
	SHELL_CMD_ARG(wrong_source, NULL, "Unknown source, expect no response: <slot>",
		      cmd_uwb_test_run, 2, 0),
	SHELL_CMD_ARG(bad_type, NULL, "Reserved packet type, expect no response: <slot>",
		      cmd_uwb_test_run, 2, 0),
	SHELL_CMD_ARG(bad_reserved, NULL, "Reserved type bit, expect no response: <slot>",
		      cmd_uwb_test_run, 2, 0),
	SHELL_CMD_ARG(bad_length, NULL, "Truncated DT_sampling, expect no response: <slot>",
		      cmd_uwb_test_run, 2, 0),
	SHELL_CMD_ARG(bad_dt, NULL, "DT_sampling outside the slot, expect no response: <slot>",
		      cmd_uwb_test_run, 2, 0),
	SHELL_CMD_ARG(rx_boundary, NULL, "Not implemented yet: <slot>", cmd_uwb_test_run, 2, 0),
	SHELL_CMD_ARG(discovery, NULL, "Silence until the COLETEC enters discovery, then reenter: <slot>",
		      cmd_uwb_test_run, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_uwb,
	SHELL_CMD(status, NULL, "Show RECEPTEC radio state", cmd_uwb_status),
	SHELL_CMD(identity, &sub_identity, "Show or configure local UWB identity", NULL),
	SHELL_CMD_ARG(sync, &sub_sync, "Periodic SYNC; `sync <slot>` uses 1000 ms",
		     cmd_uwb_sync, 2, 1),
	SHELL_CMD(peer, &sub_peer, "Manage registered COLETEC peers", NULL),
	SHELL_CMD(schedule, &sub_schedule, "Configure per-peer UWB schedule", NULL),
	SHELL_CMD(range, &sub_range, "A-T DS-TWR round (proof of concept)", NULL),
	SHELL_CMD_ARG(stop, NULL, "Send STOP (requires timeout config): stop <slot>",
		      cmd_uwb_stop, 2, 0),
	SHELL_CMD(poll, &sub_poll, "Periodic DATA_REQ polling", NULL),
	SHELL_CMD(test, &sub_test, "Runtime UWB tests against the COLETEC", NULL),
	SHELL_CMD_ARG(lna, NULL, "External receive LNA (bench A/B): lna on|off",
		      cmd_uwb_lna, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(uwb, &sub_uwb, "RECEPTEC UWB controls", NULL);
