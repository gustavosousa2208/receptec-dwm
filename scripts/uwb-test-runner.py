#!/usr/bin/env python3
"""Host runner for the RECEPTEC runtime UWB tests (`uwb test <name> <slot>`).

Runs the tests against the production COLETEC through the RECEPTEC shell, waits for
exactly one terminal result line per test, and writes a raw log and a summary:

  UWB_TEST result=PASS test=<name> slot=<0-3> key=value ...
  UWB_TEST result=FAIL test=<name> slot=<0-3> reason=<token> key=value ...

Order: data_payload, retry 1, retry 2, wrong_source, bad_type, bad_reserved,
bad_length, bad_dt, duplicate, sequence_wrap, rx_boundary, discovery.

It never runs STOP, never flashes, resets or power-cycles a board. Before each test
it stops scheduled traffic (`uwb poll stop`) and leaves it stopped at the end; if
that cannot be confirmed the summary says so. Exit code 0 only when every selected
test passed (a test that cannot run is SKIP, and a SKIP is never a pass); 1 otherwise; 2
when the runner itself could not start.

  uwb-test-runner.py --slot 0                      all tests
  uwb-test-runner.py --slot 0 --only duplicate,bad_dt
  uwb-test-runner.py --list
  uwb-test-runner.py --fixture recorded.json       no hardware: replay a transcript

The RECEPTEC shell is reached by lstty nickname (RTT) or a serial port (--port).
--coletec-rtt NICK also records that Nordic's RTT log next to the results.
"""

import argparse
import json
import re
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LOGS = REPO / "logs"

# (name, shell arguments after `uwb test <name>`), in the order the plan requires.
SUITE = [
    ("data_payload", []),
    ("retry", ["1"]),
    ("retry", ["2"]),
    ("wrong_source", []),
    ("bad_type", []),
    ("bad_reserved", []),
    ("bad_length", []),
    ("bad_dt", []),
    ("duplicate", []),
    ("sequence_wrap", []),
    ("rx_boundary", []),
    ("discovery", []),
]
# Finite deadlines per test in seconds (generous; the firmware has its own).
TIMEOUTS = {"rx_boundary": 30, "discovery": 90, "retry": 20, "data_payload": 20}
DEFAULT_TIMEOUT = 15

LINE = re.compile(r"^UWB_TEST (.*)$")
# The shell answers these instead of a result when a test cannot start; waiting the full
# deadline for a UWB_TEST line that will never come would only slow the suite down.
STOP_RE = r"UWB_TEST result=|not implemented|could not start|has no registered peer|is running|usage:"
TOKEN = re.compile(r"^[a-z0-9_.-]+$")
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
# The production COLETEC prints this every ~5 s: rx = frames with a valid FCS that passed its
# hardware filter, tx = DATA / ACK / STOP_ACK responses it sent.
COLETEC_LINE = re.compile(r"UWB: state=(\d+) rx=(\d+) err/to=(\d+)/(\d+) tx=(\d+)/(\d+)/(\d+)")
# Tests that send a malformed request and pass on "no response". "No response" alone cannot tell
# a rejection from a lost frame, so these also need the COLETEC to have counted the frame.
MUTATION_TESTS = {"wrong_source", "bad_type", "bad_reserved", "bad_length", "bad_dt"}
# Lines the COLETEC Nordic prints on its own RTT when it changes discovery state.
COLETEC_EVENTS = {
    "entering-discovery": "UWB synchronization lost: entering discovery",
    "discovery-off": "UWB discovery OFF",
    "reentry": "UWB discovery reentry",
}


class ParseError(Exception):
    pass


class Disconnected(Exception):
    pass


def parse_result_line(line):
    """Parse one `UWB_TEST ...` line into a dict; raises ParseError when malformed."""
    m = LINE.match(ANSI.sub("", line).strip())
    if not m:
        raise ParseError("not a UWB_TEST line")
    fields = {}
    for part in m.group(1).split(" "):
        if "=" not in part:
            raise ParseError(f"field without '=': {part!r}")
        key, value = part.split("=", 1)
        value_ok = value in ("PASS", "FAIL") if key == "result" else bool(TOKEN.match(value))
        if not TOKEN.match(key) or not value_ok or key in fields:
            raise ParseError(f"bad or repeated field: {part!r}")
        fields[key] = value
    for required in ("result", "test", "slot"):
        if required not in fields:
            raise ParseError(f"missing {required}")
    if fields["result"] not in ("PASS", "FAIL"):
        raise ParseError(f"result must be PASS or FAIL, got {fields['result']}")
    if fields["result"] == "FAIL" and "reason" not in fields:
        raise ParseError("FAIL without reason")
    if not fields["slot"].isdigit() or int(fields["slot"]) > 3:
        raise ParseError(f"slot out of range: {fields['slot']}")
    return fields


