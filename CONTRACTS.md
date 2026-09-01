# Contract compliance — receptec-dwm

Tracks the contract revision used by this bench firmware.

## Status

`contracts/` pin: `b1d0fc5`

| Contract | Targeted version | Status | Notes |
|----------|-----------------|--------|-------|
| UWB RECEPTEC↔COLETEC | 1.0 | partial | Wire format and PHY profile implemented. Retry scheduling and RX gating follow the companion HANDOFF rather than two disputed canonical-spec sections; see A7/A8 in the Nordic consolidation backlog. |

The pin is reachable from the contracts default branch. Bench results must
record this commit so behavior can be correlated with the exact specification
revision used for the test.
