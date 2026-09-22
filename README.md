# Atletec DWM UWB bench

DWM3001C firmware for exercising the Atletec EPTS UWB protocol before enough
production units are available. Product names are used consistently at the
operator boundary; component and development-kit names stay in the hardware
implementation.

| Name | Purpose | Processor | UWB radio | Firmware |
|---|---|---|---|---|
| RECEPTEC | Fixed protocol initiator | nRF52840 | QM33120W | Production RECEPTEC repository |
| COLETEC | Mobile protocol responder | nRF52840 | QM33120W | `nrf52-bringup-fw` |
| `receptec` | RECEPTEC-compatible bench target | nRF52833 | DW3110 | This repository |
| `coletec` | COLETEC-compatible bench target | nRF52833 | DW3110 | This repository |

The two lower-case names are Zephyr board targets for the DWM3001C bench
hardware. They identify the firmware role directly and must not be confused
with production hardware.

## Current implementation boundary

The production RECEPTEC boots and has a verified RTT shell, but its QM33120W
radio remains disabled while the proven COLETEC QM33120W driver is ported into
the RECEPTEC firmware. The DWM3001C DW3110 driver in this repository cannot be
reused for that hardware.

Likewise, this repository currently builds one generic DWM COLETEC image with
one compiled EUI-64. `COLETEC-0` and `COLETEC-1` are the bench identities to
implement next; distinct image configuration and identities are required
before flashing two DWM responders concurrently. This naming change does not
claim that two-tag operation is already qualified.

## Branch and build model

Permanent branches are:

| Branch | Purpose |
|---|---|
| `master` | Stable integration; changes arrive only through pull requests. |
| `develop` | Active integration for completed feature and fix branches. |

The compatibility harness has `receptec` and `coletec` board targets. Each
target selects its hardware and protocol role directly. One checkout and one
source commit produce two independent images:

```text
build-receptec/zephyr/zephyr.hex
build-coletec/zephyr/zephyr.hex
```

## Source of truth

The canonical protocol is
[`contracts/protocol/uwb-receptec-coletec/protocol-v1.0.md`](contracts/protocol/uwb-receptec-coletec/protocol-v1.0.md).
Read it before changing frames, timing, retries, addressing, or PHY settings.
`HANDOFF.md` beside it is supporting context, not an alternative specification.

Contract versions and conformance status live only in:

- [`contracts/VERSIONS.md`](contracts/VERSIONS.md)
- [`CONTRACTS.md`](CONTRACTS.md)

Shared protocol changes must be coordinated with every endpoint.

## First-time setup

Keep the repository beside the Zephyr source tree:

```text
zephyrproject/
├── .venv/
├── zephyr/
├── atletec-uwb-dwm-bench/
└── nrf52-bringup-fw/
```

```sh
cd /path/to/zephyrproject
git clone --recurse-submodules \
  git@bitbucket.org:techfusionwksp/atletec-uwb-dwm-bench.git
cd atletec-uwb-dwm-bench
git switch develop

source ../.venv/bin/activate
export ZEPHYR_BASE="$PWD/../zephyr"
git submodule update --init --recursive
```

Never edit the shared Zephyr tree directly. Application changes belong here;
unavoidable Zephyr changes must be recorded under `patches/zephyr/` and applied
with `scripts/apply-zephyr-patches.sh`.

## Build the named bench boards

Build both named targets concurrently:

```sh
./scripts/build-both.sh
```

Equivalent individual commands are:

```sh
west build -b receptec -d build-receptec .

west build -b coletec -d build-coletec .
```

The role is compiled into each image. Nordic device IDs are printed for
diagnostics but never select behavior.

The startup role indicator uses the board LED aliases and leaves the other role
LED off:

| Role | Indicator |
|---|---|
| RECEPTEC | Red `led1` (`D12`, P0.14) |
| COLETEC surrogate | Blue `led3` (`D10`, P0.05) |

Run host protocol tests separately:

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Flash both DWM reference boards

The default mapping is:

| Image | J-Link serial |
|---|---:|
| RECEPTEC bench target | `760216755` |
| COLETEC bench target | `760216737` |

Build, connect both probes and antennas, then run:

```sh
./scripts/flash-both.sh
```

The script verifies that both images came from the current source commit and
contracts pin, confirms their compiled roles, displays the mapping, and flashes
both probes concurrently. Override the mapping when necessary:

```sh
./scripts/flash-both.sh \
  --receptec-probe <serial> \
  --coletec-probe <serial>
```

It refuses identical probe serials and reports partial failures explicitly.
Never choose a probe from USB enumeration order.

For a repository-owned fault scenario, build both explicit roles with the same
test fragment before flashing:

```sh
./scripts/build-both.sh --test-conf tests/m7-drop-two-acks.conf
./scripts/flash-both.sh
```

The flash confirmation displays the selected test fragment. Rebuild normally
with `./scripts/build-both.sh` immediately after fault testing.

## First bench exchange

Connect to the **RECEPTEC bench target** UART at 115200 bit/s. Both bench boot
logs must show the same source/contracts revisions and their distinct compiled
roles. The production RECEPTEC uses its own firmware image.

```text
uwb status
uwb peers
uwb data_req 0
uwb test sync 0
uwb slotted start
```

Observe at least 20 successful exchanges without final transaction failures or
late schedules, then stop traffic:

```text
uwb slotted stop
```

When UART wiring is unavailable, open the same Zephyr shell through SEGGER RTT:

```sh
./scripts/rtt-shell.sh 802008564
```

The script requires SEGGER's `JLinkGDBServerCLExe` and `JLinkRTTClient`. It can
also use `JLINK_SN_RECEPTEC` from `lstty` when invoked without an argument.

## UWB shell commands

Enter `help uwb` to display the command tree. Peer slots range from 0 to 3;
always run `uwb peers` before selecting one.

| Command | Purpose |
|---|---|
| `uwb status` | Show role, protocol/schedule state, cycle, and queued operation. |
| `uwb peers` | List peer slots, addresses, and expected presence. |
| `uwb slotted start` | Start the normal periodic DATA_REQ/DATA schedule. |
| `uwb slotted stop` | Finish the active cycle and stop the schedule safely. |
| `uwb data_req <slot>` | Queue one DATA_REQ/DATA transaction. |
| `uwb sync start <slot>` | Send periodic SYNC/ACK to one peer. |
| `uwb sync stop` | Stop periodic SYNC. |
| `uwb coletec start` | On the COLETEC image, enable RX and await synchronization. |
| `uwb test list` | Print available runtime tests. |
| `uwb test sync <slot>` | Run one addressed SYNC/ACK transaction. |
| `uwb test stop <slot>` | Run STOP/STOP_ACK; reset the peer afterward. |
| `uwb test wrong_eui <slot>` | Send a request to an unknown destination; expect no response. |
| `uwb test out_of_slot <slot>` | Send outside the peer RX window; slotted mode must run. |
| `uwb test silence <cycles>` | Suppress requests to exercise synchronization loss/discovery. |
| `uwb test abort` | Cancel a queued test or active silence safely. |

Commands are queued to the exclusive UWB owner thread and results print
asynchronously. `uwb test stop` leaves the target radio stopped until reset or
power cycle. Further protocol-test planning and qualification evidence are kept
in local operator notes outside repository tracking.

## Code map

| Path | Responsibility |
|---|---|
| `src/main.c` | Startup, build identity, and compiled role selection |
| `src/dw3110_radio.c` | Radio owner, RECEPTEC scheduler, and COLETEC responder |
| `src/protocol.c` | Frame codec and retry scheduling helpers |
| `src/coletec_sync.c` | Synchronization, guards, and discovery state |
| `src/dw3110_port.c` | SPI, GPIO, interrupt, and timing bridge |
| `src/uwb_shell.c` | Operator control queued to the radio owner |
| `configs/` | Explicit RECEPTEC and COLETEC build selections |
| `tests/` | Host tests and hardware fault configurations |
| `scripts/` | Dual builds, dual flashing, and qualification tools |
| `vendor/qorvo/` | Pinned Qorvo DW3XXX driver and provenance |

Qorvo driver licensing and checksums are in
[`vendor/qorvo/README.md`](vendor/qorvo/README.md).