def find_results(text):
    """Every result line in the text, parsed, or the error for a malformed one.

    Split on the `UWB_TEST result=` marker, not on newlines: a line can arrive in two
    reads, and two results can arrive without a newline between them once the shell strips
    it. Each chunk runs up to the next newline. The shell's own messages may mention the
    name UWB_TEST without being a result, and are ignored.
    """
    found = []
    clean = ANSI.sub("", text).replace("\r", "\n")
    for chunk in re.split(r"(?=UWB_TEST result=)", clean)[1:]:
        line = chunk.split("\n", 1)[0]
        try:
            found.append(parse_result_line(line))
        except ParseError as exc:
            found.append({"malformed": str(exc), "line": line.strip()[:160]})
    return found


def judge(text, test, slot):
    """Decide one test from the console text it produced.

    Returns (verdict, reason, fields): verdict PASS/FAIL, reason a stable token.
    A test is PASS only for exactly one well-formed PASS line for this test and slot.
    """
    results = find_results(text)
    if not results and "not implemented" in text:
        return "SKIP", "not-implemented", {}
    if not results and "has no registered peer" in text:
        return "FAIL", "no-peer", {}
    if not results and "is running" in text:
        return "FAIL", "busy", {}
    if not results:
        return "FAIL", "no-result", {}
    if len(results) > 1:
        return "FAIL", "duplicate-result", {"lines": len(results)}
    r = results[0]
    if "malformed" in r:
        return "FAIL", "malformed-result", {"detail": r["malformed"]}
    if r["test"] != test or r["slot"] != str(slot):
        return "FAIL", "wrong-test", {"got": f"{r['test']}/{r['slot']}"}
    if r["result"] == "PASS":
        return "PASS", "ok", r
    return "FAIL", r.get("reason", "unknown"), r


def parse_coletec_counters(text):
    """The last COLETEC status line in text as a dict, or None."""
    found = None
    for m in COLETEC_LINE.finditer(ANSI.sub("", text)):
        found = {"rx": int(m[2]), "err": int(m[3]), "to": int(m[4]), "tx": int(m[5]) + int(m[6]) + int(m[7])}
    return found


def count_coletec_events(text):
    """How many times each COLETEC discovery-state line appears in the text."""
    clean = ANSI.sub("", text)
    counts = {name: clean.count(marker) for name, marker in COLETEC_EVENTS.items()}
    # The order matters: a reentry that happened before the COLETEC went to discovery (the
    # warm-up starts from a peer that lost sync while the poll was stopped) proves nothing.
    entered = clean.find(COLETEC_EVENTS["entering-discovery"])
    counts["reentry-after-entry"] = int(entered >= 0 and clean.find(COLETEC_EVENTS["reentry"], entered) >= 0)
    return counts


def evidence_delta(before, after):
    """What the COLETEC counted between two snapshots; unanswered = received but not answered."""
    if not before or not after:
        return None
    d = {k: after[k] - before[k] for k in ("rx", "err", "to", "tx")}
    d["unanswered"] = d["rx"] - d["tx"]
    return d


