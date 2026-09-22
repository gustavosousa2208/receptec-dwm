/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include <receptec/dw3110_radio.h>

static const char *operation_name(enum dw3110_radio_operation operation)
{
	switch (operation) {
	case DW3110_RADIO_OPERATION_NONE:
		return "none";
	case DW3110_RADIO_OPERATION_DATA_REQ:
		return "data_req";
	case DW3110_RADIO_OPERATION_TEST_SYNC:
		return "test-sync";
	case DW3110_RADIO_OPERATION_TEST_STOP:
		return "test-stop";
	case DW3110_RADIO_OPERATION_TEST_WRONG_EUI:
		return "test-wrong-eui";
	case DW3110_RADIO_OPERATION_TEST_OUT_OF_SLOT:
		return "test-out-of-slot";
	case DW3110_RADIO_OPERATION_TEST_SILENCE:
		return "test-silence";
	case DW3110_RADIO_OPERATION_TEST_REJECTION:
		return "test-rejection";
	case DW3110_RADIO_OPERATION_TEST_SEQUENCE_WRAP:
		return "test-sequence-wrap";
	case DW3110_RADIO_OPERATION_TEST_WINDOW:
		return "test-window";
	default:
		return "unknown";
	}
}

static int parse_u32(const struct shell *shell, const char *text, uint32_t minimum,
		     uint32_t maximum, uint32_t *value)
{
	char *end;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 0);
	if (errno != 0 || end == text || *end != '\0' || parsed < minimum || parsed > maximum) {
		shell_error(shell, "expected number from %u to %u", minimum, maximum);
		return -EINVAL;
	}
	*value = (uint32_t)parsed;
	return 0;
}

static int report_submission(const struct shell *shell, int status, const char *description)
{
	if (status == 0) {
		shell_print(shell, "%s queued; result will print asynchronously", description);
		return 0;
	}

	switch (status) {
	case -EBUSY:
		shell_error(shell, "another one-shot operation or test is already active");
		break;
	case -ENOMSG:
		shell_error(shell, "UWB command queue is full; retry");
		break;
	case -ENOTSUP:
		shell_error(shell, "command is only available on the RECEPTEC");
		break;
	case -EACCES:
		shell_error(shell, "start the slotted schedule before this test");
		break;
	case -EAGAIN:
		shell_error(shell, "UWB protocol thread is not ready");
		break;
	default:
		shell_error(shell, "%s failed: %d", description, status);
		break;
	}
	return status;
}

static int cmd_status(const struct shell *shell, size_t argc, char **argv)
{
	struct dw3110_radio_control_status status;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = dw3110_radio_get_control_status(&status);
	if (ret < 0) {
		return report_submission(shell, ret, "status");
	}

	shell_print(shell, "role: %s", status.receptec_role ? "RECEPTEC" : "COLETEC");
	shell_print(shell, "protocol: %s", status.protocol_running ? "running" : "idle");
	shell_print(shell, "slotted schedule: %s", status.slotted_running ? "running" : "stopped");
	if (status.periodic_sync_running) {
		shell_print(shell, "periodic SYNC: running on peer slot %u",
			    status.periodic_sync_peer_slot);
	} else {
		shell_print(shell, "periodic SYNC: stopped");
	}
	shell_print(shell, "cycle: %u", status.cycle);
	shell_print(shell, "one-shot/test: %s%s", operation_name(status.operation),
		    status.operation_pending ? " (queued/running)" : "");
	return 0;
}

static int cmd_peers(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (uint8_t slot = 0U; slot < DW3110_RADIO_PEER_COUNT; ++slot) {
		struct dw3110_radio_peer_info info;
		int status = dw3110_radio_get_peer_info(slot, &info);

		if (status < 0) {
			return report_submission(shell, status, "peer query");
		}
		shell_print(shell, "slot %u: %-20s %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X %s",
			    slot, info.name, info.eui[0], info.eui[1], info.eui[2], info.eui[3],
			    info.eui[4], info.eui[5], info.eui[6], info.eui[7],
			    info.expected_present ? "present" : "missing-test");
	}
	return 0;
}

static int cmd_slotted_start(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return report_submission(shell, dw3110_radio_set_slotted(true), "slotted schedule start");
}

static int cmd_slotted_stop(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return report_submission(shell, dw3110_radio_set_slotted(false), "slotted schedule stop");
}

static int cmd_sync_start(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t slot;
	int status;

	ARG_UNUSED(argc);
	status = parse_u32(shell, argv[1], 0U, DW3110_RADIO_PEER_COUNT - 1U, &slot);
	if (status < 0) {
		return status;
	}
	return report_submission(shell, dw3110_radio_set_periodic_sync(true, (uint8_t)slot),
				 "periodic SYNC start");
}

