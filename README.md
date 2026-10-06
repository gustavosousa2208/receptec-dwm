# receptec — RECEPTEC firmware and COLETEC test bench

This repository holds one RECEPTEC application for two boards: the real
engineering-sample Murata Type2AB (nRF52840 + QM33120W) and the DWM3001C-based
RECEPTEC-Qorvo (nRF52833 + DW3110). Both builds share the source under
`src/receptec/`, shell and persistent peer registry; board configuration selects
the radio transport. The repository also contains DWM3001C fake-COLETEC
responder images for the Atletec EPTS UWB bench. Production COLETEC firmware
remains separate in
`nrf52-bringup-fw`.

## Bench topology

| Bench name | Hardware | Protocol role | Firmware source |
|---|---|---|---|
| RECEPTEC | Production receiver, nRF52840 + QM33120W | Shared RECEPTEC application | This repository, target `receptec/nrf52840` |
| RECEPTEC-Qorvo | DWM3001C, nRF52833 + DW3110 | Same RECEPTEC application | This repository, target `receptec_qorvo/nrf52833` |
| Fake COLETEC-0 | DWM3001C, nRF52833 + DW3110 | Surrogate responder (on-air identity COLETEC-0) | This repository, target `fake_coletec_0/nrf52833` |
| Fake COLETEC-1 | DWM3001C, nRF52833 + DW3110 | Surrogate responder (on-air identity COLETEC-1) | This repository, target `fake_coletec_1/nrf52833` |
| COLETEC | Production tag, nRF52840 + QM33120W | Production responder | `nrf52-bringup-fw` |

Both receiver targets build the shared application from the repository root.
The Type2AB overlay assigns its Pi UART pins and the DW3720 SPI/reset/IRQ/wake
signals. The Qorvo overlay selects the DW3110 SPI/reset/IRQ port. A manual
`uwb sync 0|1` command sends one SYNC and waits for a matching ACK;
both fake COLETEC identities have passed on the bench. Periodic DATA_REQ polling
(`uwb poll`, see "Operate the bench") is implemented. Pi UART export and full
contract qualification remain future work.
Use only `receptec_qorvo/nrf52833` for the RECEPTEC-Qorvo hardware; the two
`fake_coletec_*` targets remain COLETEC responders.

The two surrogate images have distinct compiled identities:

| Target | EUI-64 | lstty nickname | J-Link (this bench) |
|---|---|---|---:|
| `fake_coletec_0/nrf52833` (fake-COLETEC-0) | `31:5D:02:D5:15:8C:FA:40` | `fake-coletec-0` | `760216755` |
| `fake_coletec_1/nrf52833` (fake-COLETEC-1) | `31:5D:02:D5:15:8C:FA:41` | `fake-coletec-1` | `760216737` |
| `receptec/nrf52840` (real RECEPTEC, Murata Type 2AB) | n/a | `receptec` | `802008564` |

The fake images have distinct compiled identities. The nicknames come from
`lstty` (see the workspace instructions). The serial
numbers above are a record of this bench, not defaults: the flash script takes
them from `lstty`, an environment variable or an option.

Both EUI-64 addresses must be registered in the real RECEPTEC configuration.

The RECEPTEC board target uses the nRF52840 SoC with its own board files; the
application overlay assigns UART P0.05/P0.06 and QM33120W SPIM3/CS/reset/IRQ/wake
pins. See [`docs/receptec-bringup.md`](docs/receptec-bringup.md) for hardware
scope and porting status.

The RECEPTEC-Qorvo target reuses the DWM3001C/DW3110 radio pin mapping from the
fake boards. Its overlay assigns the Raspberry Pi UART on P0.15/P0.19 and the
Pi SPI master interface to SPIS0 (MISO P0.09, MOSI P0.28, SCK P0.31, CS P0.30).
Carrier-only LEDs, button and accelerometer nodes are disabled; the board's
external blue LED on P1.08 is the role indicator. The Zephyr SPIS peripheral is
described, but the Raspberry Pi application protocol is not implemented. Its
peer registry, NVS storage and operator shell are shared with Type2AB.
Build with:

