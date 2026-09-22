# Contract compliance — atletec-uwb-dwm-bench

Tracks the contract revision used by this bench firmware.

## Status

`contracts/` pin: `f19587a`

| Contract | Targeted version | Status | Notes |
|----------|-----------------|--------|-------|
| UWB RECEPTEC↔COLETEC | 1.0 | partial | COLETEC-0 and COLETEC-1 are DWM surrogate responders. Wire format and PHY profile are implemented. COLETEC RX remains enabled through the slot, diverging from the canonical immediate-shutdown text that OI-01 itself flags as broken; see A8 in the Nordic consolidation backlog. |

Bench results must record the pin so behavior can be correlated with the exact
specification revision used for the test.
