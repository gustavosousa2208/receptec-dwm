# COLETEC DWM3001C

UWB responder firmware for a DWM3001C (nRF52833 + DW3110). This repository was
formerly named `receptec-dwm`, but its `main` branch now contains only the
**COLETEC** endpoint used to validate the RECEPTEC in `Firmware-Atletec`. The
former dual-role/reference initiator is preserved in the `receptec` branch.

The canonical wire contract is
[`contracts/protocol/uwb-receptec-coletec/protocol-v1.0.md`](contracts/protocol/uwb-receptec-coletec/protocol-v1.0.md).
Read it before changing a frame, address, PHY setting, or timing value. The
v1.0 document is authoritative; `HANDOFF.md` is bench context, not an
alternative specification.

## Operation

At boot the firmware configures the DW3110 and starts its UWB thread. It listens
continuously for 10 s to acquire the first addressed request, replies to
`DATA_REQ` with `DATA`, `SYNC` with `ACK`, and `STOP` with `STOP_ACK`, then
predicts the next slot from `DT_sampling`.

It keeps RX enabled through the rest of a slot after every response. That is the
response-loss recovery model: a retransmission can still be received. Five
missed slots enter discovery. `STOP` disables the radio until reset, power
cycle, or an external wake source. The responder accepts every valid 16-bit
`DT_sampling` value, including Hubert's non-fixed retry backoff.

## Required matching configuration

Both endpoints must use the same PHY and addresses. The current bench values
are visible in `src/coletec_radio.c`.

| Item | Value | Purpose |
|---|---:|---|
| Channel / bitrate | 9 / 6.8 Mbit/s | Shared UWB PHY profile |
| Preamble / code | 128 symbols / 9 | Shared acquisition settings |
| STS / PDOA | Disabled / disabled | Current v1 profile |
| `T_sampling` | 1,000,000 µs | Nominal interval between a COLETEC's slots |
| `T_slot` | 20,000 µs | Includes retry-reception time |
| Response delay | 2,000 µs | Request RX RMARKER to response TX RMARKER |
| Initial guard / step | 500 µs / 100 µs | Slot-edge margin and miss recovery |
| Discovery | 10 s initial RX; 2 s on / 2–10 s off | Reacquisition duty cycle |

EUI-64 arrays are on-air, least-significant-octet-first. The bench pair is
RECEPTEC `41:16:A8:06:82:F3:C6:3E` and this DWM COLETEC
`31:5D:02:D5:15:8C:FA:40`. Change both endpoints together.

## Frame/FCS rule

The encoder returns logical bytes only. The DW3110 creates/checks the IEEE
802.15.4 2-byte FCS, but the TX length registers must reserve it:

```text
radio_psdu_length = logical_frame_length + 2
```

A SYNC is 22 logical bytes and a 24-byte radio PSDU. Supplying 22 makes the
DW3110 overwrite the two final logical bytes with FCS; the result is CRC-valid
but malformed.

## Build, test, and flash

```sh
git submodule update --init --recursive
west build -b decawave_dwm3001cdk/nrf52833 -d build .
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

The current bench COLETEC probe is `760216755`. Start J-Link with:

```sh
JLinkExe -NoGui 1 -ExitOnError 1 -USB 760216755 \
  -Device NRF52833_XXAA -If SWD -Speed 4000
```

At its prompt, run:

```text
connect
loadfile build/zephyr/zephyr.hex
r
g
exit
```

Never flash this COLETEC image onto the board running Hubert's RECEPTEC.

## Pairing with Hubert's RECEPTEC

Build and flash `Firmware-Atletec/Zephyr` to probe `760216737`, then power both
boards. A successful first exchange logs `COLETEC ready` followed by response
counters. Hubert's side should show a confirmed TX and valid response. Its two
essential safeguards are: include the two FCS bytes in both TX-length calls,
and call `dwt_forcetrxoff()` after every response wait before retrying or moving
to the next slot.

## Layout

| Path | Purpose |
|---|---|
| `src/coletec_radio.c` | COLETEC state machine, timings, DW3110 operations |
| `src/protocol.c` | Byte-exact frame encoder/decoder shared by host tests |
| `src/coletec_sync.c` | Synchronization, guard, and discovery state |
| `src/dw3110_port.c` | Application-owned SPI/GPIO/IRQ bridge |
| `boards/` | DWM3001C DW3110 wiring overlay |
| `tests/` | Host frame and synchronization tests |
| `contracts/` | Pinned shared protocol source of truth |

For historical RECEPTEC scheduler, shell tooling, fault injection, and
qualification evidence, check out `receptec`; do not merge them into COLETEC
`main`.
