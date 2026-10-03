#!/bin/sh
# Flash the production COLETEC Nordic through its MCU-Link CMSIS-DAP probe.
# The prod-coletec nRF52840 is wired to MCU-Link, not to a J-Link, so the west
# default (jlink runner) cannot reach it; flashing goes through the pyOCD
# runner instead. Same safeties as scripts/flash-one.sh (target-device check,
# dirty-tree refusal); keep the two in sync.
#
# There is no commit metadata on nrf52 build.sh output (that file is only
# written by build-one.sh roles), so staleness is covered by west rebuilding
# the directory before flashing, not by a metadata comparison.
set -u

# Agents often run scripts without sourcing the workspace env first.
if [ -z "${ATLETEC_ZEPHYR_WORKSPACE:-}" ] && [ -f "$HOME/source/atletec/.envrc" ]; then
	. "$HOME/source/atletec/.envrc"
fi

script_resolved=$(readlink -f -- "$0" 2>/dev/null || realpath -- "$0" 2>/dev/null || echo "$0")
script_dir=$(CDPATH='' cd -- "$(dirname -- "$script_resolved")" && pwd)
app_dir=$(CDPATH='' cd -- "$script_dir/.." && pwd)
workspace_source=$(CDPATH='' cd -- "$app_dir/.." && pwd)
nrf52_dir="$workspace_source/nrf52-bringup-fw"
nordic_build_root=
if [ -n "${ATLETEC_ZEPHYR_WORKSPACE:-}" ]; then
	nordic_build_root="$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/nrf52-bringup-fw"
fi
if [ -z "$nordic_build_root" ]; then
	nordic_build_root="$nrf52_dir/build"
fi
default_build="$nordic_build_root/build-prod-coletec"

probe_opt=
build_opt=
source_opt=
assume_yes=false
dry_run=false
allow_dirty=false
role=

