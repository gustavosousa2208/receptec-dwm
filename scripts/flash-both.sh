#!/bin/sh
set -u

# Agents often run scripts without sourcing the workspace env first.
if [ -z "${ATLETEC_ZEPHYR_WORKSPACE:-}" ] && [ -f "$HOME/source/atletec/.envrc" ]; then
	. "$HOME/source/atletec/.envrc"
fi

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH='' cd -- "$script_dir/.." && pwd)
# Fresh build output belongs in the west workspace build tree. Without a
# workspace variable it falls back to build-*/ inside the repository. Old
# build-*/ directories copied from another machine hold a CMake cache for a
# different source path and cannot be reused.
build_root=${ATLETEC_BUILD_ROOT:-}
if [ -z "$build_root" ] && [ -n "${ATLETEC_ZEPHYR_WORKSPACE:-}" ]; then
	build_root="$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec"
fi
if [ -n "$build_root" ]; then
	coletec_0_build="$build_root/fake-coletec-0"
	coletec_1_build="$build_root/fake-coletec-1"
	default_receptec_build="$build_root/receptec-board"
else
	coletec_0_build="$app_dir/build-fake-coletec-0"
	coletec_1_build="$app_dir/build-fake-coletec-1"
	default_receptec_build="$app_dir/build-receptec"
fi
receptec_build=${RECEPTEC_BUILD_DIR:-$default_receptec_build}

# Probe serials. There are no hard-coded defaults on purpose: a stale default
# once sent a DWM image to the wrong board. Order: option, environment, then the
# lstty nickname (fake-coletec-0, fake-coletec-1, receptec).
coletec_0_probe=${JLINK_SN_FAKE_COLETEC_0:-}
coletec_1_probe=${JLINK_SN_FAKE_COLETEC_1:-}
receptec_probe=${JLINK_SN_RECEPTEC:-}
flash_receptec=true
assume_yes=false
dry_run=false
backup_receptec=false
allow_dirty=false

usage()
{
	cat <<'EOF'
Usage: scripts/flash-both.sh [options]

Flash the two fake-COLETEC DWM surrogates and the real RECEPTEC (Murata Type 2AB,
nRF52840) concurrently. Run scripts/build-both.sh and commit first.

Probe serials come from the option, then JLINK_SN_FAKE_COLETEC_0,
JLINK_SN_FAKE_COLETEC_1 and JLINK_SN_RECEPTEC, then the lstty nicknames
fake-coletec-0, fake-coletec-1 and receptec. Each probe's target device must
match its role (nRF52833 for the fakes, nRF52840 for RECEPTEC) when lstty knows it.

Options:
  --fake-coletec-0-probe SERIAL  fake-COLETEC-0 J-Link serial
  --fake-coletec-1-probe SERIAL  fake-COLETEC-1 J-Link serial
  --receptec-probe SERIAL        real RECEPTEC J-Link serial
  --no-receptec                  flash only the two fake COLETECs
  --dry-run                      run every check, print the plan, flash nothing
  --backup                       read the RECEPTEC flash and UICR back to backups/ first (lstty jlink dump)
  --allow-dirty                  accept uncommitted changes outside the image sources
  --yes                          skip the interactive mapping confirmation
  -h, --help                     show this help

No backup is taken by default: the RECEPTEC image is rebuilt from source.
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
	--fake-coletec-0-probe) [ "$#" -ge 2 ] || exit 2; coletec_0_probe=$2; shift 2 ;;
	--fake-coletec-1-probe) [ "$#" -ge 2 ] || exit 2; coletec_1_probe=$2; shift 2 ;;
	--receptec-probe) [ "$#" -ge 2 ] || exit 2; receptec_probe=$2; shift 2 ;;
	--no-receptec) flash_receptec=false; shift ;;
	--dry-run) dry_run=true; shift ;;
	--backup) backup_receptec=true; shift ;;
	--allow-dirty) allow_dirty=true; shift ;;
	--yes) assume_yes=true; shift ;;
	-h|--help) usage; exit 0 ;;
	*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

have_lstty=false
command -v lstty >/dev/null 2>&1 && have_lstty=true

resolve_probe()
{
	current=$1
	nick=$2
	if [ -n "$current" ]; then
		echo "$current"
	elif [ "$have_lstty" = true ]; then
		lstty jlink get "$nick" 2>/dev/null
	fi
}

coletec_0_probe=$(resolve_probe "$coletec_0_probe" fake-coletec-0)
coletec_1_probe=$(resolve_probe "$coletec_1_probe" fake-coletec-1)
if [ "$flash_receptec" = true ]; then
	receptec_probe=$(resolve_probe "$receptec_probe" receptec)
fi

