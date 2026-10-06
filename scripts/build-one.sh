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

stm32_dir="$workspace_source/stm32n6_bringup_fw"
stm32_build_dir=${STM32_BUILD_DIR:-}
if [ -z "$stm32_build_dir" ]; then
	if [ -n "${ATLETEC_ZEPHYR_WORKSPACE:-}" ]; then
		stm32_build_dir="$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/stm32n6_bringup_fw/build-prod-coletec"
	else
		stm32_build_dir="$stm32_dir/build-prod-coletec"
	fi
fi

role=
test_config=none

usage()
{
	cat <<'EOF'
Usage: scripts/build-one.sh TARGET [--test-conf tests/SCENARIO.conf]

TARGET: fake-coletec-0 | fake-coletec-1 | fake-coletec-0-anchor | receptec | receptec-qorvo | receptec-qorvo-anchor | prod-coletec-qorvo | prod-coletec-stm32 | prod-coletec-stm32-prov

Builds one image. Each physical board has one canonical output directory. Output goes to $ATLETEC_BUILD_ROOT, else
$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec, else build/ in this
repository. $RECEPTEC_BUILD_DIR and $RECEPTEC_QORVO_BUILD_DIR override the
corresponding RECEPTEC output directories. --test-conf applies only to fake
COLETEC images. fake-coletec-0-anchor reconfigures the same
build-fake-coletec-0 directory used by fake-coletec-0, building the RECEPTEC
application on that board as fixed UWB anchor 1.
receptec-qorvo-anchor is a separate RECEPTEC-Qorvo physical board running
as an anchor, with RTT and its own build-coletec-qorvo-1 directory.
$RECEPTEC_QORVO_ANCHOR_BUILD_DIR overrides that directory.
--source-dir DIR builds prod-coletec-qorvo from that nrf52-bringup-fw
checkout (for example a codex/* worktree) instead of the sibling one.
prod-coletec-qorvo is the Nordic Rev4 product image
(decawave_dwm3001cdk/nrf52833, -DATLETEC_HW=ON -DATLETEC_HW_REV=4), built from
the sibling nrf52-bringup-fw checkout. Its output goes to
$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/nrf52-bringup-fw
(build-prod-coletec-qorvo); $PROD_COLETEC_QORVO_BUILD_DIR overrides it.
prod-coletec-stm32 builds the signed A/B application; prod-coletec-stm32-prov
builds the RAM-only provisioning shell. Both use the COLETEC physical-board
directory $STM32_BUILD_DIR (override: STM32_BUILD_DIR).
EOF
}

[ "$#" -ge 1 ] || { echo "Missing target." >&2; exit 2; }
case "$1" in
	fake-coletec-0|fake-coletec-1|fake-coletec-0-anchor|receptec|receptec-qorvo|receptec-qorvo-anchor|prod-coletec-qorvo|prod-coletec-stm32|prod-coletec-stm32-prov) role=$1; shift ;;
	-h|--help) usage; exit 0 ;;
	*)
		echo "Unknown target: $1" >&2
		echo "Targets: fake-coletec-0, fake-coletec-1, fake-coletec-0-anchor, receptec, receptec-qorvo, receptec-qorvo-anchor, prod-coletec-qorvo, prod-coletec-stm32, prod-coletec-stm32-prov" >&2
		exit 2
	;;
esac

while [ "$#" -gt 0 ]; do
	case "$1" in
		--test-conf)
			[ "$#" -ge 2 ] || { echo "Missing value for --test-conf." >&2; exit 2; }
			test_config=$2
			shift 2
			;;
		--source-dir)
			[ "$#" -ge 2 ] || { echo "Missing value for --source-dir." >&2; exit 2; }
			source_opt=$2
			shift 2
			;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown option: $1" >&2; exit 2 ;;
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
	receptec-qorvo-anchor)
		board=receptec_qorvo/nrf52833
		build_dir=${RECEPTEC_QORVO_ANCHOR_BUILD_DIR:-$build_root/build-coletec-qorvo-1}
		identity=1
		metadata_role=RECEPTEC-QORVO-ANCHOR
		;;
	prod-coletec-qorvo)
		board=decawave_dwm3001cdk/nrf52833
		build_dir=${PROD_COLETEC_QORVO_BUILD_DIR:-$nordic_build_root/build-prod-coletec-qorvo}
		identity=
		metadata_role=PROD-COLETEC-QORVO
		;;
	prod-coletec-stm32)
		build_dir=$stm32_build_dir
		metadata_role=PROD-COLETEC-STM32-APP
		;;
prod-coletec-stm32-prov)
		build_dir=$stm32_build_dir
		metadata_role=PROD-COLETEC-STM32-PROV
		;;
esac

if [ "$role" = prod-coletec-qorvo ] && [ ! -d "$nrf52_dir" ]; then
	echo "nrf52 checkout not found beside this repo: $nrf52_dir" >&2
	exit 1
fi

case "$role" in
prod-coletec-stm32|prod-coletec-stm32-prov)
	[ -d "$stm32_dir/scripts" ] || { echo "STM32 checkout not found: $stm32_dir" >&2; exit 1; }
	[ "$test_config" = none ] || { echo "--test-conf does not apply to STM32 targets." >&2; exit 2; }
	mkdir -p "$build_dir"
	stm32_log_root="${ATLETEC_ZEPHYR_WORKSPACE:-$HOME/zephyrproject}/build/atletec/logs/stm32n6_bringup_fw"
	mkdir -p "$stm32_log_root"
	if [ "$role" = prod-coletec-stm32 ]; then
		log_file="$stm32_log_root/prod-coletec-app-$(date -u +%Y%m%dT%H%M%SZ).log"
		if ! ATLETEC_BUILD_DIR="$build_dir" "$stm32_dir/scripts/build-main-emmc-image.sh" --pristine >"$log_file" 2>&1; then
			echo "$role build failed; log: $log_file" >&2
			tail -n 30 "$log_file" >&2
			exit 1
		fi
		[ -s "$build_dir/zephyr/zephyr.signed.bin" ] || { echo "Missing signed app image in $build_dir" >&2; exit 1; }
	else
		log_file="$stm32_log_root/prod-coletec-prov-$(date -u +%Y%m%dT%H%M%SZ).log"
		if ! ATLETEC_BUILD_DIR="$build_dir" "$stm32_dir/scripts/build-prov-image.sh" >"$log_file" 2>&1; then
			echo "$role build failed; log: $log_file" >&2
			tail -n 30 "$log_file" >&2
			exit 1
		fi
		[ -s "$build_dir/zephyr/zephyr.bin" ] || { echo "Missing provisioning shell image in $build_dir" >&2; exit 1; }
	fi
	stm32_worktree_hash=$(
		{
			git -C "$stm32_dir" diff HEAD --binary -- scripts stm32
			printf 'contracts=%s\n' "$(git -C "$stm32_dir/contracts" rev-parse HEAD)"
			git -C "$stm32_dir/contracts" diff HEAD --binary
			if [ -d "$stm32_dir/contracts/common/.git" ] || git -C "$stm32_dir/contracts/common" rev-parse --git-dir >/dev/null 2>&1; then
				printf 'common=%s\n' "$(git -C "$stm32_dir/contracts/common" rev-parse HEAD)"
				git -C "$stm32_dir/contracts/common" diff HEAD --binary
			fi
			for repo in "$stm32_dir" "$stm32_dir/contracts" "$stm32_dir/contracts/common"; do
				if git -C "$repo" rev-parse --git-dir >/dev/null 2>&1; then
					git -C "$repo" ls-files --others --exclude-standard | while IFS= read -r file; do
						[ -f "$repo/$file" ] && sha256sum -- "$repo/$file"
					done
				fi
			done
		} | sha256sum | awk '{print $1}'
	) || { echo "Could not fingerprint STM32 sources." >&2; exit 1; }
	{
		echo "role=$metadata_role"
		echo "source_commit=$(git -C "$stm32_dir" rev-parse HEAD)"
		echo "contracts_commit=$(git -C "$stm32_dir/contracts" rev-parse HEAD)"
		echo "worktree_hash=$stm32_worktree_hash"
	} >"$build_dir/atletec-stm32-build.txt"
	if [ "$role" = prod-coletec-stm32 ]; then artifact=zephyr.signed.bin; else artifact=zephyr.bin; fi
	echo "$role: $build_dir/zephyr/$artifact"
	echo "Build log: $log_file"
	echo "Source: $(git -C "$stm32_dir" rev-parse HEAD)"
	exit 0
;;
esac

mkdir -p "$build_dir"
log_dir="$build_root/logs"
mkdir -p "$log_dir"
log_file="$log_dir/build-$role.log"
echo "Building $role ($board) -> $build_dir"
if [ "$test_config" = none ]; then
	if [ "$role" = fake-coletec-0-anchor ] || [ "$role" = receptec-qorvo-anchor ]; then
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
	fake-coletec-0-anchor|receptec-qorvo-anchor)
		if [ "$role" = receptec-qorvo-anchor ]; then
			grep -qx 'CONFIG_BOARD_RECEPTEC_QORVO=y' "$config" &&
			grep -qx 'CONFIG_SHELL_BACKEND_RTT=y' "$config" || exit 1
		fi
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
		fake-coletec-0-anchor|receptec-qorvo-anchor)
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
