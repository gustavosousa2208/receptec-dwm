# RECEPTEC–COLETEC Communication Protocol Specification

**Version 1.0 — Review-Integrated Working Draft**  
**Confidential**

> **Draft status:** This document is an initial protocol definition. Numerical values may remain `TBD` until system measurements and timing budgets are available. Notes marked **OPEN ITEM** identify behavioral decisions, missing definitions, or interface details that must be resolved before the protocol is considered implementation-ready.

## Revision History

| Version | Date | Description | Author |
|---|---|---|---|
| 1.0 | Jun 2026 | Initial version of the specification | — |
| 1.0-R1 | Jun 2026 | English review integrated; open technical items mapped into the specification | — |

## Table of Contents

1. Introduction
2. Protocol Overview
3. Packet Structure Definition
4. RECEPTEC Behavior (Master)
5. COLETEC Behavior (Slave)
6. System Parameters
7. State Diagrams
8. Fault Handling
9. Constraints and Points of Attention
10. Open Items and Missing Definitions

---

# 1. Introduction

## 1.1 Purpose

This document specifies the UWB-based wireless communication protocol used in the ATLETEC project. The protocol defines the message exchange rules between the anchor device (RECEPTEC) and the wearable devices attached to athletes (COLETECs), including synchronization, power management, and fault-handling mechanisms.

## 1.2 Scope

The protocol covers:

- Polling, synchronization, and receive-window management mechanisms.
- RECEPTEC (master) and COLETEC (slave) state machines.
- Configurable parameters and their operating constraints.
- Expected behavior under normal and fault conditions.

## 1.3 Definitions and Acronyms

| Term | Definition |
|---|---|
| RECEPTEC | Anchor device. Acts as the protocol master and initiates all communication. |
| COLETEC | Wearable device attached to an athlete. Acts as a slave and responds to RECEPTEC requests. |
| UWB | Ultra-Wideband, the radio technology used for communication and high-accuracy positioning. |
| EUI-64 | 64-bit Extended Unique Identifier. The unique address of each COLETEC. |
| DATA_REQ | Data-request packet sent by the RECEPTEC to a COLETEC. |
| DATA | Response packet containing application data, sent by a COLETEC in response to a DATA_REQ packet. |
| SYNC | Synchronization packet sent by the RECEPTEC when it does not need to collect data from a COLETEC. |
| ACK | Acknowledgment packet sent by a COLETEC in response to a SYNC packet. |
| STOP | Packet sent by the RECEPTEC to stop the COLETEC sampling operation. |
| Polling | Periodic scan performed by the RECEPTEC to query all registered COLETECs. |
| `T_sampling` | Nominal sampling period of the polling cycle. |
| `DT_sampling` | Timing correction applied relative to `T_sampling`. |
| `T_timeout` | Maximum time the RECEPTEC waits for a response after transmitting a packet. |
| `T_guard` | Guard time used to accommodate clock drift between the RECEPTEC and COLETEC. |
| `T_on_init` | Duration of the COLETEC initial receive period after power-on. |
| `T_sniff_on` | Duration of the radio-on period in SNIFF mode. |
| `T_sniff_off` | Base duration of the radio-off period in SNIFF mode. |
| `N_cnt_sniff` | SNIFF-cycle counter. |
| `N_cnt_sniff_off_max` | Maximum permitted value of the SNIFF off-period multiplier. |
| `N_cnt_guard` | Counter of cycles without a valid packet. It expands the receive window. |
| `N_cnt_guard_max` | Maximum value of `N_cnt_guard` before synchronization loss is declared. |
| `N_retry_max` | Maximum number of transmission attempts per polling cycle. |
| RX_ON | COLETEC state in which the radio remains continuously enabled and waits for reception. |
| SNIFF | Low-power state in which the radio is enabled periodically. |
| SYNC_WINDOW | Normal operating state in which the radio is enabled only during the scheduled receive window. |
| FCS | Frame Check Sequence, an integrity field generated and checked by the radio hardware. |

## 1.4 References

- Hardware documentation for the Qorvo UWB module used by the system.
- IEEE Std 802.15.4-2020 — *IEEE Standard for Low-Rate Wireless Networks*.
- Functional requirements of the athlete-monitoring system.

---

# 2. Protocol Overview

## 2.1 Topology

The network uses a star topology in which a single RECEPTEC acts as the central node and communicates with multiple COLETECs. Each COLETEC is identified by a unique EUI-64 address and must be registered with the RECEPTEC before it can participate in the network polling cycle.

## 2.2 Poll/Response Model

The protocol uses a poll/response communication architecture. The RECEPTEC acts exclusively as the master and is responsible for initiating every network transaction. COLETECs act exclusively as slaves and respond only to received requests. Spontaneous transmissions by COLETECs are therefore not permitted, ensuring that all network traffic remains under RECEPTEC control.

The RECEPTEC and COLETECs may occasionally receive packets that are not defined by this protocol, including packets from other networks or external devices. Such packets shall be discarded immediately without generating a response or changing the internal state of either device. Likewise, packets that conform to the protocol structure but contain addressing inconsistent with the expected recipient shall be rejected.

## 2.3 Polling Cycle