static int cmd_sync_stop(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return report_submission(shell, dw3110_radio_set_periodic_sync(false, 0U),
				 "periodic SYNC stop");
}

static int cmd_data_req(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t slot;
	int status;

	ARG_UNUSED(argc);
	status = parse_u32(shell, argv[1], 0U, DW3110_RADIO_PEER_COUNT - 1U, &slot);
	if (status < 0) {
		return status;
	}
	return report_submission(shell, dw3110_radio_request_data((uint8_t)slot),
				 "one-shot DATA_REQ");
}

static int cmd_coletec_start(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return report_submission(shell, dw3110_radio_enable_coletec(),
				 "COLETEC SYNC receiver start");
}

static int run_peer_test(const struct shell *shell, const char *slot_text,
			 enum dw3110_radio_test test, const char *description)
{
	uint32_t slot;
	int status;

	status = parse_u32(shell, slot_text, 0U, DW3110_RADIO_PEER_COUNT - 1U, &slot);
	if (status < 0) {
		return status;
	}
	status = dw3110_radio_run_test(test, (uint8_t)slot, 0U);
	if (status == 0 && test == DW3110_RADIO_TEST_STOP) {
		shell_warn(shell, "STOP leaves that COLETEC stopped until it is reset");
	}
	return report_submission(shell, status, description);
}

static int cmd_test_sync(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return run_peer_test(shell, argv[1], DW3110_RADIO_TEST_SYNC, "one-shot SYNC test");
}

static int cmd_test_stop(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return run_peer_test(shell, argv[1], DW3110_RADIO_TEST_STOP, "STOP/STOP_ACK test");
}

static int cmd_test_wrong_eui(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return run_peer_test(shell, argv[1], DW3110_RADIO_TEST_WRONG_EUI, "wrong-EUI test");
}

static int cmd_test_reject(const struct shell *shell, size_t argc, char **argv)
{
	static const struct {
		const char *name;
		enum dw3110_radio_rejection_case test;
	} cases[] = {
		{ "wrong_source", DW3110_RADIO_REJECT_WRONG_SOURCE },
		{ "wrong_type", DW3110_RADIO_REJECT_WRONG_TYPE },
		{ "reserved", DW3110_RADIO_REJECT_RESERVED_BITS },
		{ "short", DW3110_RADIO_REJECT_SHORT_PAYLOAD },
		{ "dt_range", DW3110_RADIO_REJECT_DT_SAMPLING },
	};
	uint32_t slot;
	int status;

	ARG_UNUSED(argc);
	status = parse_u32(shell, argv[2], 0U, DW3110_RADIO_PEER_COUNT - 1U,
			   &slot);
	if (status < 0) {
		return status;
	}
	for (size_t i = 0U; i < ARRAY_SIZE(cases); ++i) {
		if (strcmp(argv[1], cases[i].name) == 0) {
			return report_submission(
				shell,
				dw3110_radio_run_test(DW3110_RADIO_TEST_REJECTION,
						      (uint8_t)slot, cases[i].test),
				"P-02 rejection test");
		}
	}
	shell_error(shell,
		    "case must be wrong_source, wrong_type, reserved, short, or dt_range");
	return -EINVAL;
}

static int cmd_test_out_of_slot(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return run_peer_test(shell, argv[1], DW3110_RADIO_TEST_OUT_OF_SLOT, "out-of-slot test");
}

static int cmd_test_sequence_wrap(const struct shell *shell, size_t argc,
				  char **argv)
{
	ARG_UNUSED(argc);
	return run_peer_test(shell, argv[1], DW3110_RADIO_TEST_SEQUENCE_WRAP,
			     "P-04 sequence-wrap test");
}

static int cmd_test_window(const struct shell *shell, size_t argc, char **argv)
{
	static const struct {
		const char *name;
		enum dw3110_radio_window_case test;
	} cases[] = {
		{ "before", DW3110_RADIO_WINDOW_BEFORE },
		{ "inside", DW3110_RADIO_WINDOW_INSIDE },
		{ "end", DW3110_RADIO_WINDOW_END },
		{ "after", DW3110_RADIO_WINDOW_AFTER },
	};
	uint32_t slot;
	int status;

	ARG_UNUSED(argc);
	status = parse_u32(shell, argv[2], 0U, DW3110_RADIO_PEER_COUNT - 1U,
			   &slot);
	if (status < 0) {
		return status;
	}
	for (size_t i = 0U; i < ARRAY_SIZE(cases); ++i) {
		if (strcmp(argv[1], cases[i].name) == 0) {
			return report_submission(
				shell,
				dw3110_radio_run_test(DW3110_RADIO_TEST_WINDOW,
						      (uint8_t)slot, cases[i].test),
				"P-05 receive-window test");
		}
	}
	shell_error(shell, "case must be before, inside, end, or after");
	return -EINVAL;
}

