# Contract compliance — atletec-uwb-dwm-bench

Tracks the contract revision used by this bench firmware.

## Status

`contracts/` pin: `f19587a`

| Contract | Targeted version | Status | Notes |
|----------|-----------------|--------|-------|
| UWB RECEPTEC↔COLETEC | 1.0 | partial | DWM COLETEC-0/1 surrogate responders implement the PHY and wire codec; RECEPTEC implements the initiator PHY, peer registry, and manual SYNC/ACK. COLETEC RX remains enabled through the slot, diverging from the canonical immediate-shutdown text that OI-01 itself flags as broken; see A8 in the Nordic consolidation backlog. Automated scheduling/retries, DATA collection, and the application-payload/export schema remain incomplete. |

Bench results must record the pin so behavior can be correlated with the exact
specification revision used for the test.