The RECEPTEC shall periodically poll all registered COLETECs. For each COLETEC participating in the cycle, one of the following operations shall be performed:

- **DATA_REQ:** Requests data from the COLETEC and waits for the corresponding response.
- **SYNC:** Keeps the COLETEC synchronized with the network without collecting data.
- **STOP:** Instructs the COLETEC to stop radio reception and local data logging immediately.

The polling cycle shall be executed strictly sequentially. The RECEPTEC may advance to the next COLETEC only after completing the current transaction, either by receiving the expected response or by exhausting the maximum number of retransmissions defined by the protocol. In addition, each transaction with a COLETEC shall begin only at the protocol-defined time slot assigned to that device.

## 2.4 Real-Time Data Policy

The system is designed for real-time monitoring. A COLETEC shall therefore always transmit the most recent information available. Retransmissions are permitted only during the active transaction and are intended to increase the probability of message delivery. Once the transaction ends—either because the expected response was received successfully or because the maximum number of retransmissions was exhausted—undelivered data shall not be retransmitted in later cycles.

All measurements shall nevertheless be stored locally to maintain a complete history for later offline analysis.

---

# 3. Packet Structure Definition

## 3.1 Frame Structure

All packets shall use the frame structure shown in Figure 1. The fields are described in Table 3-1. Frame fields shall be transmitted sequentially in the defined order, starting with the Frame Control field and ending with the FCS field.

**Figure 1 — Protocol Frame Structure**

| Field | Size | Value / Description |
|---|---:|---|
| Frame Control | 2 bytes | Fixed value: `0xEC41`. Configures the frame as an IEEE 802.15.4 Data Frame using EUI-64 addressing in both address fields. |
| Seq Number | 1 byte | Packet sequence number. |
| Destination Address | 8 bytes (EUI-64) | EUI-64 address of the destination device. |
| Source Address | 8 bytes (EUI-64) | EUI-64 address of the source device. |
| Packet Type | 4 bits | `0000b` = DATA_REQ; `0001b` = SYNC; `0010b` = DATA; `0011b` = ACK; `0100b` = STOP. |
| Payload | Variable | Structure and content depend on the transmitted packet type. |
| FCS | 2 bytes | Frame Check Sequence generated and verified by the radio hardware. |

**Table 3-1 — Fields that comprise the protocol data frame.**

> **OPEN ITEM OI-06 — Complete byte-level wire format:** Before independent implementations are developed, define the byte order of all multibyte fields, the EUI-64 byte order, the exact byte/bit position of the 4-bit Packet Type field, the value and meaning of the unused nibble, and the payload offsets and lengths for every packet type. Also state whether the FCS bytes are included in firmware buffers or are appended/removed by the radio hardware. Numerical payload sizes may remain `TBD`, but the encoding rules and ownership of each field must be explicit.


> **Implementation Note:** The Qorvo hardware frame-filtering features should be used where possible to reject frames automatically when they contain an incorrect destination address, have an invalid FCS, or fail other configurable checks. This reduces firmware-side frame processing and therefore helps reduce power consumption.

## 3.2 DATA_REQ Packet

The RECEPTEC sends a DATA_REQ packet when it needs to collect data from a COLETEC. After transmission, the RECEPTEC waits for a DATA packet for up to `T_timeout`.

- **Source:** RECEPTEC.
- **Destination:** A specific COLETEC identified by EUI-64.
- **Packet Type field:** `0000b`.
- **Payload:** Contains the correction to be applied to the nominal center of the COLETEC's next receive window.

## 3.3 SYNC Packet

The RECEPTEC sends a SYNC packet when it does not need to collect data from the COLETEC during the current cycle but must keep the device synchronized with the network. Upon receiving a valid SYNC packet, the COLETEC shall schedule its next receive window and transmit an ACK packet in response.

- **Source:** RECEPTEC.
- **Destination:** A specific COLETEC identified by EUI-64.
- **Packet Type field:** `0001b`.
- **Payload:** Contains the correction to be applied to the nominal center of the COLETEC's next receive window.

## 3.4 DATA Packet

A COLETEC sends a DATA packet in response to a valid DATA_REQ packet. The `SEQ_NUMBER` field in the DATA packet shall exactly match the `SEQ_NUMBER` field in the DATA_REQ packet that triggered the response.

- **Source:** COLETEC.
- **Destination:** RECEPTEC.
- **Packet Type field:** `0010b`.
- **Payload:** Contains application-specific data.

## 3.5 ACK Packet

A COLETEC sends an ACK packet in response to a valid SYNC packet. The `SEQ_NUMBER` field in the ACK packet shall exactly match the `SEQ_NUMBER` field in the SYNC packet that triggered the response.

- **Source:** COLETEC.
- **Destination:** RECEPTEC.
- **Packet Type field:** `0011b`.
- **Payload:** None. The Payload field is omitted from the frame.

## 3.6 STOP Packet

The STOP packet is used to terminate the COLETEC sampling operation. It stops both the acquisition of data intended for real-time monitoring and the local storage of raw data. The COLETEC shall also disable its radio after processing the command.

- **Source:** RECEPTEC.
- **Destination:** A specific COLETEC identified by EUI-64.
- **Packet Type field:** `0100b`.
- **Payload:** None. The Payload field is omitted from the frame.

