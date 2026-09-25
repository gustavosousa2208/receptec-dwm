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
test passed; 1 otherwise; 2 when the runner itself could not start.

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
TIMEOUTS = {"rx_boundary": 30, "discovery": 240, "retry": 20, "data_payload": 20}
DEFAULT_TIMEOUT = 15

LINE = re.compile(r"^UWB_TEST (.*)$")
TOKEN = re.compile(r"^[a-z0-9_.-]+$")
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


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
    """Every UWB_TEST line in the text, parsed, or the error for a malformed one."""
    found = []
    for raw in text.replace("\r", "\n").split("\n"):
        if "UWB_TEST" not in raw:
            continue
        try:
            found.append(parse_result_line(raw[raw.index("UWB_TEST"):]))
        except ParseError as exc:
            found.append({"malformed": str(exc), "line": raw.strip()[:160]})
    return found


def judge(text, test, slot):
    """Decide one test from the console text it produced.

    Returns (verdict, reason, fields): verdict PASS/FAIL, reason a stable token.
    A test is PASS only for exactly one well-formed PASS line for this test and slot.
    """
    results = find_results(text)
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


class BenchTransport:
    """The RECEPTEC shell through benchlib (RTT by lstty nickname, or a serial port)."""

    def __init__(self, target, coletec, log):
        sys.path.insert(0, str(Path.home() / "source/dotfiles/scripts"))
        from benchlib.console import ConsoleError, ShellRunner, open_console
        self.ConsoleError = ConsoleError
        self.shell = ShellRunner(open_console(target, log=log))
        self.coletec = open_console(coletec, check=False, log=open(log.name + ".coletec", "a")) if coletec else None

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
                if self.coletec:
                    self.coletec.recv()
                time.sleep(0.05)
        except self.ConsoleError as exc:
            raise Disconnected(str(exc)) from exc
        return text

    def close(self):
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
    try:
        for name, args in selection:
            label = name + (" " + " ".join(args) if args else "")
            timeout = TIMEOUTS.get(name, DEFAULT_TIMEOUT)
            entry = {"test": label, "verdict": "FAIL", "reason": "not-run"}
            summary["results"].append(entry)
            transport.run("uwb poll stop", 5)
            t0 = clock()
            text = transport.run(f"uwb test {label} {slot}", timeout, stop_re=r"UWB_TEST result=")
            text += "\n" + transport.drain(0.7)  # a second result line after the first is a defect
            verdict, reason, fields = judge(text, name, slot)
            if verdict == "FAIL" and reason == "no-result" and clock() - t0 >= timeout - 0.5:
                reason = "runner-timeout"
            entry.update(verdict=verdict, reason=reason, seconds=round(clock() - t0, 1),
                         fields={k: v for k, v in fields.items() if k not in ("result", "test", "slot")})
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
    ran = len(summary["results"])
    summary["passed"], summary["ran"], summary["selected"] = passed, ran, len(selection)
    summary["ok"] = passed == len(selection) and ran == len(selection) and not summary["aborted"]
    return summary


def render(summary):
    lines = [f"# UWB runtime tests, slot {summary['slot']}", "",
             f"**{'PASS' if summary['ok'] else 'FAIL'}**: {summary['passed']}/{summary['selected']} passed "
             f"({summary['ran']} run). Cleanup (`uwb poll stop`): {summary['cleanup']}."]
    if summary["aborted"]:
        lines.append(f"Aborted: {summary['aborted']}")
    lines += ["", "| Test | Result | Reason | Detail |", "|---|---|---|---|"]
    for r in summary["results"]:
        detail = " ".join(f"{k}={v}" for k, v in r.get("fields", {}).items())
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
