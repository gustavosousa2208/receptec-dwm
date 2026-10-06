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

stm32_dir="$workspace_source/stm32n6_bringup_fw"
if [ -n "${ATLETEC_ZEPHYR_WORKSPACE:-}" ]; then
	default_stm32_build="$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/stm32n6_bringup_fw/build-prod-coletec"
else
	default_stm32_build="$stm32_dir/build-prod-coletec"
fi
stm32_build=${STM32_BUILD_DIR:-$default_stm32_build}

probe_opt=
assume_yes=false
dry_run=false
backup=false
allow_dirty=false
force_dirty=false
stlink=false
mculink=false
source_opt=
role=

usage()
{
	cat <<'EOF'
Usage: scripts/flash-one.sh ROLE [options]

ROLE: fake-coletec-0 | fake-coletec-0-anchor | fake-coletec-1 | receptec | receptec-qorvo | receptec-qorvo-anchor | prod-coletec-qorvo | prod-coletec-stm32 | prod-coletec-stm32-prov

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
                   Probe serial or lstty nickname for this board (J-Link, or
                   STM32 ST-Link for prod-coletec-stm32*)
  --yes            skip the interactive confirmation (also automatic when
                   stdin is not a tty, e.g. under an agent, or when
                   ATLETEC_NONINTERACTIVE is set)
  --dry-run        run every check, print the plan, flash nothing
  --backup         read the RECEPTEC flash back to backups/ first (receptec only)
  --allow-dirty    accept uncommitted changes outside the image sources
  --force-dirty    STM32 only: allow built dirty sources when their fingerprint
                   still matches the current worktree
  --mculink        flash receptec-qorvo-anchor through MCU-Link (pyOCD);
                   --probe accepts its serial or lstty mculink nickname
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

[ "$#" -ge 1 ] || { echo "Missing role." >&2; exit 2; }
case "$1" in
fake-coletec-0|fake-coletec-0-anchor|fake-coletec-1|receptec|receptec-qorvo|receptec-qorvo-anchor|prod-coletec-qorvo|prod-coletec-stm32|prod-coletec-stm32-prov) role=$1; shift ;;
-h|--help) usage; exit 0 ;;
*)
	echo "Unknown role: $1" >&2
	echo "Roles: fake-coletec-0, fake-coletec-0-anchor, fake-coletec-1, receptec, receptec-qorvo, receptec-qorvo-anchor, prod-coletec-qorvo, prod-coletec-stm32, prod-coletec-stm32-prov" >&2
	exit 2
;;
esac

while [ "$#" -gt 0 ]; do
	case "$1" in
	--probe) [ "$#" -ge 2 ] || { echo "Missing value for --probe." >&2; exit 2; }; probe_opt=$2; shift 2 ;;
	--yes) assume_yes=true; shift ;;
	--dry-run) dry_run=true; shift ;;
	--backup) backup=true; shift ;;
	--allow-dirty) allow_dirty=true; shift ;;
	--force-dirty) force_dirty=true; shift ;;
	--stlink) stlink=true; shift ;;
	--mculink) mculink=true; shift ;;
	--source-dir) [ "$#" -ge 2 ] || { echo "Missing value for --source-dir." >&2; exit 2; }; source_opt=$2; shift 2 ;;
	-h|--help) usage; exit 0 ;;
	*) echo "Unknown option: $1" >&2; exit 2 ;;
	esac
done

[ "$role" = prod-coletec-stm32 ] || [ "$role" = prod-coletec-stm32-prov ] || [ "$force_dirty" = false ] || { echo "--force-dirty applies only to STM32 targets." >&2; exit 2; }