> **OPEN ITEM OI-02 — STOP reliability and restart behavior:** STOP currently has no response packet, while the generic RECEPTEC transaction flow waits for a response and may retry. Decide whether STOP is: (a) acknowledged by a new `STOP_ACK` packet, or (b) intentionally unacknowledged and repeated according to a separate rule. Also decide whether operation resumes only after reset/power cycle or through a defined wake mechanism and RESUME/START command. A radio-disabled COLETEC cannot receive a normal over-the-air resume command.

## 3.7 SEQUENCE_NUMBER Field

The 1-byte `SEQ_NUMBER` field stores the transaction sequence number for communication with a given COLETEC. The RECEPTEC maintains a dedicated, independent counter for each associated device and increments it for every new transaction.

When validating received packets, the RECEPTEC shall:

- Compare the received `SEQ_NUMBER` value with the expected value.
- Verify that the source and destination addresses match the values expected for the active transaction.
- Treat the packet as a valid response only when both checks succeed.

---

# 4. RECEPTEC Behavior (Master)

## 4.1 Overview

The RECEPTEC shall be configured with the list of COLETECs to be monitored and their respective EUI-64 addresses. Using this list, the RECEPTEC performs periodic polling cycles and communicates with each registered COLETEC sequentially.

## 4.2 Communication Flow

Each COLETEC schedules its receive windows on the assumption that a new transaction will occur every `T_sampling` interval. The RECEPTEC shall therefore ensure that a packet addressed to each COLETEC is transmitted once per sampling period. This packet may be a DATA_REQ packet when data collection is required or a SYNC packet when only device synchronization must be maintained.

The `T_sampling` interval shall be measured relative to the device's nominal communication schedule rather than relative to the transmission time of the most recent attempt. Retransmissions shall therefore neither shift the scheduled time of the next communication nor introduce cumulative timing drift into the system.

To collect data from a given COLETEC, the RECEPTEC shall transmit a DATA_REQ packet and wait up to `T_timeout` for the corresponding DATA response. If no valid response is received during this interval, the RECEPTEC shall retransmit the request while keeping the `SEQ_NUMBER` value unchanged. The RECEPTEC shall increment `SEQ_NUMBER` only when starting a new transaction.

When a valid response is received, the transaction is complete and the RECEPTEC may proceed to the next COLETEC in the polling list. If no valid response is received after `N_retry_max` attempts, communication with that COLETEC shall be aborted for the current cycle.

> **OPEN ITEM OI-04 — Retry count and slot boundary:** Define whether `N_retry_max` means the total number of attempts, including the first transmission, or only the number of retransmissions. Then align the retry counter, exponential-backoff exponent, and maximum-backoff equation with that definition. The specification must also state that no retry may extend beyond the COLETEC's assigned slot deadline, or otherwise define how an overrun is handled. The numerical value of the limit may remain `TBD`.


Retransmissions shall use a binary exponential backoff scheme. Before each retransmission, the RECEPTEC calculates the waiting time as follows:

```text
T_wait = T_base × 2^N_retry + random(0, T_jitter_max)
```

where `N_retry` is an internal RECEPTEC variable initialized to 0 at the start of each transaction and incremented after each retransmission. `T_base` and `T_jitter_max` are protocol parameters. The maximum backoff interval is determined by `N_retry_max`:

```text
T_wait_max = T_base × 2^N_retry_max + T_jitter_max
```

The delay introduced by retransmissions shall be reported to the COLETEC so that it can adjust the timing of its next receive window. For this purpose, DATA_REQ and SYNC packets shall carry a `DT_sampling` field containing the accumulated delay between the nominal transaction transmission time and the packet's actual transmission time. The `DT_sampling` field is 2 bytes long and represents the delay in microseconds.

When calculating the center of its next receive window, the COLETEC shall use `DT_sampling` to compensate for retransmission delays and preserve alignment with the nominal communication schedule.

Packets sent by the COLETEC shall use exactly the same `SEQ_NUMBER` value as the packet that triggered the response. The RECEPTEC shall validate each received response by checking:

- That `SEQ_NUMBER` and Packet Type match the values expected for the active transaction.
- That the source and destination addresses are valid.
- That the frame passes the FCS integrity check.

When the RECEPTEC does not need to collect data from a COLETEC during a particular cycle, it shall transmit a SYNC packet. The transmission, timing, validation, and retransmission procedures for SYNC are identical to those defined for DATA_REQ, except that the expected response is an ACK packet.

> **OPEN ITEM OI-01 — Response-loss retry behavior:** The current COLETEC processing rules disable the receiver immediately after sending DATA or ACK. If that response is lost, the COLETEC will not be listening for the RECEPTEC retransmission. Select one retry model: keep RX enabled until an absolute transaction/slot deadline, or define deterministic retry receive sub-windows known to both devices. Randomized backoff combined with immediate RX shutdown is not sufficient.


Communication with the COLETECs is organized into predefined time slots. Each COLETEC has a dedicated communication slot within the polling cycle, during which the RECEPTEC may initiate a transaction with that device. Consequently, even when communication with one COLETEC finishes early, the RECEPTEC shall not immediately begin communication with the next device. It shall wait for the start of the next device's assigned slot.

