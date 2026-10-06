#!/bin/sh

set -eu

probe=${1:-}
device=${JLINK_DEVICE:-}
gdb_server=${JLINK_GDB_SERVER:-JLinkGDBServerCLExe}
rtt_client=${JLINK_RTT_CLIENT:-JLinkRTTClient}

if [ -z "$probe" ]; then
	echo "Usage: scripts/rtt-shell.sh <debug-probe serial | lstty nickname>" >&2
	echo "Nicknames: fake-coletec-0, fake-coletec-1, receptec, prod-coletec (see: lstty)." >&2
	exit 2
fi

# The COLETEC STM32 can use OpenOCD RTT through its ST-Link when the nickname is
# an online STM32N657 probe. Keep this explicit transport in bench so UART stays
# the default for ordinary STM32 console access.
if command -v lstty >/dev/null 2>&1 && case "$probe" in *[!0-9]*) true ;; *) false ;; esac; then
	stlink_info=$(lstty stlink get "$probe" --json 2>/dev/null || true)
	case "$stlink_info" in
	*'"connected": true'*)
		# Prefer the workspace-local OpenOCD when this checkout has one.
		script_path=$0
		while [ -L "$script_path" ]; do
			script_dir=$(CDPATH= cd -P "$(dirname "$script_path")" && pwd)
			link_target=$(readlink "$script_path")
			case "$link_target" in
				/*) script_path=$link_target ;;
				*) script_path=$script_dir/$link_target ;;
			esac
		done
		script_dir=$(CDPATH= cd -P "$(dirname "$script_path")" && pwd)
		atletec_root=$(CDPATH= cd -P "$script_dir/../.." && pwd)
		if [ -x "$atletec_root/tools/openocd-local" ]; then
			OPENOCD=$atletec_root/tools/openocd-local
			OPENOCD_SCRIPTS=$atletec_root/tools/openocd-install/share/openocd/scripts
			export OPENOCD OPENOCD_SCRIPTS
		fi
		exec bench console "$probe" --stlink --transport stlink-rtt --interactive
		;;
	esac
fi

# The production Nordic is currently connected through an MCU-Link CMSIS-DAP
# probe. RTT itself is probe-independent, but the SEGGER clients below are not;
# use the CMSIS-DAP backend when that probe is connected.
if command -v lstty >/dev/null 2>&1 && case "$probe" in *[!0-9]*) true ;; *) false ;; esac; then
	mculink_info=$(lstty mculink get "$probe" --json 2>/dev/null || true)
	case "$mculink_info" in
	*'"connected": true'*)
		exec bench console "$probe" --mculink --transport mculink-rtt --interactive
		;;
	esac
fi

# A nickname resolves through lstty, which also knows the target device.
case "$probe" in
*[!0-9]*)
	command -v lstty >/dev/null 2>&1 || {
		echo "J-Link serial must contain decimal digits only: $probe" >&2
		exit 2
	}
	nick=$probe
	probe=$(lstty jlink get "$nick" 2>/dev/null) || {
		echo "No J-Link nickname '$nick'; see: lstty" >&2
		exit 2
	}
	[ -n "$device" ] || device=$(lstty jlink get "$nick" --field device 2>/dev/null)
	;;
esac
# The default is a DWM (nRF52833); RECEPTEC is an nRF52840, so name it via lstty.
device=${device:-nRF52833_xxAA}

case "$probe" in
*[!0-9]*)
	echo "J-Link serial must contain decimal digits only: $probe" >&2
	exit 2
	;;
esac

command -v "$gdb_server" >/dev/null 2>&1 || {
	echo "J-Link GDB Server not found: $gdb_server" >&2
	exit 1
}
command -v "$rtt_client" >/dev/null 2>&1 || {
	echo "J-Link RTT Client not found: $rtt_client" >&2
	exit 1
}

server_pid=''
stop_server()
{
	if [ -n "$server_pid" ]; then
		kill "$server_pid" 2>/dev/null || true
		wait "$server_pid" 2>/dev/null || true
	fi
}
trap stop_server EXIT HUP INT TERM

echo "Opening RTT shell through J-Link $probe ($device)"
"$gdb_server" \
	-select "USB=$probe" \
	-device "$device" \
	-if SWD \
	-speed 4000 \
	-nohalt \
	-noir \
	-silent &
server_pid=$!

# Allow the GDB server to claim the probe and expose its RTT TCP endpoint.
sleep 1
if ! kill -0 "$server_pid" 2>/dev/null; then
	wait "$server_pid"
	exit $?
fi

"$rtt_client" -LocalEcho Off
