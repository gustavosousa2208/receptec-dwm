#!/bin/sh

set -u

# Load the shared Zephyr environment when the caller has not done so.
if [ -z "${ATLETEC_ZEPHYR_WORKSPACE:-}" ] && [ -f "$HOME/source/atletec/.envrc" ]; then
	. "$HOME/source/atletec/.envrc"
fi

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH='' cd -- "$script_dir/.." && pwd)
build_root=${ATLETEC_BUILD_ROOT:-}
if [ -z "$build_root" ] && [ -n "${ATLETEC_ZEPHYR_WORKSPACE:-}" ]; then
	build_root="$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec"
fi
if [ -z "$build_root" ]; then
	build_root="$app_dir/build"
fi

role=
test_config=none

usage()
{
	cat <<'EOF'
Usage: scripts/build-one.sh TARGET [--test-conf tests/SCENARIO.conf]

TARGET: fake-coletec-0 | fake-coletec-1 | receptec | receptec-qorvo

Builds one image. Output goes to $ATLETEC_BUILD_ROOT, else
$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec, else build/ in this
repository. $RECEPTEC_BUILD_DIR and $RECEPTEC_QORVO_BUILD_DIR override the
corresponding RECEPTEC output directories. --test-conf applies only to fake
COLETEC images.
EOF
}

[ "$#" -ge 1 ] || { usage >&2; exit 2; }
case "$1" in
	fake-coletec-0|fake-coletec-1|receptec|receptec-qorvo) role=$1; shift ;;
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
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

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
		build_dir="$build_root/fake-coletec-0"
		identity=0
		metadata_role=fake-COLETEC
		;;
	fake-coletec-1)
		board=fake_coletec_1/nrf52833
		build_dir="$build_root/fake-coletec-1"
		identity=1
		metadata_role=fake-COLETEC
		;;
	receptec)
		board=receptec/nrf52840
		build_dir=${RECEPTEC_BUILD_DIR:-$build_root/receptec-board}
		identity=
		metadata_role=RECEPTEC
		;;
	receptec-qorvo)
		board=receptec_qorvo/nrf52833
		build_dir=${RECEPTEC_QORVO_BUILD_DIR:-$build_root/receptec-qorvo}
		identity=
		metadata_role=RECEPTEC-QORVO
		;;
esac

mkdir -p "$build_dir"
log_file="$build_dir/build.log"
echo "Building $role ($board) -> $build_dir"
if [ "$test_config" = none ]; then
	if ! west build -p auto -b "$board" -d "$build_dir" "$app_dir" >"$log_file" 2>&1; then
		echo "$role build failed; log: $log_file" >&2
		exit 1
	fi
else
	if ! west build -p auto -b "$board" -d "$build_dir" "$app_dir" -- \
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
		if ! grep -qx "CONFIG_DWM_COLETEC_INSTANCE=$identity" "$config"; then
			echo "$build_dir does not select COLETEC-$identity (see $log_file)" >&2
			exit 1
		fi
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
esac

source_commit=$(git -C "$app_dir" rev-parse HEAD)
contracts_commit=$(git -C "$app_dir/contracts" rev-parse HEAD)
{
	echo "role=$metadata_role"
	if [ -n "$identity" ]; then echo "identity=COLETEC-$identity"; fi
	echo "source_commit=$source_commit"
	echo "contracts_commit=$contracts_commit"
	echo "test_config=$test_config"
} >"$build_dir/atletec-dwm-build.txt"

echo "$role: $build_dir/zephyr/zephyr.hex"
echo "Build log: $log_file"
echo "Source: $source_commit"
echo "Contracts: $contracts_commit"
echo "Test config: $test_config"