class BenchTransport:
    """The RECEPTEC shell through benchlib (RTT by lstty nickname, or a serial port)."""

    def __init__(self, target, coletec, log):
        sys.path.insert(0, str(Path.home() / "source/dotfiles/scripts"))
        from benchlib.console import ConsoleError, ShellRunner, open_console
        self.ConsoleError = ConsoleError
        self.shell = ShellRunner(open_console(target, log=log))
        self.coletec = open_console(coletec, check=False, log=open(log.name + ".gdb", "a")) if coletec else None
        self.coletec_log = open(log.name + ".coletec", "a") if coletec else None
        self.snap_lock, self.snap_seq, self.snap = threading.Lock(), 0, None
        self.coletec_text = ""
        self.stop_reader = False
        self.reader = None
        if self.coletec:
            self.reader = threading.Thread(target=self._read_coletec, daemon=True)
            self.reader.start()

    def _read_coletec(self):
        buf = ""
        while not self.stop_reader:
            try:
                chunk = self.coletec.recv()
            except Exception:  # noqa: BLE001 - the RTT link can drop; evidence just stops
                return
            if not chunk:
                time.sleep(0.05)
                continue
            self.coletec_log.write(chunk)
            self.coletec_log.flush()
            with self.snap_lock:
                self.coletec_text += chunk
            buf += chunk
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                found = parse_coletec_counters(line)
                if found:
                    with self.snap_lock:
                        self.snap, self.snap_seq = found, self.snap_seq + 1

    def mark(self):
        with self.snap_lock:
            return len(self.coletec_text)

    def text_since(self, mark):
        with self.snap_lock:
            return self.coletec_text[mark:]

    def fresh_snapshot(self, timeout=9.0):
        """Wait for a COLETEC status line newer than any seen so far and return its counters."""
        if not self.coletec:
            return None
        with self.snap_lock:
            seen = self.snap_seq
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            with self.snap_lock:
                if self.snap_seq > seen:
                    return dict(self.snap)
            time.sleep(0.1)
        return None

    def run(self, command, timeout, stop_re=None):
        try:
            text, _ = self.shell.run(command, stop_re=stop_re, max_wait=timeout)
            return text
        except self.ConsoleError as exc:
            raise Disconnected(str(exc)) from exc

    def drain(self, seconds):
        end = time.monotonic() + seconds
        text = ""
        try:
            while time.monotonic() < end:
                text += self.shell.recv()
                time.sleep(0.05)
        except self.ConsoleError as exc:
            raise Disconnected(str(exc)) from exc
        return text

    def close(self):
        self.stop_reader = True
        if self.reader:
            self.reader.join(2)
        for c in (self.shell, self.coletec):
            try:
                if c:
                    c.close()
            except Exception:  # noqa: BLE001
                pass


class FixtureTransport:
    """Replays a recorded transcript: {"commands": {"uwb test bad_dt 0": "text", ...}}."""

    def __init__(self, path):
        self.commands = json.loads(Path(path).read_text())["commands"]
        self.sent = []

    def run(self, command, timeout, stop_re=None):
        self.sent.append(command)
        if command not in self.commands:
            raise Disconnected(f"fixture has no reply for {command!r}")
        reply = self.commands[command]
        if reply == "__disconnect__":
            raise Disconnected("fixture disconnect")
        return reply

    def drain(self, seconds):
        return self.commands.get("__drain__", "")

    def fresh_snapshot(self, timeout=9.0):
        return None

    def mark(self):
        return 0

    def text_since(self, mark):
        return self.commands.get("__coletec__", "")

    def close(self):
        pass


def select(only):
    if not only:
        return list(SUITE)
    wanted = [w.strip() for w in only.split(",") if w.strip()]
    chosen = [entry for entry in SUITE if entry[0] in wanted]
    unknown = [w for w in wanted if w not in {e[0] for e in SUITE}]
    if unknown:
        raise SystemExit(f"unknown test(s): {', '.join(unknown)}; --list shows them")
    return chosen


def run_suite(transport, slot, selection, out_dir=None, clock=time.monotonic):
    """Run the selection and return the summary dict. Never raises for a test failure."""
    summary = {"slot": slot, "results": [], "cleanup": "unconfirmed", "aborted": None}
    snapshot = getattr(transport, "fresh_snapshot", lambda timeout=9.0: None)
    mark = getattr(transport, "mark", lambda: 0)
    text_since = getattr(transport, "text_since", lambda m: "")
    try:
        previous = snapshot()
        for name, args in selection:
            label = name + (" " + " ".join(args) if args else "")
            timeout = TIMEOUTS.get(name, DEFAULT_TIMEOUT)
            entry = {"test": label, "verdict": "FAIL", "reason": "not-run"}
            summary["results"].append(entry)
            transport.run("uwb poll stop", 5)
            window_start = mark()
            t0 = clock()
            text = transport.run(f"uwb test {label} {slot}", timeout, stop_re=STOP_RE)
            text += transport.drain(0.7)  # a second result line after the first is a defect
            window_end = mark()  # the COLETEC's own lines up to here belong to this test
            verdict, reason, fields = judge(text, name, slot)
            if verdict == "FAIL" and reason == "no-result" and clock() - t0 >= timeout - 0.5:
                reason = "runner-timeout"
            entry.update(verdict=verdict, reason=reason, seconds=round(clock() - t0, 1),
                         fields={k: v for k, v in fields.items() if k not in ("result", "test", "slot")})
            current = snapshot() if verdict != "SKIP" else previous
            delta = evidence_delta(previous, current)
            previous = current or previous
            if name == "discovery" and verdict != "SKIP":
                window = text_since(window_start)
                if window_end > window_start:
                    window = window[: window_end - window_start]
                events = count_coletec_events(window)
                entry["coletec_events"] = events
                if verdict == "PASS" and not events["reentry-after-entry"]:
                    # The RECEPTEC only saw silence and then an answer; without the COLETEC's own
                    # lines the transition is a guess, so it is not a pass.
                    entry.update(verdict="FAIL", reason="no-evidence-discovery")
            if delta:
                entry["coletec"] = delta
                if name in MUTATION_TESTS and verdict == "PASS" and delta["unanswered"] < 1:
                    # Nothing reached the COLETEC unanswered: the silence proves nothing.
                    entry.update(verdict="FAIL", reason="no-evidence-frame-received")
            if out_dir:
                (out_dir / "raw.log").open("a").write(f"=== {label} slot {slot}\n{text}\n")
    except Disconnected as exc:
        summary["aborted"] = f"console disconnected: {exc}"
    finally:
        try:
            transport.run("uwb poll stop", 5)
            summary["cleanup"] = "confirmed"
        except Exception:  # noqa: BLE001
            summary["cleanup"] = "unconfirmed"
    passed = sum(1 for r in summary["results"] if r["verdict"] == "PASS")
    skipped = sum(1 for r in summary["results"] if r["verdict"] == "SKIP")
    ran = len(summary["results"])
    summary["passed"], summary["ran"], summary["selected"] = passed, ran, len(selection)
    summary["skipped"] = skipped
    summary["ok"] = passed == len(selection) and ran == len(selection) and not summary["aborted"]
    return summary


