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

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_uwb,
	SHELL_CMD(status, NULL, "Show RECEPTEC radio state", cmd_uwb_status),
	SHELL_CMD_ARG(sync, NULL, "Send one SYNC and validate ACK: sync <slot>",
		     cmd_uwb_sync, 2, 0),
	SHELL_CMD(peer, &sub_peer, "Manage registered COLETEC peers", NULL),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(uwb, &sub_uwb, "RECEPTEC UWB controls", NULL);
