# UWB Protocol v1.0 Test Guide

Last updated: 2026-07-26

This document explains how the RECEPTEC–COLETEC UWB protocol has been tested,
what has been proven, what remains unproven, and how to repeat the qualification
with the communications team.

The canonical requirements remain:

- `contracts/protocol/uwb-receptec-coletec/HANDOFF.md`
- `contracts/protocol/uwb-receptec-coletec/protocol-v1.0.md`

This file is a test and evidence guide. It does not redefine the protocol.

## 1. Executive summary

The protocol behavior was developed and qualified using two DWM3001C boards:

- one DWM3001C acted as the real RECEPTEC;
- one DWM3001C acted as a temporary COLETEC surrogate.

That setup proved the wire format, timing model, retries, synchronization,
discovery, and acknowledged STOP before introducing the additional BLE, IPC,
power-management, and sensor load of the production COLETEC.

The production COLETEC is different hardware:

- nRF52840 coordinator;
- QM33120W module containing the DW3720 UWB radio;
- BLE and Nordic–STM32 IPC active on the same MCU.

Therefore, a DWM result proves the protocol design and the RECEPTEC behavior,
but does not by itself prove production COLETEC timing. The production port
is qualified separately; its current over-the-air evidence is recorded below.

Current headline results:

- 1,000/1,000 consecutive addressed transactions completed on the DWM setup;
- all three deterministic attempt offsets were exercised;
- maximum 105-byte DATA frames were exchanged;
- duplicate requests returned frozen, byte-identical payloads;
- guard expansion and the complete discovery backoff were exercised;
- normal, dropped, duplicate, and wrong-sequence STOP_ACK cases passed;
- the IRQ-driven DWM implementation measured a maximum 732 us from GPIO IRQ
  entry to delayed-TX arm, with zero late schedules in the final run;
- production nRF52840/QM33120W completed at least 298 consecutive observed
  slot-1 exchanges on attempt 1 while the DWM surrogate remained live in
  slot 0;
- production RX-to-TX timing measured exactly 1,000 us;
- production DATA, ACK, and STOP_ACK all passed on attempt 1 after the final
  SPIM3 and compact-buffer optimization;
- stable production IRQ-to-delayed-TX-arm latency was approximately 701 us;
- the conservative IRQ-to-return-from-`dwt_starttx()` time was approximately
  884 us, leaving about 116 us before the response RMARKER;
- both live peers crossed sequence wraparound from 255 to 0 without failure.

## 2. Active test profile

Both ends must use the same profile.

| Parameter | Value |
|---|---:|
| Channel | 9 |
| Data rate | 6.8 Mbit/s |
| PRF | 64 MHz |
| Preamble | 128 symbols |
| TX/RX preamble code | 9 |
| RX PAC | 8 |
| SFD | Qorvo proprietary 8-symbol |
| SFD timeout | 129 symbols |
| PHR mode/rate | Standard |
| STS | Disabled |
| PDOA | Disabled |
| Hardware FCS | Enabled |
| Hardware frame filtering | COLETEC destination EUI-64 enabled |
| Sampling period | 1,000 ms |
| Slot duration | 20,000 us |
| Response delay | 1,000 us from request RX RMARKER |
| Response timeout | 3,000 us |
| Attempt offsets | 0, 5,000, and 10,000 us |
| Maximum attempts | 3, including the first |
| Initial guard | 500 us on each side |
| Guard increase | 100 us per missed slot |
| Synchronization loss | Fifth consecutive missed slot |

The 20 ms slot is intentionally larger than the retry schedule:

```text
last attempt + timeout = 10,000 us + 3,000 us = 13,000 us
slot deadline          = 20,000 us
```

This leaves 7,000 us before the next slot boundary. Retries never move the next
slot or the next one-second sampling epoch.

## 3. Test levels

### 3.1 Host tests

Host tests run without radio hardware. They verify deterministic logic:

- exact frame bytes and FCF;
- EUI-64 on-air order;
- packet type and reserved bits;
- `DT_sampling` encoding;
- minimum and maximum frame lengths;
- malformed-frame rejection;
- DATA payload freezing;
- STOP and STOP_ACK encoding;
- guard counter behavior;
- discovery backoff and reset.

Host tests are fast and repeatable, but they cannot prove SPI, IRQ latency,
hardware FCS, RF reception, delayed TX, or power behavior.

### 3.2 Firmware build tests

Each hardware test begins with a clean Zephyr build. This verifies:

