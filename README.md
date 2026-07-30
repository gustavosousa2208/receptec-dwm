# RECEPTEC DWM3001C

Fixed UWB base-station firmware for the Atletec EPTS RECEPTEC.

The RECEPTEC initiates scheduled request/response transactions with one or
more COLETEC tags. This application runs on a Qorvo DWM3001C module
(nRF52833 host + DW3110 UWB transceiver).

## Source of truth

This README is an operational cheat sheet for development and communication.
The canonical wire contract is
[`contracts/protocol/uwb-receptec-coletec/HANDOFF.md`](contracts/protocol/uwb-receptec-coletec/HANDOFF.md).
If a value here differs from that document, the canonical contract wins.

The active baseline is **UWB RECEPTEC↔COLETEC v1.0**. Shared or tunable values
must be changed on both endpoints through the process in
[`contracts/VERSIONS.md`](contracts/VERSIONS.md).

### Protocol idea versus current implementation

The v1.0 contract remains unchanged. The current bench firmware intentionally
deviates from one tunable timing value while we validate a reliability fix:

| Item | Protocol v1.0 | Current implementation | Status |
|---|---:|---:|---|
| Sampling period | 1,000 ms | 1,000 ms | Matches |
| Slot duration | 20,000 µs | 20,000 µs | Matches |
| Attempt offsets | 0 / 5,000 / 10,000 µs | 0 / 5,000 / 10,000 µs | Matches |
| Response delay | 1,000 µs | 2,000 µs | Experimental deviation |
| Time-unit meaning | SI microseconds | SI microseconds | Corrected to match |

The response delay was increased after a 21.8-hour run found 608 COLETEC
delayed-TX schedules that were armed too late. With 2,000 µs, a five-minute
check completed 301/301 SYNC/ACK transactions with no new late schedules.
Clock-drift telemetry and the soak logger are diagnostics only; they do not
change the v1.0 wire format.

## Terms to use

| Term | Meaning |
|---|---|
| RECEPTEC | Fixed initiator/base station that schedules requests |
| COLETEC | Mobile responder/tag assigned to one slot |
| Sampling cycle | Complete repeating schedule; currently 1,000 ms |
| Slot | Time reserved for one COLETEC; currently 20 ms |
| RX interval | COLETEC receive time around its slot, including guard |
| Attempt | One request transmission within a slot |
| RMARKER | UWB hardware timestamp for the physical frame reference point |
| SNIFF | Firmware discovery duty cycle; **not** Qorvo hardware SNIFF mode |
| EUI-64 | Eight-byte UWB protocol address |
| J-Link serial | Debug-probe identifier; **not** an on-air address |

Do not call the 2,000 µs response delay a slot. The current slot is 20,000 µs.

## Bench IDs

EUI-64 arrays in the firmware and the bytes transmitted over the air are in
**least-significant-octet-first order**. Nordic factory IDs are printed in the
opposite, human-readable order.

| Role | Slot | Nordic ID as printed | EUI-64 in on-air order | J-Link serial |
|---|---:|---|---|---:|
| RECEPTEC DWM3001C | — | `3E:C6:F3:82:06:A8:16:41` | `41:16:A8:06:82:F3:C6:3E` | `760216737` |
| DWM COLETEC surrogate | 0 | `40:FA:8C:15:D5:02:5D:31` | `31:5D:02:D5:15:8C:FA:40` | `760216755` |
| Production COLETEC | 1 | Not used as its protocol address | `02:00:00:00:00:00:00:02` | `802003538` |
| Intentionally missing peer | 2 | — | `03:00:00:00:00:00:00:03` | — |
| Intentionally missing peer | 3 | — | `04:00:00:00:00:00:00:04` | — |

Example of the byte-order rule:

```text
Displayed RECEPTEC ID:  3E:C6:F3:82:06:A8:16:41
On-air EUI-64 bytes:    41 16 A8 06 82 F3 C6 3E
```