def render(summary):
    failed = sum(1 for r in summary["results"] if r["verdict"] == "FAIL")
    verdict = "PASS" if summary["ok"] else ("FAIL" if failed or summary["aborted"] else "INCOMPLETE")
    lines = [f"# UWB runtime tests, slot {summary['slot']}", "",
             f"**{verdict}**: {summary['passed']}/{summary['selected']} passed, {summary.get('skipped', 0)} skipped "
             f"(not implemented), {failed} failed. Cleanup (`uwb poll stop`): {summary['cleanup']}."]
    if summary["aborted"]:
        lines.append(f"Aborted: {summary['aborted']}")
    lines += ["", "| Test | Result | Reason | Detail |", "|---|---|---|---|"]
    for r in summary["results"]:
        detail = " ".join(f"{k}={v}" for k, v in r.get("fields", {}).items())
        if r.get("coletec_events"):
            e = r["coletec_events"]
            detail += (f" | coletec entered-discovery={e['entering-discovery']} reentry={e['reentry']} "
                       f"reentry-after-entry={e['reentry-after-entry']} off-windows={e['discovery-off']}")
        if r.get("coletec"):
            c = r["coletec"]
            detail += f" | coletec rx+{c['rx']} tx+{c['tx']} unanswered={c['unanswered']} err+{c['err']} to+{c['to']}"
        lines.append(f"| {r['test']} | {r['verdict']} | {r['reason']} | {detail} |")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--slot", type=int, default=0, choices=range(4))
    ap.add_argument("--target", default="receptec", help="lstty nickname of the RECEPTEC (RTT), or use --port")
    ap.add_argument("--port", help="serial port of the RECEPTEC shell instead of RTT")
    ap.add_argument("--only", help="comma separated test names")
    ap.add_argument("--coletec-rtt", help="lstty nickname of the COLETEC to record RTT from, e.g. prod-coletec")
    ap.add_argument("--out", type=Path, help="output directory (default logs/uwb-tests-<UTC>)")
    ap.add_argument("--fixture", help="replay a recorded transcript instead of using hardware")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.list:
        print("\n".join(name + (" " + " ".join(args) if args else "") for name, args in SUITE))
        return 0
    selection = select(a.only)
    out_dir = a.out or LOGS / f"uwb-tests-{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}"
    out_dir.mkdir(parents=True, exist_ok=True)
    try:
        transport = (FixtureTransport(a.fixture) if a.fixture
                     else BenchTransport(a.port or a.target, a.coletec_rtt, open(out_dir / "console.log", "a")))
    except Exception as exc:  # noqa: BLE001 - nothing was started, so nothing needs cleaning up
        print(f"cannot start: {exc}", file=sys.stderr)
        return 2
    try:
        summary = run_suite(transport, a.slot, selection, out_dir)
    except KeyboardInterrupt:
        summary = {"slot": a.slot, "results": [], "cleanup": "unconfirmed", "aborted": "interrupted",
                   "passed": 0, "ran": 0, "selected": len(selection), "ok": False}
        try:
            transport.run("uwb poll stop", 5)
            summary["cleanup"] = "confirmed"
        except Exception:  # noqa: BLE001
            pass
    finally:
        transport.close()
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=1))
    (out_dir / "summary.md").write_text(render(summary))
    print(render(summary))
    print(f"logs: {out_dir}")
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