- the selected board and overlay;
- Qorvo driver integration;
- static buffers and stack configuration;
- test-only Kconfig fault injection;
- that no direct Zephyr source modification is required.

### 3.3 Over-the-air hardware tests

Hardware tests use UART from the DWM boards and J-Link RTT from the production
COLETEC. RTT is the primary production telemetry path because that board has no
available UART; BLE and forwarding diagnostics through the STM32 are not used
for timing evidence. The test operator checks:

- the selected role and device ID;
- device ID and PHY initialization;
- sequence, type, address, and FCS results;
- attempt number and fixed offset;
- transaction and fault counters;
- delayed-TX late counters;
- synchronization and discovery transitions;
- STOPPED state and radio shutdown.

UART/RTT diagnostics are deferred until timing-critical deadlines have passed.
Logging inside the response hot path is prohibited because it can create a
false late-TX failure.

### 3.4 Fault-injection tests

Fault injection deliberately creates a known failure while leaving the frame
and radio path otherwise normal. Existing cases can:

- drop the first one or two responses;
- transmit a valid-FCS response with a wrong sequence;
- silence RECEPTEC for a selected number of one-second cycles;
- initiate STOP after a selected number of DATA transactions.

After every fault test, both devices are rebuilt and restored with all
fault-injection options set to zero.

## 4. Original DWM bench setup

The original qualification used:

| Role | Nordic device ID | J-Link serial |
|---|---|---:|
| RECEPTEC | `3E:C6:F3:82:06:A8:16:41` | `760216737` |
| Temporary COLETEC | `40:FA:8C:15:D5:02:5D:31` | `760216755` |

Both DWM boards ran the same image. The nRF52833 factory device ID selected the
runtime role.

The normal four-peer schedule was:

```text
one-second sampling cycle
├─ slot 0,  0–20 ms: live DWM COLETEC
├─ slot 1, 20–40 ms: production COLETEC reservation, absent
├─ slot 2, 40–60 ms: absent
├─ slot 3, 60–80 ms: absent
└─ 80–1000 ms: no configured slot
```

The temporary COLETEC listened through its own slot plus guard:

```text
RX interval = [slot start - guard, slot end + guard]
```

With a 500 us guard, it could hear the first 500 us of slot 1. The slot-1
request used a different destination EUI-64 and was discarded without a
response or state change. The radio was not receiving during slots 2 and 3.

## 5. How the existing DWM tests are run

Run these commands from the root of this `receptec-dwm` repository.

### 5.1 Host suite

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The host executable uses assertions and fails immediately when a test vector
does not match.

### 5.2 Normal hardware image

```sh
source ../.venv/bin/activate
west build -p always -b decawave_dwm3001cdk/nrf52833 -d build .
./scripts/flash-both.sh
```

Flashing is permitted only when the selected probes and roles have been
confirmed. The normal image remains idle after boot. Connect to the RECEPTEC
UART and enter `uwb slotted start` before collecting normal schedule evidence.

### 5.3 Fault configuration

Example: force the first two responses to be dropped so attempt 3 is required.

```sh
source ../.venv/bin/activate
west build -p always -b decawave_dwm3001cdk/nrf52833 -d build . -- \
  -DEXTRA_CONF_FILE=tests/m7-drop-two-acks.conf
./scripts/flash-both.sh
```

Existing fragments:

| Fragment | Purpose |
|---|---|
| `tests/m7-drop-two-acks.conf` | Drop responses 1 and 2; require attempt 3 |
| `tests/m7-two-bad-acks.conf` | Wrong sequence on responses 1 and 2 |
| `tests/m8-five-misses.conf` | Seven-slot silence; show every guard step and reentry |
| `tests/m8-discovery.conf` | 100-slot silence; exercise discovery OFF multiplier through 5 |
| `tests/m9-normal-stop.conf` | Normal STOP after three DATA transactions |
| `tests/m9-drop-stop-ack.conf` | Drop first DATA/STOP_ACK response |
| `tests/m9-bad-stop-ack.conf` | Send wrong sequence before corrected duplicate |
| `tests/production-stop.conf` | Target production peer slot 1 with STOP after three DATA transactions |
| `tests/production-sync.conf` | Target production peer slot 1 with SYNC after three DATA transactions |
| `tests/production-discovery.conf` | Silence both live peers for 100 cycles and qualify production discovery/backoff/reentry |
| `tests/production-out-of-slot.conf` | Send a production-addressed DATA_REQ at 200 ms and require no response |

The build must be made pristine again without `EXTRA_CONF_FILE` after fault
testing. Verify the final `.config` contains zero for every test fault option.

