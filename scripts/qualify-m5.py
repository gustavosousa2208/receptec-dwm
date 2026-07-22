#!/usr/bin/env python3
"""Count consecutive addressed SYNC/ACK exchanges from both DWM UARTs."""

import argparse
from datetime import datetime
from pathlib import Path
import serial
import threading
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--receptec", required=True)
    parser.add_argument("--coletec", required=True)
    parser.add_argument("--target", type=int, default=1000)
    parser.add_argument("--log", default=None,
                        help="log path (default: logs/m5-qualification-<timestamp>.log)")
    args = parser.parse_args()

    lock = threading.Lock()
    report_lock = threading.Lock()
    stop = threading.Event()
    state = {"successes": 0, "errors": 0}
    started = time.monotonic()
    log_path = Path(args.log) if args.log else Path("logs") / (
        "m5-qualification-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".log")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_file = log_path.open("w", encoding="utf-8")

    def report(message):
        with report_lock:
            print(message, flush=True)
            log_file.write(message + "\n")
            log_file.flush()

    def heartbeat():
        while not stop.wait(10.0):
            with lock:
                elapsed = time.monotonic() - started
                report(f"heartbeat: consecutive={state['successes']} "
                       f"total={state['total']} errors={state['errors']} "
                       f"elapsed={elapsed:.1f}s")

    def read_lines(port, role):
        try:
            with serial.Serial(port, 115200, timeout=0.2) as uart:
                while not stop.is_set():
                    line = uart.readline().decode(errors="replace").strip()
                    if not line:
                        continue
                    report(f"{role}: {line}")
                    if role == "RECEPTEC" and "SYNC/ACK OK:" in line:
                        with lock:
                            state["successes"] += 1
                            state["total"] += 1
                            count = state["successes"]
                            if count % 100 == 0 or count == args.target:
                                elapsed = time.monotonic() - started
                                report(f"progress: {count}/{args.target} ({elapsed:.1f}s)")
                            if count >= args.target:
                                stop.set()
                    elif "SYNC/ACK failed:" in line or "scheduled too late" in line:
                        with lock:
                            state["errors"] += 1
                            state["successes"] = 0
                            report(f"{role}: {line} -- consecutive count reset")
        except Exception as exc:
            report(f"{role} serial monitor error: {exc}")
            stop.set()

    state["total"] = 0
    heartbeat_thread = threading.Thread(target=heartbeat)
    heartbeat_thread.start()
    threads = [
        threading.Thread(target=read_lines, args=(args.receptec, "RECEPTEC")),
        threading.Thread(target=read_lines, args=(args.coletec, "COLETEC")),
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    stop.set()
    heartbeat_thread.join()

    elapsed = time.monotonic() - started
    report(f"qualification complete: successes={state['successes']} "
           f"errors={state['errors']} elapsed={elapsed:.1f}s log={log_path}")
    log_file.close()
    return 0 if state["successes"] >= args.target else 1


if __name__ == "__main__":
    raise SystemExit(main())
