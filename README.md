# receptec-dwm — RECEPTEC firmware and COLETEC test bench

This repository holds the firmware of the RECEPTEC (a real engineering-sample
board, Murata Type 2AB, nRF52840 + QM33120W, in `firmware/receptec`) and the
DWM3001C fake-COLETEC responder images used to test it on the Atletec EPTS UWB
bench. Production COLETEC firmware remains separate in `nrf52-bringup-fw`.

## Bench topology

| Bench name | Hardware | Protocol role | Firmware source |
|---|---|---|---|
| RECEPTEC | Production receiver, nRF52840 + QM33120W | Manual SYNC initiator (bring-up stage) | `firmware/receptec` |
| Fake COLETEC-0 | DWM3001C, nRF52833 + DW3110 | Surrogate responder (on-air identity COLETEC-0) | This repository, target `fake_coletec_0/nrf52833` |
| Fake COLETEC-1 | DWM3001C, nRF52833 + DW3110 | Surrogate responder (on-air identity COLETEC-1) | This repository, target `fake_coletec_1/nrf52833` |
| COLETEC | Production tag, nRF52840 + QM33120W | Production responder | `nrf52-bringup-fw` |

The RECEPTEC application has its own `receptec/nrf52840` board target, board
configuration, and `receptec.overlay` for the Type2AB carrier. It assigns the
UARTE1 Pi UART pins and builds the UWB frame codec and Qorvo DW3720 radio driver
for its QM33120W.
A manual `uwb sync 0|1` command sends one SYNC and waits for a matching ACK;
both fake COLETEC identities have passed on the bench. Periodic DATA_REQ polling
(`uwb poll`, see "Operate the bench") is implemented. Pi UART export and full
contract qualification remain future work.
Neither DWM board may be flashed as RECEPTEC.

The two surrogate images have distinct compiled identities:

| Target | EUI-64 | lstty nickname | J-Link (this bench) |
|---|---|---|---:|
| `fake_coletec_0/nrf52833` (fake-COLETEC-0) | `31:5D:02:D5:15:8C:FA:40` | `fake-coletec-0` | `760216755` |
| `fake_coletec_1/nrf52833` (fake-COLETEC-1) | `31:5D:02:D5:15:8C:FA:41` | `fake-coletec-1` | `760216737` |
| `receptec/nrf52840` (real RECEPTEC, Murata Type 2AB) | n/a | `receptec` | `802008564` |

Every DWM3001C on the bench is a **fake COLETEC**; the real RECEPTEC is the Type 2AB.
The nicknames come from `lstty` (see the workspace instructions). The serial
numbers above are a record of this bench, not defaults: the flash script takes
them from `lstty`, an environment variable or an option.

Both EUI-64 addresses must be registered in the real RECEPTEC configuration.

The RECEPTEC board target uses the nRF52840 SoC with its own board files; the
application overlay assigns UART P0.05/P0.06 and QM33120W SPIM3/CS/reset/IRQ/wake
pins. See [`docs/receptec-bringup.md`](docs/receptec-bringup.md) for hardware
scope and porting status.

## Branch model

Work lands on `gusta-develop` in this repository, as in every ATLETEC
repository. `develop` belongs to other people and is not used. Only the owner
opens a pull request to advance `master`. The canonical remote is Bitbucket. The
RECEPTEC feature branch (`feature/receptec-board-payload-export`) folds into
`gusta-develop`, and nothing is pushed before it has been tested on hardware.

## Source of truth

The canonical protocol is
[`contracts/protocol/uwb-receptec-coletec/protocol-v1.0.md`](contracts/protocol/uwb-receptec-coletec/protocol-v1.0.md).
Read it before changing frames, timing, addressing, retries, or PHY settings.
Contract versions and conformance status live in
[`contracts/VERSIONS.md`](contracts/VERSIONS.md) and
[`CONTRACTS.md`](CONTRACTS.md).

## Setup

Keep this repository under `~/source/atletec/` and use the existing west
workspace. On Mac it is `~/zephyrproject/`; on Dell it is
`~/source/zephyr-v4.3.0/`. The source root's `.envrc` selects that workspace
when direnv is enabled. In an agent shell without direnv, run:

