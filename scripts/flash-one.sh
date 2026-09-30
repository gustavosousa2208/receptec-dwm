#!/bin/sh
# Flash ONE board: a fake COLETEC, fixed fake-board anchor or real RECEPTEC.
# Each physical board has one canonical build directory; the anchor role shares
# build-fake-coletec-0 with the fake-coletec-0 responder role.
# Same safeties as scripts/flash-both.sh (distinct probe, target-device check,
# image staleness check, dirty-tree refusal); keep the two in sync.
set -u

# Agents often run scripts without sourcing the workspace env first.
if [ -z "${ATLETEC_ZEPHYR_WORKSPACE:-}" ] && [ -f "$HOME/source/atletec/.envrc" ]; then
	. "$HOME/source/atletec/.envrc"
fi

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH='' cd -- "$script_dir/.." && pwd)
build_root=${ATLETEC_BUILD_ROOT:-}
if [ -z "$build_root" ] && [ -n "${ATLETEC_ZEPHYR_WORKSPACE:-}" ]; then
	build_root="$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec"
fi
if [ -n "$build_root" ]; then
	coletec_0_build="$build_root/build-fake-coletec-0"
	coletec_0_anchor_build="$build_root/build-fake-coletec-0"
	coletec_1_build="$build_root/build-fake-coletec-1"
	default_receptec_build="$build_root/build-receptec"
	default_receptec_qorvo_build="$build_root/build-receptec-qorvo"
else
	coletec_0_build="$app_dir/build-fake-coletec-0"
	coletec_0_anchor_build="$app_dir/build-fake-coletec-0"
	coletec_1_build="$app_dir/build-fake-coletec-1"
	default_receptec_build="$app_dir/build-receptec"
	default_receptec_qorvo_build="$app_dir/build-receptec-qorvo"
fi
receptec_build=${RECEPTEC_BUILD_DIR:-$default_receptec_build}
receptec_qorvo_build=${RECEPTEC_QORVO_BUILD_DIR:-$default_receptec_qorvo_build}

probe_opt=
assume_yes=false
dry_run=false
backup=false
allow_dirty=false
role=

usage()
{
	cat <<'EOF'
Usage: scripts/flash-one.sh ROLE [options]

ROLE: fake-coletec-0 | fake-coletec-0-anchor | fake-coletec-1 | receptec | receptec-qorvo

Flash one board. Probe serial comes from --probe, then JLINK_SN_FAKE_COLETEC_0 /
JLINK_SN_FAKE_COLETEC_1 / JLINK_SN_RECEPTEC, then the lstty nickname matching
the role. The probe's registered target device must match the role (nRF52833
for the fakes and RECEPTEC-Qorvo, nRF52840 for Type2AB RECEPTEC) when lstty knows it.

Options:
  --probe SERIAL   J-Link serial for this board
  --yes            skip the interactive confirmation (also automatic when
                   stdin is not a tty, e.g. under an agent, or when
                   ATLETEC_NONINTERACTIVE is set)
  --dry-run        run every check, print the plan, flash nothing
  --backup         read the RECEPTEC flash back to backups/ first (receptec only)
  --allow-dirty    accept uncommitted changes outside the image sources
  -h, --help       show this help
EOF
}

[ "$#" -ge 1 ] || { usage >&2; exit 2; }
case "$1" in
fake-coletec-0|fake-coletec-0-anchor|fake-coletec-1|receptec|receptec-qorvo) role=$1; shift ;;
-h|--help) usage; exit 0 ;;
*) echo "Unknown role: $1 (want fake-coletec-0, fake-coletec-1, receptec or receptec-qorvo)" >&2; exit 2 ;;
esac

while [ "$#" -gt 0 ]; do
	case "$1" in
	--probe) [ "$#" -ge 2 ] || exit 2; probe_opt=$2; shift 2 ;;
	--yes) assume_yes=true; shift ;;
	--dry-run) dry_run=true; shift ;;
	--backup) backup=true; shift ;;
	--allow-dirty) allow_dirty=true; shift ;;
	-h|--help) usage; exit 0 ;;
	*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

if [ "$backup" = true ] && [ "$role" != receptec ]; then
	echo "--backup applies to receptec only." >&2
	exit 2
fi

case "$role" in
fake-coletec-0) build_dir=$coletec_0_build; nick=fake-coletec-0;
	env_sn=${JLINK_SN_FAKE_COLETEC_0:-}; want=nRF52833_xxAA ;;
fake-coletec-0-anchor) build_dir=$coletec_0_anchor_build; nick=fake-coletec-0;
	env_sn=${JLINK_SN_FAKE_COLETEC_0:-}; want=nRF52833_xxAA ;;
fake-coletec-1) build_dir=$coletec_1_build; nick=fake-coletec-1;
	env_sn=${JLINK_SN_FAKE_COLETEC_1:-}; want=nRF52833_xxAA ;;
receptec) build_dir=$receptec_build; nick=receptec;
	env_sn=${JLINK_SN_RECEPTEC:-}; want=nRF52840_xxAA ;;
receptec-qorvo) build_dir=$receptec_qorvo_build; nick=receptec;
	env_sn=${JLINK_SN_RECEPTEC:-}; want=nRF52833_xxAA ;;
esac

have_lstty=false
command -v lstty >/dev/null 2>&1 && have_lstty=true

probe=$probe_opt
if [ -z "$probe" ] && [ -n "$env_sn" ]; then
	probe=$env_sn
fi
if [ -z "$probe" ] && [ "$have_lstty" = true ]; then
	probe=$(lstty jlink get "$nick" 2>/dev/null)
fi
if [ -z "$probe" ]; then
	echo "No probe for $role: pass --probe or nickname it with lstty." >&2
	exit 2