The start of a slot is defined by:

```text
slot_start = index × T_slot
```

where `index` identifies the COLETEC's position in the list and `T_slot` is the communication time allocated to each COLETEC.

> **NOTE:** System parameters shall be sized so that the sum of all communication-slot durations remains less than `T_sampling`. This is a design constraint and is neither monitored nor enforced by the protocol at runtime.

## 4.3 Retransmission Timing

The RECEPTEC shall not calculate the time of the COLETEC's next receive window. This is solely the COLETEC's responsibility. The COLETEC derives the effective time of the next communication from the received `DT_sampling` value and the protocol parameter `T_sampling`, as described in Section 5.4.1.

The RECEPTEC is responsible only for reporting the packet-transmission delay through the `DT_sampling` field. The COLETEC uses this field to calculate the timing correction required to keep the next expected communication aligned with the RECEPTEC's nominal schedule, regardless of the number of retransmissions that occurred.

---

# 5. COLETEC Behavior (Slave)

## 5.1 General Description

At startup, the COLETEC shall enter the RX_ON state and keep its radio enabled for a duration of `T_on_init`. During this interval, the device shall be ready to receive commands from its associated RECEPTEC. The COLETEC shall respond only to packets originating from its associated RECEPTEC and shall validate the source address of each received packet.

If no valid packet is received during `T_on_init`, the COLETEC shall enter the SNIFF state. Reception of a valid packet while in SNIFF causes the COLETEC to leave that state and enter the synchronized operating state, SYNC_WINDOW, in which normal message exchange occurs.

If a valid packet is received during `T_on_init`, the COLETEC shall enter SYNC_WINDOW.

Table 5-1 summarizes the COLETEC operating states.

| State | Description |
|---|---|
| RX_ON | Radio continuously enabled. Initial state after power-on. Duration: `T_on_init`. |
| SNIFF | Low-power mode. The radio alternates between enabled for `T_sniff_on` and disabled for `N_cnt_sniff_off × T_sniff_off`. Used when synchronization has not yet been established or has been lost. |
| SYNC_WINDOW | Normal operating mode. The radio is enabled only during the scheduled receive window. |

**Table 5-1 — COLETEC operating states.**

## 5.2 RX_ON State (Initialization)

After reset, the COLETEC shall keep the radio in continuous receive mode for `T_on_init`. If it receives a valid packet from its associated RECEPTEC during this period, the COLETEC shall process the packet, transmit the corresponding response when applicable, and then transition to SYNC_WINDOW.

If no valid packet is received before `T_on_init` expires, the COLETEC shall transition to SNIFF and initialize the `N_cnt_sniff` and `N_cnt_sniff_off` counters.

> **NOTE:** `T_on_init` shall be sized to cover at least one complete RECEPTEC polling cycle, taking into account the maximum number of registered COLETECs and the worst-case retransmission scenario.

## 5.3 SNIFF State

> **OPEN ITEM OI-08 — SNIFF terminology:** Confirm whether this state is a firmware-controlled, seconds-scale discovery duty cycle or the Qorvo hardware SNIFF/preamble-hunt feature. If it is the software state described below, consider renaming it to `DISCOVERY_DUTY_CYCLE` or explicitly state that it is not the Qorvo hardware SNIFF function.

In the SNIFF state, the COLETEC alternates the radio between receive and powered-off periods to reduce power consumption. These durations are controlled by `N_cnt_sniff`, `N_cnt_sniff_off`, `T_sniff_on`, and `T_sniff_off`:

- **Radio ON:** Duration `T_sniff_on`.
- **Radio OFF:** Duration `N_cnt_sniff_off × T_sniff_off`.

The `N_cnt_sniff` and `N_cnt_sniff_off` counters shall be initialized when entering SNIFF. After every `N_cnt_sniff` complete SNIFF cycles without receiving a valid packet, `N_cnt_sniff_off` shall be incremented by one, up to `N_cnt_sniff_off_max`. Whenever a valid packet is received, both counter values shall be reset.

When a valid packet is received in SNIFF, the COLETEC shall process it according to the protocol rules, transmit the corresponding response when applicable, and then transition to SYNC_WINDOW.

## 5.4 SYNC_WINDOW State (Normal Operation)

SYNC_WINDOW is the normal COLETEC operating state. In this state, the device remains synchronized with the RECEPTEC and performs the data exchanges defined by the protocol.

The COLETEC shall remain in this state while it continues to receive valid packets within its scheduled receive windows. If no valid message is received during a receive window, the device shall treat this as a partial synchronization loss, update its timing parameters as defined by the protocol, and remain in SYNC_WINDOW. If the synchronization-recovery limit is reached, the COLETEC shall declare that synchronization with the RECEPTEC has been lost and transition to SNIFF.

### 5.4.1 Receive-Window Calculation

The time corresponding to the center of the next receive window, `T_w`, shall be calculated by the COLETEC from the received `DT_sampling` value:

```text
T_w = T_sampling − DT_sampling
```

