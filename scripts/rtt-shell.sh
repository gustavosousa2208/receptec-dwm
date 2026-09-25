#!/bin/sh

set -eu

probe=${1:-}
device=${JLINK_DEVICE:-}
gdb_server=${JLINK_GDB_SERVER:-JLinkGDBServerCLExe}
rtt_client=${JLINK_RTT_CLIENT:-JLinkRTTClient}

if [ -z "$probe" ]; then
	echo "Usage: scripts/rtt-shell.sh <J-Link serial | lstty nickname>" >&2
	echo "Nicknames: fake-coletec-0, fake-coletec-1, receptec (see: lstty)." >&2
	exit 2
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