if [ -z "$coletec_0_probe" ] || [ -z "$coletec_1_probe" ]; then
	echo "No probe for a fake COLETEC: pass --fake-coletec-N-probe or nickname it with lstty." >&2
	exit 2
fi
if [ "$flash_receptec" = true ] && [ -z "$receptec_probe" ]; then
	echo "No probe for RECEPTEC: pass --receptec-probe, nickname it 'receptec' with lstty, or use --no-receptec." >&2
	exit 2
fi
case "$coletec_0_probe:$coletec_1_probe:$receptec_probe" in
*[!0-9:]*) echo "Probe serials must contain decimal digits only." >&2; exit 2 ;;
esac
if [ "$coletec_0_probe" = "$coletec_1_probe" ] ||
   { [ "$flash_receptec" = true ] &&
     { [ "$receptec_probe" = "$coletec_0_probe" ] || [ "$receptec_probe" = "$coletec_1_probe" ]; }; }; then
	echo "Every role needs a distinct probe serial." >&2
	exit 2
fi

# Refuse a probe whose registered target device does not match the role.
check_probe()
{
	serial=$1
	role=$2
	want=$3
	if [ "$have_lstty" = true ]; then
		info=$(lstty jlink get --serial "$serial" --json 2>/dev/null || true)
		case "$info" in
		*'"connected": true'*) ;;
		*) echo "J-Link $serial ($role) is not connected or has no lstty nickname." >&2; return 1 ;;
		esac
		device=$(lstty jlink get --serial "$serial" --field device 2>/dev/null || true)
		if [ -n "$device" ] && [ "$device" != "$want" ]; then
			echo "J-Link $serial is registered as $device, but $role needs $want." >&2
			return 1
		fi
		[ -n "$device" ] || echo "warning: lstty has no device recorded for $serial; cannot check it is $want." >&2
	else
		echo "warning: lstty not found; cannot check that $serial is a $want." >&2
	fi
}

command -v west >/dev/null 2>&1 || {
	echo "west was not found; activate the workspace environment first." >&2
	exit 1
}
if ! git -C "$app_dir" diff --quiet || ! git -C "$app_dir" diff --cached --quiet; then
	# A dirty tree is acceptable only when no file that goes into an image changed,
	# so the images still match the recorded commit. Docs and scripts do not.
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

verify_image()
{
	build_dir=$1
	expected_identity=$2
	instance=${expected_identity#COLETEC-}
	metadata="$build_dir/atletec-dwm-build.txt"
	config="$build_dir/zephyr/.config"

	[ -f "$build_dir/zephyr/zephyr.hex" ] && [ -f "$metadata" ] &&
		[ -f "$config" ] || {
		echo "Missing $expected_identity build; run scripts/build-both.sh." >&2
		return 1
	}
	grep -qx "role=fake-COLETEC" "$metadata" &&
	grep -qx "identity=$expected_identity" "$metadata" &&
	grep -qx "source_commit=$source_commit" "$metadata" &&
	grep -qx "contracts_commit=$contracts_commit" "$metadata" &&
	grep -qx "CONFIG_DWM_COLETEC_INSTANCE=$instance" "$config" || {
		echo "$expected_identity image metadata or configuration is stale." >&2
		return 1
	}
}

verify_receptec_image()
{
	metadata="$receptec_build/atletec-dwm-build.txt"

	[ -f "$receptec_build/zephyr/zephyr.hex" ] && [ -f "$metadata" ] || {
		echo "Missing RECEPTEC build in $receptec_build; run scripts/build-both.sh." >&2
		return 1
	}
	grep -qx "role=RECEPTEC" "$metadata" &&
	grep -qx "source_commit=$source_commit" "$metadata" &&
	grep -qx "contracts_commit=$contracts_commit" "$metadata" || {
		echo "RECEPTEC image metadata is stale (source or contracts commit differs)." >&2
		return 1
	}
	# The image must target the nRF52840, never a DWM board.
	grep -qx 'CONFIG_SOC_NRF52840=y' "$receptec_build/zephyr/.config" || {
		echo "$receptec_build is not an nRF52840 build; refusing to flash it as RECEPTEC." >&2
		return 1
	}
}

verify_image "$coletec_0_build" COLETEC-0 || exit 1
verify_image "$coletec_1_build" COLETEC-1 || exit 1
[ "$flash_receptec" = false ] || verify_receptec_image || exit 1

check_probe "$coletec_0_probe" fake-COLETEC-0 nRF52833_xxAA || exit 1
check_probe "$coletec_1_probe" fake-COLETEC-1 nRF52833_xxAA || exit 1
[ "$flash_receptec" = false ] || check_probe "$receptec_probe" RECEPTEC nRF52840_xxAA || exit 1

coletec_0_test_config=$(sed -n 's/^test_config=//p' "$coletec_0_build/atletec-dwm-build.txt")
coletec_1_test_config=$(sed -n 's/^test_config=//p' "$coletec_1_build/atletec-dwm-build.txt")
if [ -z "$coletec_0_test_config" ] ||
   [ "$coletec_0_test_config" != "$coletec_1_test_config" ]; then
	echo "Surrogate images do not share the same test configuration." >&2
	exit 1
fi

echo "Flash mapping:"
echo "  fake-COLETEC-0 image -> J-Link $coletec_0_probe"
echo "  fake-COLETEC-1 image -> J-Link $coletec_1_probe"
if [ "$flash_receptec" = true ]; then
	echo "  RECEPTEC image       -> J-Link $receptec_probe (real Type 2AB)"
else
	echo "  RECEPTEC             -> not flashed (--no-receptec)"
fi
echo "  Test config (fakes)  -> $coletec_0_test_config"
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
	printf 'Flash these boards concurrently? [y/N] '
	IFS= read -r answer
	case "$answer" in y|Y|yes|YES) ;; *) echo "Flash cancelled."; exit 1 ;; esac