usage()
{
	cat <<'EOF'
Usage: scripts/flash-mculink.sh ROLE [options]

ROLE: prod-coletec

Flash the production COLETEC Nordic (nRF52840, Rev3) through its MCU-Link
CMSIS-DAP probe using the pyOCD runner. Probe serial comes from --probe, then
the lstty mculink nickname matching the role (prod-coletec).

Options:
  --probe SERIAL|NICKNAME
                   MCU-Link serial or lstty mculink nickname for this board
  --build-dir DIR  build directory (default: build-prod-coletec under the
                   Nordic build tree; use for the recovery build)
  --source-dir DIR source checkout to run the dirty-tree/image-source check
                   against (default: the nrf52-bringup-fw checkout beside
                   this repo). Point this at the worktree the build was
                   actually produced from, e.g. a codex/* worktree, so the
                   check inspects the right tree instead of the main
                   checkout's unrelated uncommitted work.
  --yes            skip the interactive confirmation (also automatic when
                   stdin is not a tty, e.g. under an agent, or when
                   ATLETEC_NONINTERACTIVE is set)
  --dry-run        run every check, print the plan, flash nothing
  --allow-dirty    accept uncommitted changes outside the image sources
  -h, --help       show this help
EOF
}

[ "$#" -ge 1 ] || { usage >&2; exit 2; }
case "$1" in
prod-coletec) role=$1; shift ;;
-h|--help) usage; exit 0 ;;
*) echo "Unknown role: $1 (want prod-coletec)" >&2; exit 2 ;;
esac

while [ "$#" -gt 0 ]; do
	case "$1" in
	--probe) [ "$#" -ge 2 ] || exit 2; probe_opt=$2; shift 2 ;;
	--build-dir) [ "$#" -ge 2 ] || exit 2; build_opt=$2; shift 2 ;;
	--source-dir) [ "$#" -ge 2 ] || exit 2; source_opt=$2; shift 2 ;;
	--yes) assume_yes=true; shift ;;
	--dry-run) dry_run=true; shift ;;
	--allow-dirty) allow_dirty=true; shift ;;
	-h|--help) usage; exit 0 ;;
	*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

if [ ! -d "$nrf52_dir" ]; then
	echo "nrf52 checkout not found beside this repo: $nrf52_dir" >&2
	exit 1
fi
if [ -n "$source_opt" ]; then
	fw_src=$source_opt
else
	fw_src=$nrf52_dir
fi

# prod-coletec is Rev3: nRF52840, pyOCD target nrf52840.
nick=prod-coletec
pyocd_target=nrf52840
if [ -n "$build_opt" ]; then
	build_dir=$build_opt
else
	build_dir=$default_build
fi

have_lstty=false
command -v lstty >/dev/null 2>&1 && have_lstty=true

probe=$probe_opt
if [ -n "$probe" ] && [ "$have_lstty" = true ]; then
	# A nickname resolves through lstty; a literal serial does not, so
	# resolution is tried first and the value is kept as-is on failure.
	# (MCU-Link serials are alphanumeric, so unlike J-Link there is no
	# digits-only test to tell the two apart.)
	resolved=$(lstty mculink get "$probe" 2>/dev/null || true)
	if [ -n "$resolved" ]; then
		probe=$resolved
	fi
fi
if [ -z "$probe" ] && [ "$have_lstty" = true ]; then
	probe=$(lstty mculink get "$nick" 2>/dev/null || true)
fi
if [ -z "$probe" ]; then
	echo "No probe for $role: pass --probe or nickname it with lstty." >&2
	exit 2
fi

command -v west >/dev/null 2>&1 || {
	echo "west was not found; activate the workspace environment first." >&2
	exit 1
}
if ! git -C "$fw_src" diff --quiet || ! git -C "$fw_src" diff --cached --quiet; then
	image_changes=$(git -C "$fw_src" diff HEAD --name-only -- \
		src include boards dts firmware Kconfig prj.conf CMakeLists.txt contracts patches)
	if [ "$allow_dirty" = true ] && [ -z "$image_changes" ]; then
		echo "warning: working tree has changes, none in image sources; images match HEAD." >&2
	else
		echo "Working tree has tracked changes; rebuild and commit before flashing." >&2
		[ -z "$image_changes" ] || printf 'Changed image sources:\n%s\n' "$image_changes" >&2
		[ "$allow_dirty" = true ] || echo "(--allow-dirty accepts changes that do not touch image sources.)" >&2
		exit 1
	fi
fi

config="$build_dir/zephyr/.config"
if [ ! -f "$build_dir/zephyr/zephyr.hex" ] || [ ! -f "$config" ]; then
	echo "Missing $role build in $build_dir; build it with nrf52-bringup-fw/build.sh first." >&2
	exit 1
fi
grep -qx 'CONFIG_SOC_NRF52840=y' "$config" || {
	echo "$build_dir is not an nRF52840 prod-coletec build." >&2
	exit 1
}

if ! command -v pyocd >/dev/null 2>&1; then
	echo "pyocd not found; activate the west workspace environment first." >&2
	exit 1
fi
if ! pyocd list 2>/dev/null | grep -q "$probe"; then
	echo "MCU-Link $probe is not visible to pyocd (see: pyocd list)." >&2
	exit 1
fi

echo "Flash mapping:"
echo "  $role image -> MCU-Link $probe (pyOCD target $pyocd_target)"
echo "  build       -> $build_dir"
if [ "$dry_run" = true ]; then
	echo "Dry run: every check passed, nothing was flashed."
	exit 0
fi
if [ -n "${ATLETEC_NONINTERACTIVE:-}" ]; then
	assume_yes=true
elif [ "$assume_yes" = false ] && [ ! -t 0 ]; then
	assume_yes=true
fi
if [ "$assume_yes" = false ]; then
	printf 'Flash %s? [y/N] ' "$role"
	IFS= read -r answer
	case "$answer" in y|Y|yes|YES) ;; *) echo "Flash cancelled."; exit 1 ;; esac
fi

if west flash -d "$build_dir" --runner pyocd -- \
		--target="$pyocd_target" --board-id="$probe" \
	>"$build_dir/flash-mculink.log" 2>&1; then
	echo "$role flash passed"
	[ "$have_lstty" = false ] ||
		lstty mculink use "$probe" "$role bench firmware" >/dev/null 2>&1
else
	echo "$role flash FAILED (see $build_dir/flash-mculink.log)" >&2
	exit 1
fi