if [ "$role" = prod-coletec-stm32 ] || [ "$role" = prod-coletec-stm32-prov ]; then
	[ -d "$stm32_dir/scripts" ] || { echo "STM32 checkout not found: $stm32_dir" >&2; exit 1; }
	[ -f "$stm32_build/atletec-stm32-build.txt" ] || { echo "Missing STM32 build metadata; run build-one $role." >&2; exit 1; }
	stm32_meta="$stm32_build/atletec-stm32-build.txt"
	stm32_source=$(git -C "$stm32_dir" rev-parse HEAD) || exit 1
	stm32_contract=$(git -C "$stm32_dir/contracts" rev-parse HEAD) || exit 1
	stm32_worktree_hash=$(
		{
			git -C "$stm32_dir" diff HEAD --binary -- scripts stm32
			printf 'contracts=%s\n' "$stm32_contract"
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
	if [ "$role" = prod-coletec-stm32 ]; then
		grep -qx 'role=PROD-COLETEC-STM32-APP' "$stm32_meta" &&
		grep -qx "source_commit=$stm32_source" "$stm32_meta" &&
		grep -qx "contracts_commit=$stm32_contract" "$stm32_meta" &&
		grep -qx "worktree_hash=$stm32_worktree_hash" "$stm32_meta" &&
		[ -s "$stm32_build/zephyr/zephyr.signed.bin" ] || { echo "STM32 app image/metadata is stale; rebuild it." >&2; exit 1; }
		stm32_image="$stm32_build/zephyr/zephyr.signed.bin"
	else
		grep -qx 'role=PROD-COLETEC-STM32-PROV' "$stm32_meta" &&
		grep -qx "source_commit=$stm32_source" "$stm32_meta" &&
		grep -qx "contracts_commit=$stm32_contract" "$stm32_meta" &&
		grep -qx "worktree_hash=$stm32_worktree_hash" "$stm32_meta" &&
		[ -s "$stm32_build/zephyr/zephyr.bin" ] || { echo "STM32 provisioning image/metadata is stale; rebuild it." >&2; exit 1; }
		stm32_image="$stm32_build/zephyr/zephyr.bin"
	fi
	if ! git -C "$stm32_dir" diff --quiet || ! git -C "$stm32_dir" diff --cached --quiet; then
		stm32_changes=$(git -C "$stm32_dir" diff HEAD --name-only -- scripts stm32 contracts)
		if [ -n "$stm32_changes" ] && [ "$force_dirty" != true ]; then
			echo "STM32 source has tracked changes; rebuild/commit before flashing." >&2
			[ -z "$stm32_changes" ] || printf 'Changed image sources:\n%s\n' "$stm32_changes" >&2
			echo "Pass --force-dirty to flash an image built from these exact dirty sources." >&2
			exit 1
		fi
		if [ -z "$stm32_changes" ] && [ "$allow_dirty" != true ]; then
			echo "STM32 working tree has non-image changes; pass --allow-dirty to continue." >&2
			exit 1
		elif [ -n "$stm32_changes" ]; then
			echo "warning: --force-dirty enabled for image sources; build fingerprint matches." >&2
			printf 'Dirty image sources included in the build:\n%s\n' "$stm32_changes" >&2
		else
			echo "warning: STM32 working tree has non-image changes." >&2
		fi
	fi
	[ "$backup" = false ] || { echo "--backup currently applies only to RECEPTEC." >&2; exit 2; }
	[ "$stlink" = false ] && [ "$mculink" = false ] || { echo "Use the STM32 ST-Link mapping; --stlink/--mculink select Nordic runners." >&2; exit 2; }
	[ -z "$source_opt" ] || { echo "--source-dir applies only to prod-coletec-qorvo." >&2; exit 2; }
	STM32_Programmer_CLI=$(command -v STM32_Programmer_CLI 2>/dev/null || true)
	[ -n "$STM32_Programmer_CLI" ] || {
		for cli in "$HOME/STMicroelectronics/STM32/STM32CubeProgrammer/bin/STM32_Programmer_CLI" \
			"$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI"; do
			[ -x "$cli" ] && STM32_Programmer_CLI="$cli" && break
		done
	}
	[ -x "$(command -v STM32_Programmer_CLI 2>/dev/null || printf '%s' "${STM32_Programmer_CLI:-}")" ] || {
		echo "STM32_Programmer_CLI not found; add it to PATH." >&2; exit 1;
	}
	STM32_STLINK_NICK=${STM32_STLINK_NICK:-coletec-qorvo-stm32}
	command -v lstty >/dev/null 2>&1 || { echo "lstty is required to identify the STM32 ST-Link." >&2; exit 1; }
	if [ -n "$probe_opt" ]; then
		stm32_probe=$(lstty stlink get "$probe_opt" --field serial 2>/dev/null || printf '%s' "$probe_opt")
	else
		stm32_probe=$(lstty stlink get "$STM32_STLINK_NICK" --field serial 2>/dev/null || true)
	fi
	[ -n "$stm32_probe" ] || { echo "No STM32 ST-Link; pass --probe or configure lstty stlink nickname $STM32_STLINK_NICK." >&2; exit 1; }
	stm32_probe_info=$(lstty stlink get --serial "$stm32_probe" --json 2>/dev/null || true)
	case "$stm32_probe_info" in *'"connected": true'*) ;; *) echo "STM32 ST-Link $stm32_probe is not connected." >&2; exit 1 ;; esac
	stm32_device=$(lstty stlink get --serial "$stm32_probe" --field device 2>/dev/null || true)
	stm32_console_nick=$(printf '%s\n' "$stm32_probe_info" | sed -n 's/.*"name": "\([^"]*\)".*/\1/p')
	[ -n "$stm32_console_nick" ] || stm32_console_nick=$STM32_STLINK_NICK
	case "$stm32_device" in
		STM32N6*|STM32N657*|stm32n6*) ;;
		"")
			case "$stm32_console_nick" in
				prod-coletec|coletec-qorvo-stm32)
					echo "warning: ST-Link $stm32_probe ($stm32_console_nick) is connected, but lstty has no target-device value; trusting the explicit STM32 nickname." >&2
				;;
				*) echo "ST-Link $stm32_probe has no target-device value and nickname '$stm32_console_nick' is not a known STM32 probe." >&2; exit 1 ;;
			esac
		;;
		*) echo "ST-Link $stm32_probe is registered as '$stm32_device', expected STM32N657." >&2; exit 1 ;;
	esac
	have_lstty=true
	echo "STM32 flash mapping: $role -> ST-Link $stm32_probe ($stm32_console_nick), build $stm32_build"
	if [ "$dry_run" = true ]; then
		echo "Dry run: checks passed; no STM32 image was loaded."
		exit 0
	fi

	if [ -t 0 ] && [ "$assume_yes" = false ]; then
		printf 'Confirm STM32 is in DEV boot and freshly reset, then load the selected image into RAM over SWD? [y/N] '
		IFS= read -r answer
		case "$answer" in y|Y|yes|YES) ;; *) echo "Load cancelled."; exit 1 ;; esac
	fi

	readelf_tool=${READELF:-${ZEPHYR_SDK_INSTALL_DIR:-}/arm-zephyr-eabi/bin/arm-zephyr-eabi-readelf}
	[ -x "$readelf_tool" ] || readelf_tool=$(command -v arm-zephyr-eabi-readelf 2>/dev/null || true)
	[ -n "$readelf_tool" ] || { echo "arm-zephyr-eabi-readelf not found." >&2; exit 1; }
	entry_addr=$("$readelf_tool" -h "$stm32_build/zephyr/zephyr.elf" | awk '/Entry point address:/ {print $4}')
	case "$entry_addr" in 0x[0-9a-fA-F]*) ;; *) echo "Could not read STM32 ELF entry point." >&2; exit 1 ;; esac

	if [ "$role" = prod-coletec-stm32-prov ]; then
		load_addr=0x34180400
		flash_log="$stm32_build/flash-one-stm32-prov.log"
	else
		load_addr=0x34000000
		app_size=$(wc -c <"$stm32_image" | tr -d ' ')
		[ "$app_size" -le 1572864 ] || { echo "Signed app is too large for the SRAM staging region." >&2; exit 1; }
		flash_log="$stm32_build/flash-one-stm32-app.log"
	fi

	if ! "$STM32_Programmer_CLI" -c port=SWD sn="$stm32_probe" mode=UR ap=1 \
		-w "$stm32_image" "$load_addr" -v -g "$entry_addr" >"$flash_log" 2>&1; then
		echo "STM32 RAM load failed; log: $flash_log" >&2
		tail -n 30 "$flash_log" >&2
		exit 1
	fi
	echo "STM32 image loaded and started in RAM; eMMC was not modified. Log: $flash_log"
	command -v bench >/dev/null 2>&1 || { echo "bench is required to verify the STM32 shell." >&2; exit 1; }
	if ! bench console "$stm32_console_nick" --stlink --send "help" --listen 1; then
		echo "Image loaded, but no shell response arrived on ST-Link VCP." >&2
		exit 1
	fi
	lstty stlink use "$stm32_probe" "STM32 RAM image ($role, $stm32_source)" >/dev/null 2>&1 || true
	exit 0
