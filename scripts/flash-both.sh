#!/bin/sh

set -u

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH='' cd -- "$script_dir/.." && pwd)
receptec_build="$app_dir/build-receptec"
coletec_build="$app_dir/build-coletec"
receptec_probe=760216755
coletec_probe=760216737
assume_yes=false

usage()
{
	cat <<'EOF'
Usage: scripts/flash-both.sh [options]

Options:
  --receptec-probe SERIAL  RECEPTEC J-Link serial (default: 760216755)
  --coletec-probe SERIAL   COLETEC J-Link serial (default: 760216737)
  --yes                    Skip the interactive mapping confirmation
  -h, --help               Show this help
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
	--receptec-probe)
		[ "$#" -ge 2 ] || { echo "Missing RECEPTEC probe serial" >&2; exit 2; }
		receptec_probe=$2
		shift 2
		;;
	--coletec-probe)
		[ "$#" -ge 2 ] || { echo "Missing COLETEC probe serial" >&2; exit 2; }
		coletec_probe=$2
		shift 2
		;;
	--yes)
		assume_yes=true
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

case "$receptec_probe:$coletec_probe" in
*[!0-9:]*|:*)
	echo "Probe serials must contain decimal digits only." >&2
	exit 2
	;;
esac
if [ "$receptec_probe" = "$coletec_probe" ]; then
	echo "RECEPTEC and COLETEC probe serials must differ." >&2
	exit 2
fi
if [ -z "$receptec_probe" ] || [ -z "$coletec_probe" ]; then
	echo "Both probe serials are required." >&2
	exit 2
fi
if ! command -v west >/dev/null 2>&1; then
	echo "west was not found; activate the workspace environment first." >&2
	exit 1
fi
if ! git -C "$app_dir" diff --quiet || ! git -C "$app_dir" diff --cached --quiet; then
	echo "Working tree has tracked changes; rebuild and commit before flashing." >&2
	exit 1
fi

source_commit=$(git -C "$app_dir" rev-parse HEAD)
contracts_commit=$(git -C "$app_dir/contracts" rev-parse HEAD)

verify_image()
{
	build_dir=$1
	expected_role=$2
	metadata="$build_dir/atletec-dwm-build.txt"
	config="$build_dir/zephyr/.config"

	if [ ! -f "$build_dir/zephyr/zephyr.hex" ] || [ ! -f "$metadata" ] ||
	   [ ! -f "$config" ]; then
		echo "Missing $expected_role build; run scripts/build-both.sh." >&2
		return 1
	fi
	grep -qx "role=$expected_role" "$metadata" || {
		echo "$build_dir metadata does not identify $expected_role." >&2
		return 1
	}
	grep -qx "source_commit=$source_commit" "$metadata" || {
		echo "$expected_role image is stale for the current source commit." >&2
		return 1
	}
	grep -qx "contracts_commit=$contracts_commit" "$metadata" || {
		echo "$expected_role image uses a stale contracts pin." >&2
		return 1
	}
	grep -qx "CONFIG_DWM_UWB_ROLE_${expected_role}=y" "$config" || {
		echo "$expected_role image has the wrong Kconfig role." >&2
		return 1
	}
}

verify_image "$receptec_build" RECEPTEC
verify_image "$coletec_build" COLETEC

receptec_test_config=$(sed -n 's/^test_config=//p' \
	"$receptec_build/atletec-dwm-build.txt")
coletec_test_config=$(sed -n 's/^test_config=//p' \
	"$coletec_build/atletec-dwm-build.txt")
if [ -z "$receptec_test_config" ] ||
   [ "$receptec_test_config" != "$coletec_test_config" ]; then
	echo "Role images do not share the same recorded test configuration." >&2
	exit 1
fi

echo "Flash mapping:"
echo "  RECEPTEC image -> J-Link $receptec_probe"
echo "  COLETEC image  -> J-Link $coletec_probe"
echo "  Test config    -> $receptec_test_config"
if [ "$assume_yes" = false ]; then
	printf 'Flash both boards concurrently? [y/N] '
	IFS= read -r answer
	case "$answer" in
	y|Y|yes|YES) ;;
	*) echo "Flash cancelled."; exit 1 ;;
	esac
fi

flash_role()
{
	role=$1
	build_dir=$2
	probe=$3
	log_file=$4

	west flash -d "$build_dir" --runner jlink --dev-id "$probe" \
		>"$log_file" 2>&1
}

receptec_log="$receptec_build/flash.log"
coletec_log="$coletec_build/flash.log"
flash_role RECEPTEC "$receptec_build" "$receptec_probe" "$receptec_log" &
receptec_pid=$!
flash_role COLETEC "$coletec_build" "$coletec_probe" "$coletec_log" &
coletec_pid=$!

receptec_status=0
coletec_status=0
wait "$receptec_pid" || receptec_status=$?
wait "$coletec_pid" || coletec_status=$?

if [ "$receptec_status" -eq 0 ]; then
	echo "RECEPTEC flash passed: J-Link $receptec_probe"
else
	echo "RECEPTEC flash FAILED: J-Link $receptec_probe; log: $receptec_log" >&2
fi
if [ "$coletec_status" -eq 0 ]; then
	echo "COLETEC flash passed: J-Link $coletec_probe"
else
	echo "COLETEC flash FAILED: J-Link $coletec_probe; log: $coletec_log" >&2
fi

if [ "$receptec_status" -ne 0 ] || [ "$coletec_status" -ne 0 ]; then
	echo "Partial flash result; inspect both logs before retrying." >&2
	exit 1
fi

echo "Both role-specific images flashed successfully."
