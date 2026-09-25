#!/usr/bin/env python3
"""Acceptance check for the COLETEC -> UWB -> RECEPTEC -> PC sensor-data path.

Registers the production COLETEC on the real RECEPTEC, polls it for long enough to
collect --samples payloads, decodes each one as UWB payload v1
(contracts/protocol/uwb-receptec-coletec/payload-v1.md) and judges the run against
numeric thresholds. A check that cannot run reports SKIP, never PASS.

Checks:
  shell      the RECEPTEC shell answers                              (SKIP if not)
  delivery   ok / transactions >= --min-delivery over the run        (SKIP if nothing polled)
  late       no late schedule or late response                       (FAIL if any)
  decoded    >= --samples payloads received, all payload version 1   (FAIL otherwise)
  sequence   payload sequence never goes backwards                   (FAIL if it does)
  sensor     >= --samples payloads carrying a fresh STM32 summary    (SKIP if the COLETEC
             has no session running: start one, e.g. over BLE)

Result: PASS only if every check passed. Exit code 0 PASS, 1 FAIL, 77 SKIP.
Raw output and the decoded records are kept under logs/acceptance-<UTC>/.
"""

import argparse
import importlib.util
import json
import math
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent

sys.path.insert(0, str(Path.home() / "source/dotfiles/scripts"))
from benchlib.console import ShellRunner, open_console  # noqa: E402

spec = importlib.util.spec_from_file_location("sensor_viewer", HERE / "sensor-viewer.py")
viewer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(viewer)

STATS = re.compile(r"slot (\d+): transactions=(\d+) ok=(\d+) .*?late=(\d+) exhausted=(\d+)")
ANSI = re.compile(r"\x1b\[[0-9;]*m")
PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--peer", default="02:00:00:00:00:00:00:02",
                    help="EUI-64 of the COLETEC, registered alone in slot 0 (default: production)")
    ap.add_argument("--period-ms", type=int, default=1000)
    ap.add_argument("--samples", type=int, default=60)
    ap.add_argument("--min-delivery", type=float, default=0.95)
    ap.add_argument("--port", help="serial port of the RECEPTEC shell (default: RTT)")
    args = ap.parse_args()

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    outdir = HERE.parent / "logs" / f"acceptance-{stamp}"
    outdir.mkdir(parents=True)
    checks: dict[str, tuple[str, str]] = {}
    records: list[dict] = []
    stats = None

    def done(result: str) -> int:
        overall = FAIL if FAIL in {c[0] for c in checks.values()} else (
            SKIP if SKIP in {c[0] for c in checks.values()} else PASS)
        for name, (state, detail) in checks.items():
            print(f"  {state:4} {name:9} {detail}")
        print(f"RESULT: {overall}  ({outdir})")
        (outdir / "result.json").write_text(json.dumps(
            {"result": overall, "checks": checks, "samples": args.samples,
             "min_delivery": args.min_delivery, "period_ms": args.period_ms,
             "peer": args.peer, "stats": stats}, indent=1))
        return {PASS: 0, FAIL: 1, SKIP: 77}[overall]

    try:
        shell = ShellRunner(open_console(args.port or "receptec", log=open(outdir / "console.log", "w")))
    except Exception as exc:  # noqa: BLE001 - any failure to open means the bench is not there
        checks["shell"] = (SKIP, f"cannot open the RECEPTEC console: {exc}")
        return done(SKIP)

    seconds = math.ceil(args.samples * args.period_ms / 1000 * 1.25) + 10
    try:
        probe, _ = shell.run("uwb peer list", max_wait=3.0)
        if "slot" not in probe:
            checks["shell"] = (SKIP, "the RECEPTEC shell did not answer 'uwb peer list'")
            return done(SKIP)
        checks["shell"] = (PASS, "answers")
        for slot in range(4):
            shell.run(f"uwb peer unregister {slot}", max_wait=1.5)
        out, _ = shell.run(f"uwb peer register 0 {args.peer}", max_wait=2.0)
        shell.run(f"uwb poll start {args.period_ms}", max_wait=3.0)
        shell.run("uwb poll stream on", max_wait=2.0)
        end = time.monotonic() + seconds
        buffer = ""
        while time.monotonic() < end:
            buffer += shell.recv()
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                m = viewer.DATA_LINE.search(ANSI.sub("", line))
                if m:
                    d = viewer.decode_payload(bytes.fromhex(m[4]))
                    records.append({"t": time.time(), "slot": int(m[1]), "n": int(m[2]), **d})
        shell.run("uwb poll stream off", max_wait=2.0)
        out, _ = shell.run("uwb poll stats", max_wait=3.0)
        m = STATS.search(ANSI.sub("", out.replace("\r", "")))
        if m:
            stats = {"slot": int(m[1]), "transactions": int(m[2]), "ok": int(m[3]),
                     "late": int(m[4]), "exhausted": int(m[5])}
        shell.run("uwb poll stop", max_wait=5.0)
    finally:
        shell.close()

    (outdir / "records.jsonl").write_text("".join(json.dumps(r) + "\n" for r in records))

    if not stats or stats["transactions"] == 0:
        checks["delivery"] = (SKIP, "no transactions were polled (peer not registered or poll not running)")
        checks["late"] = (SKIP, "no poll statistics")
    else:
        rate = stats["ok"] / stats["transactions"]
        checks["delivery"] = ((PASS if rate >= args.min_delivery else FAIL),
                              f"{stats['ok']}/{stats['transactions']} = {rate:.1%}, "
                              f"threshold {args.min_delivery:.0%}, exhausted {stats['exhausted']}")
        checks["late"] = ((PASS if stats["late"] == 0 else FAIL), f"late = {stats['late']}")

    versions = {r.get("version") for r in records}
    ok_count = len(records) >= args.samples and versions == {1}
    checks["decoded"] = ((PASS if ok_count else FAIL),
                         f"{len(records)} payloads (need {args.samples}), versions {sorted(v for v in versions if v is not None)}")

    seqs = [r["payload_seq"] for r in records if r.get("summary_valid")]
    backwards = [(a, b) for a, b in zip(seqs, seqs[1:]) if (b - a) & 0xFFFF > 0x8000]
    if not seqs:
        checks["sequence"] = (SKIP, "no payload carried a valid summary, nothing to order")
    else:
        checks["sequence"] = ((PASS if not backwards else FAIL),
                              f"{len(seqs)} sequenced payloads, {len(backwards)} step(s) backwards")

    fresh = [r for r in records if r.get("summary_valid") and not r.get("stale")]
    if len(fresh) >= args.samples:
        checks["sensor"] = (PASS, f"{len(fresh)} payloads with a fresh STM32 summary")
    elif not any(r.get("summary_valid") for r in records):
        checks["sensor"] = (SKIP, "the COLETEC sent no STM32 summary: no session is running "
                                  "(start one, e.g. over BLE, and run again)")
    else:
        checks["sensor"] = (FAIL, f"only {len(fresh)} of {args.samples} payloads carried a fresh summary")
    return done(PASS)


if __name__ == "__main__":
    sys.exit(main())
