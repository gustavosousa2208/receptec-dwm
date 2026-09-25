#!/usr/bin/env python3
"""Long-duration UWB test manager: COLETEC <-> RECEPTEC, detached, with statistics.

`start` launches a worker in its own session and returns at once; nothing waits for
it. The worker keeps the RECEPTEC shell (RTT) and the COLETEC Nordic log (RTT) open,
polls the COLETEC with `uwb poll`, samples every --sample-s seconds and writes:

  logs/long-<UTC>-<name>/
    run.json         configuration, revisions, boards
    samples.csv      one row per sample: cumulative poll counters + COLETEC link state
    events.jsonl     start, stop, reconnects, errors
    summary.json     KPIs and checks       (written at the end, or by `report`)
    summary.md       the same, readable    (idem)
    worker.log, receptec-console.log, coletec-rtt.log

  uwb-long-test.py start [--hours H] [--period-ms P] [--sample-s S] [--session] [--name N]
  uwb-long-test.py status [RUN]      progress of a run (default: the latest)
  uwb-long-test.py stop [RUN]        stop early; the summary is still written
  uwb-long-test.py report [RUN]      rebuild summary.* from samples.csv

KPIs and what they can and cannot say:
  packet statistics   transactions, ok, first-attempt share, retries, timeouts, rx errors,
                      late schedules, exhausted transactions          (RECEPTEC `uwb poll stats`)
  timing precision    poll-cycle overruns, late schedules, COLETEC clock drift in ppm and its
                      trend per hour                                  (COLETEC Nordic log)
  link                RSL and first-path power at the COLETEC          (COLETEC Nordic log)
  range accuracy      NOT MEASURED: protocol v1.0 exchanges SYNC and DATA, there is no ranging
  thermal drift       NOT MEASURED: no temperature source is exposed; the clock drift trend is
                      reported instead and must not be read as a temperature result
A KPI that cannot be measured is listed as SKIP, never PASS.

Only one long run at a time: the two probes and the RECEPTEC shell are exclusive.
"""

import argparse
import csv
import json
import math
import os
import re
import signal
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LOGS = REPO / "logs"
LOCK = LOGS / "long-test.lock"
HIL_PYTHON = Path.home() / ".local/share/atletec-hil-venv/bin/python"
NRF52 = Path.home() / "source/atletec/nrf52-bringup-fw"

STATS_HEAD = re.compile(r"poll running=(\d+) period=(\d+) ms elapsed=(\d+) ms cycles=(\d+) overruns=(\d+)")
STATS_SLOT = re.compile(
    r"slot (\d+): transactions=(\d+) ok=(\d+) \([\d.]+%\) by_attempt=(\d+)/(\d+)/(\d+) attempts=(\d+) "
    r"timeouts=(\d+) rx_err=(\d+) bad=(\d+) late=(\d+) exhausted=(\d+) bytes=(\d+) sample_gaps=(\d+)")
N_UWB = re.compile(r"UWB: state=(\d+) rx=(\d+) err/to=(\d+)/(\d+) tx=(\d+)/(\d+)/(\d+) late=(\d+)")
N_LINK = re.compile(r"UWB link: samples=(\d+) rsl=(-?[\d.]+) dBm fp=(-?[\d.]+) dBm min=(-?[\d.]+) "
                    r"max=(-?[\d.]+) avg=(-?[\d.]+) dBm")
N_DRIFT = re.compile(r"UWB drift: samples=(\d+) phase=(-?\d+) us min=(-?\d+) us max=(-?\d+) us "
                     r"sum=(-?\d+) us average=(-?\d+) milli-ppm")
N_IPC = re.compile(r"IPC: tx=(\d+) rx=(\d+) cksErr=(\d+)")
N_VDD = re.compile(r"VDD=(\d+)mV")
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