where `T_sampling` is the nominal communication period defined by the protocol and `DT_sampling` is the accumulated delay reported by the RECEPTEC to compensate for retransmissions. This allows the COLETEC to adjust the timing of the next receive window while preserving alignment with the nominal communication schedule even when retransmissions occur.

> **OPEN ITEM OI-03 — Timing reference:** Define the timestamp from which `T_w` is measured. Suitable choices include the hardware RX RMARKER/SFD timestamp, the end-of-frame timestamp, or another explicitly named radio timestamp. Do not base the next window on ISR or task execution time. Also define how the implementation reacts when delayed RX is scheduled too late and the radio driver reports a late-scheduling error.


The receive window is defined by:

```text
Window = [T_w − T_guard − (N_cnt_guard × DT_guard),
          T_w + T_guard + (N_cnt_guard × DT_guard)]
```

The radio shall be enabled at the beginning of this window and remain active until the window ends or a valid packet is received, whichever occurs first.

If multiple valid packets are received during the same receive window, the COLETEC shall process and respond to all of them. However, the parameters used to schedule the next receive window shall be those contained in the last valid packet received.

> **Implementation Note:** Receive-window scheduling should use the Qorvo module's Delayed RX feature.

### 5.4.2 `N_cnt_guard` Counter Behavior

The `N_cnt_guard` counter controls the progressive expansion of the receive window while the COLETEC remains in SYNC_WINDOW without receiving valid packets. Whenever a receive window ends without a valid packet, `N_cnt_guard` shall be incremented, up to `N_cnt_guard_max`. When this limit is reached, the COLETEC shall declare synchronization loss with the RECEPTEC and transition to SNIFF.

| Event | Action on `N_cnt_guard` |
|---|---|
| A valid packet is received within the window. | Reset to 0. |
| The window ends without receiving a valid packet. | Increment by 1. For the next window calculation, `T_w` is set to `T_sampling`. |
| `N_cnt_guard` reaches `N_cnt_guard_max`. | The COLETEC declares synchronization loss and transitions to SNIFF. |

**Table 5-2 — `N_cnt_guard` behavior.**

> **NOTE:** Progressive growth of `N_cnt_guard` expands the receive window on both sides and accommodates accumulated clock drift. This allows the COLETEC to recover synchronization after several consecutive packet losses before falling back to SNIFF mode.

## 5.5 Validation of Received Packets

The COLETEC shall validate every received packet before processing it. A packet is valid if and only if all the following conditions are met:

- The destination address matches the COLETEC's own EUI-64 address.
- The source address matches the RECEPTEC associated with the COLETEC.
- The packet is intact, with a valid FCS and valid structure.

Packets that fail any of these criteria shall be silently discarded without generating a response or changing device state.

## 5.6 DATA_REQ Processing

Upon receiving a valid DATA_REQ packet, the COLETEC shall:

- Copy the `SEQ_NUMBER` value from the received DATA_REQ packet into the DATA response.
- If the request is a retransmission, transmit the most recent data available, regardless of whether it was transmitted previously.
- Transmit a DATA packet containing the most recent data available.
- After transmitting the response, disable the radio and schedule the next receive window as described in Section 5.4.1.

> **OPEN ITEM OI-01 applies here:** This immediate shutdown requirement must be revised after the response-loss retry model is selected.

## 5.7 SYNC Processing

Upon receiving a valid SYNC packet, the COLETEC shall:

- Copy the `SEQ_NUMBER` value from the received SYNC packet into the ACK response.
- Transmit an ACK packet confirming receipt of the SYNC packet.
- After transmitting the response, disable the radio and schedule the next receive window as described in Section 5.4.1.

> **OPEN ITEM OI-01 applies here:** This immediate shutdown requirement must be revised after the response-loss retry model is selected.

## 5.8 STOP Processing

Upon receiving a valid STOP packet, the COLETEC shall:

- Stop data acquisition immediately.
- Stop local data storage.
- Disable the radio after processing the command.
- Remain in this state until it is restarted or receives an instruction to resume operation.

> **OPEN ITEM OI-02 applies here:** If the radio is disabled, an over-the-air resume instruction is impossible unless a separate wake mechanism is defined.

---

# 6. System Parameters

## 6.1 Parameter Table

> **OPEN ITEM OI-05 — Parameter and runtime-symbol inventory:** The current draft uses symbols that are not fully listed or classified, including `T_base`, `T_slot`, `DT_guard`, `N_cnt_sniff_off`, and the runtime variable `N_retry`. Add each symbol with its unit, valid range, initial value, and classification as a configured parameter, derived value, or runtime state. The actual numerical values may remain `TBD`.

| Parameter | Description | Typical Value | Unit |
|---|---|---:|---|
| `T_sampling` | Nominal polling-cycle period. | 1000 | ms |
| `DT_sampling` | Correction applied relative to `T_sampling` due to retransmission delay. | TBD | µs |
| `T_timeout` | Maximum time to wait for a response after transmission. | TBD | µs |
| `N_retry_max` | Maximum number of attempts per COLETEC in each cycle. | TBD | — |
| `T_on_init` | Duration of the RX_ON state during initialization. | 10 | s |
| `T_sniff_on` | Duration for which the radio remains enabled in SNIFF mode. | 2 | s |
| `T_sniff_off` | Base duration for which the radio remains disabled in SNIFF mode. | 2 | s |
| `N_cnt_sniff_off_max` | Maximum value of `N_cnt_sniff_off`. | TBD | — |
| `T_guard` | Guard time used to accommodate clock drift. | TBD | µs |
| `T_jitter_max` | Maximum jitter used in retransmission backoff. | TBD | µs |
| `N_cnt_guard_max` | Maximum number of cycles without valid packets before entering SNIFF mode. | TBD | — |

