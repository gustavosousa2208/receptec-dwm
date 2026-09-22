#!/bin/sh

set -u

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH='' cd -- "$script_dir/.." && pwd)
receptec_build="$app_dir/build-receptec"
coletec_build="$app_dir/build-coletec"
test_config=none

usage()
{
	cat <<'EOF'
Usage: scripts/build-both.sh [--test-conf tests/SCENARIO.conf]

Build the explicit RECEPTEC and COLETEC images concurrently. With --test-conf,
apply the same bounded repository-owned fault scenario to both role builds.
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
	--test-conf)
		[ "$#" -ge 2 ] || { echo "Missing test configuration" >&2; exit 2; }
		test_config=$2
		shift 2
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

receptec_config=
coletec_config=
if [ "$test_config" != none ]; then
	case "$test_config" in
	tests/*.conf) ;;
	*) echo "Test configuration must be a tests/*.conf path." >&2; exit 2 ;;
	esac
	if [ ! -f "$app_dir/$test_config" ]; then
		echo "Test configuration not found: $test_config" >&2
		exit 2
	fi
	receptec_config="$app_dir/$test_config"
	coletec_config="$app_dir/$test_config"
fi

if ! command -v west >/dev/null 2>&1; then
	echo "west was not found; activate the workspace environment first." >&2
	exit 1
fi

build_role()
{
	role=$1
	board=$2
	build_dir=$3
	config_file=$4
	log_file=$5

	if [ -n "$config_file" ]; then
		west build -p auto -b "$board" -d "$build_dir" "$app_dir" -- \
			-DEXTRA_CONF_FILE="$config_file" >"$log_file" 2>&1
	else
		west build -p auto -b "$board" -d "$build_dir" "$app_dir" \
			>"$log_file" 2>&1
	fi
}

mkdir -p "$receptec_build" "$coletec_build"
receptec_log="$receptec_build/build.log"
coletec_log="$coletec_build/build.log"

echo "Building RECEPTEC and COLETEC images concurrently"
build_role RECEPTEC receptec "$receptec_build" "$receptec_config" \
	"$receptec_log" &
receptec_pid=$!
build_role COLETEC coletec "$coletec_build" "$coletec_config" \
	"$coletec_log" &
coletec_pid=$!

receptec_status=0
coletec_status=0
wait "$receptec_pid" || receptec_status=$?
wait "$coletec_pid" || coletec_status=$?

if [ "$receptec_status" -ne 0 ]; then
	echo "RECEPTEC build failed; log: $receptec_log" >&2
fi
if [ "$coletec_status" -ne 0 ]; then
	echo "COLETEC build failed; log: $coletec_log" >&2
fi
if [ "$receptec_status" -ne 0 ] || [ "$coletec_status" -ne 0 ]; then
	exit 1
fi

verify_role()
{
	build_dir=$1
	expected=$2
	unexpected=$3
	config="$build_dir/zephyr/.config"

	if [ ! -f "$build_dir/zephyr/zephyr.hex" ] || [ ! -f "$config" ]; then
		echo "Incomplete build output: $build_dir" >&2
		return 1
	fi
	if ! grep -qx "CONFIG_DWM_UWB_ROLE_${expected}=y" "$config"; then
		echo "$build_dir does not select $expected" >&2
		return 1
	fi
	if grep -qx "CONFIG_DWM_UWB_ROLE_${unexpected}=y" "$config"; then
		echo "$build_dir also selects $unexpected" >&2
		return 1
	fi
}

verify_role "$receptec_build" RECEPTEC COLETEC
verify_role "$coletec_build" COLETEC RECEPTEC

source_commit=$(git -C "$app_dir" rev-parse HEAD)
contracts_commit=$(git -C "$app_dir/contracts" rev-parse HEAD)
for build_dir in "$receptec_build" "$coletec_build"; do
	role=COLETEC
	if [ "$build_dir" = "$receptec_build" ]; then
		role=RECEPTEC
	fi
	{
		echo "role=$role"
		echo "source_commit=$source_commit"
		echo "contracts_commit=$contracts_commit"
		echo "test_config=$test_config"
	} >"$build_dir/atletec-dwm-build.txt"
done

echo "RECEPTEC: $receptec_build/zephyr/zephyr.hex"
echo "COLETEC:  $coletec_build/zephyr/zephyr.hex"
echo "Source:   $source_commit"
echo "Contracts: $contracts_commit"
echo "Test config: $test_config"