### 5.4 Runtime UART shell

Connect at 115,200 bit/s and press Enter:

```sh
picocom -b 115200 /dev/cu.usbmodem0007602167371
```

The shell queues commands to the exclusive UWB owner. A command does not call
the Qorvo driver from the shell thread and therefore cannot race the protocol
thread. State changes normally take effect after the current one-second cycle
finishes. The normal image boots with the schedule stopped; begin normal
traffic with `uwb slotted start`.

Manual operation:

| Command | Result |
|---|---|
| `uwb status` | Show active modes, cycle, and queued/running operation |
| `uwb peers` | Show slot numbers, peer names, EUI-64 values, and expected presence |
| `uwb slotted start` | Start the normal four-peer one-second schedule |
| `uwb slotted stop` | Finish the current cycle, force radio idle, then wait |
| `uwb sync start <slot>` | Send SYNC every second to one peer |
| `uwb sync stop` | Stop periodic SYNC |
| `uwb data_req <slot>` | Queue one DATA_REQ/DATA transaction |

Runtime test battery:

| Command | Test |
|---|---|
| `uwb test list` | List runtime tests and external prerequisites |
| `uwb test sync <slot>` | One addressed SYNC/ACK |
| `uwb test stop <slot>` | STOP/STOP_ACK; the peer must be reset afterward |
| `uwb test wrong_eui <slot>` | One valid frame with an unknown destination EUI-64 |
| `uwb test out_of_slot <slot>` | Correctly addressed request at 200 ms; slotted mode must be running |
| `uwb test silence <cycles>` | Suppress every UWB request for the selected one-second cycles |
| `uwb test abort` | Abort a queued test or active silence at a safe boundary |

The shell cannot make a remote COLETEC deliberately drop a response or return
a wrong sequence. Retry/drop and bad-sequence cases still require the matching
COLETEC fault image. Collision testing still requires an independent
transmitter.

### 5.5 Long qualification monitor

The DWM repository includes a dual-UART monitor:

```sh
python scripts/qualify-m5.py \
  --receptec /dev/tty.usbmodem0007602167371 \
  --coletec /dev/tty.usbmodem0007602167551 \
  --target 1000
```

It records both serial streams, counts completed transactions and attempt
failures, and resets the consecutive-success count on final transaction
failure. Start the RECEPTEC schedule from its shell before launching the
qualification monitor.

Raw serial captures are stored under the ignored `logs/` directory. Important
results are summarized in `receptec-dwm/MILESTONES.md`, but the raw logs are
not currently versioned. For partner qualification, preserve the raw log,
firmware commit IDs, `.config`, board IDs, distance, orientation, and test date
together.

### 5.6 Long-duration drift soak

`qualify-m5.py` proves ~1,000 transactions (~17 minutes); it does not
demonstrate clock-drift stability over real time. `scripts/qualify-soak.py`
runs the same dual-UART capture with no fixed transaction target, intended
for an overnight or multi-day run against the normal four-peer DATA_REQ/DATA
schedule:

```sh
python scripts/qualify-soak.py \
  --receptec /dev/tty.usbmodem0007602167371 \
  --coletec /dev/tty.usbmodem0007602167551 \
  --duration-hours 12
```

Omit `--duration-hours` to run until interrupted (Ctrl+C). Start the
RECEPTEC schedule from its shell (`uwb slotted start`) before launching the
monitor, same as §5.5.

It parses telemetry the firmware already emits — no firmware change is
required:

- `Protocol counters` (cumulative txns/attempts/timeouts/rx_errors/late),
  dumped every 100 successful exchanges and on every failure;
- `COLETEC slot missed` / guard-window growth and `COLETEC synchronization
  lost` (drift-recovery events — the primary signal a long soak is for);
- `COLETEC discovery reentry` (successful resync after a loss);
- `COLETEC timing` (`IRQ-to-arm` and `max-IRQ-to-arm`), sampled every 10
  responses.

Output, all under `logs/`:

- the same raw dual-role log as §5.5;
- a CSV with one row per parsed event, so drift/latency trends can be
  plotted over the run;
- a periodic JSON checkpoint (default every 5 minutes) so an interrupted or
  crashed overnight run still leaves usable data.

This measures long-run *stability* at bench ambient temperature, not
full-range oscillator ppm characterization — real temperature variation is
a separate, still-open item (see §8).

## 6. Executed test cases and evidence

### 6.1 Wire format and validation

Executed:

- byte-exact SYNC vector;
- little-endian `DT_sampling` values 0, 5,000, 10,000, and 65,535;
- maximum 105-byte payload;
- wrong FCF;
- reserved high type nibble;
- undefined packet type;
- too-short logical frame;
- wrong destination;
- wrong source/sequence through runtime checks;
- STOP and STOP_ACK with no payload.

Hardware FCS validation is performed by the DW3xxx. Firmware receives a
successful RX-good event only for a valid FCS.

### 6.2 Addressed SYNC/ACK soak

Result:

- 1,000/1,000 completed transactions;
- elapsed time: 1,025.6 seconds;
- attempt 1: 955 transactions;
- attempt 2: 43 transactions;
- attempt 3: 2 transactions;
- 1,047 transmitted attempts;
- 11 response timeouts;
- 36 radio RX errors;
- zero retry exhaustion;
- zero delayed-scheduling errors;
- zero invalid length/FCF/type/address/sequence.

This run crossed sequence-number wraparound multiple times.

Natural SFD timeout events were observed during development. They demonstrated
why retries are necessary. A prior exploratory run had 11 final failures among
1,776 transactions before retry qualification and timing behavior were
stabilized; it is not used as the final acceptance result.

### 6.3 DATA_REQ/DATA

Executed:

- 20/20 normal maximum-size DATA transactions on attempt 1;
- 105 payload bytes per response;
- maximum 127-byte PSDU including FCS;
- diagnostic source EUI and sample counter validation;
- deterministic fill validation;
- duplicate request with wrong response sequence on attempts 1 and 2;
- corrected attempt 3 accepted;
- duplicate DATA payload compared byte-for-byte.

The wire-protocol layer treats DATA as opaque. Diagnostic payload inspection is
a test-application feature, not a protocol requirement.

### 6.4 Deterministic retries

Executed:

- natural attempt-2 recovery;
- forced attempt-3 recovery;
- wrong-sequence response rejection;
- unchanged request sequence across retries;
- exact attempt offsets 0, 5,000, and 10,000 us;
- `DT_sampling` removal from the next nominal slot;
- no cumulative drift from retry timing.

The forced attempt-3 test measured the duplicate nominal-slot calculation
within approximately 0.015 us of the expected hardware-time value.

### 6.5 Four-peer schedule and filtering

Executed:

- DWM surrogate live in slot 0;
- production nRF52840/QM33120W live in slot 1;
- two absent peers in slots 2 and 3;
- all three attempts consumed for each missing peer;
- no compression of later slots;
- no change to the one-second cycle epoch;
- the earlier absent-slot test verified that wrong-destination traffic during
  the slot-1 guard overlap generated no response;
- COLETEC RX disabled for the later out-of-window slots.

This is not equivalent to testing two live COLETECs transmitting at the same
time.

### 6.6 Guard and discovery

Executed:

- normal 500 us guard;
- consecutive-miss guards 600, 700, 800, and 900 us;
- synchronization loss on miss 5;
- 2-second discovery ON;
- discovery OFF periods increasing through 2, 4, 6, 8, and 10 seconds;
- three failed ON periods before each multiplier increase;
- requests during discovery OFF generated no state change;
- successful automatic reentry;
- next nominal cadence restored after reentry.

The final IRQ-driven seven-slot silence test reentered on cycle 33, attempt 1.
Its maximum measured GPIO-IRQ-to-delayed-TX-arm time was 732 us with zero late
schedules.

### 6.7 STOP/STOP_ACK

Executed on the DWM pair:

- 20 normal reset-and-STOP runs;
- three DATA transactions followed by STOP;
- STOP_ACK scheduled at RX RMARKER + 1,000 us;
- first STOP_ACK deliberately dropped in five runs;
- duplicate STOP acknowledged on attempt 2;
- wrong-sequence STOP_ACK rejected;
- corrected duplicate accepted;
- RX kept active until the absolute slot deadline;
- UWB disabled only after the deadline;
- reset restored normal operation;
- zero STOP buffer-write failures and zero late schedules in accepted runs.

Executed on the production COLETEC:

- RECEPTEC targeted production peer slot 1 after three DATA transactions;
- DATA, ACK, and STOP_ACK were preloaded at compact DW3720 TX-buffer offsets
  64, 0, and 32 respectively;
- all start offsets remain within the DW3720 direct-offset range;
- TX frame control and delayed time were programmed in one contiguous SPI
  burst;
