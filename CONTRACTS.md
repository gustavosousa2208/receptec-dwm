# Contract compliance — receptec

Tracks the contract revision used by this bench firmware.

## Status

`contracts/` pin: `ba39ee0`

| Contract | Targeted version | Status | Notes |
|----------|-----------------|--------|-------|
| UWB RECEPTEC↔COLETEC | 1.0 | partial | DWM fake-COLETEC-0/1 surrogate responders implement the PHY and wire codec; RECEPTEC implements the initiator PHY, peer registry, and manual SYNC/ACK. COLETEC RX remains enabled through the slot, diverging from the canonical immediate-shutdown text that OI-01 itself flags as broken; see A8 in the Nordic consolidation backlog. RECEPTEC now schedules DATA_REQ polling with retries (`uwb poll`) and stores each DATA payload per peer. The Raspberry Pi export is not implemented. |
| UWB DATA payload | 1.0 | partial | RECEPTEC side: stores the 105-byte payload and decodes it in `scripts/sensor-viewer.py` (size checked against the contract's 88-byte summary). Draft contract. |

Bench results must record the pin so behavior can be correlated with the exact
specification revision used for the test.
