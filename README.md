# Atletec UWB bench firmware

This repository contains the DWM3001C fake-COLETEC responders and the
RECEPTEC Type2AB bring-up baseline for the Atletec EPTS UWB bench. Production
COLETEC firmware remains separate in `nrf52-bringup-fw`.

## Bench topology

| Bench name | Hardware | Protocol role | Firmware source |
|---|---|---|---|
| RECEPTEC | Production receiver, nRF52840 + QM33120W | Manual SYNC initiator (bring-up stage) | `firmware/receptec` |
| Fake COLETEC-0 | DWM3001C, nRF52833 + DW3110 | Surrogate responder (on-air identity COLETEC-0) | This repository, target `coletec_0` |
| Fake COLETEC-1 | DWM3001C, nRF52833 + DW3110 | Surrogate responder (on-air identity COLETEC-1) | This repository, target `coletec_1` |
| COLETEC | Production tag, nRF52840 + QM33120W | Production responder | `nrf52-bringup-fw` |

The RECEPTEC application has its own `receptec/nrf52840` board target, board
configuration, and `receptec.overlay` for the Type2AB carrier. It assigns the
UARTE1 Pi UART pins and builds the UWB frame codec and Qorvo DW3720 radio driver
for its QM33120W.
A manual `uwb sync 0|1` command sends one SYNC and waits for a matching ACK;
both fake COLETEC identities have passed on the bench. Periodic scheduling,
DATA collection/export, and full contract qualification remain future work.
Neither DWM board may be flashed as RECEPTEC.

The two surrogate images have distinct compiled identities:

| Target | EUI-64 | Default J-Link |
|---|---|---:|
| `coletec_0` (fake COLETEC-0) | `31:5D:02:D5:15:8C:FA:40` | `760216755` |
| `coletec_1` (fake COLETEC-1) | `31:5D:02:D5:15:8C:FA:41` | `760216737` |

Both EUI-64 addresses must be registered in the real RECEPTEC configuration.

The RECEPTEC board target uses the nRF52840 SoC with its own board files; the
application overlay assigns UART P0.05/P0.06 and QM33120W SPIM3/CS/reset/IRQ/wake
pins. See [`docs/receptec-bringup.md`](docs/receptec-bringup.md) for hardware
scope and porting status.

## Branch model

`develop` is the active integration branch. Feature and fix branches merge
into `develop`. Integration from `develop` to the protected `main` branch is
performed only through a pull request in Bitbucket.

## Source of truth

The canonical protocol is
[`contracts/protocol/uwb-receptec-coletec/protocol-v1.0.md`](contracts/protocol/uwb-receptec-coletec/protocol-v1.0.md).
Read it before changing frames, timing, addressing, retries, or PHY settings.
Contract versions and conformance status live in
[`contracts/VERSIONS.md`](contracts/VERSIONS.md) and
[`CONTRACTS.md`](CONTRACTS.md).

## Setup

Keep this repository beside the Zephyr source tree, activate the workspace
environment, and initialize the contracts submodule:

```sh
source ../.venv/bin/activate
export ZEPHYR_BASE="$PWD/../zephyr"
git submodule update --init --recursive
```

Never edit the shared Zephyr tree directly. Application changes belong here;
unavoidable Zephyr changes must be recorded under `patches/zephyr/`.

## Build both surrogate boards

```sh
./scripts/build-both.sh
```

Equivalent direct commands:

```sh
west build -b coletec_0 -d build-coletec-0 .
west build -b coletec_1 -d build-coletec-1 .
```

Outputs:

```text
build-coletec-0/zephyr/zephyr.hex
build-coletec-1/zephyr/zephyr.hex
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
  -d build-receptec firmware/receptec
```

The planned data path and its still-open payload decisions are summarized in
[`docs/receptec-data-export.md`](docs/receptec-data-export.md). This is a design
note, not a wire-format contract; tag payload content is owned by COLETEC and
the normative schema belongs in the shared contracts repository.

## Flash both DWM boards

After building and committing the source:

```sh
./scripts/flash-both.sh
```

Override the explicit probe mapping when necessary:

```sh
./scripts/flash-both.sh \
  --coletec-0-probe <serial> \
  --coletec-1-probe <serial>
```

The script rejects duplicate probe serials, stale images, mismatched compiled
identities, and builds made from different test configurations. It never uses
USB enumeration order to decide which image belongs to a board.

For a repository-owned fault scenario, apply the same fragment to both
surrogates:

```sh
./scripts/build-both.sh --test-conf tests/m7-drop-two-acks.conf
```

Rebuild without `--test-conf` immediately after fault testing.

## Operate the bench

Start both surrogate responders before starting traffic on the real RECEPTEC:

```text
uwb coletec start
```

Run that command on both DWM shells. Scheduling, peer selection, DATA_REQ,
SYNC, and STOP commands belong to the real RECEPTEC firmware, not these DWM
images.

Open an individual DWM RTT shell with:

```sh
./scripts/rtt-shell.sh <J-Link serial>
```

The DWM startup log prints its target, `COLETEC-0` or `COLETEC-1` identity,
source revision, contracts revision, Nordic device ID, and UWB frame-filter
address. Both surrogate images use the blue COLETEC role indicator.

## Code map

| Path | Responsibility |
|---|---|
| `boards/atletec/coletec/` | Direct `coletec_0` and `coletec_1` board targets |
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