- the final STOP received a valid, correctly addressed STOP_ACK on attempt 1;
- RECEPTEC then entered `STOPPED` and disabled UWB;
- the STOP response armed at approximately 701 us and returned from
  `dwt_starttx()` at approximately 884 us.

After this test, the normal RECEPTEC image was restored and the production
COLETEC was reset. Both live peers then completed more than 50 additional
observed cycles on attempt 1 while the two absent peers exhausted their
expected retries.

### 6.8 Production SYNC/ACK

Executed:

- RECEPTEC targeted production peer slot 1 after three DATA transactions;
- SYNC carried the normal two-byte `DT_sampling` correction;
- production COLETEC selected the preloaded ACK at offset 0;
- ACK passed on attempt 1 with valid FCS, addressing, type, and sequence;
- the measured response RMARKER remained exactly 1,000 us after request
  reception.

### 6.9 Four-slot production rerun — 2026-07-27

The normal images were flashed to the RECEPTEC, DWM surrogate COLETEC, and
production COLETEC. The captured UART interval covered cycles 25 through 118:

- slot 0 DWM and slot 1 production COLETEC answered on attempt 1 in every
  captured cycle;
- slots 2 and 3 each consumed the fixed three attempts and timed out as
  expected;
- valid FCS, addressing, maximum 105-byte DATA, and fixed slot ordering passed;
- the RECEPTEC reported `live=100` at cycle 106 with zero RX errors;
- production RTT showed zero steady-state RX errors/timeouts, an exact 1,000 us
  response delay, and no increase in its initial late-schedule counter;
- production BLE advertising and STM32 IPC traffic remained active.

### 6.10 Production discovery and out-of-slot rejection — 2026-07-27

The production COLETEC was synchronized in slot 1 before the RECEPTEC stopped
all requests for 100 sampling periods. Production RTT recorded:

- missed-slot guards of 600, 700, 800, and 900 us;
- synchronization loss on the fifth missed slot;
- discovery OFF intervals of 2, 4, 6, 8, and 10 seconds;
- three unsuccessful discovery ON periods before each multiplier increase;
- automatic reentry on sequence 12, attempt 2;
- normal attempt-1 DATA responses from the following cycle.

A separate test transmitted a valid DATA_REQ addressed to the production
COLETEC at 200 ms from the cycle epoch. Its assigned slot-1 receive interval
had ended near 40.5 ms. The request produced a real response timeout, and the
next scheduled slot-1 DATA transaction passed on attempt 1. This directly
qualifies the protocol behavior outside the assigned receive interval.

### 6.11 Hardware destination filtering — 2026-07-27

The normal COLETEC receive path was configured with IEEE 802.15.4 Data Frame
filtering and each COLETEC's local on-air EUI-64:

- DWM surrogate/DW3110: `31:5D:02:D5:15:8C:FA:40`;
- production QM33120W: `02:00:00:00:00:00:00:02`.

After three correct exchanges per live peer, the RECEPTEC replaced one normal
request in each assigned slot with a valid-FCS DATA_REQ addressed to an
unknown EUI-64:

- slot 0 sent `B1:5D:02:D5:15:8C:FA:40`;
- slot 1 sent `82:00:00:00:00:00:00:02`.

Both requests produced a real response timeout. The first radio event in each
assigned COLETEC slot reported the Qorvo hardware address-filter rejection
event (`ARFE`, bit `0x20000000`):

- DWM surrogate J-Link counter:
  `coletec_slot_first_event_filter_rejections = 1`;
- production RTT:
  `hw-filter-rejections=7 first-event-filter=1`.

The larger production cumulative count includes other peers heard during
initial discovery and guard overlap. `first-event-filter=1` is the decisive
measurement: the first frame in the assigned slot was rejected by hardware
before software decoding.

On the next cycle both live peers recovered on attempt 2, because the rejected
request counted as one missed synchronized slot. Both returned to attempt 1
from the following cycle. Software validation of destination, source, type,
length, and sequence remains enabled after hardware acceptance.

### 6.12 Runtime UWB shell — 2026-07-27

The normal shell-enabled RECEPTEC image was exercised against both live
COLETECs:

- `uwb status` and `uwb peers` reported the four configured slots correctly;
- `uwb slotted stop` completed the active cycle, forced the radio idle, and
  stopped all periodic output;
- `uwb data_req 1` completed one production DATA transaction while the normal
  schedule remained stopped;
- `uwb sync start 1` ran independently once per second, recovered the
  production COLETEC from discovery, and then completed on attempt 1;