**Table 6-1 — Protocol parameters.**

## 6.2 Sizing Considerations

**`T_guard`:** Shall be sized as a function of the frequency error, in ppm, of the RECEPTEC and COLETEC oscillators and the value of `T_sampling`. A conservative initial value is:

```text
T_guard = T_sampling × (sum of clock errors in ppm) × N_cnt_guard_max
```

The ppm term shall be converted to a dimensionless ratio when applying this equation.

**`N_cnt_guard_max`:** Shall balance robustness, meaning more cycles before entering SNIFF, against power consumption, since a larger receive window keeps the radio enabled for longer.

**`N_cnt_sniff_off_max`:** Controls the maximum network-reentry latency. `T_sniff_on` shall be selected to cover at least one RECEPTEC polling period with an appropriate margin.

**`T_on_init`:** Shall balance power consumption, latency, and COLETEC availability.

---

# 7. State Transitions

## 7.1 COLETEC

| Source State | Transition Condition | Destination State |
|---|---|---|
| POWER_ON | — | RX_ON |
| RX_ON | A valid packet is received within `T_on_init`. | SYNC_WINDOW |
| RX_ON | `T_on_init` expires without a valid packet. | SNIFF |
| SNIFF | A valid packet is received during `T_sniff_on`. | SYNC_WINDOW |
| SYNC_WINDOW | The window ends without a packet and `N_cnt_guard < N_cnt_guard_max`. | SYNC_WINDOW (`N_cnt_guard++`) |
| SYNC_WINDOW | `N_cnt_guard` reaches `N_cnt_guard_max`. | SNIFF |
| SYNC_WINDOW | A valid packet is received. | SYNC_WINDOW |
| Any state | A valid STOP packet is received. | STOPPED, radio disabled |

**Table 7-1 — COLETEC state transitions.**

## 7.2 RECEPTEC — Per-COLETEC Flow

| Action | Condition | Next Action |
|---|---|---|
| Send DATA_REQ, SYNC, or STOP. | — | Wait for a response for up to `T_timeout`. |
| Wait for response. | A valid response is received within `T_timeout`. | Complete the transaction for this COLETEC. |
| Wait for response. | A corrupted packet is received. | Do not change the timeout counter; continue waiting for a valid packet. |
| Wait for response. | The timeout expires and the attempt counter is less than `N_retry_max`. | Retransmit using the backoff scheme. |
| Wait for response. | The timeout expires and the attempt counter equals `N_retry_max`. | End communication attempts and proceed to the next COLETEC. |

**Table 7-2 — RECEPTEC communication flow for each COLETEC.**

---

# 8. Fault Handling

## 8.1 Packet Loss (No Response)

The RECEPTEC performs up to `N_retry_max` attempts before aborting communication with a COLETEC for the current polling cycle. Retransmissions use binary exponential backoff.

## 8.2 Corrupted Packet

Packets that fail the integrity check, such as packets with an invalid FCS, or that have a malformed structure are ignored by both devices. The current draft states that the RECEPTEC resets the timeout counter when it receives a structurally valid packet.

> **OPEN ITEM OI-07 — Transaction timeout rule:** Prefer an absolute transaction deadline that is not extended by unrelated traffic. Only the expected response, with matching addresses, Packet Type, and `SEQ_NUMBER`, shall complete the transaction. Decide whether any other packet is allowed to affect the timer; the recommended behavior is that it does not.

## 8.3 COLETEC Synchronization Loss

If `N_cnt_guard` reaches `N_cnt_guard_max` without reception of a valid packet, the COLETEC concludes that synchronization with the RECEPTEC has been lost and transitions to SNIFF mode. In SNIFF mode, the COLETEC alternates the radio between enabled and disabled periods with an increasing off duration until it receives a valid packet, at which point it returns to SYNC_WINDOW.

## 8.4 Unreachable COLETEC

When the RECEPTEC receives no response from a COLETEC after `N_retry_max` attempts, the failure is logged and polling of that COLETEC resumes during the next cycle. The protocol does not automatically remove unreachable COLETECs from the polling list.

---

# 9. Constraints and Points of Attention
10. Open Items and Missing Definitions

## 9.1 Handling of Corrupted Packets

Immediate retransmission upon receipt of a corrupted packet is prohibited. Corrupted, unrelated, or unexpected packets shall be ignored. See **OI-07** for the final timeout/deadline rule.

## 9.2 Polling-Cycle Completion Constraint (Design Requirement)

The system shall be parameterized so that, under worst-case conditions, the entire polling cycle completes within `T_sampling`.

## 9.3 COLETEC Registration

