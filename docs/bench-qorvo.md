# Qorvo bench commissioning snapshot

Recorded 2026-10-06 on gusta-dell. Discover live connections with `lstty` before every operation; this table records the commissioned assignment, not guaranteed probe availability.

| Nickname | Role | Probe | Local EUI | Installed source |
|---|---|---|---|---|
| coletec-qorvo-0 | MASTER | MCU-Link VQ13RAT1ND2HT | 44:B9:8D:76:2C:80:EC:A3 | receptec 0928c89 |
| coletec-qorvo-1 | BASE A | J-Link 802003538 | 83:B0:09:B0:3C:04:B9:BF | receptec fixed anchor 0928c89 |
| prod-coletec-qorvo | TAG | J-Link 802004653 | C2:3C:F4:90:45:6F:59:D2 | nrf52-bringup-fw e64d213 |

All three installed images pin contracts feb5fbbc. Qorvo 1 FICR PART was read as nRF52833; its previous nRF52840 probe registration was corrected. J-Link 802008564 is the former MASTER probe, not the active MASTER. fake-coletec-0 / 760216755 is excluded from the commissioned peers; its UART did not respond, so disabling its own responder was not confirmed.

## Persisted peers and scheduler

- MASTER: slot 0 = BASE A, enabled, nickname BASE-A; slot 1 = TAG, enabled, nickname TAG; slots 2/3 empty.
- BASE A: slot 0 = MASTER, enabled, nickname MASTER; slot 1 = TAG, enabled, nickname TAG; slots 2/3 empty.
- TAG: slot 0 = MASTER; slot 1 = BASE A.

The MASTER owns the only periodic schedule: cycle configured, tag slot 1, base slot 0, period 500 ms, measured baseline, autostart enabled. BASE A has the auto-responder enabled and independent range autostart disabled. The physical M-A reference of 950 mm is recorded for a later controlled test; fixed mode was not applied.

RECEPTEC peer set refuses occupied slots with -EEXIST. Clear the intended slot, register its EUI, restore its nickname, and verify peer list. TAG peer changes require reboot before ranging.

## Visual identification

The operator confirmed MASTER with both LEDs lit and BASE A with only blue lit. MASTER shell override is volatile and must be reapplied after reboot. BASE A currently boots with blue as its role indicator.

```sh
bench console coletec-qorvo-0 --mculink --transport mculink-rtt --send 'led control all on'
bench console coletec-qorvo-1 --send 'led control 1 off' --send 'led control 0 on'
```

Current RECEPTEC-Qorvo mapping: LED 0 blue P1.08, LED 1 green P1.09, active high. GPIO input readback can report low for output-only pins; physical observation and output latch are stronger evidence of commanded state.

## Validation and remaining work

All three flashes passed and running fw info / identity / peer list were checked. Best-effort BLE is in the MASTER: fixed latest-per-pair mailbox, no GATT calls from the UWB publisher, no application retry, transport counters under uwb range ble. Firmware host tests passed 8/8 and position/dashboard/replay regressions passed 18/18.

The remapped 500 ms, 60 second capture completed but FAILED the distance success gate: M-T 2/121, A-T 33/121, M-A 28/121; zero complete successful triplets. There were no duplicate, cadence, deadline or invalid payload errors. Valid ranges were often close to zero; physical separation was not controlled, so proximity remains a hypothesis rather than a proven cause. Do not claim the new bench is qualified.

Evidence directory on Dell: `/home/gusta/source/zephyr-v4.3.0/build/atletec/receptec/mvp-qorvo-remap-500-short/` (records.jsonl and summary.json). Next: separate the commissioned boards, repeat the measured 500 ms capture, then qualify higher common rates. Firmware publication remains pending the hardware gate; do not relax that gate to treat a failed ranging capture as PASS.
