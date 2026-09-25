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
build_receptec=true
test_config=none

usage()
{
	cat <<'EOF'
Usage: scripts/build-both.sh [--test-conf tests/SCENARIO.conf] [--no-receptec]

Build the explicit fake-COLETEC-0 and fake-COLETEC-1 surrogate images and the
real RECEPTEC (receptec/nrf52840) image concurrently. With --test-conf, apply
the same bounded repository-owned fault scenario to both fake COLETECs; the
RECEPTEC image never takes a test configuration. --no-receptec skips it.

Output goes to $ATLETEC_BUILD_ROOT, else
$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec (fake-coletec-0, fake-coletec-1,
receptec-board), else build-*/ in this repository. $RECEPTEC_BUILD_DIR overrides
only the RECEPTEC directory.
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
	--test-conf)
		[ "$#" -ge 2 ] || { echo "Missing test configuration" >&2; exit 2; }
		test_config=$2
		shift 2
		;;
	--no-receptec)
		build_receptec=false
		shift
		;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		echo "Unknown option: $1" >&2
		usage >&2
		exit 2
		;;
	esac
done

coletec_0_config=
coletec_1_config=
if [ "$test_config" != none ]; then
	case "$test_config" in
	tests/*.conf) ;;
	*) echo "Test configuration must be a tests/*.conf path." >&2; exit 2 ;;
	esac
	if [ ! -f "$app_dir/$test_config" ]; then
		echo "Test configuration not found: $test_config" >&2
		exit 2
	fi
	coletec_0_config="$app_dir/$test_config"
	coletec_1_config="$app_dir/$test_config"
fi

if ! command -v west >/dev/null 2>&1; then
	echo "west was not found; activate the workspace environment first." >&2
	exit 1
fi

build_role()
{
	board=$1
	build_dir=$2
	config_file=$3
	log_file=$4

	if [ -n "$config_file" ]; then
		west build -p auto -b "$board" -d "$build_dir" "$app_dir" -- \
			-DEXTRA_CONF_FILE="$config_file" >"$log_file" 2>&1
	else
		west build -p auto -b "$board" -d "$build_dir" "$app_dir" \
			>"$log_file" 2>&1
	fi
}

mkdir -p "$coletec_0_build" "$coletec_1_build"
coletec_0_log="$coletec_0_build/build.log"
coletec_1_log="$coletec_1_build/build.log"

echo "Building fake-COLETEC-0 and fake-COLETEC-1 surrogate images concurrently"
build_role fake_coletec_0/nrf52833 "$coletec_0_build" "$coletec_0_config" \
	"$coletec_0_log" &
coletec_0_pid=$!
build_role fake_coletec_1/nrf52833 "$coletec_1_build" "$coletec_1_config" \
	"$coletec_1_log" &
coletec_1_pid=$!

receptec_pid=
if [ "$build_receptec" = true ]; then
	mkdir -p "$receptec_build"
	receptec_log="$receptec_build/build.log"
	echo "Building the real RECEPTEC image (receptec/nrf52840) -> $receptec_build"
	west build -p auto -b receptec/nrf52840 -d "$receptec_build" \
		"$app_dir/firmware/receptec" >"$receptec_log" 2>&1 &
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

if [ "$coletec_0_status" -ne 0 ]; then
	echo "fake-COLETEC-0 build failed; log: $coletec_0_log" >&2
fi
if [ "$coletec_1_status" -ne 0 ]; then
	echo "fake-COLETEC-1 build failed; log: $coletec_1_log" >&2
fi
if [ "$receptec_status" -ne 0 ]; then
	echo "RECEPTEC build failed; log: $receptec_log" >&2
fi
if [ "$coletec_0_status" -ne 0 ] || [ "$coletec_1_status" -ne 0 ] ||
   [ "$receptec_status" -ne 0 ]; then
	exit 1
fi

verify_identity()
{
	build_dir=$1
	expected=$2
	config="$build_dir/zephyr/.config"

	if [ ! -f "$build_dir/zephyr/zephyr.hex" ] || [ ! -f "$config" ]; then
		echo "Incomplete build output: $build_dir" >&2
		return 1
	fi
	if ! grep -qx "CONFIG_DWM_COLETEC_INSTANCE=$expected" "$config"; then
		echo "$build_dir does not select COLETEC-$expected" >&2
		return 1
	fi
}

verify_identity "$coletec_0_build" 0
verify_identity "$coletec_1_build" 1
if [ "$build_receptec" = true ] && [ ! -f "$receptec_build/zephyr/zephyr.hex" ]; then
	echo "Incomplete build output: $receptec_build" >&2
	exit 1
fi

source_commit=$(git -C "$app_dir" rev-parse HEAD)
contracts_commit=$(git -C "$app_dir/contracts" rev-parse HEAD)
for build_dir in "$coletec_0_build" "$coletec_1_build"; do
	identity=1
	if [ "$build_dir" = "$coletec_0_build" ]; then
		identity=0
	fi
	{
		echo "role=fake-COLETEC"
		echo "identity=COLETEC-$identity"
		echo "source_commit=$source_commit"
		echo "contracts_commit=$contracts_commit"
		echo "test_config=$test_config"
	} >"$build_dir/atletec-dwm-build.txt"
done
if [ "$build_receptec" = true ]; then
	{
		echo "role=RECEPTEC"
		echo "source_commit=$source_commit"
		echo "contracts_commit=$contracts_commit"
		echo "test_config=none"
	} >"$receptec_build/atletec-dwm-build.txt"
fi

echo "fake-COLETEC-0: $coletec_0_build/zephyr/zephyr.hex"
echo "fake-COLETEC-1: $coletec_1_build/zephyr/zephyr.hex"
if [ "$build_receptec" = true ]; then
	echo "RECEPTEC:       $receptec_build/zephyr/zephyr.hex"
fi
echo "Source:   $source_commit"
echo "Contracts: $contracts_commit"
echo "Test config: $test_config"
