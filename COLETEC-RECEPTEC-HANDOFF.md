# COLETEC–RECEPTEC Interoperability Handoff

**Status:** Initial implementation contract

**Protocol baseline:** RECEPTEC–COLETEC v1.0 review-integrated draft

**Purpose:** Freeze the behavior required for the first reliable over-the-air
interoperability milestone. Values marked *tunable* may be changed later, but
both devices must always use the same active profile.

## 1. Initial milestone

The initial milestone is reliable addressed request/response communication
between one RECEPTEC and one or more COLETECs. Application data is opaque to
this protocol.

The milestone includes:

- matched UWB PHY configuration;
- software encoding and validation of protocol frames;
- hardware-timestamped delayed transmission;
- deterministic retransmission within an absolute slot;
- duplicate request handling;
- synchronization correction through `DT_sampling`;
- acknowledged STOP processing.

Registration and application payload interpretation remain outside the wire
protocol. The RECEPTEC may obtain its COLETEC list from compile-time data,
persistent storage, a management interface, or another application component.

## 2. UWB PHY profile

Both devices shall use the following initial profile:

| Setting | Initial value | Classification |
|---|---:|---|
| Channel | 9 | Shared, tunable |
| Center frequency | 7987.2 MHz | Derived |
| Data rate | 6.8 Mbit/s | Shared, tunable |
| PRF | 64 MHz | Shared, tunable |
| TX preamble length | 128 symbols | Shared, tunable |
| TX/RX preamble code | 9 | Shared, tunable |
| RX PAC | 8 | Shared, tunable |
| SFD | Qorvo proprietary 8-symbol SFD | Shared, tunable |
| SFD timeout | 129 symbols | Derived from `128 + 1 + 8 - 8` |
| PHR mode | Standard | Shared |
| PHR rate | Standard | Shared |
| STS mode | Disabled | Shared initial requirement |
| STS length | 64, ignored while STS is disabled | Tunable |
| PDOA | Disabled | Shared initial requirement |
| Maximum PSDU | 127 bytes, including FCS | Protocol requirement |
| FCS | Hardware-generated IEEE 802.15.4 CRC-16 | Protocol requirement |
| IEEE MAC ACK request | Disabled | Protocol requirement |

STS shall remain disabled until both implementations explicitly program and
persist an agreed STS key and IV and define IV update/resynchronization rules.

The following are initial radio tuning values, not wire-protocol constants:

| Setting | Initial value |
|---|---:|
| TX pulse-generator delay | `0x34` |
| TX power | `0xFDFDFDFD` |
| TX antenna delay | 16385 |
| RX antenna delay | 16385 |

Each radio implementation must confirm that these values are valid for its
chip and board. TX power is subject to the deployment region's regulatory
limits. Antenna delays must eventually be calibrated per hardware design.

Hardware frame filtering should remain disabled during initial integration.
Destination, source, type, length, and sequence validation shall be performed
in software. Hardware FCS checking remains enabled.

## 3. Frame representation

### 3.1 Logical frame

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Frame Control, bytes `41 EC` |
| 2 | 1 | Sequence number |
| 3 | 8 | Destination EUI-64 |
| 11 | 8 | Source EUI-64 |
| 19 | 1 | Reserved/type byte |
| 20 | 0–105 | Payload |
| End | 2 | Hardware-generated FCS |

The Frame Control numeric value is `0xEC41`; IEEE 802.15.4 transmits its
least-significant byte first, hence bytes `41 EC`. It describes a Data Frame,
no security, no MAC ACK request, PAN-ID compression enabled, sequence number
present, IEEE 802.15.4-2015 frame version, and extended source and destination
addressing. No PAN-ID field is present.

The type is stored in the low nibble of byte 19. The high nibble is reserved,
shall be transmitted as zero, and shall be rejected when nonzero.

| Type | Value | Direction | Payload length |
|---|---:|---|---:|
| DATA_REQ | `0x0` | RECEPTEC → COLETEC | 2 |
| SYNC | `0x1` | RECEPTEC → COLETEC | 2 |
| DATA | `0x2` | COLETEC → RECEPTEC | 0–105 |
| ACK | `0x3` | COLETEC → RECEPTEC | 0 |
| STOP | `0x4` | RECEPTEC → COLETEC | 0 |
| STOP_ACK | `0x5` | COLETEC → RECEPTEC | 0 |
| `0x6`–`0xF` | — | Reserved | — |

The received PHY frame length determines the payload length; there is no
separate protocol payload-length field.

### 3.2 FCS ownership

The protocol codec produces and consumes bytes from Frame Control through the
payload. It never calculates or validates the FCS in software.

The DW3xxx transmit frame length programmed into the radio includes the two
FCS bytes. The adapter therefore programs:

```text
radio_tx_frame_length = codec_length + 2
```

Only `codec_length` meaningful bytes exist. If a particular Qorvo API requires
a buffer covering the complete programmed length, its last two bytes shall be
zero placeholders and are overwritten/ignored by the hardware FCS logic.