COLUMNS = ["utc", "elapsed_s", "poll_elapsed_ms", "cycles", "overruns", "transactions", "ok",
           "first", "second", "third", "attempts", "timeouts", "rx_err", "bad", "late", "exhausted",
           "bytes", "sample_gaps", "c_state", "c_rx", "c_err", "c_to", "c_late", "rsl_dbm", "fp_dbm",
           "rsl_min", "rsl_max", "drift_mppm", "drift_phase_us", "cks_err", "vdd_mv"]

SESSION_HELPER = r"""
import asyncio, sys, json
sys.path.insert(0, %(nrf52)r)
from hil import ble
async def main(action):
    dev, name = await ble.find()
    async with ble.Coletec(dev, name) as c:
        code = ble.SESSION_START if action == "start" else ble.SESSION_STOP
        await c.write(ble.SESSION_CONTROL, bytes([code]))
        st, sid = -1, 0
        for _ in range(50):
            st = (await c.read(ble.SESSION_STATE))[0]
            sid = int.from_bytes((await c.read(ble.SESSION_INFO))[0:2], "little")
            if (action == "start" and st == 1 and sid) or (action == "stop" and st == 0):
                break
            await asyncio.sleep(0.3)
        print(json.dumps({"action": action, "state": st, "session_id": sid}))
asyncio.run(main(sys.argv[1]))
"""


def utc_now():
    return datetime.now(timezone.utc)