```sh
export ATLETEC_ZEPHYR_WORKSPACE="$HOME/zephyrproject" # Dell: "$HOME/source/zephyr-v4.3.0"
export ZEPHYR_BASE="$ATLETEC_ZEPHYR_WORKSPACE/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-0.17.4" # Dell: "$HOME/source/zephyr-sdk-0.17.4"
source "$ATLETEC_ZEPHYR_WORKSPACE/.venv/bin/activate"
```

The contracts submodule is already populated in the migrated checkout.
Read the source root and repository `AGENTS.md` files before changing it.

Never edit the shared Zephyr tree directly. Application changes belong here;
unavoidable Zephyr changes must be recorded under `patches/zephyr/`.

## Build both surrogate boards

```sh
./scripts/build-both.sh
```

Equivalent direct commands:

```sh
west build -b fake_coletec_0/nrf52833 -d build-fake-coletec-0 .
west build -b fake_coletec_1/nrf52833 -d build-fake-coletec-1 .
```

Outputs:

```text
build-fake-coletec-0/zephyr/zephyr.hex
build-fake-coletec-1/zephyr/zephyr.hex
```

The build script verifies the compiled identity of each image and records the
source commit, contracts pin, and optional test configuration in each build
directory.

Run host protocol tests separately:

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Run RECEPTEC codec tests and build its provisional baseline separately:

```sh
cmake -S firmware/receptec/tests -B build-receptec-tests
cmake --build build-receptec-tests
ctest --test-dir build-receptec-tests --output-on-failure
west build -p always -b receptec/nrf52840 \
  -d "$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec-dwm/receptec-board" \
  firmware/receptec
```

Generated build directories copied from Dell contain the former absolute
source path. Configure a fresh build directory before using those outputs.

The planned data path and its still-open payload decisions are summarized in
[`docs/receptec-data-export.md`](docs/receptec-data-export.md). This is a design
note, not a wire-format contract; tag payload content is owned by COLETEC and
the normative schema belongs in the shared contracts repository.

## Flash the bench

After building and committing the source:

```sh
./scripts/build-both.sh          # fake-COLETEC-0, fake-COLETEC-1 and the real RECEPTEC
./scripts/flash-both.sh --dry-run  # every check, no flashing
./scripts/flash-both.sh
```

It flashes the two fake COLETECs and the real RECEPTEC at once, each to the probe
nicknamed `fake-coletec-0`, `fake-coletec-1` and `receptec` in `lstty`. `--backup` reads the
real RECEPTEC's flash (1 MiB) and UICR back to
`backups/real-receptec-<probe>-<UTC>/` (`flash.bin`, `uicr.bin`) first, using
`lstty jlink dump`, and cancels the flash if that fails. Never `nrfjprog`.
`--no-receptec` flashes only the
fakes. Override the mapping when necessary:

```sh
./scripts/flash-both.sh \
  --fake-coletec-0-probe <serial> \
  --fake-coletec-1-probe <serial> \
  --receptec-probe <serial>
```

The script rejects duplicate probe serials, probes whose registered target
device does not match the role (a DWM can never receive the RECEPTEC image),
stale images, mismatched compiled identities, and fake-COLETEC builds made from
different test configurations. It never uses
USB enumeration order to decide which image belongs to a board.

For a repository-owned fault scenario, apply the same fragment to both
surrogates:

```sh
./scripts/build-both.sh --test-conf tests/m7-drop-two-acks.conf
```

Rebuild without `--test-conf` immediately after fault testing.

## Operate the bench

Build, flash and measure with the scripts; do not call `west flash` or
`nrfjprog` by hand (`nrfjprog` opens a probe-selection window). Probes are
found by `lstty` nickname (`fake-coletec-0`, `fake-coletec-1`, `receptec`).