```sh
west build -b receptec_qorvo/nrf52833 \
  -d "$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec/build-receptec-qorvo" .
```

The shared Zephyr workspace keeps one canonical build directory per physical
board while both RECEPTEC targets use this repository root as their Zephyr
application source.

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

Build just one target with `scripts/build-one.sh`:

```sh
./scripts/build-one.sh receptec-qorvo
./scripts/build-one.sh receptec-qorvo-anchor
./scripts/build-one.sh receptec
./scripts/build-one.sh fake-coletec-0
```

The supported targets are `fake-coletec-0`, `fake-coletec-1`, `fake-coletec-0-anchor`, `receptec`
(Type2AB, nRF52840), and `receptec-qorvo` (nRF52833). For example, the
Qorvo image is written to `build/atletec/receptec/build-receptec-qorvo` in the
shared Zephyr workspace. The fake-coletec-0 responder and fixed-anchor images
share `build-fake-coletec-0` and must be built sequentially.

`receptec-qorvo-anchor` builds a separate physical RECEPTEC-Qorvo board as
BASE B. Its canonical directory is `build-receptec-qorvo-anchor`, its RTT prompt
is `rtt-receptec-qorvo-anchor:~$`, and its BLE name is `ATL-BASE-B`.
This board does not yet participate in the canonical three-node or two-pair
bench profile. Its current `CONFIG_RECEPTEC_FIXED_ANCHOR_ID=1` selects the
legacy pair index 1 (now named A–T), which cannot identify a separate B–T
operation. Do not start that role against TAG until the four-node coordinator
extension defines its pair mapping. The network EUI comes from hardware ID or
saved identity; register and verify it separately. MASTER, BASE A, BASE B and
TAG are distinct nodes.

After confirming the connected MCU-Link's serial and nRF52833 target with
`lstty`, flash this board with:

```sh
./scripts/flash-one.sh receptec-qorvo-anchor --mculink --probe receptec-qorvo-anchor
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

Build either receiver target from the repository root; each gets its own
build directory but both compile the same RECEPTEC application:

```sh
west build -b receptec/nrf52840 \
  -d "$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec/build-receptec" .
west build -b receptec_qorvo/nrf52833 \
  -d "$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec/build-receptec-qorvo" .
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
  --fake-coletec-0-probe <serial-or-lstty-name> \
  --fake-coletec-1-probe <serial-or-lstty-name> \
  --receptec-probe <serial-or-lstty-name>