- `uwb sync stop` returned the radio to idle;
- `uwb slotted start` restored the four-peer schedule and both live peers;
- one-shot SYNC/ACK completed on production slot 1 at attempt 1;
- runtime wrong-EUI and out-of-slot tests both produced no response and passed;
- a two-cycle silence completed and recovered automatically;
- a five-cycle silence was aborted safely, after which normal traffic
  recovered;
- final status was `slotted=running`, `periodic SYNC=stopped`, and
  `one-shot/test=none`.

The production antenna fault made some post-silence recovery requests require
attempt 2, but steady traffic returned to attempt 1. No shell callback touched
the radio directly; every operation was executed by the existing UWB owner.

## 7. Coverage calculation

This project does not currently run `gcov`, `lcov`, or compiler-based
line/branch instrumentation. No source-line coverage percentage is claimed.

The percentage below is **protocol scenario coverage**. It is calculated from
18 externally observable requirement areas:

- Pass = 1 point;
- Partial = 0.5 point;
- Not run = 0 points.

| # | Requirement area | Status | Score | Reason |
|---:|---|---|---:|---|
| 1 | Matched PHY on final hardware | Pass | 1.0 | DW3110 and production QM33120W exchanged maximum DATA OTA with STS disabled |
| 2 | Byte-exact frame codec | Pass | 1.0 | Host vectors and DWM interoperability |
| 3 | Malformed-frame software validation | Partial | 0.5 | Host coverage is broad; OTA valid-FCS injection covered mainly wrong sequence |
| 4 | Hardware-generated/validated FCS | Pass | 1.0 | Verified through DW3xxx RX-good events |
| 5 | Source and destination filtering | Pass | 1.0 | Wrong destination injected in both live assigned slots; DW3110 and QM33120W reported hardware ARFE and produced no response |
| 6 | Opaque DATA lengths and freezing | Partial | 0.5 | Zero and maximum DATA host boundaries plus maximum OTA DATA tested; intermediate lengths are intentionally not enumerated |
| 7 | Sequence matching and wraparound | Pass | 1.0 | Wrong sequences rejected; DWM and production runs crossed wraparound |
| 8 | Response at RX RMARKER + 1,000 us | Pass | 1.0 | DWM and production QM33120W hardware timestamp qualification |
| 9 | Attempts at 0/5,000/10,000 us | Pass | 1.0 | Natural and injected tests |
| 10 | Lost-response recovery | Pass | 1.0 | Attempts 2 and 3 verified |
| 11 | Duplicate request behavior | Pass | 1.0 | Frozen DATA and duplicate STOP_ACK verified |
| 12 | Fixed multi-peer slot schedule | Pass | 1.0 | Two live and two absent peers; no compression |
| 13 | RX disabled outside assigned interval | Pass | 1.0 | Correctly addressed production DATA_REQ at 200 ms received no response; next assigned slot passed |
| 14 | Guard expansion and fifth-miss loss | Pass | 1.0 | Complete controlled sequence |
| 15 | Discovery backoff and reentry | Pass | 1.0 | Full multiplier range and automatic recovery |
| 16 | Acknowledged STOP and reset recovery | Pass | 1.0 | Full DWM fault suite plus production STOP_ACK on attempt 1 and normal-image recovery |
| 17 | Two live COLETECs/collision behavior | Partial | 0.5 | Two live tags passed in separate slots; intentional same-slot collision not run |
| 18 | Production nRF52840/QM33120W interoperability under BLE/IPC load | Partial | 0.5 | 298+ observed cycles passed with BLE advertising and STM32 IPC streaming; connected BLE notifications and long soak remain |

```text
14 Pass + 4 Partial × 0.5 = 16 points
16 / 18 = 89% scenario coverage
```

Important interpretation:

- **89% does not mean the production COLETEC is 89% validated.**
- Production baseline OTA interoperability and timing now pass, but retries,
  recovery, connected-BLE load, and the 1,000-transaction production soak have
  not yet been fully repeated on final hardware. Production DATA, SYNC/ACK, and
  STOP/STOP_ACK now pass on attempt 1.

## 8. Tests not yet executed

