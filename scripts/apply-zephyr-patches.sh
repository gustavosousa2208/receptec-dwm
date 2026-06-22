#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
patch_dir="$app_dir/patches/zephyr"

if [ -n "${ZEPHYR_BASE:-}" ]; then
	zephyr_dir=$ZEPHYR_BASE
else
	west_topdir=$(west topdir)
	zephyr_dir="$west_topdir/zephyr"
fi

if ! git -C "$zephyr_dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	echo "Not a Git worktree: $zephyr_dir" >&2
	exit 1
fi

found=false
for patch in "$patch_dir"/*.patch; do
	if [ ! -e "$patch" ]; then
		continue
	fi

	found=true
	echo "Checking $(basename -- "$patch")"

	if git -C "$zephyr_dir" apply --reverse --check "$patch" >/dev/null 2>&1; then
		echo "Already applied: $(basename -- "$patch")"
		continue
	fi

	if ! git -C "$zephyr_dir" apply --check "$patch"; then
		echo "Patch does not apply cleanly; Zephyr tree was not changed." >&2
		exit 1
	fi

	git -C "$zephyr_dir" apply "$patch"
	echo "Applied: $(basename -- "$patch")"
done

if [ "$found" = false ]; then
	echo "No Zephyr patches to apply."
fi