The two missing peers are schedule reservations used to prove fixed timing.
They are not physical boards.

### Debug connections

| Device | Primary diagnostics |
|---|---|
| RECEPTEC | `/dev/cu.usbmodem0007602167371` |
| DWM COLETEC surrogate | `/dev/cu.usbmodem0007602167551` |
| Production COLETEC | J-Link RTT; no available UART |

## Active UWB PHY

Both endpoints must use these values:

| Setting | Value |
|---|---:|
| Channel | 9 |
| Center frequency | 7,987.2 MHz |
| Data rate | 6.8 Mbit/s |
| PRF | 64 MHz |
| TX preamble | 128 symbols |
| TX/RX preamble code | 9 |
| RX PAC | 8 |
| SFD | Qorvo proprietary 8-symbol SFD |
| SFD timeout | 129 symbols |
| PHR mode/rate | Standard / standard |
| STS | Disabled |
| PDOA | Disabled |
| Maximum PSDU | 127 bytes, including FCS |
| FCS | Hardware IEEE 802.15.4 CRC-16 |
| IEEE MAC ACK request | Disabled |
| Hardware frame filtering | COLETEC destination EUI-64 enabled; RECEPTEC disabled |

Initial board/radio tuning values are not wire-format constants:

| Setting | Value |
|---|---:|
| TX pulse-generator delay | `0x34` |
| TX power register | `0xFDFDFDFD` |
| TX antenna delay | 16,385 |
| RX antenna delay | 16,385 |
| DWM fast SPI clock | 32 MHz |
| DWM initialization SPI clock | 2 MHz |

TX power must comply with the deployment region, and antenna delays still
require calibration for the final hardware.

## Frame numbers

All protocol messages use the same IEEE 802.15.4 Data Frame layout. The packet
type and payload change; the MAC layout does not.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Frame Control bytes `41 EC` |
| 2 | 1 | Sequence number |
| 3 | 8 | Destination EUI-64 |
| 11 | 8 | Source EUI-64 |
| 19 | 1 | Reserved/type byte |
| 20 | 0–105 | Opaque payload |
| End | 2 | Hardware-generated FCS |

Frame Control is numeric `0xEC41`, transmitted as `41 EC`. The low nibble of
byte 19 contains the packet type. Its high nibble must be zero.

| Packet | Type ID | Direction | Payload | Logical bytes without FCS | Radio PSDU bytes with FCS |
|---|---:|---|---:|---:|---:|
| `DATA_REQ` | `0x0` | RECEPTEC → COLETEC | 2 | 22 | 24 |
| `SYNC` | `0x1` | RECEPTEC → COLETEC | 2 | 22 | 24 |
| `DATA` | `0x2` | COLETEC → RECEPTEC | 0–105 | 20–125 | 22–127 |
| `ACK` | `0x3` | COLETEC → RECEPTEC | 0 | 20 | 22 |
| `STOP` | `0x4` | RECEPTEC → COLETEC | 0 | 20 | 22 |
| `STOP_ACK` | `0x5` | COLETEC → RECEPTEC | 0 | 20 | 22 |
| Reserved | `0x6`–`0xF` | — | — | — | — |

`DATA_REQ` and `SYNC` carry `DT_sampling` as a 16-bit little-endian
microsecond value. The received PHY length defines the DATA payload length;
there is no separate payload-length field.

The sequence number is unsigned 8-bit, wraps modulo 256, and is maintained
independently for each COLETEC. Retries keep the same sequence number.

## Timing numbers

