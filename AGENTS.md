# Repository Instructions

## System context & contracts

Part of the **Atletec EPTS** workspace — read `contracts/` first. This repo is
the **COLETEC** DWM3001C bench responder (nRF52833 + DW3110). Hubert's active
RECEPTEC is in `Firmware-Atletec`; this repository's historic reference
initiator is preserved in its `receptec` branch.

Contract this repo implements (pinned in `contracts/`; follow `contracts/VERSIONS.md`
to change it):
- **UWB RECEPTEC↔COLETEC v1.0** — this repo is the **COLETEC** end:
  `contracts/protocol/uwb-receptec-coletec/`. Run the **same active PHY profile**
  as the RECEPTEC (Ch 9, 6.8 Mbit/s, PRF 64 MHz, preamble 128 / code 9, STS
  disabled — the protocol doc is the authority). Hardware facts:
  `contracts/hardware/receptec-dwm.md` (its repository name is historical).

**When implementing or modifying the UWB protocol, read the canonical doc from
`contracts/` — not a local copy.** If you find a contract doc outside `contracts/`,
move it into `contracts/` and delete the stray (no duplicates, no symlinks).

> **Audit note:** `protocol-v1.0.md` is canonical. It leaves response-loss
> retry behavior open as OI-01, so this COLETEC deliberately keeps RX enabled
> through the slot after a response. That permits retries from a RECEPTEC using
> binary exponential backoff plus jitter. Do not reintroduce the old fixed
> 0/5000/10000-us restriction from `HANDOFF.md`.

## Zephyr source changes

Do not edit files inside the Zephyr source tree directly. In this workspace the
tree is normally located at `$ZEPHYR_BASE` or at the `zephyr` directory under
the west top directory.

When an upstream Zephyr change is required:

1. Prefer an application DeviceTree overlay, Kconfig fragment, Zephyr module,
   board extension, or application-owned driver whenever possible.
2. If the Zephyr tree itself must change, create a numbered patch under
   `patches/zephyr/`. Write or update the patch file in this repository; do not
   make an unrecorded edit in the Zephyr checkout.
3. Verify the patch against the intended Zephyr revision with `git apply
   --check`.
4. Apply recorded patches only through `scripts/apply-zephyr-patches.sh`.
5. Keep each patch focused and include its purpose and upstream Zephyr baseline
   in the patch commit message or header.
6. Never include, revert, overwrite, or clean unrelated modifications already
   present in the user's Zephyr worktree.

Before creating or applying a patch, inspect `git -C "$ZEPHYR_BASE" status
--short`. If the target files already contain unrelated changes, stop and work
around them or request direction rather than merging them into the patch.

Build output belongs in this application's build directories and must not be
placed in the Zephyr source tree.