The DW3xxx receive frame length includes the FCS. The radio adapter shall
verify that the hardware reported a good FCS, subtract two from that length,
and pass only the logical bytes to the codec.

### 3.3 EUI-64 representation

EUI-64 values are transmitted least-significant octet first. For example, the
human-readable EUI:

```text
01:23:45:67:89:AB:CD:EF
```

is transmitted as:

```text
EF CD AB 89 67 45 23 01
```

The protocol's internal `uint8_t eui64[8]` representation uses on-air order.
Conversion to or from display strings is performed only at configuration and
diagnostic boundaries.

### 3.4 Opaque DATA payload

The DATA payload is an arbitrary byte string of length 0 through 105. Its
schema, versioning, and interpretation are owned by the application. The
protocol layer shall preserve the bytes exactly and shall not inspect them.

For a given transaction, the COLETEC shall freeze the selected DATA payload
when it handles the first DATA_REQ. Duplicate requests with the same sequence
number during the same slot shall return the same payload bytes. A new
transaction may select newer application data.

## 4. Sequence numbers and transaction identity

The RECEPTEC maintains one 8-bit sequence counter per configured COLETEC.

- The counter initializes to zero at RECEPTEC boot.
- It increments modulo 256 when a new transaction begins.
- It does not increment for retransmissions.
- A response copies the triggering request's sequence number.
- A response completes a transaction only if source, destination, type, and
  sequence all match the active transaction.
- A delayed or unrelated response is discarded and does not change a timer.

Duplicate recognition is bounded by the active slot. No real-time frame is
queued for delivery in a later slot. This bounded lifetime makes modulo-256
reuse acceptable for the initial protocol without a session identifier.

## 5. Timing contract

### 5.1 Initial parameters

| Symbol | Initial value | Unit | Classification |
|---|---:|---|---|
| `T_sampling` | 1000 | ms | Shared, tunable |
| `T_slot` | 20,000 | µs | Shared, tunable |
| `T_response_delay` | 1,000 | µs | Shared, tunable |
| `T_timeout` | 3,000 | µs | RECEPTEC, tunable |
| `N_attempts_max` | 3 | attempts | Shared, tunable |
| Attempt offsets | 0, 5,000, 10,000 | µs | Shared, tunable |
| `T_guard` | 500 | µs | COLETEC, tunable |
| `DT_guard` | 100 | µs/missed cycle | COLETEC, tunable |
| `N_cnt_guard_max` | 5 | missed cycles | COLETEC, tunable |
| `T_on_init` | 10 | s | COLETEC, tunable |
| `T_sniff_on` | 2 | s | COLETEC, tunable |
| `T_sniff_off` | 2 | s | COLETEC, tunable |
| `N_sniff_cycles_per_step` | 3 | cycles | COLETEC, tunable |
| `N_cnt_sniff_off_max` | 5 | multiplier | COLETEC, tunable |

`N_attempts_max` includes the first transmission. Random jitter and exponential
backoff are not used. A configuration is invalid unless:

```text
number_of_slots * T_slot <= T_sampling
last_attempt_offset + T_timeout < T_slot
maximum_DT_sampling < 65,536 us
```

With the initial values, at most 50 slots fit in one sampling period.

### 5.2 Hardware-clock scheduling

Protocol scheduling shall use the UWB hardware time domain, not ISR entry time,
Zephyr uptime, or thread wake time. All timestamp arithmetic shall use modular
DW3xxx timestamp helpers so radio timestamp wraparound is handled correctly.

The RECEPTEC selects a future cycle epoch with enough setup margin and defines:

```text
slot_start = cycle_epoch + slot_index * T_slot
attempt_tx = slot_start + attempt_offset[attempt]
```

Requests shall use delayed TX. If the driver reports that a delayed operation
was scheduled too late, the attempt is skipped. The implementation shall never
fall back to an uncontrolled immediate transmission. If no later attempt fits,
the slot fails and polling resumes at the next nominal slot.

The COLETEC transmits a response using delayed TX at:

```text
response_tx_rmarker = request_rx_rmarker + T_response_delay
```

If that delayed TX cannot be scheduled, the response is abandoned. The radio
returns to RX for the remainder of the slot.

### 5.3 `DT_sampling`

`DT_sampling` is encoded as an unsigned 16-bit little-endian number of
microseconds. Its valid range is 0 through 65,535 µs.

It represents:

```text
DT_sampling = scheduled_request_tx_rmarker - nominal_slot_start
```

The value is computed from scheduled hardware timestamps before the frame is
encoded. For the initial retry profile, normal values are 0, 5,000, and
10,000 µs. A request shall not be transmitted if the correction is outside the
representable range.

After receiving DATA_REQ or SYNC, the COLETEC derives its next nominal slot:

```text
next_slot_start = request_rx_rmarker + T_sampling - DT_sampling
```

This equation intentionally uses the hardware RX RMARKER of the request.

### 5.4 Receive intervals and retries

For the initial reliable implementation, a synchronized COLETEC keeps RX
enabled continuously across the complete assigned slot:

```text
expanded_guard = T_guard + N_cnt_guard * DT_guard

RX interval = [slot_start - expanded_guard,
               slot_start + T_slot + expanded_guard]
```

This allows recovery when either the request or its response is lost. After
responding, the COLETEC re-enables RX until the slot deadline. A duplicate
request produces another delayed response.

The RECEPTEC uses an absolute timeout measured from each scheduled request TX
RMARKER. Invalid, corrupted, unrelated, or stale packets do not reset or extend
the timeout. No retry may start outside the configured attempt offsets, and no
radio operation may extend the absolute slot deadline.

Continuous slot reception may later be replaced with deterministic retry
sub-windows after interoperability and timing margins have been measured.

### 5.5 Missed slots and discovery duty cycle

After a complete synchronized slot without a valid request, the COLETEC
increments `N_cnt_guard`. The next slot remains on the nominal
`T_sampling` cadence and uses the expanded guard. A valid DATA_REQ, SYNC, or
STOP resets `N_cnt_guard` to zero. On the fifth consecutive missed slot, the
COLETEC declares synchronization lost and enters discovery duty cycle.

The state called SNIFF in the protocol is a firmware-controlled discovery duty
cycle, not the Qorvo hardware SNIFF feature. On entry:

```text
sniff_cycle_count = 0
sniff_off_multiplier = 1
```

RX remains enabled for `T_sniff_on`, then disabled for:

```text
sniff_off_multiplier * T_sniff_off
```

After every `N_sniff_cycles_per_step` unsuccessful ON periods, the multiplier
increments up to `N_cnt_sniff_off_max`. A valid addressed request resets both
counters and returns the COLETEC to synchronized operation.

## 6. Transaction behavior

### 6.1 DATA_REQ

The RECEPTEC sends DATA_REQ and expects DATA. The COLETEC freezes the current
opaque application payload on the first request and retransmits the same bytes
for duplicates during that slot.

### 6.2 SYNC

The RECEPTEC sends SYNC and expects ACK. DATA_REQ and SYNC use identical timing
and retry behavior.

### 6.3 STOP

The RECEPTEC sends STOP and expects STOP_ACK using the normal deterministic
retry schedule. Upon the first valid STOP, the COLETEC:

1. stops application acquisition and local storage;
2. sends STOP_ACK at `T_response_delay`;
3. re-enables RX through the slot deadline;
4. sends another STOP_ACK for duplicate STOP requests;
5. enters STOPPED and disables UWB after the slot deadline.

STOPPED is exited only by reset, power cycle, or a separately implemented
external wake source. There is no over-the-air resume command in this protocol
version.

## 7. Validation and fault behavior

A received frame is accepted only when all applicable checks succeed:

- hardware reports a valid FCS;
- logical length is between 20 and 125 bytes;
- Frame Control bytes are exactly `41 EC`;
- reserved high nibble is zero;
- packet type is defined and legal in the receiving role;
- payload length is legal for that type;
- destination matches the local EUI-64;
- source matches the configured peer;
- response type and sequence match the active transaction when applicable;
- the frame was received during a state in which that request is legal.

Rejected frames generate no response and do not change protocol state,
sequence state, synchronization state, or deadlines.

The RECEPTEC records at least these diagnostic counters per COLETEC:

- transactions started and completed;
- attempts transmitted;
- response timeouts;
- retry exhaustion;
- invalid length, FCF, type, address, and sequence;
- radio RX/FCS errors reported by the driver;
- delayed TX/RX late errors.

## 8. Driver boundary

The implementation uses Qorvo's `dwt_uwb_driver` behind a thin Zephyr adapter
providing SPI, GPIO reset/wake/IRQ, delays, critical sections, and logging.
Application and protocol code shall not call Zephyr SPI/GPIO APIs or Qorvo
platform callbacks directly.

The adapter must expose at least:

- initialize/reset/probe/configure;
- read device ID and hardware time;
- immediate and delayed RX control;
- delayed TX with hardware FCS;
- RX-good, RX-timeout, RX-error, and TX-complete events;
- RX and TX RMARKER timestamps;
- frame read/write and reported frame length;
- sleep/idle control;
- late-scheduling status.

The exact Qorvo driver release and its file-level licenses shall be recorded
when the source package is imported. The current upstream Zephyr DWM3001CDK
board definition exposes the nRF52833 but does not provide a DW3110 UWB radio
driver; its `zephyr,ieee802154` choice refers to the Nordic 2.4 GHz radio and
must not be used for this protocol.

## 9. Integration order

1. Verify DW3xxx reset, SPI access, device ID, IRQ, and status clearing.
2. Verify matched raw TX/RX using the profile in Section 2.
3. Verify frame codec interoperability and maximum-length DATA frames.
4. Verify request RX timestamp and delayed response TX at 1,000 µs.
5. Verify attempts at 0, 5,000, and 10,000 µs and duplicate responses.
6. Verify next-slot calculation from each `DT_sampling` value.
7. Verify missed-slot guard expansion and discovery reentry.
8. Verify acknowledged STOP and duplicate STOP_ACK behavior.
