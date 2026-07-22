#!/usr/bin/env python3
"""Count consecutive addressed SYNC/ACK exchanges from both DWM UARTs."""

import argparse
import serial
import threading
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--receptec", required=True)
    parser.add_argument("--coletec", required=True)
    parser.add_argument("--target", type=int, default=1000)
    args = parser.parse_args()

    lock = threading.Lock()
    stop = threading.Event()
    state = {"successes": 0, "errors": 0}
    started = time.monotonic()

    def heartbeat():
        while not stop.wait(10.0):
            with lock:
                elapsed = time.monotonic() - started
                print(f"heartbeat: consecutive={state['successes']} "
                      f"total={state['total']} errors={state['errors']} "
                      f"elapsed={elapsed:.1f}s", flush=True)

    def read_lines(port, role):
        try:
            with serial.Serial(port, 115200, timeout=0.2) as uart:
                while not stop.is_set():
                    line = uart.readline().decode(errors="replace").strip()
                    if not line:
                        continue
                    if role == "RECEPTEC" and "SYNC/ACK OK:" in line:
                        with lock:
                            state["successes"] += 1
                            state["total"] += 1
                            count = state["successes"]
                            if count % 100 == 0 or count == args.target:
                                elapsed = time.monotonic() - started
                                print(f"progress: {count}/{args.target} ({elapsed:.1f}s)", flush=True)
                            if count >= args.target:
                                stop.set()
                    elif "SYNC/ACK failed:" in line or "scheduled too late" in line:
                        with lock:
                            state["errors"] += 1
                            state["successes"] = 0
                            print(f"{role}: {line} -- consecutive count reset", flush=True)
        except Exception as exc:
            print(f"{role} serial monitor error: {exc}", flush=True)
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
    print(f"qualification complete: successes={state['successes']} "
          f"errors={state['errors']} elapsed={elapsed:.1f}s", flush=True)
    return 0 if state["successes"] >= args.target else 1


if __name__ == "__main__":
    raise SystemExit(main())
