# RECEPTEC Type 2AB UWB bring-up

Imported from `gusta-mac:/Users/gusta/zephyrproject/receptec` at commit
`bebd60f0daba0ce552d72d1765fb41daea8f82b7`; the source worktree was left
unchanged.

The RECEPTEC carrier uses Murata `LBUA5QJ2AB-828`: an nRF52840 and a Qorvo
QM33120W. It is not a DWM3001C; the latter's nRF52833/DW3110 overlay and driver
must not be flashed or reused as a board definition.

## What is ready

- `firmware/receptec` builds for the repo-owned `receptec/nrf52840` target and
  assigns UARTE1 to the Pi UART at 115200 bit/s, 8N1. The current console and
  shell use SEGGER RTT; UART console is disabled in `prj.conf`.
- The carrier report confirms `RX_NORDIC=P0.05` and `TX_NORDIC=P0.06`; those
  are the nRF UART RX and TX pins respectively.
- The UWB v1.0 frame codec is ported from the DWM bench and covered by a host
  golden-vector test. It is byte-compatible with the canonical UWB contract.
- The app integrates Qorvo DW3XXX driver 08.19.02, with the DW3720 variant for
  QM33120W and the COLETEC-compatible Type2AB SPI adapter. The receiver starts
  with channel 9, 6.8 Mb/s, PRF64, preamble 128/code 9, STS off.
- On 2026-09-23, RECEPTEC probe `802008564` initialized the QM33120W over RTT:
  device ID `0xDECA0314`, part ID `0xCA415713`, PHY ready.
- One-shot transactions passed against both fake identities: `uwb sync 0`
  against fake-COLETEC-0 (probe `760216755`, sequence 2) and `uwb sync 1` against
  fake-COLETEC-1 (probe `760216737`, sequence 3). RECEPTEC reported valid FCS,
  expected peer address, and matching ACK for both. DWM RTT confirmed each
  target identity and its hardware destination filter (`…40` / `…41`). An
  earlier attempt timed out while COLETEC-0 was in its periodic discovery
  state; after a DWM reset and clean responder start, the transaction passed.
  This is an interoperability smoke test, not full v1.0 conformance or
  reliability qualification.
- Reset recovery was then checked with J-Link system resets (SYSRESETREQ):
  RECEPTEC and each DWM were reset, each responder was started, and the
  corresponding SYNC/ACK passed. Both peers timed out once, then passed after
  another reset/start. This covers MCU reset recovery, not a physical
  power-cycle test, and the intermittent timeout means reliability still
  needs repetition.
- Runtime peer management was built/flashed and bench-tested on 2026-09-23.
  `uwb peer list` showed default slots 0/1 (`…40` / `…41`) and empty slots
  2/3. Registration in slot 2 succeeded, unregister removed it, duplicate EUI
  registration returned `-EADDRINUSE`, and SYNC to an empty slot returned
  `-ENOENT` without transmission. For OTA re-slot coverage, COLETEC-1 EUI
  `…41` was removed from slot 1 and registered in slot 2: SYNC/ACK passed at
  sequence 1, with the DWM logging the matching request/response. After
  unregister, SYNC to slot 2 returned `-ENOENT`. Re-registering the same EUI
  in slot 3 then passed at sequence 2, demonstrating sequence continuity
  across unregister/re-slot; unregistering slot 3 again prevented another
  transaction. These checks were repeated against the final image after the
  per-EUI history refactor. A synthetic delayed ACK was not injected OTA; a
  host test directly verifies the ACK predicate rejects an older sequence and
  wrong addresses/type.
  Per-peer sequence history is retained independently of slot storage for up
  to 256 distinct EUIs per boot; replacing a slot cannot erase another peer's
  counter. A 257th unique EUI returns `-ENOSPC`. Registry/history changes are
  volatile, so reset re-seeds slots 0/1 and resets their sequences (reset/wrap
  semantics remain the protocol's AI-01). Final J-Link reset restored default
  slots 0/1 and empty slots 2/3. The software allow-list is authoritative; no
  QM33120W hardware frame filter is configured.

## Current UWB bring-up scope

COLETEC and RECEPTEC use the same Type 2AB module. The proven COLETEC module
mapping is SPIM3 P0.16 SCK / P0.17 MOSI / P0.23 MISO, CS P0.20, IRQ P0.25,
reset P0.15, and wake P1.02. The DWM3001C overlay is still unrelated hardware:
it targets an nRF52833 and DW3110.

`CONFIG_RECEPTEC_UWB_RADIO` enables the radio and shell commands `uwb status`,
`uwb sync <slot>`, `uwb peer list`, `uwb peer register <slot> <EUI-64>`, and
`uwb peer unregister <slot>`. Slots 0/1 are seeded with the bench EUIs at each
boot; slots 2/3 start empty. Changes are volatile. EUI input/output uses
colon-separated bytes in on-air order. Up to 256 unique peer EUIs per boot
retain independent sequence counters even after unregister/re-slot; the next
unique EUI after history capacity is exhausted is rejected with `-ENOSPC`.
Registration applies a software
allow-list; the QM33120W hardware frame filter is not configured. Peer changes
are serialized with a full SYNC transaction by a mutex. A SYNC sends one v1.0 frame with zero-valued
`DT_sampling`, then accepts only an ACK matching peer EUI, receiver EUI, type,
and sequence within a fixed 100 ms timeout. Unrelated/malformed frames do not
extend the timeout. There are no retries or periodic initiator scheduling yet;
the timeout is a bench default, not a finalized contract parameter.

Host checks are in `firmware/receptec/tests`; run them with:

```sh
cmake -S firmware/receptec/tests -B firmware/receptec/tests/build
cmake --build firmware/receptec/tests/build
ctest --test-dir firmware/receptec/tests/build --output-on-failure
```

## Build and host test

From this repository root, with the Zephyr workspace environment active:

```sh
cmake -S firmware/receptec/tests -B build-receptec-tests
cmake --build build-receptec-tests
ctest --test-dir build-receptec-tests --output-on-failure
west build -p always -b receptec/nrf52840 \
  -d build-receptec firmware/receptec
```

The RECEPTEC image was flashed to probe `802008564` for the bench test. Keep
flash operations targeted to that nRF52840; never use a DWM as RECEPTEC.