Every COLETEC shall be registered with the RECEPTEC using its EUI-64 address before participating in the polling cycle. Unregistered COLETECs transmitting on the channel shall be ignored by the RECEPTEC based on source- and destination-address validation.

## 9.4 Lack of Cryptographic Authentication

As specified, the protocol does not include cryptographic authentication or payload encryption. Source validation relies exclusively on the EUI-64 address, which does not protect against spoofing. Additional security layers should be considered for environments with security requirements.

---


# 10. Open Items and Missing Definitions

## 10.1 Purpose and Status Convention

This section is the consolidated map of decisions and definitions that remain open in this initial protocol draft. An item marked **Value TBD** does not prevent the architecture from being discussed; it means that the final value depends on measurement, simulation, hardware characterization, or the selected number of COLETECs. An item marked **Design decision required** changes observable protocol behavior and must be resolved before two independent implementations can interoperate reliably.

| Status | Meaning |
|---|---|
| Design decision required | A behavioral contradiction or protocol choice must be resolved. |
| Definition required | The intended behavior is mostly clear, but an encoding, timestamp, range, or state rule is missing. |
| Value TBD | The parameter exists, but its final numerical value has not yet been selected. |
| External ICD required | The information may be specified in another controlled document, which must be referenced and versioned. |

## 10.2 Primary Open-Item Map

| ID | Status | Affected Sections | Missing or Conflicting Point | Decision / Definition Needed |
|---|---|---|---|---|
| OI-01 | Design decision required | 4.2, 5.4.1, 5.6, 5.7 | COLETEC disables RX after replying, but RECEPTEC may retransmit if the reply is lost. | Keep RX active until a transaction deadline, or define deterministic retry receive sub-windows. Define duplicate-request handling. |
| OI-02 | Design decision required | 2.3, 3.6, 5.8, 7.2 | STOP has no response, and the stated resume path is impossible with the radio disabled. | Define `STOP_ACK` or an unacknowledged STOP procedure; define reset-only restart or a wake/START/RESUME mechanism. |
| OI-03 | Definition required | 4.3, 5.4.1 | The receive-window equation has no explicit timestamp origin. | Name the radio timestamp used as the reference and define delayed-RX late-error handling. |
| OI-04 | Definition required | 4.2, 7.2, 8.1, 9.2 | Attempts versus retransmissions and retry behavior at the slot boundary are ambiguous. | Define counter semantics, exponent sequence, slot deadline, and abort behavior. Final values may remain `TBD`. |
| OI-05 | Definition required / Value TBD | 1.3, 4.2, 5.3, 5.4.1, 6.1 | Several symbols are used without complete definitions or table entries. | Add units, ranges, initialization, ownership, and configured/runtime classification. |
| OI-06 | Definition required | 3.1–3.6 | The wire representation is not fully specified. | Define byte order, field offsets, Packet Type packing, reserved bits, payload lengths, and FCS buffer behavior. |
| OI-07 | Design decision required | 4.2, 7.2, 8.2, 9.1 | A structurally valid but unrelated frame may reset the timeout. | Use an absolute deadline; only the expected matching response completes the transaction. |
| OI-08 | Definition required | 5.1, 5.3, 8.3 | “SNIFF” may be confused with the Qorvo hardware SNIFF feature. | Rename the protocol state or explicitly distinguish it from hardware SNIFF mode. |

## 10.3 Parameter and Timing Register

The following values may legitimately remain `TBD` in this starting specification. Their definitions and units should nevertheless be fixed now so that later measurements fill known fields rather than changing the protocol model.

| Symbol | Category | Current State | Required Before Final Release |
|---|---|---|---|
| `T_sampling` | Configured timing | Typical value defined | Confirm range, tolerance, and whether it is common to all COLETECs. |
| `DT_sampling` | Transmitted timing correction | Width and unit partly defined | Define unsigned/signed encoding, endianness, range, overflow/saturation behavior, and requirement `DT_sampling < T_sampling`. |
| `T_timeout` | Configured timing | Value TBD | Derive from maximum response latency, frame airtime, turnaround, processing, and margin. |
| `T_response_max` | Derived timing | Missing | Define the maximum COLETEC request-to-response latency used to size `T_timeout` and `T_slot`. |
| `N_retry_max` | Configured limit | Value and semantics TBD | State whether it includes the initial attempt. |
| `T_base` | Configured timing | Used but not tabulated | Define unit, range, and relationship to the slot budget. |
| `T_jitter_max` | Configured timing | Value TBD | Confirm whether random jitter is compatible with the selected COLETEC retry-listening model. |
| `T_slot` | Configured/derived timing | Used but not tabulated | Define cycle epoch, slot index convention, and worst-case sizing equation. |
| `T_guard` | Configured/derived timing | Value TBD | Derive from combined clock error, radio timing uncertainty, scheduler uncertainty, and margin. |
| `DT_guard` | Derived timing increment | Used but undefined | Define formula and ensure drift is not counted twice with `T_guard`. |
| `N_cnt_guard_max` | Configured limit | Value TBD | Balance resynchronization robustness and RX energy. |
| `T_on_init` | Configured timing | Typical value defined | Verify it covers the selected worst-case polling/retry cycle. |
| `T_sniff_on` | Configured timing | Typical value defined | Confirm coverage of at least one complete polling opportunity with margin. |
| `T_sniff_off` | Configured timing | Typical value defined | Confirm base unit and power/reentry target. |
| `N_cnt_sniff_off_max` | Configured limit | Value TBD | Define maximum discovery/reentry latency. |
| `N_cnt_sniff` | Runtime counter or threshold | Ambiguous | Separate the runtime count from the configured number of cycles per off-time increment. |
| `N_cnt_sniff_off` | Runtime multiplier | Used but not tabulated | Define initial value, reset value, increment point, and saturation behavior. |
| `N_retry` | Runtime counter | Partly defined | Define exact initial value and whether it counts attempts or retransmissions. |

