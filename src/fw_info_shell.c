#include <zephyr/shell/shell.h>

#include "fw_info_build.h"

static int cmd_fw_info(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Firmware type: %s", FW_INFO_TYPE);
	shell_print(shell, "Variant: %s", FW_INFO_VARIANT);
	shell_print(shell, "Source commit: %s", FW_INFO_COMMIT);
	shell_print(shell, "Contracts commit: %s", FW_INFO_CONTRACTS_COMMIT);
	shell_print(shell, "Source state: %s", FW_INFO_DIRTY ? "dirty" : "clean");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(fw_subcommands,
	SHELL_CMD(info, NULL, "Show firmware image identity", cmd_fw_info),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(fw, &fw_subcommands, "Firmware image information", NULL);
