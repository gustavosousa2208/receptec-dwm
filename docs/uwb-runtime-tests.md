# UWB runtime tests (RECEPTEC as initiator, COLETEC as responder)

Runtime tests that the real RECEPTEC (`firmware/receptec`) runs against an unmodified
production COLETEC, plus the host runner and the long-duration run. They replace the
`uwb test` facility of the retired DWM-as-RECEPTEC bench; the plan they follow is that
bench's ten-item test plan, re-targeted at this firmware.

Nothing here changes the wire format, the PHY, the timing parameters or the COLETEC.

## How a test runs

The production COLETEC only listens in the rhythm the RECEPTEC sets, so a test cannot be
fired at an arbitrary moment. `uwb test <name> <slot>` arms the test and starts a 1 s poll
of that one peer; the test then replaces one transaction, in that peer's own slot:

1. **Warm-up.** Two consecutive ordinary transactions must succeed (else `no-baseline`).
2. **Test.** The test itself, one or more cycles.
3. **Verify.** One ordinary transaction must succeed afterwards, proving the test did not
   poison the peer (else `recovery-failed`).

Exactly one terminal line is printed, then the poll stops. Everything runs on the poll
thread under the radio mutex; the shell only arms the test and returns. A test has a finite
deadline (40 cycles) and `uwb test abort` ends it at the next cycle boundary.

```text
UWB_TEST result=PASS test=<name> slot=<0-3> key=value ...
UWB_TEST result=FAIL test=<name> slot=<0-3> reason=<token> key=value ...
```

A timeout is `FAIL reason=timeout` and an abort is `FAIL reason=aborted`. Names and keys are
lowercase `[a-z0-9_-]`, values also allow `.`, reasons are hyphenated. The line is a
compatibility surface for `scripts/uwb-test-runner.py`.

Prerequisites: the peer registered (`uwb peer register 0 <EUI-64>`), no poll running
(`uwb poll stop`), and one test at a time (a second `uwb test` answers busy).

## The tests

| Command | What it does | PASS needs | Failure reasons |
|---|---|---|---|
| `data_payload <slot>` | One transaction plus one repeated request; checks the payload v1 layout | 2 valid responses with the request's sequence, byte-identical payloads, layout valid | `wrong-length`, `wrong-version`, `reserved-flags`, `stale-without-summary`, `summary-not-zero`, `summary-header`, `padding-not-zero`, `duplicate-*-changed` |
| `retry <1\|2> <slot>` | Ignores 1 or 2 valid responses as if lost, repeats on the ordinary backoff schedule | ignored+1 valid responses, same sequence, identical payloads, each attempt at its generated offset (60 us), none after the slot end | `missing-response`, `late-response`, `attempt-timing`, `bad-response`, `duplicate-*-changed` |
| `duplicate <slot>` | Same as `retry 1` | idem | idem |
| `sequence_wrap <slot>` | Three ordinary transactions with sequences 254, 255, 0, one per cycle; restores the peer's next sequence | three valid responses in that order | `missing-response`, `bad-response` (field `step`) |
| `wrong_source` | Valid DATA_REQ with an unknown source EUI-64 | no response | `unexpected-response`, `rx-error` |
| `bad_type` | Reserved packet type `0x06` | no response | idem |
| `bad_reserved` | Reserved high-nibble bit set (`0x10`) | no response | idem |
| `bad_length` | DT_sampling truncated to one byte (21 logical bytes) | no response | idem |
| `bad_dt` | DT_sampling of 60 ms, outside any slot | no response | idem |
| `discovery <slot>` | 6 silent cycles (the COLETEC leaves sync after 5 missed slots), then one probe per cycle until it answers again | a valid response within 24 probes, then the verify transaction | `no-reentry` |

Each mutation is one fixed case built from a valid DATA_REQ (`uwb_test_frames.c`), sent with
a hardware-generated valid FCS so the COLETEC hardware filter accepts it and the rejection
has to come from its software checks. There is no free-form frame builder and no way to set a
sequence number from the shell.

`data_payload` validates **payload v1** (`contracts/protocol/uwb-receptec-coletec/payload-v1.md`,
draft), not the diagnostic pattern the original plan names: the production COLETEC now sends
the STM32 summary by default. It checks the layout the contract fixes, never sensor values, and
it is an implementation check, not a stable application format.

## What "no response" does and does not prove

