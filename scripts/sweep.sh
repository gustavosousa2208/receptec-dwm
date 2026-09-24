#!/bin/sh
# Poll-period sweep: build + flash + sync-sweep for each fake-COLETEC period.
# Replaces the hand-rolled `for P in ...` shell loop: one call, one log dir,
# same safeties as build-both.sh / flash-both.sh (this script delegates to
# them, it does not reimplement flashing).
set -u

if [ -z "${ATLETEC_ZEPHYR_WORKSPACE:-}" ] && [ -f "$HOME/source/atletec/.envrc" ]; then
	. "$HOME/source/atletec/.envrc"
fi

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH='' cd -- "$script_dir/.." && pwd)

poll_list="200,100,50"
duration=60
slots="0,1"
flash_receptec=true
do_build=true
do_flash=true
assume_yes=false
allow_dirty=false
log_dir=

usage()
{
	cat <<'EOF'
Usage: scripts/sweep.sh [options]

For each poll period: build the fake COLETECs with tests/throughput-<P>ms.conf,
flash the bench, run sync-sweep.py --poll-ms <P> --duration <s>.

Options:
  --poll-ms LIST   comma list, each needs tests/throughput-<P>ms.conf (default: 200,100,50)
  --duration S     poll run length in seconds per period (default: 60)
  --slots S        slots passed to sync-sweep.py (default: 0,1)
  --no-receptec    build/flash the fake COLETECs only
  --no-build       skip the build step (flash + measure only)
  --no-flash       skip the flash step (build + measure only)
  --yes            skip flash confirmations (also automatic when stdin is not
                   a tty, e.g. under an agent, or when ATLETEC_NONINTERACTIVE set)
  --allow-dirty    pass through to the flash step
  --log-dir DIR    sweep console logs (default: logs/sweep-<utc-stamp>/)
  -h, --help       show this help

Console output per period goes to <log-dir>/sweep-<P>ms.log; the raw
shell transcript stays where sync-sweep.py writes it (--log-dir).
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
	--poll-ms) [ "$#" -ge 2 ] || exit 2; poll_list=$2; shift 2 ;;
	--duration) [ "$#" -ge 2 ] || exit 2; duration=$2; shift 2 ;;
	--slots) [ "$#" -ge 2 ] || exit 2; slots=$2; shift 2 ;;
	--no-receptec) flash_receptec=false; shift ;;
	--no-build) do_build=false; shift ;;
	--no-flash) do_flash=false; shift ;;
	--yes) assume_yes=true; shift ;;
	--allow-dirty) allow_dirty=true; shift ;;
	--log-dir) [ "$#" -ge 2 ] || exit 2; log_dir=$2; shift 2 ;;
	-h|--help) usage; exit 0 ;;
	*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

case "$duration" in
*[!0-9]*|"") echo "--duration must be seconds (digits only)." >&2; exit 2 ;;
esac

if [ -n "${ATLETEC_NONINTERACTIVE:-}" ]; then
	assume_yes=true
elif [ "$assume_yes" = false ] && [ ! -t 0 ]; then
	assume_yes=true
fi

if [ -z "$log_dir" ]; then
	log_dir="$app_dir/logs/sweep-$(date -u +%Y%m%dT%H%M%SZ)"
fi
mkdir -p "$log_dir" || exit 1

if command -v python >/dev/null 2>&1; then
	py=python
else
	py=python3
fi

flash_args=""
if [ "$flash_receptec" = false ]; then
	build_receptec_flag="--no-receptec"
	flash_args="$flash_args --no-receptec"
else
	build_receptec_flag=""
fi
if [ "$assume_yes" = true ]; then
	flash_args="$flash_args --yes"
fi
if [ "$allow_dirty" = true ]; then
	flash_args="$flash_args --allow-dirty"
fi

pass_list=""
fail_list=""
echo "Sweep log dir: $log_dir"
# shellcheck disable=SC2086
set -f
old_ifs=$IFS; IFS=,
for P in $poll_list; do
	IFS=$old_ifs
	case "$P" in
	*[!0-9]*|"") echo "Skipping bad poll value: $P" >&2; fail_list="$fail_list $P"; continue ;;
	esac
	conf="tests/throughput-${P}ms.conf"
	if [ ! -f "$app_dir/$conf" ]; then
		echo "=== ${P} ms: no $conf; skipping" | tee "$log_dir/sweep-${P}ms.log"
		fail_list="$fail_list $P"
		continue
	fi
	{
	echo "=== ${P} ms: build ($conf)"
	if [ "$do_build" = true ]; then
		# shellcheck disable=SC2086
		if ! sh "$script_dir/build-both.sh" $build_receptec_flag --test-conf "$conf"; then
			echo "${P} ms: BUILD FAILED"
			exit 1
		fi
	else
		echo "(build skipped)"
	fi
	echo "=== ${P} ms: flash"
	if [ "$do_flash" = true ]; then
		# shellcheck disable=SC2086
		if ! sh "$script_dir/flash-both.sh" $flash_args; then
			echo "${P} ms: FLASH FAILED"
			exit 1
		fi
	else
		echo "(flash skipped)"
	fi
	echo "=== ${P} ms: measure (${duration}s)"
	if ! "$py" "$script_dir/sync-sweep.py" --poll-ms "$P" --duration "$duration" \
		--slots "$slots" --log-dir "$log_dir"; then
		echo "${P} ms: MEASURE FAILED"
		exit 1
	fi
	} >"$log_dir/sweep-${P}ms.log" 2>&1
	status=$?
	grep -E "start ->|FINAL|^poll running=0|^slot|Raw log|FAILED|skipped|BUILD" "$log_dir/sweep-${P}ms.log" | tail -8 || true
	if [ "$status" -eq 0 ]; then
		pass_list="$pass_list $P"
	else
		fail_list="$fail_list $P"
	fi
done

echo "--- sweep summary (ms) ---"
echo "passed:${pass_list:- (none)}"
echo "failed:${fail_list:- (none)}"
echo "logs: $log_dir"
[ -z "$fail_list" ]
