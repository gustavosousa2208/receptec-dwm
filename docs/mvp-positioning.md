# UWB positioning MVP bench controls

The MASTER owns all three ranges. Upgrade M, A and T together to contract v1.6.
The physical BASE B is outside this profile. The STM32 and its bootloader are unchanged.

## Common sampling rate

Use `bench console receptec --transport rtt --send "uwb range cycle stop"`;
wait until `uwb range cycle status` reports running=0. Set
`uwb range cycle period 500` (milliseconds), then `uwb range cycle start`.
Period changes while running fail with EBUSY. Periods 300..1000 are supported
for experiments, not automatically qualified. Settings survive MASTER restart;
legacy saved configuration loads at 1000 ms. All pairs use one shared round.

Capture with the HIL Python interpreter and `tools/cycle-benchmark.py`:
`--address CC:9B:2A:FA:A1:07 --mode measured --period-ms 500 --seconds 120
--minimum-seconds 60 --minimum-success 90 --output <new-evidence-directory>`.
The tool counts missing rounds and silent boundary intervals as losses.
The default 1 Hz reference keeps its 30-minute/99% acceptance gate.

## Position and filters

Serve `tools/range-dashboard.html` over localhost or HTTPS and connect to
ATL-RANGE. Position processing happens in the browser using same-round BLE
metadata. It does not change BLE framing or the raw ranges from the firmware.
BLE age gives conservative timestamp estimates; it is not an exact RF timestamp.

M=(0,0), A=(M-A,0). Choose a fixed positive/negative half-plane.
Offsets in mm are added per pair to raw distances; all start at zero. Measure
physical references before applying offsets. Do not use the previously observed
triangle-inconsistent values as a calibration reference.

Bounds, triangle consistency and raw-position speed are checked before a short
position mean. The adjustable time-aware exponential motion filter follows it.
Window 1 disables the mean; time constant 0 disables motion filtering. Defaults
are a three-sample mean, 1000 ms motion time constant and 3000 mm/s speed limit;
these are initial bench settings, not calibrated athletic movement limits.

Failed, missing or rejected data never produces a zero distance or new position.
The last valid coordinate remains visible as held/stale, without claiming a fresh
measurement. A long outage resets mean history before recovery. Configuration
changes clear old coordinates so changed references do not reuse old positions.

Tests: `node --test tests/tools/uwb-position.test.js`,
`python3 tests/tools/cycle-benchmark.test.py`, and the normal firmware host suite.
Physical calibration, fixed-baseline comparison and tuning the movement filter
remain operator benchmarks in the shared seven-step plan.
