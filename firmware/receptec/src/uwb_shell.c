/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "uwb_radio.h"

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

static int cmd_uwb_status(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(shell, "QM33120W radio initialized at boot; RF is idle between tests");
	print_peer_slots(shell);
	return 0;
}

static int cmd_uwb_sync(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t slot;
	int ret;

	ARG_UNUSED(argc);
	ret = parse_slot(shell, argv[1], &slot);
	if (ret < 0) {
		shell_error(shell, "usage: uwb sync <slot>");
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
		shell_error(shell, "peer registration failed: %d", ret);
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
	SHELL_CMD_ARG(unregister, NULL, "Unregister peer: unregister <slot>",
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
	shell_print(shell, "poll running=%u period=%u ms elapsed=%u ms cycles=%u overruns=%u",
		    status.running, status.period_us / 1000U, (uint32_t)elapsed_ms,
		    status.cycles, status.overruns);
	for (slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		const struct receptec_uwb_poll_peer_stats *p = &status.peer[slot];
		uint32_t permille = p->transactions == 0U ? 0U :
			(uint32_t)(((uint64_t)p->completed * 1000ULL) / p->transactions);

		if (p->transactions == 0U) {
			continue;
		}
		shell_print(shell,
			    "slot %u: transactions=%u ok=%u (%u.%u%%) by_attempt=%u/%u/%u "
			    "attempts=%u timeouts=%u rx_err=%u bad=%u late=%u exhausted=%u "
			    "bytes=%u sample_gaps=%u",
			    slot, p->transactions, p->completed, permille / 10U,
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

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_uwb,
	SHELL_CMD(status, NULL, "Show RECEPTEC radio state", cmd_uwb_status),
	SHELL_CMD_ARG(sync, NULL, "Send one SYNC and validate ACK: sync <slot>",
		     cmd_uwb_sync, 2, 0),
	SHELL_CMD(peer, &sub_peer, "Manage registered COLETEC peers", NULL),
	SHELL_CMD(poll, &sub_poll, "Periodic DATA_REQ polling", NULL),
	SHELL_CMD_ARG(lna, NULL, "External receive LNA (bench A/B): lna on|off",
		      cmd_uwb_lna, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(uwb, &sub_uwb, "RECEPTEC UWB controls", NULL);
