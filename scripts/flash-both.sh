#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
build_dir="$app_dir/build"
venv_activate="$app_dir/../.venv/bin/activate"

jlink_1=760216755
jlink_2=760216737

if [ ! -f "$venv_activate" ]; then
	echo "Workspace virtual environment not found: $venv_activate" >&2
	exit 1
fi

. "$venv_activate"

echo "Building DWM3001C firmware"
if ! west build -b decawave_dwm3001cdk/nrf52833 -d "$build_dir" "$app_dir"; then
	echo "Build failed; neither board was flashed." >&2
	exit 1
fi

flash_board()
{
	probe_id=$1

	echo "[$probe_id] Starting flash"
	if west flash -d "$build_dir" -r jlink --no-rebuild --dev-id "$probe_id"; then
		echo "[$probe_id] Flash complete"
		return 0
	fi

	echo "[$probe_id] Flash failed" >&2
	return 1
}

flash_board "$jlink_1" &
pid_1=$!
flash_board "$jlink_2" &
pid_2=$!

status=0
wait "$pid_1" || status=1
wait "$pid_2" || status=1

if [ "$status" -ne 0 ]; then
	echo "One or more boards failed to flash." >&2
	exit "$status"
fi

echo "Both boards flashed successfully."