def git_rev(path):
    try:
        out = subprocess.run(["git", "-C", str(path), "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True, timeout=10)
        return out.stdout.strip() or None
    except (OSError, subprocess.SubprocessError):
        return None


def event(run_dir, kind, **data):
    with open(run_dir / "events.jsonl", "a") as f:
        f.write(json.dumps({"utc": utc_now().isoformat(timespec="seconds"), "event": kind, **data}) + "\n")


def latest_run():
    runs = sorted(LOGS.glob("long-*/run.json"))
    return runs[-1].parent if runs else None


def resolve_run(arg):
    if arg:
        p = Path(arg)
        return p if p.is_absolute() or p.exists() else LOGS / arg
    return latest_run()


def lock_pid():
    try:
        pid = int(LOCK.read_text().split()[0])
        os.kill(pid, 0)
        return pid
    except (OSError, ValueError, IndexError):
        return None


def session(action):
    """Start or stop a STM32 collection session over BLE; returns the helper's JSON or None."""
    if not HIL_PYTHON.exists():
        return None
    out = subprocess.run([str(HIL_PYTHON), "-c", SESSION_HELPER % {"nrf52": str(NRF52)}, action],
                         capture_output=True, text=True, timeout=120)
    for line in reversed(out.stdout.strip().splitlines()):
        try:
            return json.loads(line)
        except ValueError:
            continue
    return {"action": action, "error": (out.stderr.strip().splitlines() or ["no output"])[-1][:200]}


# ------------------------------------------------------------------ worker
class Nordic:
    """Keeps the last values printed by the COLETEC Nordic log (every ~5 s)."""

    def __init__(self):
        self.buf = ""
        self.v = {}

    def feed(self, text):
        self.buf += ANSI.sub("", text).replace("\r", "")
        while "\n" in self.buf:
            line, self.buf = self.buf.split("\n", 1)
            for rx, keys in ((N_UWB, ("c_state", "c_rx", "c_err", "c_to", None, None, None, "c_late")),
                             (N_LINK, (None, "rsl_dbm", "fp_dbm", "rsl_min", "rsl_max", None)),
                             (N_DRIFT, (None, "drift_phase_us", None, None, None, "drift_mppm")),
                             (N_IPC, (None, None, "cks_err")), (N_VDD, ("vdd_mv",))):
                m = rx.search(line)
                if m:
                    for key, val in zip(keys, m.groups()):
                        if key:
                            self.v[key] = float(val) if "." in val else int(val)


def open_consoles(run_dir, cfg):
    from benchlib.console import ShellRunner, open_console
    shell = ShellRunner(open_console("receptec", log=open(run_dir / "receptec-console.log", "a")))
    nordic = None
    if cfg["coletec"]:
        nordic = open_console(cfg["coletec"], check=False, log=open(run_dir / "coletec-rtt.log", "a"))
    return shell, nordic


def worker(run_dir):
    sys.path.insert(0, str(Path.home() / "source/dotfiles/scripts"))
    cfg = json.loads((run_dir / "run.json").read_text())["config"]
    stop = {"flag": False}
    signal.signal(signal.SIGTERM, lambda *_: stop.update(flag=True))
    signal.signal(signal.SIGINT, lambda *_: stop.update(flag=True))
    LOCK.write_text(f"{os.getpid()} {run_dir.name}\n")
    status, session_id = "completed", None
    shell = nordic = None
    csv_file = open(run_dir / "samples.csv", "w", newline="")
    writer = csv.writer(csv_file)
    writer.writerow(COLUMNS)
    try:
        shell, nordic = open_consoles(run_dir, cfg)
        event(run_dir, "consoles-open")
        for command in ("uwb poll stop", f"uwb lna {cfg['lna']}"):
            shell.run(command, max_wait=3.0)
        for slot in range(4):
            shell.run(f"uwb peer unregister {slot}", max_wait=1.5)
        out, _ = shell.run(f"uwb peer register 0 {cfg['peer']}", max_wait=2.0)
        event(run_dir, "peer-registered", output=out.strip()[:120])
        if cfg["session"]:
            result = session("start")
            session_id = (result or {}).get("session_id")
            event(run_dir, "session-start", result=result)
        out, _ = shell.run(f"uwb poll start {cfg['period_ms']}", max_wait=3.0)
        event(run_dir, "poll-start", output=out.strip()[:120])
        state = Nordic()
        t0 = time.monotonic()
        next_sample = t0 + cfg["sample_s"]
        failures = 0
        while not stop["flag"] and time.monotonic() - t0 < cfg["seconds"]:
            if nordic is not None:
                try:
                    state.feed(nordic.recv())
                except Exception as exc:  # noqa: BLE001 - the RTT link can drop; keep polling
                    event(run_dir, "coletec-rtt-error", error=str(exc)[:160])
                    nordic = None
            if time.monotonic() < next_sample:
                time.sleep(0.2)
                continue
            next_sample += cfg["sample_s"]
            try:
                out, _ = shell.run("uwb poll stats", max_wait=3.0)
            except Exception as exc:  # noqa: BLE001
                failures += 1
                event(run_dir, "receptec-shell-error", error=str(exc)[:160], consecutive=failures)
                if failures >= 5:
                    status = "aborted-receptec-shell"
                    break
                continue
            head, slot = STATS_HEAD.search(out), STATS_SLOT.search(out)
            if not head or not slot:
                failures += 1
                event(run_dir, "stats-unparsed", output=out.strip()[:200], consecutive=failures)
                if failures >= 5:
                    status = "aborted-stats"
                    break
                continue
            failures = 0
            h, s = [int(x) for x in head.groups()], [int(x) for x in slot.groups()]
            row = [utc_now().isoformat(timespec="seconds"), round(time.monotonic() - t0, 1), h[2], h[3], h[4],
                   s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]]
            row += [state.v.get(k, "") for k in COLUMNS[len(row):]]
            writer.writerow(row)
            csv_file.flush()
    except Exception as exc:  # noqa: BLE001
        status = "aborted-setup"
        event(run_dir, "error", error=repr(exc)[:300])
    finally:
        try:
            if shell:
                shell.run("uwb poll stop", max_wait=5.0)
        except Exception:  # noqa: BLE001
            pass
        if cfg["session"] and session_id:
            event(run_dir, "session-stop", result=session("stop"))
        for c in (shell, nordic):
            try:
                if c:
                    c.close()
            except Exception:  # noqa: BLE001
                pass
        csv_file.close()
        event(run_dir, "end", status=status, stopped_early=stop["flag"])
        data = json.loads((run_dir / "run.json").read_text())
        data.update(end_utc=utc_now().isoformat(timespec="seconds"), status=status, session_id=session_id)
        (run_dir / "run.json").write_text(json.dumps(data, indent=1))
        try:
            report(run_dir, quiet=True)
        finally:
            LOCK.unlink(missing_ok=True)


# ------------------------------------------------------------------ report
def load_rows(run_dir):
    rows = []
    with open(run_dir / "samples.csv") as f:
        for r in csv.DictReader(f):
            rows.append({k: (float(v) if v not in ("", None) and k != "utc" else (v or None)) for k, v in r.items()})
    return rows


def slope_per_hour(xs, ys):
    """Least-squares slope of ys over xs (seconds), per hour; None when it cannot be fitted."""
    pts = [(x, y) for x, y in zip(xs, ys) if y is not None]
    if len(pts) < 3:
        return None
    mx, my = statistics.fmean(p[0] for p in pts), statistics.fmean(p[1] for p in pts)
    den = sum((p[0] - mx) ** 2 for p in pts)
    return None if den == 0 else 3600 * sum((p[0] - mx) * (p[1] - my) for p in pts) / den


def report(run_dir, quiet=False):
    meta = json.loads((run_dir / "run.json").read_text())
    cfg = meta["config"]
    rows = load_rows(run_dir)
    checks, kpi = [], {}

    def add(name, state, detail):
        checks.append({"check": name, "result": state, "detail": detail})

    if not rows:
        add("samples", "FAIL", "no sample was collected")
    else:
        last = rows[-1]
        tx, ok = last["transactions"], last["ok"]
        rate = ok / tx if tx else 0.0
        first_share = last["first"] / ok if ok else 0.0
        kpi["packets"] = {"transactions": int(tx), "ok": int(ok), "delivery": round(rate, 5),
                          "first_attempt_share": round(first_share, 5), "recovered_by_retry": int(last["second"] + last["third"]),
                          "attempts": int(last["attempts"]), "timeouts": int(last["timeouts"]), "rx_err": int(last["rx_err"]),
                          "bad": int(last["bad"]), "late": int(last["late"]), "exhausted": int(last["exhausted"]),
                          "sample_gaps": int(last["sample_gaps"])}
        # per-interval delivery from cumulative counters
        worst, bad_intervals, run, longest = 1.0, 0, 0, 0
        for a, b in zip(rows, rows[1:]):
            dt, dk = b["transactions"] - a["transactions"], b["ok"] - a["ok"]
            if dt > 0:
                worst = min(worst, dk / dt)
            failed = (b["exhausted"] - a["exhausted"]) > 0
            bad_intervals += 1 if dt > 0 and dk / dt < cfg["min_delivery"] else 0
            run = run + 1 if failed else 0
            longest = max(longest, run)
        kpi["intervals"] = {"count": len(rows) - 1, "worst_delivery": round(worst, 4),
                            "below_threshold": bad_intervals, "longest_run_with_exhausted": longest}
        add("delivery", "PASS" if rate >= cfg["min_delivery"] else "FAIL",
            f"{int(ok)}/{int(tx)} = {rate:.2%} (threshold {cfg['min_delivery']:.0%}); first attempt {first_share:.1%}; "
            f"retry recovered {int(last['second'] + last['third'])}; exhausted {int(last['exhausted'])}; worst interval {worst:.1%}")
        add("late", "PASS" if last["late"] == 0 else "FAIL", f"late schedules = {int(last['late'])}")
        add("cycle-overruns", "PASS" if last["overruns"] == 0 else "FAIL", f"poll-cycle overruns = {int(last['overruns'])}")
        xs = [r["elapsed_s"] for r in rows]
        rsl = [r["rsl_dbm"] for r in rows if r.get("rsl_dbm") is not None]
        fp = [r["fp_dbm"] for r in rows if r.get("fp_dbm") is not None]
        if rsl:
            kpi["link"] = {"rsl_mean": round(statistics.fmean(rsl), 2), "rsl_min": round(min(rsl), 2),
                           "rsl_max": round(max(rsl), 2), "rsl_std": round(statistics.pstdev(rsl), 2),
                           "fp_mean": round(statistics.fmean(fp), 2) if fp else None,
                           "rsl_slope_db_per_hour": slope_per_hour(xs, [r.get("rsl_dbm") for r in rows])}
            add("link", "PASS", f"RSL mean {kpi['link']['rsl_mean']} dBm (min {kpi['link']['rsl_min']}, max {kpi['link']['rsl_max']}, "
                                f"std {kpi['link']['rsl_std']}); informational, no threshold defined")
        else:
            add("link", "SKIP", "the COLETEC Nordic log gave no RSL sample (RTT not attached or no link lines)")
        drift = [r["drift_mppm"] / 1000.0 for r in rows if r.get("drift_mppm") is not None]
        if drift:
            kpi["clock_drift"] = {"mean_ppm": round(statistics.fmean(drift), 3), "std_ppm": round(statistics.pstdev(drift), 3),
                                  "min_ppm": round(min(drift), 3), "max_ppm": round(max(drift), 3),
                                  "trend_ppm_per_hour": slope_per_hour(xs, [None if r.get("drift_mppm") is None else r["drift_mppm"] / 1000.0 for r in rows])}
            add("timing-precision", "PASS", f"COLETEC clock drift mean {kpi['clock_drift']['mean_ppm']} ppm, std {kpi['clock_drift']['std_ppm']}; informational")
        else:
            add("timing-precision", "SKIP", "no drift sample from the COLETEC log")
        cks = [r["cks_err"] for r in rows if r.get("cks_err") is not None]
        if cks:
            kpi["coletec_ipc_checksum_errors"] = {"start": int(cks[0]), "end": int(cks[-1])}
    add("range-accuracy", "SKIP", "not measured: protocol v1.0 has no ranging exchange")
    add("thermal-drift", "SKIP", "not measured: no temperature source is exposed; see the clock drift trend, which is not a temperature result")
    status = meta.get("status", "running")
    if status != "completed":
        add("run", "FAIL", f"run ended as {status}")
    measured = [c for c in checks if c["result"] != "SKIP"]
    overall = "FAIL" if any(c["result"] == "FAIL" for c in checks) else "PASS"
    skipped = [c["check"] for c in checks if c["result"] == "SKIP"]
    summary = {"run": run_dir.name, "result": overall, "measured_checks": len(measured), "not_measured": skipped,
               "config": cfg, "started": meta.get("start_utc"), "ended": meta.get("end_utc"), "status": status,
               "revisions": meta.get("revisions"), "kpi": kpi, "checks": checks}
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=1))
    md = [f"# UWB long test {run_dir.name}", "", f"**Result: {overall}** ({len(measured)} measured checks; not measured: "
          f"{', '.join(skipped) or 'none'})", "", f"- period {cfg['period_ms']} ms, sample every {cfg['sample_s']} s, "
          f"planned {cfg['seconds']} s, status `{status}`", f"- peer `{cfg['peer']}`, LNA {cfg['lna']}, STM32 session {'on' if cfg['session'] else 'off'}",
          f"- revisions {json.dumps(meta.get('revisions'))}", "", "| Check | Result | Detail |", "|---|---|---|"]
    md += [f"| {c['check']} | {c['result']} | {c['detail']} |" for c in checks]
    (run_dir / "summary.md").write_text("\n".join(md) + "\n")
    if not quiet:
        print("\n".join(md))
    return summary


