#!/bin/sh

set -u

# Load the shared Zephyr environment when the caller has not done so.
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
if [ -z "$build_root" ]; then
	build_root="$app_dir/build"
fi

# Nordic roles are built from the sibling nrf52-bringup-fw checkout, so their
# output belongs with the other Nordic build trees, not with the receptec ones.
workspace_source=$(CDPATH='' cd -- "$app_dir/.." && pwd)
nrf52_dir="$workspace_source/nrf52-bringup-fw"
nordic_build_root=
if [ -n "${ATLETEC_ZEPHYR_WORKSPACE:-}" ]; then
	nordic_build_root="$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/nrf52-bringup-fw"
fi
if [ -z "$nordic_build_root" ]; then
	nordic_build_root="$nrf52_dir/build"
fi

role=
test_config=none

usage()
{
	cat <<'EOF'
Usage: scripts/build-one.sh TARGET [--test-conf tests/SCENARIO.conf]

TARGET: fake-coletec-0 | fake-coletec-1 | fake-coletec-0-anchor | receptec | receptec-qorvo | prod-coletec-qorvo

Builds one image. Each physical board has one canonical output directory. Output goes to $ATLETEC_BUILD_ROOT, else
$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec, else build/ in this
repository. $RECEPTEC_BUILD_DIR and $RECEPTEC_QORVO_BUILD_DIR override the
corresponding RECEPTEC output directories. --test-conf applies only to fake
COLETEC images. fake-coletec-0-anchor reconfigures the same
build-fake-coletec-0 directory used by fake-coletec-0, building the RECEPTEC
application on that board as fixed UWB anchor 1.
--source-dir DIR builds prod-coletec-qorvo from that nrf52-bringup-fw
checkout (for example a codex/* worktree) instead of the sibling one.
prod-coletec-qorvo is the Nordic Rev4 product image
(decawave_dwm3001cdk/nrf52833, -DATLETEC_HW=ON -DATLETEC_HW_REV=4), built from
the sibling nrf52-bringup-fw checkout. Its output goes to
$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/nrf52-bringup-fw
(build-prod-coletec-qorvo); $PROD_COLETEC_QORVO_BUILD_DIR overrides it.
EOF
}

[ "$#" -ge 1 ] || { usage >&2; exit 2; }
case "$1" in
	fake-coletec-0|fake-coletec-1|fake-coletec-0-anchor|receptec|receptec-qorvo|prod-coletec-qorvo) role=$1; shift ;;
	-h|--help) usage; exit 0 ;;
	*) echo "Unknown target: $1" >&2; usage >&2; exit 2 ;;
esac

while [ "$#" -gt 0 ]; do
	case "$1" in
		--test-conf)
			[ "$#" -ge 2 ] || { echo "Missing test configuration" >&2; exit 2; }
			test_config=$2
			shift 2
			;;
		--source-dir)
			[ "$#" -ge 2 ] || { echo "Missing source directory" >&2; exit 2; }
			source_opt=$2
			shift 2
			;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

if [ -n "${source_opt:-}" ]; then
	[ "$role" = prod-coletec-qorvo ] || { echo "--source-dir applies to prod-coletec-qorvo only." >&2; exit 2; }
	nrf52_dir=$(CDPATH='' cd -- "$source_opt" 2>/dev/null && pwd) || {
		echo "--source-dir not found: $source_opt" >&2
		exit 2
	}
fi

if [ "$test_config" != none ]; then
	case "$test_config" in tests/*.conf) ;; *)
		echo "Test configuration must be a tests/*.conf path." >&2
		exit 2
	;; esac
	if [ ! -f "$app_dir/$test_config" ]; then
		echo "Test configuration not found: $test_config" >&2
		exit 2
	fi
	case "$role" in fake-coletec-0|fake-coletec-1) ;; *)
		echo "--test-conf applies only to fake COLETEC targets." >&2
		exit 2
	;; esac
fi

if ! command -v west >/dev/null 2>&1; then
	echo "west was not found; activate the workspace environment first." >&2
	exit 1
fi

case "$role" in
	fake-coletec-0)
		board=fake_coletec_0/nrf52833
		build_dir="$build_root/build-fake-coletec-0"
		identity=0
		metadata_role=fake-COLETEC
		;;
	fake-coletec-0-anchor)
		board=fake_coletec_0/nrf52833
		build_dir="$build_root/build-fake-coletec-0"
		identity=1
		metadata_role=RECEPTEC-Qorvo-anchor-1
		;;
	fake-coletec-1)
		board=fake_coletec_1/nrf52833
		build_dir="$build_root/build-fake-coletec-1"
		identity=1
		metadata_role=fake-COLETEC
		;;
	receptec)
		board=receptec/nrf52840
		build_dir=${RECEPTEC_BUILD_DIR:-$build_root/build-receptec}
		identity=
		metadata_role=RECEPTEC
		;;
	receptec-qorvo)
		board=receptec_qorvo/nrf52833
		build_dir=${RECEPTEC_QORVO_BUILD_DIR:-$build_root/build-receptec-qorvo}
		identity=
		metadata_role=RECEPTEC-QORVO
		;;
	prod-coletec-qorvo)
		board=decawave_dwm3001cdk/nrf52833
		build_dir=${PROD_COLETEC_QORVO_BUILD_DIR:-$nordic_build_root/build-prod-coletec-qorvo}
		identity=
		metadata_role=PROD-COLETEC-QORVO
		;;
esac

if [ "$role" = prod-coletec-qorvo ] && [ ! -d "$nrf52_dir" ]; then
	echo "nrf52 checkout not found beside this repo: $nrf52_dir" >&2
	exit 1
fi

mkdir -p "$build_dir"
log_file="$build_dir/build.log"
echo "Building $role ($board) -> $build_dir"
if [ "$test_config" = none ]; then
	if [ "$role" = fake-coletec-0-anchor ]; then
		if ! west build -p always -b "$board" -d "$build_dir" "$app_dir" -- \
			-DATLETEC_FIXED_ANCHOR=ON >"$log_file" 2>&1; then
			echo "$role build failed; log: $log_file" >&2
			exit 1
		fi
	elif [ "$role" = prod-coletec-qorvo ]; then
		if ! west build -p always -b "$board" -d "$build_dir" "$nrf52_dir" -- \
			-DATLETEC_HW=ON -DATLETEC_HW_REV=4 >"$log_file" 2>&1; then
			echo "$role build failed; log: $log_file" >&2
			exit 1
		fi
	elif ! west build -p always -b "$board" -d "$build_dir" "$app_dir" >"$log_file" 2>&1; then
		echo "$role build failed; log: $log_file" >&2
		exit 1
	fi
else
	if ! west build -p always -b "$board" -d "$build_dir" "$app_dir" -- \
		-DEXTRA_CONF_FILE="$app_dir/$test_config" >"$log_file" 2>&1; then
		echo "$role build failed; log: $log_file" >&2
		exit 1
	fi
fi

config="$build_dir/zephyr/.config"
if [ ! -f "$build_dir/zephyr/zephyr.hex" ] || [ ! -f "$config" ]; then
	echo "Incomplete build output: $build_dir (see $log_file)" >&2
	exit 1
fi
case "$role" in
	fake-coletec-0|fake-coletec-1)
		if ! grep -qx "CONFIG_DWM_COLETEC_INSTANCE=$identity" "$config" ||
		   ! grep -qx 'CONFIG_SHELL_BACKEND_SERIAL=y' "$config" ||
		   ! grep -qx 'CONFIG_UART_CONSOLE=y' "$config" ||
		   grep -qx 'CONFIG_SHELL_BACKEND_RTT=y' "$config"; then
			echo "$build_dir does not select COLETEC-$identity (see $log_file)" >&2
			exit 1
		fi
		;;
	fake-coletec-0-anchor)
		grep -qx 'CONFIG_RECEPTEC_UWB_RADIO=y' "$config" &&
		grep -qx 'CONFIG_RECEPTEC_FIXED_ANCHOR_ID=1' "$config" &&
		grep -qx 'CONFIG_SOC_NRF52833=y' "$config" || {
			echo "$build_dir is not a fixed anchor 1 build (see $log_file)" >&2
			exit 1
		}
		;;
	receptec)
		grep -qx 'CONFIG_SOC_NRF52840=y' "$config" || {
			echo "$build_dir is not a nRF52840 RECEPTEC build (see $log_file)" >&2
			exit 1
		}
		;;
	receptec-qorvo)
		grep -qx 'CONFIG_BOARD_RECEPTEC_QORVO=y' "$config" &&
		grep -qx 'CONFIG_SOC_NRF52833=y' "$config" || {
			echo "$build_dir is not a RECEPTEC-Qorvo build (see $log_file)" >&2
			exit 1
		}
		;;
	prod-coletec-qorvo)
		grep -qx 'CONFIG_SOC_NRF52833=y' "$config" &&
		grep -qx 'CONFIG_BT_DIS_HW_REV_STR="Rev4"' "$config" || {
			echo "$build_dir is not a Rev4 PROD-COLETEC build (see $log_file)" >&2
			exit 1
		}
		;;
esac

case "$role" in
prod-coletec-qorvo) fw_src=$nrf52_dir ;;
*) fw_src=$app_dir ;;
esac
source_commit=$(git -C "$fw_src" rev-parse HEAD)
contracts_commit=$(git -C "$fw_src/contracts" rev-parse HEAD)
{
	echo "role=$metadata_role"
	case "$role" in
		fake-coletec-0|fake-coletec-1)
			echo "identity=COLETEC-$identity"
			;;
		fake-coletec-0-anchor)
			echo "anchor_id=1"
			;;
	esac
	echo "source_commit=$source_commit"
	echo "contracts_commit=$contracts_commit"
	echo "test_config=$test_config"
} >"$build_dir/atletec-dwm-build.txt"

echo "$role: $build_dir/zephyr/zephyr.hex"
echo "Build log: $log_file"
echo "Source: $source_commit"
echo "Contracts: $contracts_commit"
echo "Test config: $test_config"
