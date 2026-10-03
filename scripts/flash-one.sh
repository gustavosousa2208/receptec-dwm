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

script_resolved=$(readlink -f -- "$0" 2>/dev/null || realpath -- "$0" 2>/dev/null || echo "$0")
script_dir=$(CDPATH='' cd -- "$(dirname -- "$script_resolved")" && pwd)
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

# prod-coletec-qorvo is built from the sibling nrf52-bringup-fw checkout, so its
# canonical build directory lives with the other Nordic build trees.
workspace_source=$(CDPATH='' cd -- "$app_dir/.." && pwd)
nrf52_dir="$workspace_source/nrf52-bringup-fw"
nordic_build_root=
if [ -n "${ATLETEC_ZEPHYR_WORKSPACE:-}" ]; then
	nordic_build_root="$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/nrf52-bringup-fw"
fi
if [ -z "$nordic_build_root" ]; then
	nordic_build_root="$nrf52_dir/build"
fi
prod_coletec_qorvo_build=${PROD_COLETEC_QORVO_BUILD_DIR:-$nordic_build_root/build-prod-coletec-qorvo}

probe_opt=
assume_yes=false
dry_run=false
backup=false
allow_dirty=false
stlink=false
source_opt=
role=

usage()
{
	cat <<'EOF'
Usage: scripts/flash-one.sh ROLE [options]

ROLE: fake-coletec-0 | fake-coletec-0-anchor | fake-coletec-1 | receptec | receptec-qorvo | prod-coletec-qorvo

Flash one board. Probe serial comes from --probe, then JLINK_SN_FAKE_COLETEC_0 /
JLINK_SN_FAKE_COLETEC_1 / JLINK_SN_RECEPTEC / JLINK_SN_PROD_COLETEC_QORVO, then
the lstty nickname matching the role. The probe's registered target device must
match the role (nRF52833 for the fakes, RECEPTEC-Qorvo and PROD-COLETEC-Qorvo,
nRF52840 for Type2AB RECEPTEC) when lstty knows it.
prod-coletec-qorvo is the Nordic Rev4 product image from the sibling
nrf52-bringup-fw checkout; give it an lstty nickname before flashing without
--probe.

Options:
  --probe SERIAL|NICKNAME
                   J-Link serial or lstty nickname for this board
  --yes            skip the interactive confirmation (also automatic when
                   stdin is not a tty, e.g. under an agent, or when
                   ATLETEC_NONINTERACTIVE is set)
  --dry-run        run every check, print the plan, flash nothing
  --backup         read the RECEPTEC flash back to backups/ first (receptec only)
  --allow-dirty    accept uncommitted changes outside the image sources
  --stlink         flash over SWD through an ST-Link V3 (pyOCD, target nrf52833)
                   instead of a J-Link. Only for prod-coletec-qorvo. The probe
                   comes from --probe (lstty stlink nickname or serial), then
                   STLINK_SN_COLETEC_QORVO, then the lstty stlink nickname
                   coletec-qorvo.
  --source-dir DIR nrf52-bringup-fw checkout the image was built from
                   (prod-coletec-qorvo only; default: the sibling checkout).
                   Use it for a codex/* worktree; the dirty-tree and metadata
                   checks then run against that tree.
  -h, --help       show this help
EOF
}

[ "$#" -ge 1 ] || { usage >&2; exit 2; }
case "$1" in
fake-coletec-0|fake-coletec-0-anchor|fake-coletec-1|receptec|receptec-qorvo|prod-coletec-qorvo) role=$1; shift ;;
-h|--help) usage; exit 0 ;;
*) echo "Unknown role: $1 (want fake-coletec-0, fake-coletec-0-anchor, fake-coletec-1, receptec, receptec-qorvo or prod-coletec-qorvo)" >&2; exit 2 ;;
esac

while [ "$#" -gt 0 ]; do
	case "$1" in
	--probe) [ "$#" -ge 2 ] || exit 2; probe_opt=$2; shift 2 ;;
	--yes) assume_yes=true; shift ;;
	--dry-run) dry_run=true; shift ;;
	--backup) backup=true; shift ;;
	--allow-dirty) allow_dirty=true; shift ;;
	--stlink) stlink=true; shift ;;
	--source-dir) [ "$#" -ge 2 ] || exit 2; source_opt=$2; shift 2 ;;
	-h|--help) usage; exit 0 ;;
	*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

if [ "$stlink" = true ] && [ "$role" != prod-coletec-qorvo ]; then
	echo "--stlink applies to prod-coletec-qorvo only." >&2
	exit 2
fi
if [ -n "$source_opt" ]; then
	[ "$role" = prod-coletec-qorvo ] || { echo "--source-dir applies to prod-coletec-qorvo only." >&2; exit 2; }
	nrf52_dir=$(CDPATH='' cd -- "$source_opt" 2>/dev/null && pwd) || {
		echo "--source-dir not found: $source_opt" >&2
		exit 2
	}
fi

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
prod-coletec-qorvo) build_dir=$prod_coletec_qorvo_build; nick=prod-coletec-qorvo;
	env_sn=${JLINK_SN_PROD_COLETEC_QORVO:-}; want=nRF52833_xxAA ;;
esac

if [ "$role" = prod-coletec-qorvo ] && [ ! -d "$nrf52_dir" ]; then
	echo "nrf52 checkout not found beside this repo: $nrf52_dir" >&2
	exit 1
fi
case "$role" in
prod-coletec-qorvo) fw_src=$nrf52_dir ;;
*) fw_src=$app_dir ;;
esac

have_lstty=false
command -v lstty >/dev/null 2>&1 && have_lstty=true

if [ "$stlink" = true ]; then
	# ST-Link serials are hexadecimal, so the J-Link digits-only test does not apply.
	nick=coletec-qorvo
	probe=$probe_opt
	if [ -z "$probe" ] && [ -n "${STLINK_SN_COLETEC_QORVO:-}" ]; then
		probe=$STLINK_SN_COLETEC_QORVO
	fi
	if [ -n "$probe" ] && [ "$have_lstty" = true ]; then
		resolved=$(lstty stlink get "$probe" --field serial 2>/dev/null || true)
		[ -z "$resolved" ] || probe=$resolved
	fi
	if [ -z "$probe" ] && [ "$have_lstty" = true ]; then
		probe=$(lstty stlink get "$nick" --field serial 2>/dev/null || true)
	fi
	if [ -z "$probe" ]; then
		echo "No ST-Link for $role: pass --probe or nickname it with lstty stlink set $nick." >&2
		exit 2
	fi
	case "$probe" in
	*[!0-9A-Fa-f]*) echo "ST-Link serial must be hexadecimal." >&2; exit 2 ;;
	esac
	probe_label="ST-Link"
else
	probe_label="J-Link"
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
	*[!0-9]*)
		if [ "$have_lstty" = false ]; then
			echo "Probe '$probe' is a nickname, but lstty is not available." >&2
			exit 2
		fi
		probe_nickname_input=$probe
		probe=$(lstty jlink get "$probe_nickname_input" --field serial 2>/dev/null || true)
		if [ -z "$probe" ]; then
			echo "lstty has no J-Link probe named '$probe_nickname_input'." >&2
			exit 2
		fi
	;;
	esac
	case "$probe" in
	*[!0-9]*) echo "Probe serial must contain decimal digits only." >&2; exit 2 ;;
	esac
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

source_commit=$(git -C "$fw_src" rev-parse HEAD)
contracts_commit=$(git -C "$fw_src/contracts" rev-parse HEAD)
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
elif [ "$role" = prod-coletec-qorvo ]; then
	grep -qx "role=PROD-COLETEC-QORVO" "$metadata" &&
	grep -qx "source_commit=$source_commit" "$metadata" &&
	grep -qx "contracts_commit=$contracts_commit" "$metadata" &&
	grep -qx 'CONFIG_SOC_NRF52833=y' "$config" &&
	grep -qx 'CONFIG_BT_DIS_HW_REV_STR="Rev4"' "$config" || {
		echo "$role image metadata or configuration is stale." >&2
		exit 1
	}
else
	instance=${role#fake-coletec-}
	grep -qx "role=fake-COLETEC" "$metadata" &&
	grep -qx "identity=COLETEC-$instance" "$metadata" &&
	grep -qx "source_commit=$source_commit" "$metadata" &&
	grep -qx "contracts_commit=$contracts_commit" "$metadata" &&
	grep -qx "CONFIG_DWM_COLETEC_INSTANCE=$instance" "$config" &&
	grep -qx 'CONFIG_SHELL_BACKEND_SERIAL=y' "$config" &&
	grep -qx 'CONFIG_UART_CONSOLE=y' "$config" &&
	! grep -qx 'CONFIG_SHELL_BACKEND_RTT=y' "$config" || {
		echo "$role image metadata or configuration is stale." >&2
		exit 1
	}
fi

if [ "$stlink" = true ]; then
	command -v pyocd >/dev/null 2>&1 || {
		echo "pyocd not found; activate the west workspace environment first." >&2
		exit 1
	}
	if ! pyocd list 2>/dev/null | grep -q "$probe"; then
		echo "ST-Link $probe ($role) is not visible to pyocd (see: pyocd list)." >&2
		exit 1
	fi
	if [ "$have_lstty" = true ]; then
		device=$(lstty stlink get --serial "$probe" --field device 2>/dev/null || true)
		if [ -n "$device" ] && [ "$device" != "$want" ]; then
			echo "ST-Link $probe is registered as $device, but $role needs $want." >&2
			exit 1
		fi
		[ -n "$device" ] || echo "warning: lstty has no device recorded for $probe; cannot check it is $want." >&2
		probe_nickname=$(lstty stlink get --serial "$probe" --json 2>/dev/null |
			sed -n 's/.*"name": "\([^"]*\)".*/\1/p')
	else
		echo "warning: lstty not found; cannot check that $probe is a $want." >&2
		probe_nickname=
	fi
elif [ "$have_lstty" = true ]; then
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
	probe_nickname=$(printf '%s\n' "$info" | sed -n 's/.*"name": "\([^"]*\)".*/\1/p')
else
	echo "warning: lstty not found; cannot check that $probe is a $want." >&2
	probe_nickname=
fi

test_config=$(sed -n 's/^test_config=//p' "$metadata")

echo "Flash mapping:"
if [ -n "$probe_nickname" ]; then
	echo "  $role image -> $probe_label $probe (lstty nickname: $probe_nickname)"
else
	echo "  $role image -> $probe_label $probe"
fi
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

if [ "$stlink" = true ]; then
	flash_log="$build_dir/flash-stlink.log"
	west flash -d "$build_dir" --runner pyocd -- \
		--target=nrf52833 --board-id="$probe" >"$flash_log" 2>&1
else
	flash_log="$build_dir/flash.log"
	west flash -d "$build_dir" --runner jlink --dev-id "$probe" >"$flash_log" 2>&1
fi
if [ $? -eq 0 ]; then
	echo "$role flash passed"
	if [ "$have_lstty" = true ]; then
		if [ "$stlink" = true ]; then
			lstty stlink use "$probe" "$role bench firmware ($source_commit)" >/dev/null 2>&1
		else
			lstty jlink use "$probe" "$role bench firmware ($source_commit)" >/dev/null 2>&1
		fi
	fi
else
	echo "$role flash FAILED (see $flash_log)" >&2
	exit 1
fi