# ------------------------------------------------------------------ commands
def cmd_start(a):
    pid = lock_pid()
    if pid:
        sys.exit(f"a long run is already active (pid {pid}); `status` shows it, `stop` ends it")
    seconds = int(a.hours * 3600) if a.hours else a.seconds
    stamp = utc_now().strftime("%Y%m%dT%H%M%SZ")
    run_dir = LOGS / f"long-{stamp}-{a.name}"
    run_dir.mkdir(parents=True)
    cfg = {"name": a.name, "seconds": seconds, "period_ms": a.period_ms, "sample_s": a.sample_s, "peer": a.peer,
           "lna": a.lna, "session": a.session, "coletec": None if a.no_coletec_log else a.coletec,
           "min_delivery": a.min_delivery}
    meta = {"config": cfg, "start_utc": utc_now().isoformat(timespec="seconds"), "status": "running",
            "revisions": {"receptec": git_rev(REPO), "nrf52-bringup-fw": git_rev(NRF52),
                          "contracts": git_rev(REPO / "contracts")}}
    (run_dir / "run.json").write_text(json.dumps(meta, indent=1))
    log = open(run_dir / "worker.log", "w")
    proc = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "worker", str(run_dir)],
                            stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
    (run_dir / "pid").write_text(str(proc.pid))
    print(f"started {run_dir.name} pid {proc.pid}: {seconds} s at {a.period_ms} ms, sample every {a.sample_s} s")
    print(f"  status: {Path(__file__).name} status    stop: {Path(__file__).name} stop    dir: {run_dir}")