```

The script rejects duplicate probe serials, probes whose registered target
device does not match the role (a DWM can never receive the RECEPTEC image),
stale images, mismatched compiled identities, and fake-COLETEC builds made from
different test configurations. It never uses
USB enumeration order to decide which image belongs to a board.
The probe options accept either a J-Link serial or an `lstty` nickname. The
flash plan prints the resolved serial and the nickname when `lstty` provides
one.

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
| `scripts/build-one.sh <target> [--test-conf tests/SCENARIO.conf]` | Build one fake COLETEC, Type2AB RECEPTEC, or RECEPTEC-Qorvo |
| `scripts/build-both.sh [--test-conf tests/throughput-<P>ms.conf] [--no-receptec]` | Build both fake COLETECs and the Type2AB RECEPTEC into the west workspace |
| `scripts/flash-one.sh <fake-coletec-0\|fake-coletec-0-anchor\|fake-coletec-1\|receptec\|receptec-qorvo>` | Flash one board; `--probe` accepts a serial or lstty nickname |
| `scripts/flash-both.sh` | Flash the two fakes and the RECEPTEC; `--dry-run` runs every check only |
| `scripts/sweep.sh [--poll-ms 200,100,50] [--duration S]` | Per period: build the fakes with the matching `.conf`, flash, run the poll and print the counters |
| `scripts/sync-sweep.py [--poll-ms P --duration S]` | Reset the boards, start the fakes, then run manual SYNCs or the poll |
| `scripts/sensor-viewer.py [--only-peer EUI --seconds S]` | Live decode of the COLETEC sensor payload v1 on the PC |
| `scripts/accept-sensor-path.py [--samples N --min-delivery F]` | Acceptance check of COLETEC to RECEPTEC to PC: delivery, late, decode, sequence, fresh summary. PASS / FAIL / SKIP (exit 0 / 1 / 77); a check that cannot run is SKIP, never PASS |
| `scripts/uwb-test-runner.py [--slot N --coletec-rtt prod-coletec]` | Runs the UWB runtime tests (`uwb test ...`) against the COLETEC and checks its counters as evidence; PASS / FAIL / SKIP; see `docs/uwb-runtime-tests.md` |
| `scripts/uwb-long-test.py start\|status\|stop\|report` | Long UWB run against the COLETEC, detached: `start --hours 2 --sample-s 30` returns at once; samples the RECEPTEC poll counters and the COLETEC Nordic RTT log (RSL, clock drift, IPC checksum errors) to `logs/long-<UTC>-<name>/samples.csv`, and writes `summary.md/json` at the end. Range accuracy and thermal drift are reported SKIP (not measurable with protocol v1.0) |

The flash scripts refuse a dirty tree, a stale image, and a probe whose target
device does not match the role. `--allow-dirty` accepts changes that do not touch
image sources (docs, scripts). A DWM can never receive the RECEPTEC image.

Shells: any image running on the fake COLETEC hardware, including the fixed-anchor
role, has its only shell transport on the J-Link VCP UART
(`~/.serial/fake-coletec-N`). Prompts identify the image: `rtt-fake-coletec-0:~$`,
`rtt-fake-coletec-0-anchor:~$`, or `rtt-fake-coletec-1:~$`; the `rtt-` prefix
is the requested shell identity and does not change the fake boards' UART
transport. The physical RECEPTEC has no
serial port on the bench yet, so its shell is on SEGGER RTT
(`./scripts/rtt-shell.sh receptec`, prompt `rtt-receptec:~$`). RECEPTEC-Qorvo
uses `rtt-receptec-qorvo:~$`. The firmware role does not change the fake-board
transport. The RTT input buffer is about 16 bytes: paste
long commands in pieces (`sync-sweep.py` does).

Start both surrogate responders first, then traffic on the real RECEPTEC:

```text
fake-COLETEC-0 / -1 shell:   uwb coletec start
RECEPTEC shell:              uwb sync <slot>              (periodic, default 1000 ms)
                             uwb sync start <slot> [period_ms]
                             uwb sync stop
                             uwb sync once <slot>          (single diagnostic)
                             led list|set|control|auto|selftest (active board LEDs)
                             uwb poll stats
                             uwb poll start|stop           (continuous DATA_REQ)
