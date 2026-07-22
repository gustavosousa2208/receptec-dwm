# RECEPTEC–COLETEC DWM3001C Bring-up Milestones

Last updated: 2026-07-22

This file is the durable implementation checkpoint for the two-DWM3001C
interoperability experiment. Update it whenever a milestone changes state so
work can resume safely after conversation context is lost.

The canonical wire and timing requirements remain in
`contracts/protocol/uwb-receptec-coletec/HANDOFF.md`. This file tracks
implementation progress only; it does not redefine the shared contract.

## Fixed experimental hardware

| Role | Nordic device ID |
|---|---|
| RECEPTEC | `3E:C6:F3:82:06:A8:16:41` |
| COLETEC | `40:FA:8C:15:D5:02:5D:31` |

Both boards run the same firmware image. The configured RECEPTEC ID selects the
single RECEPTEC role; every other device ID selects COLETEC.

| Probe | J-Link serial | Serial port |
|---|---:|---|
| DWM board 1 | `760216755` | `/dev/tty.usbmodem0007602167551` |
| DWM board 2 | `760216737` | `/dev/tty.usbmodem0007602167371` |

The mapping between J-Link serial and protocol role is intentionally not
required: role selection uses the Nordic device ID read by firmware.

## Status legend

- `[x]` Complete and verified
- `[~]` In progress
- `[ ]` Not started
- `[!]` Blocked; the milestone notes must state why

## M0 — Repository reset and identity retrieval

Status: `[x]` Complete

- Removed the earlier protocol codec, timing implementation, and host tests so
  hardware bring-up starts from a minimal application.
- Enabled Zephyr `CONFIG_HWINFO`.
- Read and printed the complete eight-byte nRF52833 factory device ID.
- Verified the application builds for `decawave_dwm3001cdk/nrf52833`.

Acceptance evidence:

- COLETEC ID: `40:FA:8C:15:D5:02:5D:31`
- RECEPTEC ID: `3E:C6:F3:82:06:A8:16:41`
- Zephyr build completed successfully.

## M1 — Runtime role selection and two-board flashing

Status: `[x]` Complete

- The configured RECEPTEC device ID selects `RECEPTEC`.
- Every nonmatching device ID selects `COLETEC`.
- Startup output prints both the Nordic ID and selected role.
- `scripts/flash-both.sh` builds once and flashes both known J-Link probes
  concurrently.
- User confirmed the two-board identity/role setup works.

## M2 — Pinned DW3110 driver dependency

Status: `[x]` Complete

Required work:

- Select an official Qorvo `dwt_uwb_driver` release supporting DW3110/DW3000.
- Record the exact driver version and source provenance.
- Preserve file-level license identifiers and document the official license
  source and distribution limitations.
- Import only the files required by the DW3000-family driver where practical.
- Integrate vendor sources through this application; do not modify Zephyr.

Done when:

- A clean build compiles and links the pinned driver.
- The source version, provenance, file-level license identifier, and known
  distribution limitation can be audited from this repository.

Implementation evidence:

- Imported Qorvo DW3XXX driver `08.19.02` from the exact committed workspace
  snapshot `nrf52-bringup-fw@628d5de9c54f35152f86a6249b66ef7ec655c3b6`.
- Imported files preserve `LicenseRef-QORVO-2` headers and have recorded SHA-256
  checksums under `vendor/qorvo/`.
- The proprietary license text was absent from the internal source snapshot;
  `vendor/qorvo/README.md` records that distribution requires the complete
  agreement from the official Qorvo package.
- Driver sources compile and link in the DWM3001C Zephyr image.

## M3 — Zephyr DW3110 hardware adapter

Status: `[x]` Complete

Required work:

- Add an application-owned devicetree overlay for the DW3110 SPI chip-select,
  reset/wake signal, and IRQ signal.
- Implement slow/fast SPI operations expected by the Qorvo driver.
- Implement reset, wake, delay, critical-section, and IRQ callbacks.
- Keep Zephyr GPIO/SPI calls behind a thin radio adapter boundary.

Done when:

- Both boards can reset and probe the DW3110 repeatedly without SPI errors.
- IRQ assertion and DW3110 status clearing are verified.

Implementation state:

- Application overlay defines SPI3/CS P1.06, IRQ P1.02, and reset P0.25.
- Zephyr port implements Qorvo slow/fast SPI, reset, CS wake, delays, critical
  sections, and deferred IRQ dispatch.
- Qorvo probe, OTP initialization, v1.0 PHY configuration, tuning values, and
  identity diagnostics compile successfully.
- Both boards now reset, probe as `0xDECA0302`, read OTP identity, and configure
  the PHY successfully on hardware.
- The raw bidirectional test uses the DW3110 IRQ line and Qorvo callbacks.
- Hardware capture verified TX-complete and RX-good IRQ delivery on both boards.

## M4 — DW3110 initialization and matched raw PHY

Status: `[x]` Complete

Required active PHY profile on both roles:

- Channel 9, 6.8 Mbit/s, PRF 64 MHz
- Preamble length 128, TX/RX code 9, PAC 8
- Qorvo proprietary 8-symbol SFD, SFD timeout 129
- Standard PHR and PHR rate
- STS disabled, PDOA disabled
- Hardware-generated IEEE 802.15.4 FCS

Initial tuning values:

- TX pulse-generator delay `0x34`
- TX power `0xFDFDFDFD`
- TX and RX antenna delay 16385

Implementation state:

- Both boards report the expected device ID and the complete matched PHY.
- RECEPTEC sends numbered raw PING frames; COLETEC validates them and sends a
  PONG with the same sequence; RECEPTEC then validates the PONG.
- RX-good is hardware-FCS-qualified. TX-complete, RX-good, RX-timeout, and
  RX-error events are handled through the DW3110 IRQ and emit diagnostics.
- Hardware capture verified bidirectional exchanges for consecutive sequences
  63 through 68. Both roles reported hardware-valid FCS and IRQ delivery.

Done when:

- Startup reports the expected DW3110 device ID and successful initialization.
- One role transmits a raw diagnostic frame and the other receives the exact
  bytes with a valid hardware FCS.
- TX-complete, RX-good, RX-timeout, and RX-error paths produce diagnostics.

## M5 — Addressed SYNC/ACK

Status: `[~]` In progress

Required work:

- Reintroduce a byte-exact v1.0 frame codec from the canonical handoff.
- Derive protocol EUI-64 arrays from the configured device IDs in on-air order.
- RECEPTEC sends an addressed `SYNC` with `DT_sampling = 0`.
- COLETEC validates FCS, length, FCF, type, destination, and source.
- COLETEC copies the sequence and schedules `ACK` at:

  `request_rx_rmarker + 1,000 us`

- RECEPTEC accepts only a matching source, destination, type, and sequence
  before the absolute 3,000 us timeout.

Done when:

- At least 1,000 consecutive `SYNC`/`ACK` transactions complete without an
  invalid frame, timeout, or late-scheduling error at normal bench distance.

Implementation state:

- A host-tested codec now emits the exact v1.0 FCF, address, type, payload, and
  EUI byte order; maximum DATA and malformed-frame cases are covered.
- RECEPTEC sends SYNC with `DT_sampling = 0` using delayed hardware TX and a
  3,000 us hardware response timeout.
- COLETEC validates length, hardware FCS, FCF, type, addresses, and payload,
  then schedules ACK from the request RX RMARKER at +1,000 UWB us.
- Hardware capture verified consecutive sequences 13 through 19 with valid
  addressing/FCS and the expected 1,000 us adjusted-RMARKER interval.
- The formal 1,000-consecutive-exchange qualification remains pending.

## M6 — DATA_REQ/DATA and duplicate requests

Status: `[ ]` Not started

Required work:

- RECEPTEC sends `DATA_REQ`; COLETEC returns an opaque `DATA` payload.
- Begin with a diagnostic payload containing the COLETEC ID and a sample
  counter.
- Freeze the selected payload on the first request in a slot.
- Duplicate requests with the same sequence return identical payload bytes.
- A new transaction may select a newer payload.