| Missing test | Why it was not done | What is needed |
|---|---|---|
| UWB timing during connected BLE load | BLE advertising and STM32 streaming passed, but BLE was not connected | BLE connection plus active notifications while repeating normal/retry/STOP |
| Intentional simultaneous collision | No independent third transmitter | Two programmable tags or a UWB packet injector |
| Every DATA payload length 0–105 | Not planned: FCS protects all lengths and exhaustive enumeration adds little protocol evidence | Add selected intermediate lengths only if a length-dependent defect appears |
| Real oscillator drift | `scripts/qualify-soak.py` (§5.6) captures timestamp/guard drift over a long bench run; real ppm characterization still needs temperature variation | Overnight/multi-day run with `qualify-soak.py`, then a temperature-varied run |
| Range/orientation/body-loss test | Bench testing was at short uncontrolled distance | Agreed distances, antenna orientations, athlete/body phantom |
| Interference/channel occupancy | No controlled interferer or spectrum setup | UWB interferer/sniffer and repeatable RF environment |
| External on-air timing measurement | Current evidence uses internal RMARKER timestamps | UWB sniffer or synchronized logic-analyzer/radio instrumentation |
| Current consumption by radio state | Original DWM test optimized behavior, not production power | Production board plus current profiler |
| Malformed-FCS firmware behavior | DW3xxx hardware discards bad FCS before firmware | Packet injector and hardware RX-error counter analysis |
| Long production soak | Initial production run covered 298+ observed RECEPTEC cycles and 425 production responses | Run `scripts/qualify-soak.py` (§5.6) overnight against production hardware; Gate 8 minimum is 1,000 transactions but an unattended overnight run is preferred |
| Security/STS | Intentionally excluded from protocol v1.0 | Agreed key, IV, persistence, update, and resynchronization design |

## 9. Production COLETEC qualification sequence

Use this order with the communications team. Do not begin with a long soak.

### Current production-hardware state

- one exclusive 4,096-byte UWB owner thread at metaIRQ priority `-16`;
- the GPIO ISR only timestamps the IRQ and wakes the owner;
- STS is disabled and the complete active PHY profile is configured;
- DATA, ACK, and STOP_ACK are preloaded at offsets 64, 0, and 32;
- UWB uses the nRF52840 SPIM3 instance at the QM33120W-supported 32 MHz rate;
- timing-critical transfers use polling EasyDMA inside the exclusive UWB owner,
  avoiding a scheduler round trip per small SPI register access;
- cached TX frame control and delayed time are written as one register burst;
- QM33120W destination EUI-64 filtering is enabled while complete software
  validation remains in place;
- delayed responses use an absolute TX time calculated from the request RX
  RMARKER, avoiding the extra SPI correction performed by `DLY_RS`;
- RX is enabled only for the assigned slot/guard or a discovery ON interval;
- no logging, BLE, IPC, allocation, or payload construction occurs before
  delayed TX is armed;
- the `ATLETEC_HW` application build and host protocol tests pass;
- build usage is 333,952 bytes flash (31.85%) and 82,432 bytes RAM (31.45%);
- production GPIO IRQ, 32 MHz SPIM3, maximum 105-byte DATA, FCS, addressing, and
  RX RMARKER + 1,000 us timing pass OTA;
- two live COLETECs pass in fixed slots 0 and 1 while slots 2 and 3 remain
  absent;
- the observed run crossed sequence 255 to 0 under BLE advertising and STM32
  IPC traffic;
- the production UWB thread used 712 of 4,096 stack bytes (17%).

The production counter reached at least 425 DATA responses in the initial run.
After restoring both normal images, a separate smoke run reached 107 DATA
responses with zero RX errors/timeouts, 1,000 us measured RX-to-TX delay, and
762 us maximum IRQ-to-arm-request latency. Five RX timeouts in the initial run
occurred while RECEPTEC was deliberately reflashed; three early late schedules
did not increase during steady-state operation.

The final production STOP and SYNC tests both passed on attempt 1. Stable
responses reached approximately 701 us IRQ-to-arm-request latency and 884 us
IRQ-to-return-from-`dwt_starttx()` latency, leaving about 116 us before the
1,000 us response RMARKER. One late schedule occurred during initial discovery
when the live RECEPTEC sequence could not have been predicted; deterministic
retry acquired synchronization, after which the response sequences were staged
before each slot.

Connected BLE notifications, the complete retry/recovery fault suite, and the
long soak remain. The current successful workspace build uses Zephyr 4.3.0;
Gate 1 still requires a clean build in the intended NCS v3.2.1 environment.

### Gate 1 — Build and static checks

- clean NCS v3.2.1 build;
- STS confirmed disabled;
- exact PHY values printed once at startup;
- protocol host tests pass;
- no fault injection enabled;
- thread stacks and priorities recorded.

### Gate 2 — Raw radio and IRQ