fi
case "$probe" in
*[!0-9]*) echo "Probe serial must contain decimal digits only." >&2; exit 2 ;;
esac

command -v west >/dev/null 2>&1 || {
	echo "west was not found; activate the workspace environment first." >&2
	exit 1
}
if ! git -C "$app_dir" diff --quiet || ! git -C "$app_dir" diff --cached --quiet; then
	image_changes=$(git -C "$app_dir" diff HEAD --name-only -- \
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

source_commit=$(git -C "$app_dir" rev-parse HEAD)
contracts_commit=$(git -C "$app_dir/contracts" rev-parse HEAD)
metadata="$build_dir/atletec-dwm-build.txt"
config="$build_dir/zephyr/.config"

if [ ! -f "$build_dir/zephyr/zephyr.hex" ] || [ ! -f "$metadata" ] || [ ! -f "$config" ]; then
	echo "Missing $role build in $build_dir; run scripts/build-one.sh $role." >&2
	exit 1
fi
if [ "$role" = receptec ]; then
	grep -qx "role=RECEPTEC" "$metadata" &&
	grep -qx "source_commit=$source_commit" "$metadata" &&
	grep -qx "contracts_commit=$contracts_commit" "$metadata" &&
	grep -qx 'CONFIG_SOC_NRF52840=y' "$config" || {
		echo "$role image metadata or configuration is stale." >&2
		exit 1
	}
elif [ "$role" = receptec-qorvo ]; then
	grep -qx "role=RECEPTEC-QORVO" "$metadata" &&
	grep -qx "source_commit=$source_commit" "$metadata" &&
	grep -qx "contracts_commit=$contracts_commit" "$metadata" &&
	grep -qx 'CONFIG_BOARD_RECEPTEC_QORVO=y' "$config" &&
	grep -qx 'CONFIG_SOC_NRF52833=y' "$config" || {
		echo "$role image metadata or configuration is stale." >&2
		exit 1
	}
	elif [ "$role" = fake-coletec-0-anchor ]; then
	grep -qx "role=RECEPTEC-Qorvo-anchor-1" "$metadata" &&
	grep -qx "source_commit=$source_commit" "$metadata" &&
	grep -qx "contracts_commit=$contracts_commit" "$metadata" &&
	grep -qx 'anchor_id=1' "$metadata" &&
	grep -qx 'CONFIG_RECEPTEC_UWB_RADIO=y' "$config" &&
	grep -qx 'CONFIG_RECEPTEC_FIXED_ANCHOR_ID=1' "$config" &&
	grep -qx 'CONFIG_SOC_NRF52833=y' "$config" || {
		echo "$role image metadata or configuration is stale." >&2
		exit 1
	}
else
	instance=${role#fake-coletec-}
	grep -qx "role=fake-COLETEC" "$metadata" &&
	grep -qx "identity=COLETEC-$instance" "$metadata" &&
	grep -qx "source_commit=$source_commit" "$metadata" &&
	grep -qx "contracts_commit=$contracts_commit" "$metadata" &&
	grep -qx "CONFIG_DWM_COLETEC_INSTANCE=$instance" "$config" || {
		echo "$role image metadata or configuration is stale." >&2
		exit 1
	}
fi

if [ "$have_lstty" = true ]; then
	info=$(lstty jlink get --serial "$probe" --json 2>/dev/null || true)
	case "$info" in
	*'"connected": true'*) ;;
	*) echo "J-Link $probe ($role) is not connected or has no lstty nickname." >&2; exit 1 ;;
	esac
	device=$(lstty jlink get --serial "$probe" --field device 2>/dev/null || true)
	if [ -n "$device" ] && [ "$device" != "$want" ]; then
		echo "J-Link $probe is registered as $device, but $role needs $want." >&2
		exit 1
	fi
	[ -n "$device" ] || echo "warning: lstty has no device recorded for $probe; cannot check it is $want." >&2
else
	echo "warning: lstty not found; cannot check that $probe is a $want." >&2
fi

test_config=$(sed -n 's/^test_config=//p' "$metadata")

echo "Flash mapping:"
echo "  $role image -> J-Link $probe"
echo "  build       -> $build_dir"
echo "  test config -> $test_config"
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

if [ "$backup" = true ]; then
	command -v lstty >/dev/null 2>&1 || {
		echo "lstty not found; cannot back up RECEPTEC before flashing." >&2
		exit 1
	}
	backup_dir="$app_dir/backups/real-receptec-$probe-$(date -u +%Y%m%dT%H%M%SZ)"
	mkdir -p "$backup_dir"
	# Flash (1 MiB) and UICR (4 KiB) of the nRF52840, read over SWD with JLinkExe.
	# Never nrfjprog: it opens a probe-selection window and blocks.
	if ! { lstty jlink dump "$probe" 0 0x100000 -o "$backup_dir/flash.bin" &&
		lstty jlink dump "$probe" 0x10001000 0x1000 -o "$backup_dir/uicr.bin"; } \
		>"$backup_dir/readback.log" 2>&1 || [ ! -s "$backup_dir/flash.bin" ]; then
		echo "RECEPTEC backup FAILED (see $backup_dir/readback.log); nothing flashed." >&2
		exit 1
	fi
	echo "RECEPTEC flash backed up to $backup_dir/flash.bin (+ uicr.bin)"
fi

if west flash -d "$build_dir" --runner jlink --dev-id "$probe" \
	>"$build_dir/flash.log" 2>&1; then
	echo "$role flash passed"
	[ "$have_lstty" = false ] ||
		lstty jlink use "$probe" "$role bench firmware ($source_commit)" >/dev/null 2>&1
else
	echo "$role flash FAILED (see $build_dir/flash.log)" >&2
	exit 1
fi