Done when:

- Normal and maximum-length DATA frames pass host tests and hardware exchange.
- Forced duplicate requests return byte-identical DATA responses.

## M7 — Hardware-time slots, retries, and `DT_sampling`

Status: `[ ]` Not started

Initial timing profile:

| Parameter | Value |
|---|---:|
| Sampling period | 1,000,000 us |
| Slot duration | 20,000 us |
| Response delay | 1,000 us |
| Response timeout | 3,000 us |
| Attempt offsets | 0, 5,000, 10,000 us |
| Maximum attempts | 3, including the first |

Required work:

- Schedule transactions entirely in the modular DW3110 hardware time domain.
- Use delayed TX for every request and response.
- Never fall back to uncontrolled immediate TX after a late scheduling error.
- Encode `DT_sampling` as unsigned 16-bit little-endian microseconds.
- Keep one modulo-256 sequence counter per configured COLETEC.
- Add all diagnostic counters required by the handoff.

Done when:

- Intentionally dropping responses exercises attempts at all three offsets.
- Duplicate responses work and the next nominal slot does not drift.
- Invalid or unrelated traffic never extends an absolute timeout.

## M8 — Synchronization recovery and discovery duty cycle

Status: `[ ]` Not started

Required work:

- COLETEC starts with continuous RX for 10 seconds.
- A synchronized COLETEC listens across the assigned slot with guard expansion.
- Guard starts at 500 us and expands by 100 us per missed slot.
- The fifth consecutive missed slot enters the firmware-controlled discovery
  duty cycle.
- Discovery uses 2 seconds RX on, an increasing multiple of 2 seconds off,
  three unsuccessful cycles per increment, and a maximum multiplier of five.

Done when:

- Controlled packet loss demonstrates each guard level, loss of sync, discovery
  duty cycling, and successful reentry.

## M9 — Acknowledged STOP

Status: `[ ]` Not started

Required work:

- RECEPTEC sends addressed `STOP` using the normal retry schedule.
- COLETEC sends `STOP_ACK` after 1,000 us.
- Duplicate STOP requests receive duplicate acknowledgements through the slot
  deadline.
- COLETEC enters `STOPPED` and disables UWB only after the deadline.
- Reset or power cycle is the only initial restart mechanism.

Done when:

- Normal STOP, lost STOP_ACK, duplicate STOP, and reset recovery are verified.

## M10 — Production COLETEC interoperability

Status: `[ ]` Not started

Required work:

- Port the verified COLETEC behavior to `nrf52-bringup-fw` on the actual
  nRF52840 + QM33120W/DW3720 hardware.
- Ensure its PHY has STS disabled; the current DW3720 initialization enabling
  STS mode 1 conflicts with the v1.0 handoff.
- Land both sides against the same pinned `contracts/` revision.
- Keep DATA payload interpretation outside the UWB wire-protocol layer.

Done when:

- DWM3001C RECEPTEC interoperates with the production COLETEC hardware through
  SYNC, DATA, retries, synchronization recovery, and STOP.

## Verification record

Add dated entries here as hardware milestones are exercised.

| Date | Milestone | Result | Evidence/notes |
|---|---|---|---|
| 2026-07-22 | M0 | Pass | Both Nordic IDs retrieved; Zephyr build passed. |
| 2026-07-22 | M1 | Pass | Same image selected the intended roles; user confirmed setup. |
| 2026-07-22 | M2 | Pass | Qorvo 08.19.02 snapshot pinned, checksummed, compiled, and linked. |

## Resume checklist

When resuming work after context loss:

1. Read `../AGENTS.md`, `AGENTS.md`, and `contracts/AGENTS.md`.
2. Read this file and find the first `[~]`, `[!]`, or `[ ]` milestone.
3. Read `contracts/protocol/uwb-receptec-coletec/HANDOFF.md` before protocol
   work.
4. Inspect `git status --short` in this repository and the Zephyr tree.
5. Preserve unrelated user changes and update this file after verified progress.