- QM33120W device ID;
- reset and repeated SPI reads;
- GPIO IRQ delivery;
- RX-good, RX-error, RX-timeout, and TX-complete status clearing;
- no 10 ms polling dependency;
- thread analyzer confirms no stack overflow.

### Gate 3 — Normal protocol

- at least 20 DATA_REQ/DATA transactions;
- all expected addresses, types, sequences, and FCS valid;
- response TX RMARKER exactly 1,000 us after request RX RMARKER;
- zero final failures and zero late schedules;
- record maximum IRQ-to-arm latency.

### Gate 4 — Retry and rejection

- drop response 1 and recover on attempt 2;
- drop responses 1 and 2 and recover on attempt 3;
- send a wrong sequence and verify rejection;
- send a wrong destination EUI-64 and verify a hardware `ARFE` event;
- verify duplicate payload remains byte-identical;
- verify next nominal slot has no retry-induced drift.

### Gate 5 — Scheduling and recovery

- four-peer schedule with absent peers;
- wrong-address traffic during guard overlap;
- four guard increments;
- discovery entry on miss 5;
- discovery OFF multiplier through 5;
- automatic reentry.

### Gate 6 — STOP

- normal STOP_ACK;
- dropped first STOP_ACK;
- duplicate STOP;
- wrong sequence;
- UWB disabled after slot deadline;
- reset recovery.

### Gate 7 — Production load

Repeat normal, retry, recovery, and STOP while:

- BLE is connected;
- notifications are active;
- STM32 IPC is streaming;
- status and battery workers are active;
- the thread analyzer reports stack use;
- synchronous logging is reduced to the agreed test level.

### Gate 8 — Soak

- minimum 1,000 consecutive transactions;
- report attempt distribution;
- report final failures separately from recovered attempts;
- report RX errors, timeouts, invalid frames, late TX/RX, and discovery entries;
- preserve raw logs and exact firmware commits.

## 10. Questions likely from the communications team

### Why is the response delay 1 ms?

It is the shared protocol value. The COLETEC does not wait 1 ms in software.
It programs delayed TX using the DW3xxx hardware clock:

```text
response TX RMARKER = request RX RMARKER + 1,000 us
```

Thread/ISR latency only determines whether firmware arms that hardware event in
time.

### Why is each slot 20 ms?

The last attempt starts at 10 ms and may wait 3 ms for a response. A 20 ms slot
contains all attempts and leaves margin without moving the next device's slot.

### Why can the COLETEC hear part of the next slot?

The 500 us post-slot guard tolerates clock drift. Address validation prevents a
request for the next COLETEC from changing state or generating a response.

### Why are retries deterministic rather than random?

Both devices must know when RX is required. Fixed offsets make the complete slot
and `DT_sampling` correction deterministic.

### What does hardware frame filtering validate?

Each COLETEC radio rejects IEEE Data Frames whose destination EUI-64 does not
match its programmed local address. The RECEPTEC source EUI-64, packet type,
length, sequence, and protocol state are still validated in software.

### Why is STS disabled?

No key, IV persistence, IV update, or resynchronization procedure has been
agreed. Enabling STS independently would make the two devices incompatible and
would not create a complete security design.

### What is the difference between an attempt failure and a transaction failure?

An attempt may time out or encounter an RX error and still recover on attempt 2
or 3. A transaction fails only when all three attempts are exhausted. Reports
must keep these numbers separate.

### What is currently the biggest production risk?

Timing margin under connected BLE load. Stable DATA, ACK, and STOP_ACK now arm
at approximately 701 us and return from the delayed-start call at approximately
884 us, leaving about 116 us before the required 1,000 us RMARKER. Steady-state
traffic passed with BLE advertising and STM32 IPC streaming, but connected
notifications and the full retry/recovery suite still need qualification.

## 11. Test-record template

Record this information for every partner run:

```text
Date/time:
Operator:
Location:
RECEPTEC commit:
COLETEC commit:
Contracts commit:
RECEPTEC board/ID:
COLETEC board/ID:
PHY profile:
Fault configuration:
Distance/orientation:
BLE state:
STM32 IPC state:
Test duration:
Transactions completed/started:
Attempt 1/2/3 counts:
Timeouts:
RX/FCS errors:
Invalid length/FCF/type/address/sequence:
Retry exhaustion:
Late TX/RX:
Maximum IRQ-to-arm:
Discovery entries/reentries:
STOP result:
Raw-log path:
Result: PASS / FAIL
Notes:
```

The test is not considered reproducible without the two firmware commits, the
contracts commit, the active configuration, and the raw serial capture.