fi

if [ "$flash_receptec" = true ] && [ "$backup_receptec" = true ]; then
	command -v lstty >/dev/null 2>&1 || {
		echo "lstty not found; cannot back up RECEPTEC before flashing." >&2
		exit 1
	}
	backup_dir="$app_dir/backups/real-receptec-$receptec_probe-$(date -u +%Y%m%dT%H%M%SZ)"
	mkdir -p "$backup_dir"
	# Flash (1 MiB) and UICR (4 KiB) of the nRF52840, read over SWD with JLinkExe.
	# Never nrfjprog: it opens a probe-selection window and blocks.
	if ! { lstty jlink dump "$receptec_probe" 0 0x100000 -o "$backup_dir/flash.bin" &&
		lstty jlink dump "$receptec_probe" 0x10001000 0x1000 -o "$backup_dir/uicr.bin"; } \
		>"$backup_dir/readback.log" 2>&1 || [ ! -s "$backup_dir/flash.bin" ]; then
		echo "RECEPTEC backup FAILED (see $backup_dir/readback.log); nothing flashed." >&2
		exit 1
	fi
	echo "RECEPTEC flash backed up to $backup_dir/flash.bin (+ uicr.bin)"
fi

west flash -d "$coletec_0_build" --runner jlink --dev-id "$coletec_0_probe" \
	>"$coletec_0_build/flash.log" 2>&1 &
coletec_0_pid=$!
west flash -d "$coletec_1_build" --runner jlink --dev-id "$coletec_1_probe" \
	>"$coletec_1_build/flash.log" 2>&1 &
coletec_1_pid=$!
receptec_pid=
if [ "$flash_receptec" = true ]; then
	west flash -d "$receptec_build" --runner jlink --dev-id "$receptec_probe" \
		>"$receptec_build/flash.log" 2>&1 &
	receptec_pid=$!
fi

coletec_0_status=0
coletec_1_status=0
receptec_status=0
wait "$coletec_0_pid" || coletec_0_status=$?
wait "$coletec_1_pid" || coletec_1_status=$?
if [ -n "$receptec_pid" ]; then
	wait "$receptec_pid" || receptec_status=$?
fi

[ "$coletec_0_status" -eq 0 ] && echo "fake-COLETEC-0 flash passed" || echo "fake-COLETEC-0 flash FAILED" >&2
[ "$coletec_1_status" -eq 0 ] && echo "fake-COLETEC-1 flash passed" || echo "fake-COLETEC-1 flash FAILED" >&2
if [ "$flash_receptec" = true ]; then
	[ "$receptec_status" -eq 0 ] && echo "RECEPTEC flash passed" || echo "RECEPTEC flash FAILED" >&2
fi
if [ "$have_lstty" = true ]; then
	[ "$coletec_0_status" -ne 0 ] || lstty jlink use "$coletec_0_probe" "fake-COLETEC-0 bench firmware ($source_commit)" >/dev/null 2>&1
	[ "$coletec_1_status" -ne 0 ] || lstty jlink use "$coletec_1_probe" "fake-COLETEC-1 bench firmware ($source_commit)" >/dev/null 2>&1
	[ "$flash_receptec" = false ] || [ "$receptec_status" -ne 0 ] ||
		lstty jlink use "$receptec_probe" "real RECEPTEC firmware ($source_commit)" >/dev/null 2>&1
fi
if [ "$coletec_0_status" -ne 0 ] || [ "$coletec_1_status" -ne 0 ] ||
   [ "$receptec_status" -ne 0 ]; then
	exit 1
fi
echo "All requested boards flashed successfully."
