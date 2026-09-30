# RECEPTEC Type 2AB UWB bring-up

Imported from `gusta-mac:/Users/gusta/zephyrproject/receptec` at commit
`bebd60f0daba0ce552d72d1765fb41daea8f82b7`; the source worktree was left
unchanged. Its app sources now live in `src/receptec/`, shared with the
RECEPTEC-Qorvo target.

The Type2AB carrier uses Murata `LBUA5QJ2AB-828`: an nRF52840 and a Qorvo
QM33120W. It is not a DWM3001C; use the matching board overlay and DW3720 port.
RECEPTEC-Qorvo uses the DWM3001C's nRF52833/DW3110 hardware port. Both targets
build the same RECEPTEC application from the repository root.

## What is ready

- The shared app builds for the repo-owned `receptec/nrf52840` target and
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
  counter. A 257th unique EUI returns `-ENOSPC`. These checks predate NVS
  persistence, which was integrated later. Current startup loads all four
  peer slots from Zephyr Settings/NVS; `uwb peer register` saves a slot and
  `uwb peer unregister` deletes it. Empty slots remain empty; firmware does not
  seed peers. On 2026-09-29, RECEPTEC-Qorvo restored a temporary slot-3 EUI
  after reset, then removed it and returned to four empty slots. The Type2AB
  hardware was not rechecked after source consolidation, although its build uses
  the same registry and storage implementation.
  The software allow-list is authoritative; the DW3720 hardware frame filter
  is not configured.

## Current UWB bring-up scope

The Type2AB RECEPTEC and production COLETEC use the same module. Its proven
mapping is SPIM3 P0.16 SCK / P0.17 MOSI / P0.23 MISO, CS P0.20, IRQ P0.25,
reset P0.15, and wake P1.02. RECEPTEC-Qorvo uses DWM3001C hardware and its own
nRF52833/DW3110 pin mapping; only the application, shell, protocol and peer
storage are shared.

`CONFIG_RECEPTEC_UWB_RADIO` enables the radio and shell commands `uwb status`,
`uwb sync <slot>` (periodic SYNC, default 1000 ms),
`uwb sync start <slot> [period_ms]`, `uwb sync stop`,
`uwb sync once <slot>` (one-shot diagnostic),
`uwb peer list`, `uwb peer register <slot> <EUI-64>`, and
`uwb peer unregister <slot>`. All four slots load from NVS at boot; unset slots
are empty. Registration and removal update NVS. EUI input/output uses
colon-separated bytes in on-air order. Up to 256 unique peer EUIs per boot
retain independent sequence counters even after unregister/re-slot; the next
unique EUI after history capacity is exhausted is rejected with `-ENOSPC`.
Registration applies a software
allow-list; the QM33120W hardware frame filter is not configured. Peer changes
are serialized with a full SYNC transaction by a mutex. A SYNC sends one v1.0 frame with zero-valued
`DT_sampling`, then accepts only an ACK matching peer EUI, receiver EUI, type,
and sequence within a fixed 100 ms timeout. Unrelated/malformed frames do not
extend the timeout. Periodic DATA_REQ polling and retries are implemented;
timing and full contract qualification remain open.

The shell also provides `led list`, `led set <on|off|toggle> <target> [...]`,
`led control <target> <operation>`, `led auto`, and `led selftest`. The same
three-second self-test is available in the fake-COLETEC and both RECEPTEC
images. It enumerates only enabled children of the board's `gpio-leds`
DeviceTree node, tests each LED and then all-on/all-off, and restores the
previous states. J200's external RGB indicators are connected to the Raspberry
Pi domain, not the nRF firmware.

Host checks for the bench and RECEPTEC helpers are under `tests/`; run them with:

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Build and host test

From this repository root, with the Zephyr workspace environment active:

```sh
west build -p always -b receptec/nrf52840 \
  -d "$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec/receptec-type2ab" .
west build -p always -b receptec_qorvo/nrf52833 \
  -d "$ATLETEC_ZEPHYR_WORKSPACE/build/atletec/receptec/receptec-qorvo" .
```

The RECEPTEC image was flashed to probe `802008564` for the bench test. Keep
flash operations targeted to that nRF52840; never use a DWM as RECEPTEC.