def cmd_status(a):
    run_dir = resolve_run(a.run)
    if not run_dir or not (run_dir / "run.json").exists():
        sys.exit("no run found")
    meta = json.loads((run_dir / "run.json").read_text())
    pid = lock_pid()
    alive = bool(pid) and LOCK.exists() and run_dir.name in LOCK.read_text()
    rows = load_rows(run_dir) if (run_dir / "samples.csv").exists() else []
    planned = meta["config"]["seconds"]
    print(f"{run_dir.name}: {'RUNNING pid ' + str(pid) if alive else meta.get('status', '?')}")
    if rows:
        r = rows[-1]
        tx, ok = r["transactions"], r["ok"]
        print(f"  elapsed {r['elapsed_s']:.0f}/{planned} s ({100 * r['elapsed_s'] / planned:.0f}%), {len(rows)} samples")
        print(f"  transactions {int(tx)}, ok {int(ok)} ({100 * ok / tx if tx else 0:.2f}%), exhausted {int(r['exhausted'])}, late {int(r['late'])}, "
              f"rsl {r.get('rsl_dbm')} dBm, last sample {r['utc']}")
    if (run_dir / "summary.md").exists() and not alive:
        print(f"  summary: {run_dir / 'summary.md'}")


def cmd_stop(a):
    run_dir = resolve_run(a.run)
    pid = lock_pid()
    if not pid:
        sys.exit("no active long run")
    os.kill(pid, signal.SIGTERM)
    print(f"SIGTERM sent to pid {pid} ({run_dir.name if run_dir else '?'}); the worker writes the summary before it exits")


