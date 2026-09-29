# receptec (RECEPTEC + fake-COLETEC bench) — agent instructions

Part of the **Atletec EPTS** workspace — read the workspace `../AGENTS.md` and the
`contracts/` submodule first.

This repo is the **RECEPTEC** repository: one RECEPTEC application under
`src/receptec/` for the real Type2AB receiver (nRF52840 + QM33120W) and the
RECEPTEC-Qorvo target (DWM3001C, nRF52833 + DW3110). The targets share their
shell, peer registry/storage, frame codec and polling behavior; board selection
chooses the DW3720 or DW3110 SPI port. The two DWM3001C fake-COLETEC surrogate
images remain bench firmware under `src/`. Raspberry Pi SPI pins are described
in the RECEPTEC-Qorvo overlay, but the Pi data protocol is not implemented.
The production COLETEC endpoint remains separate in `nrf52-bringup-fw`. The
canonical remote is Bitbucket (`receptec.git`). The repository was called
`coletec-dwm`, then `atletec-uwb-dwm-bench` and `receptec-dwm`; the GitHub
remote still carries the name `receptec-dwm`.

## Contract this repo implements

Pinned in `contracts/`; follow `contracts/VERSIONS.md` to change it.

- **UWB RECEPTEC↔COLETEC** — the DWM apps implement two surrogate **COLETEC**
  ends and the RECEPTEC app implements the initiator-side PHY, peer registry,
  frame codec, manual SYNC/ACK operation and periodic DATA_REQ polling:
  `contracts/protocol/uwb-receptec-coletec/`. Run the **same active PHY profile**
  as the COLETEC (Ch 9, 6.8 Mbit/s, PRF 64 MHz, preamble 128 / code 9, STS
  disabled — the protocol doc is the authority). DATA remains a COLETEC-owned
  application payload; its schema and RECEPTEC export to maleta are future
  contract work. The production COLETEC source remains in `nrf52-bringup-fw`.

**When implementing or modifying the UWB protocol, read the canonical doc from
`contracts/` — not a local copy.** If you find a contract doc outside `contracts/`,
move it into `contracts/` and delete the stray (no duplicates, no symlinks).

`contracts/protocol/uwb-receptec-coletec/protocol-v1.0.md` is the canonical spec.
The `HANDOFF.md` sitting beside it contradicts it on the retry/backoff model and
whether COLETEC keeps RX enabled through the slot. This repo now follows the
canonical exponential-backoff rule. It keeps COLETEC RX enabled through the
slot, while the canonical text requires immediate shutdown but flags that rule
as broken in OI-01. The document conflicts remain tracked as **A7** and **A8**
in `../nrf52-bringup-fw/docs/consolidation-backlog.md`. Audit against
`protocol-v1.0.md` specifically — matching `HANDOFF.md` is not evidence of
conformance.

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

Use fresh build directories under the existing west workspace's
`build/atletec/receptec/`. Older in-repo build directories retain paths
from before the Dell move and need reconfiguration before reuse.

## Conventions

Strategic docs — requirements, roadmap, traceability matrices, meeting notes,
cross-repo status — belong in `~/shared-data/ATLETEC/03-firmware-atletec/`,
which Obsidian opens as a vault; this repo's `docs/` is for what explains this
repo's code. See the workspace `../AGENTS.md`.

Code/comments/docs in **English**; commit messages in **PT-BR** Conventional
Commits (`feat(uwb): ...`, `fix(main): ...`).
`AGENTS.md` is the real file; `CLAUDE.md` is a symlink to it.

## Gitflow

Follow the shared ATLETEC `AGENTS.md`: `gusta-develop` is the integration
branch in this repo too (owner decision, 2026-09-24: not `develop`, which
belongs to other people), and only the owner opens a pull request to advance
`master`. The canonical remote is Bitbucket. No merge or push without a
hardware test. Preserve feature branches and all uncommitted work; the branch
`feature/receptec-board-payload-export` folds into `gusta-develop`.