For the five mutations, silence from the COLETEC is the PASS, and silence alone cannot tell a
rejection from a frame lost in the air. The COLETEC prints `UWB: state=.. rx=R err/to=.. tx=D/A/S`
every 5 s, where `rx` counts frames with a valid FCS that passed its hardware filter and `tx`
the responses it sent. The runner (`--coletec-rtt prod-coletec`) snapshots those counters
around each test and requires `rx - tx >= 1` for a mutation, that is, at least one frame
received and not answered. If it is 0, the runner turns the PASS into
`FAIL no-evidence-frame-received`. The COLETEC does not print *why* it rejected (address, type,
length), so the reason is inferred from which mutation was sent.

`discovery` is a bounded orchestration, not a discovery algorithm: the RECEPTEC sees only
silence and then an answer. The runner requires the COLETEC's own RTT lines
(`UWB synchronization lost: entering discovery` and `UWB discovery reentry`) inside the test
window, else `FAIL no-evidence-discovery`. The transition is reported as `transition=inferred`.

## Not implemented: `rx_boundary`

`uwb test rx_boundary` answers "not implemented" and the runner reports SKIP, never PASS.
It is not blocked by the contract: the retired DWM bench ran the same test (P-05) with probes at
-600, 0, 20500 and 20600 us against a window of `[-500, 20500)` us
(`verification-evidence/2026-09-18/p05-window-boundaries-ota.txt` in the share). A probe at an
offset does not need a `DT_sampling` larger than the slot. What is missing:

- The production numbers. The COLETEC window is `[slot start - guard, slot start + T_SLOT + guard]`
  (`coletec_uwb.c`, `listen_synchronized_slot`), `T_SLOT` 30 ms, guard 500 us at zero missed slots
  and growing with every missed slot (`COLETEC_N_CNT_GUARD_MAX` 5), not the DWM's 20 ms.
- Zero jitter. The poll jitter is 0.5 ms, larger than the 100 us margins of the old test, so the
  probe has to run with the jitter off and with the offset as a test parameter.
- A measured scheduling uncertainty, so the margin ("immediately before/after") is larger than it.

Implementation needs the bench, and the pass/fail rule is the old one: only probes inside the
window are answered.

## Running

```sh
# one test by hand (RTT shell; wait ~10 s between two bench console opens on the same probe)
bench console receptec --send "uwb test data_payload 0" --until "UWB_TEST result" --timeout 45

# the whole suite, with the COLETEC's counters and discovery lines as evidence
scripts/uwb-test-runner.py --slot 0 --coletec-rtt prod-coletec
scripts/uwb-test-runner.py --list
scripts/uwb-test-runner.py --slot 0 --only duplicate,bad_dt
```

The runner stops the poll before each test and at the end, waits for exactly one matching result
line, and writes `logs/uwb-tests-<UTC>/` (`raw.log`, `console.log*`, `summary.json`,
`summary.md`). Exit code 0 only if every selected test passed; a test that cannot run is SKIP and a
SKIP is never a pass; 2 if it could not start. It never runs STOP, never flashes, resets or
power-cycles a board. `python3 -m unittest discover -s scripts/tests` runs its 38 unit tests
(no hardware, 38 tests).

Host tests of the firmware helpers: `cmake -S firmware/receptec/tests -B <dir> && cmake --build <dir> &&
ctest --test-dir <dir>`.

Long-duration runs (packet statistics, link, clock drift over hours) are a different tool:
`scripts/uwb-long-test.py` (see the README).

## Evidence

2026-09-25, real RECEPTEC (`336c6ca`+) and the production COLETEC, slot 0, 1 s poll, link RSL
about -78 dBm. Runs are in `logs/uwb-tests-2026092*`.

- Four earlier suite runs exposed defects in the runner, not in the firmware, and were fixed:
  every test failed `duplicate-result` (the shell's own "armed" message mentioned the name
  UWB_TEST), and `bad_length` once failed because a result line arrived split across two reads.
- With the fixed runner and the `discovery` test in place, three consecutive full suites gave
  11 of 12 passed, 1 skipped (`rx_boundary`), 0 failed, every time: 33 executions of the 11
  implemented tests without a failure.
- With the COLETEC counters attached, each mutation showed `unanswered=1` and every other test
  `unanswered=0` with `rx = tx`. `discovery` showed the COLETEC entering discovery and then a
  reentry after it (`reentry-after-entry=1`); the answer to the first probe after 6 silent
  cycles came at once (`probes=1`).
- The suite exits 1 while `rx_boundary` is a SKIP: a test that cannot run is never a pass.

One earlier run showed the COLETEC leaving three received requests unanswered during `retry 1`
(`rx+8 tx+5`) and answering the accepted response only on the third attempt; it did not repeat in
the later runs and the cause is not explained.

What this does not show: behaviour at a weaker link (the RSL was constant), more than one peer,
the STM32 in a session, and the window edges (`rx_boundary`).