| Symbol | Meaning | Value |
|---|---|---:|
| `T_sampling` | Sampling cycle | 1,000 ms |
| `T_slot` | One COLETEC slot | 20,000 µs |
| `T_response_delay` | Request RX RMARKER to response TX RMARKER | 2,000 µs |
| `T_timeout` | RECEPTEC response timeout | 3,000 µs |
| `N_attempts_max` | Total attempts, including the first | 3 |
| Attempt offsets | Request positions inside the slot | 0 / 5,000 / 10,000 µs |
| `T_guard` | Initial guard on each side of the slot | 500 µs |
| `DT_guard` | Added guard after each missed slot | 100 µs |
| `N_cnt_guard_max` | Consecutive misses before discovery | 5 |
| `T_on_init` | Initial continuous COLETEC RX | 10 s |
| `T_sniff_on` | Discovery RX-on period | 2 s |
| `T_sniff_off` | Base discovery RX-off period | 2 s |
| `N_sniff_cycles_per_step` | Failed ON periods before increasing OFF time | 3 |
| `N_cnt_sniff_off_max` | Maximum OFF multiplier | 5 |

The response is scheduled in the UWB hardware time domain:

```text
response_tx_rmarker = request_rx_rmarker + 2,000 us
```

The request retry positions are fixed:

```text
attempt 1 = slot_start +     0 us
attempt 2 = slot_start + 5,000 us
attempt 3 = slot_start + 10,000 us
```

The synchronization correction is:

```text
DT_sampling = scheduled_request_tx_rmarker - nominal_slot_start
next_slot_start = request_rx_rmarker + 1,000,000 us - DT_sampling
```

Normal `DT_sampling` values are therefore 0, 5,000, or 10,000 µs.

## Current four-peer schedule

The normal qualification schedule has two real responders and two intentionally
missing peers:

```text
1,000 ms sampling cycle
├─ slot 0,   0–20 ms: DWM COLETEC surrogate
├─ slot 1,  20–40 ms: production COLETEC
├─ slot 2,  40–60 ms: intentionally missing
├─ slot 3,  60–80 ms: intentionally missing
└─ 80–1,000 ms: no configured slot
```

Slots are not compressed when a peer is absent. At 20 ms per slot, the v1
profile allows at most 50 slots in one 1-second sampling cycle.

A synchronized COLETEC receives only around its assigned slot:

```text
expanded_guard = 500 us + missed_slots * 100 us
RX interval = [slot_start - expanded_guard,
               slot_start + 20,000 us + expanded_guard]
```

After the fifth consecutive missed slot, the COLETEC enters discovery. Its
RX-off duration progresses through 2, 4, 6, 8, and 10 seconds, increasing one
step after every three failed 2-second RX-on periods. A valid addressed
request resets the discovery counters and resynchronizes the COLETEC.

## Transaction behavior

| Request | Expected response | Required behavior |
|---|---|---|
| `DATA_REQ` | `DATA` | Freeze one opaque payload for all retries in the slot |
| `SYNC` | `ACK` | Apply the same timing and retry rules as DATA_REQ |
| `STOP` | `STOP_ACK` | ACK duplicates through the slot deadline, then enter STOPPED |

STOPPED is exited by reset, power cycle, or a separately implemented external
wake source. Protocol v1.0 has no over-the-air resume message.

A frame is accepted only when FCS, length, Frame Control, reserved bits, type,
payload length, destination, source, sequence, expected response, and current
protocol state are all valid. Rejected frames do not produce a response or
alter protocol timing/state.

## UWB execution context

The application uses:

| Item | Current value |
|---|---:|
| Dedicated UWB thread stack | 4,096 bytes |
| UWB thread priority | Preemptive priority 0 |
| Main thread stack | 2,048 bytes |
| Radio event handling | GPIO interrupt wakes the UWB thread |
| Timing source | DW3110 40-bit hardware timestamps |
| Request/response scheduling | DW3110 delayed TX/RX |

Thread wake-up time is not used as the protocol timestamp. Interrupt handling
collects the event; RMARKER timestamps and delayed radio operations enforce
the wire timing.

## Runtime UWB shell

