#!/usr/bin/env python3
"""Repeat `uwb sync <slot>` from the real RECEPTEC to the fake COLETECs.

Baseline link measurement while the RECEPTEC has no DATA_REQ scheduler.

The fake COLETECs are driven over their serial ports (the J-Link VCP; find them
with `lstty`). The RECEPTEC has no serial port on the bench yet, so its shell is
reached over SEGGER RTT through one J-Link GDB server; pass --receptec-port once
it has a serial port and no GDB server is started at all.

Latency is host-side wall clock around the shell command, so it includes shell
overhead and is only an upper bound on the radio exchange. The pass/fail counts
are the result.
"""

import argparse
import re
import socket
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import serial

ANSI = re.compile(r"\x1b\[[0-9;]*m")
PROMPT = re.compile(r"(rtt|uart):~\$ ")
SYNC_DONE = re.compile(r"SYNC/ACK (passed|failed)")
SYNC_FAIL = re.compile(r"SYNC/ACK failed: (-?\d+)")


def lstty(*args):
    return subprocess.check_output(["lstty", *args], text=True).strip()


def reset_board(nick):
    """System reset through a plain JLinkExe session (no GDB server)."""
    serial_no = lstty("jlink", "get", nick)
    device = lstty("jlink", "get", nick, "--field", "device")
    subprocess.run(["JLinkExe", "-USB", serial_no, "-device", device, "-if", "SWD",
                    "-speed", "4000", "-autoconnect", "1", "-NoGui", "1"],
                   input="r\ng\nexit\n", text=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=60, check=True)


class Shell:
    """A Zephyr shell over some transport. Subclasses provide _recv and _send."""

    def __init__(self, name, log):
        self.name = name
        self.log = log

    def _note(self, text):
        self.log.write(f"[{self.name}] {text}")
        self.log.flush()

    def drain(self, quiet=0.3):
        last = time.monotonic()
        while time.monotonic() - last < quiet:
            if self._recv():
                last = time.monotonic()

    def run(self, command, stop_re=PROMPT, max_wait=3.0):
        """Send one line; return (output after the echo, seconds)."""
        self.drain(0.15)
        self._note(f"\n>>> {command}\n")
        start = time.monotonic()
        self._send(command.encode() + b"\r")
        out = ""
        while time.monotonic() - start < max_wait:
            out += self._recv()
            after = out.split(command, 1)[1] if command in out else ""
            if after and stop_re.search(after):
                break
        return ANSI.sub("", out.replace("\r", "")), time.monotonic() - start

    def close(self):
        pass


class SerialShell(Shell):
    def __init__(self, name, port, log, baud=115200):
        super().__init__(name, log)
        self.port = serial.Serial(port, baud, timeout=0.05)
        self.port.reset_input_buffer()
        self._send(b"\r")
        self.drain(0.4)

    def _recv(self):
        data = self.port.read(4096)
        text = data.decode("utf-8", "replace")
        if text:
            self._note(text)
        return text

    def _send(self, data):
        self.port.write(data)
        self.port.flush()

    def close(self):
        self.port.close()


