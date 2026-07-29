#!/bin/sh

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH='' cd -- "$script_dir/.." && pwd)
build_dir="$app_dir/build"
firmware_hex="$build_dir/zephyr/zephyr.hex"
venv_activate="$app_dir/../.venv/bin/activate"
jlink_commander=${JLINK_EXE:-JLinkExe}

jlink_1=760216755
jlink_2=760216737

if [ ! -f "$venv_activate" ]; then
	echo "Workspace virtual environment not found: $venv_activate" >&2
	exit 1
fi

# shellcheck disable=SC1090
. "$venv_activate"

if ! command -v "$jlink_commander" >/dev/null 2>&1; then
	echo "J-Link commander not found: $jlink_commander" >&2
	echo "Install SEGGER J-Link or set JLINK_EXE to its executable." >&2
	exit 1
fi

echo "Building DWM3001C firmware"
if ! west build -b decawave_dwm3001cdk/nrf52833 -d "$build_dir" "$app_dir"; then
	echo "Build failed; neither board was flashed." >&2
	exit 1
fi

if [ ! -f "$firmware_hex" ]; then
	echo "Firmware image not found after build: $firmware_hex" >&2
	exit 1
fi

flash_board()
{
	probe_id=$1

	echo "[$probe_id] Starting flash"
	if printf '%s\n' \
		"connect" \
		"loadfile $firmware_hex" \
		"r" \
		"g" \
		"exit" |
		"$jlink_commander" \
			-NoGui 1 \
			-ExitOnError 1 \
			-USB "$probe_id" \
			-Device NRF52833_XXAA \
			-If SWD \
			-Speed 4000
	then
		echo "[$probe_id] Flash complete"
		return 0
	fi

	echo "[$probe_id] Flash failed" >&2
	return 1
}

flash_board "$jlink_1"
flash_board "$jlink_2"

echo "Both boards flashed successfully."