fi

if [ "$mculink" = true ] && { [ "$role" != receptec-qorvo-anchor ] || [ "$stlink" = true ]; }; then
	echo "--mculink applies only to receptec-qorvo-anchor and cannot combine with --stlink." >&2
	exit 2
fi
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
receptec-qorvo-anchor) build_dir=${RECEPTEC_QORVO_ANCHOR_BUILD_DIR:-${build_root:-$app_dir/build}/build-receptec-qorvo-anchor}; nick=receptec-qorvo-anchor;
	env_sn=${JLINK_SN_RECEPTEC_QORVO_ANCHOR:-}; want=nRF52833_xxAA ;;
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

if [ "$mculink" = true ]; then
	probe_label="MCU-Link"
	probe=${probe_opt:-$nick}
	[ "$have_lstty" = true ] || { echo "lstty is required for MCU-Link." >&2; exit 2; }
	probe=$(lstty mculink get "$probe" --field serial 2>/dev/null) || exit 2
	[ -n "$probe" ] || { echo "No MCU-Link for $role." >&2; exit 2; }
elif [ "$stlink" = true ]; then
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
	elif [ "$role" = fake-coletec-0-anchor ] || [ "$role" = receptec-qorvo-anchor ]; then
	if [ "$role" = receptec-qorvo-anchor ]; then
		grep -qx 'CONFIG_BOARD_RECEPTEC_QORVO=y' "$config" &&
		grep -qx 'CONFIG_SHELL_BACKEND_RTT=y' "$config" || {
			echo "$role requires a RECEPTEC-Qorvo RTT image." >&2
			exit 1
		}
		anchor_role=RECEPTEC-QORVO-ANCHOR
	else
		anchor_role=RECEPTEC-Qorvo-anchor-1
	fi
	grep -qx "role=$anchor_role" "$metadata" &&
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