| Script | Use |
|---|---|
| `scripts/build-both.sh [--test-conf tests/throughput-<P>ms.conf] [--no-receptec]` | Build both fake COLETECs and the real RECEPTEC into the west workspace |
| `scripts/flash-one.sh <fake-coletec-0\|fake-coletec-1\|receptec>` | Flash one board; no prompt when stdin is not a tty |
| `scripts/flash-both.sh` | Flash the two fakes and the RECEPTEC; `--dry-run` runs every check only |
| `scripts/sweep.sh [--poll-ms 200,100,50] [--duration S]` | Per period: build the fakes with the matching `.conf`, flash, run the poll and print the counters |
| `scripts/sync-sweep.py [--poll-ms P --duration S]` | Reset the boards, start the fakes, then run manual SYNCs or the poll |
| `scripts/sensor-viewer.py [--only-peer EUI --seconds S]` | Live decode of the COLETEC sensor payload v1 on the PC |
| `scripts/accept-sensor-path.py [--samples N --min-delivery F]` | Acceptance check of COLETEC to RECEPTEC to PC: delivery, late, decode, sequence, fresh summary. PASS / FAIL / SKIP (exit 0 / 1 / 77); a check that cannot run is SKIP, never PASS |

The flash scripts refuse a dirty tree, a stale image, and a probe whose target
device does not match the role. `--allow-dirty` accepts changes that do not touch
image sources (docs, scripts). A DWM can never receive the RECEPTEC image.

Shells: the fake COLETECs have a UART shell on their J-Link serial port
(`~/.serial/fake-coletec-N`, prompt `uart:~$`). The RECEPTEC has no serial port on
the bench yet, so its shell is on SEGGER RTT (`./scripts/rtt-shell.sh receptec`,
prompt `rtt:~$`). The RTT input buffer is about 16 bytes: paste long commands in
pieces (`sync-sweep.py` does).

Start both surrogate responders first, then traffic on the real RECEPTEC:

```text
fake-COLETEC-0 / -1 shell:   uwb coletec start
RECEPTEC shell:              uwb poll start [period_ms]   (default 1000)
                             uwb poll stats
                             uwb poll stop
```

A responder listens continuously for 10 s after reset or `start`, so start polling
within that window. `uwb poll` sends DATA_REQ to every registered peer once per
period (slot N at N x 20 ms, up to 3 attempts with backoff, DT_sampling per
attempt). **The period must equal the fakes' `T_sampling`**, which is a build
setting: build them with the matching `tests/throughput-<P>ms.conf` (a mismatch
gave 20% success at 200 ms against 1000 ms fakes). Manual `uwb sync <slot>` is
refused while polling. Below 100 ms only two slots fit in a period.

Reference bench result, 2026-09-24, RECEPTEC and two fake COLETECs, 105-byte DATA,
30 to 60 s per period: 200 ms, 100 ms and 50 ms all completed every transaction on
the first attempt. The retry path has not been exercised.

The RECEPTEC board LEDs: blue (D300, P1.08) solid once booted; green (D301, P1.09)
blinks at 1 Hz while the kernel runs, 5 Hz if the UWB radio failed to initialize.

Open an individual fake shell over RTT with
`./scripts/rtt-shell.sh <lstty nickname or J-Link serial>`. The startup log prints
the fake's role and on-air identity, source and contracts revisions, Nordic device
ID and UWB frame-filter address.

## Code map

| Path | Responsibility |
|---|---|
| `boards/atletec/coletec/` | `fake_coletec_0/nrf52833` and `fake_coletec_1/nrf52833` board targets |
| `src/main.c` | Startup and compiled surrogate identity |
| `src/dw3110_radio.c` | DW3110 owner and COLETEC responder behavior |
| `src/protocol.c` | Protocol frame codec and timing helpers |
| `src/coletec_sync.c` | COLETEC synchronization, guards, and discovery |
| `src/dw3110_port.c` | SPI, GPIO, interrupt, and timing bridge |
| `src/uwb_shell.c` | Operator shell |
| `tests/` | Host tests and bounded hardware fault configurations |
| `scripts/` | Dual build, flash, RTT, and analysis tooling |
| `vendor/qorvo/` | Pinned Qorvo DW3XXX driver and provenance |
| `firmware/receptec/` | RECEPTEC Type2AB baseline, UWB codec, QM33120W driver, and manual SYNC initiator |

Qualification evidence and unfinished test results remain outside repository
tracking until the validation campaign is complete.
