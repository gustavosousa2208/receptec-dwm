# atletec-uwb-dwm-bench — agent instructions

Part of the **Atletec EPTS** workspace — read the workspace `../AGENTS.md` and the
`contracts/` submodule first.

This repo owns the two DWM3001C surrogate tags in the UWB bench (nRF52833 +
DW3110). It builds explicit **COLETEC-0** and **COLETEC-1** images from the same
commit. The bench initiator is the real RECEPTEC running firmware from its own
repository; never build or flash a DWM as RECEPTEC. The production COLETEC
endpoint remains `nrf52-bringup-fw`.

## Contract this repo implements

Pinned in `contracts/`; follow `contracts/VERSIONS.md` to change it.

- **UWB RECEPTEC↔COLETEC** — this repo implements two surrogate **COLETEC** ends:
  `contracts/protocol/uwb-receptec-coletec/`. Run the **same active PHY profile**
  as the COLETEC (Ch 9, 6.8 Mbit/s, PRF 64 MHz, preamble 128 / code 9, STS
  disabled — the protocol doc is the authority). The real RECEPTEC is outside
  this repository.

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

Build output belongs in this application's build directories and must not be
placed in the Zephyr source tree.

## Conventions

Strategic docs — requirements, roadmap, traceability matrices, meeting notes,
cross-repo status — belong in `~/shared-data/ATLETEC/03-firmware-atletec/`,
which Obsidian opens as a vault; this repo's `docs/` is for what explains this
repo's code. See the workspace `../AGENTS.md`.

Code/comments/docs in **English**; commit messages in **PT-BR** Conventional
Commits (`feat(uwb): ...`, `fix(main): ...`).
`AGENTS.md` is the real file; `CLAUDE.md` is a symlink to it.

## Gitflow

Use the normal Gitflow branch model:

- `main` is the protected release branch.
- `develop` is the active integration branch and the destination for completed
  feature and fix work.
- Create `feature/*` branches from `develop` and merge them back into `develop`.
- Create `release/*` branches from `develop`. Merge a completed release into
  `main` through a pull request, then merge it back into `develop`.
- Create `hotfix/*` branches from `main`. Merge a completed hotfix into `main`
  through a pull request and also merge it into `develop`.
- Never merge or push feature work directly to `main`.
- Never merge or push `develop` directly to `main`; that integration must go
  through a pull request.
