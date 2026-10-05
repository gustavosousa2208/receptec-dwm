# Contract compliance — receptec

Tracks the contract revision used by this bench firmware.

## Status

`contracts/` pin: `6679787`

| Contract | Targeted version | Status | Notes |
|----------|-----------------|--------|-------|
| UWB RECEPTEC↔COLETEC | 1.3 | implementing | Adds persistent peer enable/disable in local master configuration; registration and schedule are retained while disabled. Firmware integration is in progress. Multibase ranging, result forwarding, full schedule persistence, and hardware interoperability remain unverified. Raspberry Pi export is outside this profile. |
| UWB DATA payload | 1.0 | partial | RECEPTEC side: stores the 105-byte payload and decodes it in `scripts/sensor-viewer.py` (size checked against the contract's 88-byte summary). Draft contract. |

Bench results must record the pin so behavior can be correlated with the exact
specification revision used for the test.