A final timing budget should demonstrate at least the following bounds, using one consistent time unit:

```text
T_transaction_worst <= T_slot
N_COLETEC × T_slot <= T_sampling
DT_sampling_max_representable >= maximum_actual_transmission_delay
```

The exact expansion of `T_transaction_worst` depends on the final definition of attempts, retransmissions, response timeout, and STOP acknowledgment.

## 10.4 Additional Definitions and Checks

The following items were identified beyond the eight primary findings. They can remain open in this starting draft, but they should be tracked so they are not lost during implementation.

| ID | Priority | Affected Sections | Item to Define or Verify |
|---|---|---|---|
| AI-01 | High | 3.7, 4.2 | Define modulo-256 sequence-number wraparound, initialization after reset, and rejection of stale delayed responses. |
| AI-02 | Medium | 3.1, 3.5 | State that protocol ACK is an application-layer Data Frame, not an IEEE 802.15.4 MAC ACK, and state whether the MAC ACK-request bit is disabled. |
| AI-03 | High | 3.1 | Confirm the intended Frame Control decoding, on-air FCF byte order, PAN-ID-field omission, and EUI-64 representation against the exact Qorvo API/buffer convention. |
| AI-04 | High | 3.2–3.6 | Define one byte-level packet layout per type, including payload version/length and reserved-field behavior. |
| AI-05 | High | 3.4, 5.6 | Decide whether a retransmitted DATA response freezes the original sample or may contain a newer sample under the same `SEQ_NUMBER`. Consider an application sample counter and timestamp. |
| AI-06 | High | 5.4.1 | Define behavior when delayed RX cannot be scheduled because the requested start time is already in the past. |
| AI-07 | Medium | 5.4.1, 5.6, 5.7 | If RX remains active for retries or multiple packets, define buffering, automatic RX re-enable, and overrun handling. |
| AI-08 | High | 3.4 | Replace “application-specific data” with a reference to a versioned payload ICD, or define the DATA payload format in this document. |
| AI-09 | High | 2.3, 5.8 | Define when data acquisition begins: at boot, after synchronization, after first DATA_REQ, or after a START command. |
| AI-10 | Medium | 2.4 | Clarify that failed real-time responses are not queued for later retransmission; the next transaction sends the newest available sample. |
| AI-11 | High | 5.5 | Extend packet acceptance rules to include legal direction, Packet Type, current state, and active transaction—not only address, FCS, and structure. |
| AI-12 | Medium | 5.4.2, 6.2 | Revisit the guard-window equation so fixed guard and accumulated drift are not counted twice. Define whether the first missed interval uses `N` or `N+1`. |
| AI-13 | Medium | 5.3 | Separate SNIFF configuration thresholds from runtime counters and define all reset/saturation values. |
| AI-14 | Medium | 7 | The section contains transition tables rather than graphical diagrams. Rename it “State Transitions” or add actual state diagrams later. |
| AI-15 | Medium | 9.4 | State the deployment trust assumption. Without authentication or replay protection, spoofed/replayed STOP, SYNC, or DATA_REQ frames can affect operation. |
| AI-16 | Low | 1.4 | Decide whether the implementation intentionally targets IEEE 802.15.4-2020 or should reference a later edition; revalidate the chosen FCF/addressing rules when the target is fixed. |
| AI-17 | Medium | 2.3, 4.2 | Define behavior when the configured number of COLETECs cannot fit within `T_sampling`, including configuration rejection or degraded operation. |
| AI-18 | Medium | 4.2, 8.4 | Define logging/diagnostic counters for timeouts, FCS failures, unexpected packets, retry exhaustion, synchronization loss, and reentry. |

## 10.5 Suggested Resolution Order

Resolve the open items in this order so later timing values are based on stable behavior:

1. Select the transaction retry-listening model (**OI-01**) and STOP/restart model (**OI-02**).
2. Define the common timestamp model (**OI-03**) and retry/slot semantics (**OI-04**, **OI-07**).
3. Complete the byte-level frame and payload definitions (**OI-06**, **AI-03**, **AI-04**, **AI-08**).
4. Complete the symbol inventory (**OI-05**) and then calculate timing budgets and select numerical values.
5. Finalize synchronization recovery, SNIFF naming/counters, sequence reset/wrap behavior, and diagnostics.
6. Run a worked worst-case example for one complete polling cycle and verify it on the target Qorvo hardware.

Until these items are resolved, this document should be treated as an architectural starting point and review baseline rather than a frozen interoperability specification.

---

**Confidential — End of Document**
