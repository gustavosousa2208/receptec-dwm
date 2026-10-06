# UWB positioning MVP bench controls

The MASTER owns all three ranges. Upgrade M, A and T together to contract v1.6.
The physical BASE B is outside this profile. The STM32 and its bootloader are unchanged.

## Common sampling rate

Use `bench console receptec --transport rtt --send "uwb range cycle stop"`;
wait until `uwb range cycle status` reports running=0. Set
`uwb range cycle period 500` (milliseconds), then `uwb range cycle start`.
Period changes while running fail with EBUSY. Periods 250..1000 are supported
for experiments, not automatically qualified. Settings survive MASTER restart;
legacy saved configuration loads at 1000 ms. All pairs use one shared round.

Capture with the HIL Python interpreter and `tools/cycle-benchmark.py`:
`--address CC:9B:2A:FA:A1:07 --mode measured --period-ms 500 --seconds 120
--minimum-seconds 60 --minimum-success 90 --output <new-evidence-directory>`.
On Linux/BlueZ, `--notify-transport bluez-fd` uses AcquireNotify to isolate
actual notification delivery from D-Bus characteristic Value updates. It fails
if unsupported instead of silently falling back. Duplicate notifications remain
an acceptance failure; this option does not deduplicate the capture. The summary
records the selected transport. Keep the default path for cross-platform comparison.

The tool counts missing rounds and silent boundary intervals as losses.
The default 1 Hz reference keeps its 30-minute/99% acceptance gate.
START uses a single 5 ms ACK wait (within the 20 ms maximum), then M listens
for T directly even if that ACK was lost. This avoids discarding the first
report while waiting for a control ACK. Endpoint authorization still bounds
all retries and transmissions.

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

## Unattended candidate capture

`tools/cycle-rate-benchmark.py` wraps the existing bench console and BLE capture.
It checks an explicit live MASTER probe, reads the running/measured cycle,
and requires the target BLE advertiser before changing the period. It records
the original configuration, confirms a stopped scheduler before changing it,
then restores and verifies the original running period even if capture fails.
Do not use the board from another tool during this wrapper's run.

Example with the HIL Python interpreter:

```sh
python tools/cycle-rate-benchmark.py --nick receptec --probe 802008564 \
  --address CC:9B:2A:FA:A1:07 --period-ms 350 --seconds 1800 \
  --output <new-evidence-directory>
```

Probe numbers are local bench identities: confirm them with `lstty`.
`initial-state.json` and `restored-state.json` prove the configuration checks;
`capture/summary.json` contains the rate acceptance result. These files do not
prove physical calibration or survive a power loss by themselves.

## Replay before a physical filter test

Replay a saved `records.jsonl` through the same processor as the dashboard:

```sh
node tools/replay-cycle-position.cjs <records.jsonl> > <position-replay.json>
```

An optional second argument is a JSON settings object, for example
`'{"windowSize":1,"motionTauMs":0}'` to compare unsmoothed accepted positions.
The output includes accepted positions, per-pair age-derived timestamps,
rejection counters and the final held/stale state. Initial snapshots are not
new measurements. Round IDs retain uint64 precision; older Node releases
without JSON parse source context require string-encoded large round IDs.

Compare the accepted round sets before comparing smoothing: the mean and
motion filter should not introduce rejected ranges into the output. Reduced
spread is not evidence of physical accuracy or acceptable movement latency.

The BLE capture summary also reports `successful_triplets` and
`successful_triplets_percent`: rounds in which all three published records
pass the existing per-pair status/source/distance criteria, divided by expected
rounds (including missing rounds). This is separate from `aligned_rounds`,
which also includes complete failed rounds. Per-pair acceptance is unchanged.
Triplet success is input availability, not position validity: geometry, range
bounds, sample skew and velocity can still reject a triplet in the processor.

### Phase timing before higher-rate changes

Use `uwb range cycle timing` on the MASTER to read RAM-only per-phase diagnostics. Counters reset on `cycle start` (and reboot); shell reads copy RAM and do not access the radio. `prep_t/prep_a` measure ROUND_ARM transactions; `mt_*` and `ma_*` measure ARM, DS-TWR and result reception; `at_arm_t/at_arm_a/at_start/at_result` separate delegated control and result reception. Each row reports samples, failures, last/mean/max duration in microseconds, successful mean/max and last return code. Durations include scheduling, SPI, airtime and retries. A result with remote nonzero status counts as a timing failure even when its report arrived correctly (`last_result=-ENODATA`). A–T result wait includes the delegated exchange and uplink; it does not isolate TAG RESPONSE versus FINAL. No logging is added inside measured phases. Compare complete timing snapshots and BLE success at 350/325/300 ms before changing slot geometry or radio turnaround margins.

The RECEPTEC radio IRQ mask contains RX events only. TX completion therefore uses bounded status polling, preserving pending RX flags and semaphore events; it does not wait on the RX-only semaphore. Interrupts remain enabled. Normal occupancy is the delayed-TX margin plus airtime; faulty TX remains bounded by the slot/20 ms timeout. Qualify phase latency and ranging success on both MASTER and BASE A before changing the schedule.

### Compact four-Hz experiment

The 250..299 ms MASTER-only profile reserves 30 ms for ROUND_ARM preparation (15 ms per peer transaction, including retries), starts slots at +40 ms, and keeps at least 30 ms after the last slot. Each slot is floor((period - 70)/3) ms. At 250 ms the three 60 ms slots start at 40/100/160 and end at 220 ms. Periods >=300 ms retain their previous geometry. Default remains 1000 ms. The existing schema-2 authorization, 10 ms guard and stopped-only persistence remain unchanged. Existing endpoint binaries with bounded authorization can execute the commands; no synchronized endpoint agenda or extra result is introduced. Validate 250 ms first with a short run, then 15 minutes, counting missing/failed rounds and checking preparation and phase failures.

### Best-effort BLE delivery

The UWB owner only updates the GATT read snapshot and a fixed three-slot latest-per-pair mailbox. It never calls GATT, waits for BLE buffers or retries transmission. A new result replaces an unsent older result for the same pair. The system workqueue attempts each pending notification once using Zephyr's nonblocking resource allocation path; no application retransmission or replay is performed. BLE link-layer reliability remains active. Legacy characteristics and metadata byte layout are unchanged. Notification age retains the contract value of zero for available sources; it does not measure mailbox, radio or host delivery delay. GATT READ computes publication age. The PC must tolerate missing rounds; missing transport records do not prove failed ranging. `uwb range ble` exposes published, coalesced, metadata accepted/errors, legacy errors, no-subscriber, not-ready and submission errors. Accepted means queued by BLE, not received by the PC. Counts persist until reboot. GATT READ always exposes the latest result independently of notification loss.
