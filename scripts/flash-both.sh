#!/bin/sh
set -u

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH='' cd -- "$script_dir/.." && pwd)
coletec_0_build="$app_dir/build-fake-coletec-0"
coletec_1_build="$app_dir/build-fake-coletec-1"
coletec_0_probe=760216755
coletec_1_probe=760216737
assume_yes=false

usage()
{
	cat <<'EOF'
Usage: scripts/flash-both.sh [options]

Options:
  --fake-coletec-0-probe SERIAL  fake-COLETEC-0 J-Link serial (default: 760216755)
  --fake-coletec-1-probe SERIAL  fake-COLETEC-1 J-Link serial (default: 760216737)
  --yes                     Skip the interactive mapping confirmation
  -h, --help                Show this help
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
	--fake-coletec-0-probe) [ "$#" -ge 2 ] || exit 2; coletec_0_probe=$2; shift 2 ;;
	--fake-coletec-1-probe) [ "$#" -ge 2 ] || exit 2; coletec_1_probe=$2; shift 2 ;;
	--yes) assume_yes=true; shift ;;
	-h|--help) usage; exit 0 ;;
	*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

case "$coletec_0_probe:$coletec_1_probe" in
*[!0-9:]*|:*) echo "Probe serials must contain decimal digits only." >&2; exit 2 ;;
esac
if [ -z "$coletec_0_probe" ] || [ -z "$coletec_1_probe" ] ||
   [ "$coletec_0_probe" = "$coletec_1_probe" ]; then
	echo "fake-COLETEC-0 and fake-COLETEC-1 require distinct probe serials." >&2
	exit 2
fi
command -v west >/dev/null 2>&1 || {
	echo "west was not found; activate the workspace environment first." >&2
	exit 1
}
if ! git -C "$app_dir" diff --quiet || ! git -C "$app_dir" diff --cached --quiet; then
	echo "Working tree has tracked changes; rebuild and commit before flashing." >&2
	exit 1
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

verify_image "$coletec_0_build" COLETEC-0
verify_image "$coletec_1_build" COLETEC-1

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
echo "  Test config     -> $coletec_0_test_config"
if [ "$assume_yes" = false ]; then
	printf 'Flash both COLETEC surrogates concurrently? [y/N] '
	IFS= read -r answer
	case "$answer" in y|Y|yes|YES) ;; *) echo "Flash cancelled."; exit 1 ;; esac
fi

west flash -d "$coletec_0_build" --runner jlink --dev-id "$coletec_0_probe" \
	>"$coletec_0_build/flash.log" 2>&1 &
coletec_0_pid=$!
west flash -d "$coletec_1_build" --runner jlink --dev-id "$coletec_1_probe" \
	>"$coletec_1_build/flash.log" 2>&1 &
coletec_1_pid=$!

coletec_0_status=0
coletec_1_status=0
wait "$coletec_0_pid" || coletec_0_status=$?
wait "$coletec_1_pid" || coletec_1_status=$?

[ "$coletec_0_status" -eq 0 ] && echo "fake-COLETEC-0 flash passed" || echo "fake-COLETEC-0 flash FAILED" >&2
[ "$coletec_1_status" -eq 0 ] && echo "fake-COLETEC-1 flash passed" || echo "fake-COLETEC-1 flash FAILED" >&2
if [ "$coletec_0_status" -ne 0 ] || [ "$coletec_1_status" -ne 0 ]; then
	exit 1
fi
echo "Both fake-COLETEC surrogate images flashed successfully."