```

`led list` reports the active LEDs for the image. `led set <on|off|toggle>
<target> [...]` and `led control <target> <operation>` accept an index, an
overlay LED name, `blue`, `green`, or `all`. `led auto` resumes the board's
default indicators. The three-second `led selftest` cycles each active LED,
then all-on and all-off, and restores the previous states. Its inventory comes
from enabled children of that board's `gpio-leds` DeviceTree node, so disabled
module LEDs are not touched on RECEPTEC-Qorvo. J200's RGB indicators belong to
the Raspberry Pi domain and are not driven by the nRF shell.

A responder listens continuously for 10 s after reset or `start`, so start polling
within that window. `uwb poll` sends DATA_REQ to every registered peer once per
period (slot N at N x 20 ms, up to 3 attempts with backoff, DT_sampling per
attempt). `uwb sync` sends one SYNC each period to keep a peer synchronized; if
all three attempts fail, the next period still runs. **The period must equal the fakes' `T_sampling`**, which is a build
setting: build them with the matching `tests/throughput-<P>ms.conf` (a mismatch
gave 20% success at 200 ms against 1000 ms fakes). Periodic SYNC and DATA_REQ
polling share one radio worker, so only one can run at a time. Below 100 ms only
two peer slots fit in a period.

Reference bench result, 2026-09-24, RECEPTEC and two fake COLETECs, 105-byte DATA,
30 to 60 s per period: 200 ms, 100 ms and 50 ms all completed every transaction on
the first attempt. The retry path has not been exercised.

The RECEPTEC board LEDs: blue (D300, P1.08) solid once booted; green (D301, P1.09)
blinks at 1 Hz while the kernel runs, 5 Hz if the UWB radio failed to initialize.

Open an individual fake shell over its J-Link VCP UART with
`bench console fake-coletec-0 --interactive` (or `fake-coletec-1`); the fixed
anchor uses the same physical nickname, and its prompt identifies the role. For
the production COLETEC Nordic RTT shell, use
`./scripts/rtt-shell.sh prod-coletec`. The startup log prints the role and on-air
identity, source and contracts revisions, Nordic device ID and UWB frame-filter
address.

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
| `src/receptec/` | Shared RECEPTEC application and board-specific radio adapters |
| `src/qorvo/` | Compiled Qorvo DW3XXX driver sources, pinned by vendor checksums |
| `tests/` | Host tests and bounded hardware fault configurations |
| `scripts/` | Dual build, flash, RTT, and analysis tooling |
| `vendor/qorvo/` | Pinned Qorvo DW3XXX driver and provenance |
| `vendor/qorvo/` | Qorvo headers, license and provenance for `src/qorvo/` |

Qualification evidence and unfinished test results remain outside repository
tracking until the validation campaign is complete.


## Continuous three-pair bench profile

Names are M=MASTER (`receptec-qorvo`), A=BASE A (`fake-coletec-0-anchor`),
B=the separate BASE B (`receptec-qorvo-anchor`), T=TAG (`prod-coletec-qorvo`).
Historical three-node logs used A=MASTER and B=BASE A; translate their A-T,
B-T, A-B as M-T, A-T, M-A. Pair indices remain0=M-T,1=A-T,2=M-A.

The canonical current contract is the1.5 profile in `contracts/`; this bench
profile performs M-T, A-T and M-A once per 1000ms, at offsets 100, 375 and
650ms. The new BASE B is not scheduled. For A-T, M arms T then A and sends
START to A. T computes and reports one official distance directly to M. It publishes to the MASTER's BLE service;
it does not implement full STREAM/AGGREGATE forwarding to the tag.

For commissioned MASTER slots1=TAG and0=BASE A:

```text
uwb range stop
uwb range cycle configure 1 0
uwb range cycle baseline measured
uwb range cycle autostart on
uwb range cycle start
uwb range cycle status
```

Use `uwb range cycle baseline fixed <millimetres>` to substitute a local
constant for M-A without radio measurements of that pair. The dashboard
labels it **Constant**; it is not evidence of a measured/calibrated distance.
Return with `baseline measured`. Configuration changes apply next round.
`cycle stop` stops execution until manual start or boot; autostart remains
persisted. Invalid stored configuration blocks automatic startup.

Legacy BLE0x3101-0x3103 keep status:u8 +distance_mm:u32LE. Metadata0x3104
READ returns three20-byte records; NOTIFY returns one: version:u8, pair:u8,
source:u8 (0measured,1constant,2unavailable), status:u8, round:u64LE,
distance_mm:u32LE, age_ms:u32LE. Unavailable ages are UINT32_MAX.
The dashboard shows round, origin, age and failures; unchanged values expire
after3seconds. Unknown age/origin is explicit with older firmware.

Capture a30-minute BLE benchmark with the HIL Python environment:

```sh
python tools/cycle-benchmark.py --address <MASTER-BLE-address> --mode measured --output <new-evidence-directory>
```

Acceptance is at least99% success for each pair,99% matched rounds, and no
duplicates, stale intervals or cadence/sequence errors. BLE arrivals do not
prove radio exclusion; inspect firmware overruns, slot maxima and counters.
For fixed-mode capture supply `--mode fixed --baseline-mm <millimetres>`.
