# Contract compliance — receptec

Tracks the contract revision used by this bench firmware.

## Status

`contracts/` pin: `2e58b49`

| Contract | Targeted version | Status | Notes |
|----------|-----------------|--------|-------|
| UWB RECEPTEC↔COLETEC | 1.2 | implementing | The v1.2 multibase wire codec and DS-TWR calculation are being integrated into the RECEPTEC master/base firmware. Legacy SYNC/DATA_REQ and STOP_ACK remain. Three-pair ranging, result forwarding, persistence, and hardware interoperability remain unverified until the implementation and bench run finish. Raspberry Pi export is outside this profile. |
| UWB DATA payload | 1.0 | partial | RECEPTEC side: stores the 105-byte payload and decodes it in `scripts/sensor-viewer.py` (size checked against the contract's 88-byte summary). Draft contract. |

Bench results must record the pin so behavior can be correlated with the exact
specification revision used for the test.