static int cmd_test_silence(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t cycles;
	int status;

	ARG_UNUSED(argc);
	status = parse_u32(shell, argv[1], 1U, 1000U, &cycles);
	if (status < 0) {
		return status;
	}
	return report_submission(shell,
				 dw3110_radio_run_test(DW3110_RADIO_TEST_SILENCE, 0U, cycles),
				 "silence/discovery test");
}

static int cmd_test_abort(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return report_submission(shell, dw3110_radio_abort_test(), "test abort");
}

static int cmd_test_list(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Runtime RECEPTEC-driven tests:");
	shell_print(shell, "  uwb test sync <slot>");
	shell_print(shell, "  uwb test stop <slot>       (peer reset required)");
	shell_print(shell, "  uwb test wrong_eui <slot>");
	shell_print(shell, "  uwb test reject <case> <slot>");
	shell_print(shell, "  uwb test out_of_slot <slot> (slotted must run)");
	shell_print(shell, "  uwb test sequence_wrap <slot>");
	shell_print(shell, "  uwb test window <before|inside|end|after> <slot>");
	shell_print(shell, "  uwb test silence <cycles>");
	shell_print(shell, "  uwb test abort");
	shell_print(shell, "Normal 4-peer coverage: uwb slotted start");
	shell_print(shell,
		    "Retry/bad-sequence injection still requires a matching COLETEC test image.");
	shell_print(shell, "Collision testing still requires an independent transmitter.");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_slotted,
			       SHELL_CMD(start, NULL, "Start the normal four-peer slotted schedule",
					 cmd_slotted_start),
			       SHELL_CMD(stop, NULL, "Stop the normal four-peer slotted schedule",
					 cmd_slotted_stop),
			       SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sync,
			       SHELL_CMD_ARG(start, NULL, "Start periodic SYNC: start <peer-slot>",
					     cmd_sync_start, 2, 0),
			       SHELL_CMD(stop, NULL, "Stop periodic SYNC", cmd_sync_stop),
			       SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_coletec,
	SHELL_CMD(start, NULL, "Enable COLETEC RX and wait for SYNC", cmd_coletec_start),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_test, SHELL_CMD(list, NULL, "List runtime and external test cases", cmd_test_list),
	SHELL_CMD_ARG(sync, NULL, "Run one SYNC/ACK: sync <peer-slot>", cmd_test_sync, 2, 0),
	SHELL_CMD_ARG(stop, NULL, "Run STOP/STOP_ACK (peer reset required): stop <peer-slot>",
		      cmd_test_stop, 2, 0),
	SHELL_CMD_ARG(wrong_eui, NULL, "Send one wrong-destination request: wrong_eui <peer-slot>",
		      cmd_test_wrong_eui, 2, 0),
	SHELL_CMD_ARG(reject, NULL,
		      "P-02 rejection: reject <wrong_source|wrong_type|reserved|short|dt_range> <slot>",
		      cmd_test_reject, 3, 0),
	SHELL_CMD_ARG(out_of_slot, NULL, "Probe after all assigned slots: out_of_slot <peer-slot>",
		      cmd_test_out_of_slot, 2, 0),
	SHELL_CMD_ARG(sequence_wrap, NULL,
		      "P-04 explicit 254,255,0,1 sequence test: sequence_wrap <peer-slot>",
		      cmd_test_sequence_wrap, 2, 0),
	SHELL_CMD_ARG(window, NULL,
		      "P-05 receive-window boundary: window <before|inside|end|after> <slot>",
		      cmd_test_window, 3, 0),
	SHELL_CMD_ARG(silence, NULL, "Suppress all requests: silence <one-second-cycles>",
		      cmd_test_silence, 2, 0),
	SHELL_CMD(abort, NULL, "Abort a queued test or active silence", cmd_test_abort),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_uwb, SHELL_CMD(status, NULL, "Show UWB runtime control state", cmd_status),
	SHELL_CMD(peers, NULL, "List fixed peer slots and EUI-64 values", cmd_peers),
	SHELL_CMD(slotted, &sub_slotted, "Control the normal slotted schedule", NULL),
	SHELL_CMD(sync, &sub_sync, "Control periodic SYNC", NULL),
	SHELL_CMD(coletec, &sub_coletec, "Control the COLETEC receiver", NULL),
	SHELL_CMD_ARG(data_req, NULL, "Queue one DATA_REQ/DATA transaction: data_req <peer-slot>",
		      cmd_data_req, 2, 0),
	SHELL_CMD(test, &sub_test, "Run RECEPTEC-driven protocol tests", NULL),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(uwb, &sub_uwb, "RECEPTEC UWB control and tests", NULL);
