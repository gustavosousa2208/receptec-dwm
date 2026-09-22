# Atletec dual-COLETEC DWM bench

Firmware for the two DWM3001C boards that act as COLETEC surrogates in the
Atletec EPTS UWB bench.

## Bench topology

| Bench name | Hardware | Protocol role | Firmware source |
|---|---|---|---|
| RECEPTEC | Production receiver, nRF52840 + QM33120W | Initiator | RECEPTEC repository |
| COLETEC-0 | DWM3001C, nRF52833 + DW3110 | Surrogate responder | This repository, target `coletec_0` |
| COLETEC-1 | DWM3001C, nRF52833 + DW3110 | Surrogate responder | This repository, target `coletec_1` |
| COLETEC | Production tag, nRF52840 + QM33120W | Production responder | `nrf52-bringup-fw` |

The real RECEPTEC is the only initiator in this topology. This repository does
not produce a RECEPTEC image, and neither DWM board may be flashed as one.

The two surrogate images have distinct compiled identities:

| Target | EUI-64 | Default J-Link |
|---|---|---:|
| `coletec_0` | `31:5D:02:D5:15:8C:FA:40` | `760216737` |
| `coletec_1` | `31:5D:02:D5:15:8C:FA:41` | `760216755` |

Both EUI-64 addresses must be registered in the real RECEPTEC configuration.

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

Qualification evidence and unfinished test results remain outside repository
tracking until the validation campaign is complete.