The RECEPTEC exposes a UART shell at 115,200 bit/s. Shell callbacks never
operate the DW3110 directly. They queue work for the exclusive UWB protocol
thread, which applies each command at a safe cycle boundary.

```text
uwb status
uwb peers
uwb slotted start
uwb slotted stop
uwb sync start <peer-slot>
uwb sync stop
uwb data_req <peer-slot>
uwb test list
```

The normal image boots with the slotted schedule stopped and the radio idle.
The operator starts traffic with `uwb slotted start`. Periodic SYNC is
independent: it can replace DATA_REQ in one peer's slot while the normal
schedule runs, or run by itself while the normal schedule is stopped. Results
are asynchronous and print from the UWB owner thread.

`uwb test stop <peer-slot>` sends the protocol STOP message. A successful STOP
leaves that COLETEC stopped until it is reset.

### Production COLETEC clock-drift soak

For a single production COLETEC (QM33120W/DW3720, registered as peer slot 1),
leave the normal four-peer schedule stopped and start only its periodic SYNC:

```text
uwb slotted stop
uwb sync start 1
```

The RECEPTEC is monitored over its UART. The production COLETEC has no UART
console, so the monitor starts SEGGER RTT Logger against its J-Link probe:

```sh
python scripts/qualify-soak.py \
  --receptec ~/.serial/receptec-0 \
  --coletec-rtt-probe 802003538 \
  --duration-hours 24
```

Do not leave RTT Viewer, another RTT logger, a debugger, or a flashing process
connected to that probe while the soak monitor is running. The old DWM COLETEC
PoC can still be monitored over UART with `--coletec ~/.serial/coletec-0`.

The production COLETEC compares each request's DW3720 RX RMARKER against the
slot predicted from the previous request, before re-anchoring its schedule.
Since the sampling period is exactly one second, the mean phase error in
microseconds per cycle is numerically equal to relative frequency error in
ppm. The CSV and checkpoint contain the latest phase sample, cumulative
minimum/maximum/sum, sample count, and relative ppm. Positive ppm means request
arrival advances in the COLETEC clock domain relative to its prediction;
negative ppm means it falls behind.

Stop the RECEPTEC traffic after the soak with:

```text
uwb sync stop
```

## Validation status

The current bench has qualified:

- `DATA_REQ/DATA`, `SYNC/ACK`, and `STOP/STOP_ACK` on final COLETEC hardware;
- two live COLETECs in fixed slots 0 and 1, with slots 2 and 3 absent;
- 0 / 5,000 / 10,000 µs retry positions and duplicate responses;
- guarded slot reception, fifth-miss synchronization loss, discovery backoff,
  and automatic re-entry;
- rejection of a correctly addressed request transmitted outside the assigned
  RX interval;
- DW3110 and QM33120W hardware rejection of wrong destination EUI-64 values,
  followed by normal-traffic recovery;
- maximum-size DATA, sequence wraparound, and hardware FCS.

The scenario matrix currently reports **89% coverage**. This is scenario
coverage, not a claim that production hardware is 89% qualified. Remaining
work includes connected-BLE load, intentional same-slot collision, RF
range/interference measurements, and a minimum 1,000-transaction production
soak.

See [`TESTS.md`](TESTS.md) for the test procedure, evidence, scoring method,
limitations, and remaining cases.

## Build and test

Build the DWM3001C application:

```sh
west build -b decawave_dwm3001cdk .
```

Run the host protocol tests:

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Build once and flash both configured DWM J-Link probes:

```sh
./scripts/flash-both.sh
```

## UWB driver

Upstream Zephyr provides the DWM3001CDK nRF52833 board resources but not the
DW3110 UWB driver used by this application. The board's existing
`zephyr,ieee802154` choice is the Nordic 2.4 GHz radio and is not used by this
protocol.

This application pins Qorvo DW3XXX driver 08.19.02 under `vendor/qorvo`. See
its README for provenance and proprietary license information.