class RttShell(Shell):
    """RTT telnet port of a J-Link GDB server, for boards without a serial port."""

    def __init__(self, name, nick, log, rtt_port=19021, gdb_port=3331):
        super().__init__(name, log)
        self.server = subprocess.Popen(
            ["JLinkGDBServerCLExe", "-select", f"USB={lstty('jlink', 'get', nick)}",
             "-device", lstty("jlink", "get", nick, "--field", "device"),
             "-if", "SWD", "-speed", "4000", "-nohalt", "-noir", "-silent",
             "-port", str(gdb_port), "-swoport", str(gdb_port + 1),
             "-telnetport", str(gdb_port + 2), "-RTTTelnetPort", str(rtt_port)],
            stdout=log, stderr=subprocess.STDOUT)
        end = time.monotonic() + 15
        while True:
            if self.server.poll() is not None:
                raise RuntimeError(f"{name}: J-Link GDB server exited")
            try:
                self.sock = socket.create_connection(("127.0.0.1", rtt_port), 1.0)
                break
            except OSError:
                if time.monotonic() > end:
                    raise RuntimeError(f"{name}: RTT port never opened")
                time.sleep(0.3)
        self.sock.settimeout(0.05)
        time.sleep(0.6)
        self.drain(0.4)
        self._send(b"\r")  # clears any banner bytes the server put on the input line
        self.drain(0.4)

    def _recv(self):
        try:
            data = self.sock.recv(4096)
        except socket.timeout:
            return ""
        if not data:
            raise ConnectionError(f"{self.name}: RTT connection closed")
        text = data.decode("utf-8", "replace")
        self._note(text)
        return text

    def _send(self, data):
        # The RTT down buffer is small (about 16 bytes): send long lines in pieces.
        for i in range(0, len(data), 8):
            self.sock.sendall(data[i:i + 8])
            time.sleep(0.15)

    def close(self):
        self.sock.close()
        self.server.terminate()
        try:
            self.server.wait(5)
        except subprocess.TimeoutExpired:
            self.server.kill()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--count", type=int, default=50, help="SYNCs per slot (default 50)")
    ap.add_argument("--slots", default="0,1", help="slots to alternate (default 0,1)")
    ap.add_argument("--receptec-port", help="serial port of the RECEPTEC shell (default: RTT)")
    ap.add_argument("--fake-0-port", default=str(Path.home() / ".serial" / "fake-coletec-0"))
    ap.add_argument("--fake-1-port", default=str(Path.home() / ".serial" / "fake-coletec-1"))
    ap.add_argument("--poll-ms", type=int,
                    help="instead of manual SYNCs, run `uwb poll start <ms>` on the RECEPTEC")
    ap.add_argument("--duration", type=int, default=30, help="poll run length in seconds")
    ap.add_argument("--no-reset", action="store_true", help="do not reset the boards first")
    ap.add_argument("--no-start", action="store_true",
                    help="do not send `uwb coletec start` to the fake COLETECs")
    ap.add_argument("--log-dir", type=Path, default=Path(__file__).resolve().parent.parent / "logs")
    args = ap.parse_args()

    slots = [int(s) for s in args.slots.split(",")]
    args.log_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    raw_path = args.log_dir / f"sync-sweep-{stamp}.log"
    results = {s: {"pass": 0, "fail": {}, "lat": []} for s in slots}
    shells = []
    with open(raw_path, "w") as log:
        try:
            if not args.no_reset:
                for nick in ("receptec", "fake-coletec-0", "fake-coletec-1"):
                    reset_board(nick)
                time.sleep(2.0)
            fakes = []
            for name, port in (("fake-coletec-0", args.fake_0_port),
                               ("fake-coletec-1", args.fake_1_port)):
                sh = SerialShell(name, port, log)
                shells.append(sh)
                fakes.append(sh)
            if args.receptec_port:
                rx = SerialShell("receptec", args.receptec_port, log)
            else:
                rx = RttShell("receptec", "receptec", log)
            shells.append(rx)

            if not args.no_start:
                for sh in fakes:
                    out, _ = sh.run("uwb coletec start", max_wait=5.0)
                    lines = [l.strip() for l in out.splitlines()
                             if l.strip() and not PROMPT.search(l) and "uwb coletec start" not in l]
                    print(f"{sh.name}: start -> {lines[-1] if lines else '(no output)'}")
            out, _ = rx.run("uwb peer list", max_wait=1.5)
            print("peers:", " | ".join(l.strip() for l in out.splitlines() if "slot" in l))

            if args.poll_ms:
                out, _ = rx.run(f"uwb poll start {args.poll_ms}", max_wait=3.0)
                print("poll start:", " | ".join(l.strip() for l in out.splitlines()[1:-1]) or "(no output)")
                end = time.monotonic() + args.duration
                while time.monotonic() < end:
                    time.sleep(min(10, max(0.0, end - time.monotonic())))
                    out, _ = rx.run("uwb poll stats", max_wait=3.0)
                    print("\n".join(l for l in out.splitlines() if l.startswith(("poll", "slot"))))
                    print("--")
                out, _ = rx.run("uwb poll stop", max_wait=5.0)
                out, _ = rx.run("uwb poll stats", max_wait=3.0)
                print("FINAL")
                print("\n".join(l for l in out.splitlines() if l.startswith(("poll", "slot"))))
                print(f"Raw log: {raw_path}")
                return 0
            for _ in range(args.count):
                for slot in slots:
                    out, dt = rx.run(f"uwb sync {slot}", stop_re=SYNC_DONE, max_wait=3.0)
                    if "SYNC/ACK passed" in out:
                        results[slot]["pass"] += 1
                        results[slot]["lat"].append(dt)
                    else:
                        m = SYNC_FAIL.search(out)
                        code = m.group(1) if m else "no-reply"
                        results[slot]["fail"][code] = results[slot]["fail"].get(code, 0) + 1
        finally:
            for sh in shells:
                sh.close()

    total = total_ok = 0
    print(f"\nRaw log: {raw_path}")
    print(f"{'slot':>4} {'sent':>5} {'pass':>5} {'fail':>5}  failures / host-side latency (s)")
    for slot in slots:
        r = results[slot]
        fails = sum(r["fail"].values())
        sent = r["pass"] + fails
        total, total_ok = total + sent, total_ok + r["pass"]
        lat = ""
        if r["lat"]:
            lat = (f"min {min(r['lat']):.3f} med {statistics.median(r['lat']):.3f} "
                   f"max {max(r['lat']):.3f}")
        print(f"{slot:>4} {sent:>5} {r['pass']:>5} {fails:>5}  {r['fail'] or ''} {lat}")
    print(f"total {total_ok}/{total} passed")
    return 0 if total and total_ok == total else 1


if __name__ == "__main__":
    sys.exit(main())
