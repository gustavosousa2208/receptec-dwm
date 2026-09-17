#!/bin/sh

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH='' cd -- "$script_dir/.." && pwd)
receptec_build_dir="$app_dir/build-receptec"
coletec_build_dir="$app_dir/build-coletec"
venv_activate="$app_dir/../.venv/bin/activate"
jlink_commander=${JLINK_EXE:-JLinkExe}

receptec_probe=760216755
coletec_probe=760216737

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

echo "Building RECEPTEC image"
if ! west build -b decawave_dwm3001cdk/nrf52833 -d "$receptec_build_dir" \
	"$app_dir" -- -DEXTRA_CONF_FILE=boards/receptec.conf; then
	echo "Build failed; neither board was flashed." >&2
	exit 1
fi

echo "Building COLETEC image"
if ! west build -b decawave_dwm3001cdk/nrf52833 -d "$coletec_build_dir" \
	"$app_dir" -- -DEXTRA_CONF_FILE=boards/coletec.conf; then
	echo "Build failed; neither board was flashed." >&2
	exit 1
fi

flash_board()
{
	probe_id=$1
	firmware_hex=$2
	role=$3

	if [ ! -f "$firmware_hex" ]; then
		echo "$role firmware image not found: $firmware_hex" >&2
		return 1
	fi

	echo "[$probe_id] Starting $role flash"
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
		echo "[$probe_id] $role flash complete"
		return 0
	fi

	echo "[$probe_id] Flash failed" >&2
	return 1
}

flash_board "$receptec_probe" \
	"$receptec_build_dir/zephyr/zephyr.hex" RECEPTEC
flash_board "$coletec_probe" \
	"$coletec_build_dir/zephyr/zephyr.hex" COLETEC

echo "Both boards flashed successfully."
