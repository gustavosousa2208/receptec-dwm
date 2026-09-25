# RECEPTEC data export — design note

Status: proposal only. This note records the intended component boundaries; it
does not define a production wire format.

## Ownership and path

Application data originates on the COLETEC. COLETEC is responsible for
acquiring, timestamping/sequencing as required, and encoding its application
payload in its UWB `DATA` response. RECEPTEC should validate and forward that
payload; it should not invent or reinterpret sensor fields.

The intended delivery path is:

```text
COLETEC application payload
  → UWB DATA response (tag EUI-64 identifies the source)
  → RECEPTEC
  → framed binary UART record to the Pi
  → Pi bridge emits one UDP datagram per tag record
  → maleta application receives with recvfrom()
```

The RECEPTEC carrier routes UARTE1 to the Pi UART at 115200 bit/s, 8N1
(RECEPTEC P0.05 RX / P0.06 TX). The current firmware uses RTT for logs and
shell; binary data export and the Pi bridge are not implemented.

## Datagram behavior

Treat each accepted COLETEC `DATA` response as one logical record and preserve
the source EUI-64 in the host-visible metadata. A receiver then gets complete
tag records from `recvfrom()` without inferring boundaries from a byte stream.
UDP does not guarantee delivery, ordering, or retransmission; sequence and
loss-detection semantics must therefore be chosen in the payload contract.

For multiple consumers on one Pi, the bridge should fan out records (for
example through a configured local multicast group) rather than assume several
processes can all receive the same unicast datagram. Remote delivery can be
added separately if maleta requires it.

The UART is a stream, so its export protocol needs explicit framing, a maximum
record length, an integrity check, and parser resynchronization after a
corrupt/truncated record. UART framing and UDP datagram encoding may share a
logical envelope, but transport details should not leak into the COLETEC sensor
payload.

## Decisions still needed

Before implementing the COLETEC payload or making the interface normative,
agree with the maleta consumers on:

- sensor fields, representation, units, and schema version;
- sample timestamp/clock domain and sample counter semantics;
- whether each UWB poll returns one sample, the latest sample, or a batch;
- update rate, maximum record size, stale-data behavior, and loss reporting;
- local-only versus remote UDP delivery, multicast/unicast, and port/group;
- UART record framing and recovery behavior.

The shared UWB contract currently calls the `DATA` payload application-specific
and tracks its definition as AI-08. The normative payload schema and any
cross-repository UART/UDP interface must be defined in `contracts/`, with the
COLETEC producer and RECEPTEC/Pi consumers updated together. This repository
note is explanatory only.