def cmd_report(a):
    run_dir = resolve_run(a.run)
    if not run_dir:
        sys.exit("no run found")
    report(run_dir)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("start")
    s.add_argument("--hours", type=float, help="duration in hours (or --seconds)")
    s.add_argument("--seconds", type=int, default=600)
    s.add_argument("--period-ms", type=int, default=1000)
    s.add_argument("--sample-s", type=int, default=30)
    s.add_argument("--peer", default="02:00:00:00:00:00:00:02", help="EUI-64 of the COLETEC (default: production)")
    s.add_argument("--lna", choices=("on", "off"), default="on")
    s.add_argument("--session", action="store_true", help="start a STM32 collection session over BLE first (real workload)")
    s.add_argument("--coletec", default="prod-coletec", help="lstty nickname whose Nordic RTT log is read")
    s.add_argument("--no-coletec-log", action="store_true", help="do not attach to the COLETEC RTT")
    s.add_argument("--min-delivery", type=float, default=0.95)
    s.add_argument("--name", default="run")
    s.set_defaults(func=cmd_start)
    for name, fn in (("status", cmd_status), ("stop", cmd_stop), ("report", cmd_report)):
        p = sub.add_parser(name)
        p.add_argument("run", nargs="?")
        p.set_defaults(func=fn)
    w = sub.add_parser("worker")
    w.add_argument("run_dir")
    a = ap.parse_args()
    if a.cmd == "worker":
        worker(Path(a.run_dir))
    else:
        a.func(a)


if __name__ == "__main__":
    main()