if [ "$mculink" = true ]; then
	command -v pyocd >/dev/null 2>&1 || { echo "pyocd not found." >&2; exit 1; }
	info=$(lstty mculink get --serial "$probe" --json 2>/dev/null || true)
	case "$info" in *'"connected": true'*) ;; *) echo "MCU-Link $probe is disconnected." >&2; exit 1 ;; esac
	device=$(lstty mculink get "${probe_opt:-$nick}" --field device 2>/dev/null || true)
	case "$device" in nRF52833|nrf52833|nRF52833_xxAA) ;; *) echo "MCU-Link $probe is registered as $device, expected nRF52833." >&2; exit 1 ;; esac
	probe_nickname=$nick
elif [ "$stlink" = true ]; then
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

if [ "$mculink" = true ]; then
	flash_log="$build_dir/flash-mculink.log"
	west flash -d "$build_dir" --runner pyocd -- \
		--target=nrf52833 --board-id="$probe" >"$flash_log" 2>&1
elif [ "$stlink" = true ]; then
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
		if [ "$mculink" = true ]; then
			lstty mculink use "$probe" "$role bench firmware ($source_commit)" >/dev/null 2>&1
		elif [ "$stlink" = true ]; then
			lstty stlink use "$probe" "$role bench firmware ($source_commit)" >/dev/null 2>&1
		else
			lstty jlink use "$probe" "$role bench firmware ($source_commit)" >/dev/null 2>&1
		fi
	fi
else
	echo "$role flash FAILED (see $flash_log)" >&2
	exit 1
fi
