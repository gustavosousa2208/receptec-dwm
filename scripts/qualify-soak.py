#!/usr/bin/env python3
"""Long-duration RECEPTEC/COLETEC soak monitor.

Unlike qualify-m5.py (fixed transaction target, SYNC/ACK only), this runs the
normal four-peer DATA_REQ/DATA schedule for as long as requested (overnight or
multi-day) and tracks drift-relevant firmware telemetry that already exists:
cumulative protocol counters, guard-window growth, synchronization-loss/
reentry events, and COLETEC IRQ-to-arm timing. No firmware changes required.
"""

import argparse
import csv
import json
import re
import threading
import time
from datetime import datetime
from pathlib import Path

import serial

DATA_OK_RE = re.compile(
    r"DATA_REQ/DATA OK: cycle=(\d+) slot=(\d+) peer=(\S+) seq=(\d+) "
    r"sample=(\d+) bytes=(\d+) attempt=(\d+) offset=(\d+)")
DATA_FAILED_RE = re.compile(r"DATA_REQ/DATA failed: seq=(\d+) error=(-?\d+)")
RWD_MISSING_RE = re.compile(
    r"RWD missing peer: cycle=(\d+) slot=(\d+) peer=(\S+) seq=(\d+) "
    r"attempts=(\d+) result=timeout")
PROTOCOL_COUNTERS_RE = re.compile(
    r"Protocol counters: txns=(\d+)/(\d+) attempts=(\d+) timeouts=(\d+) "
    r"rx_errors=(\d+) exhausted=(\d+) late=(\d+) "
    r"invalid=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+) duplicate_payload=(\d+)")
SLOT_MISSED_RE = re.compile(
    r"COLETEC slot missed: count=(\d+) next-guard=(\d+) us")
SYNC_LOST_RE = re.compile(r"COLETEC synchronization lost after 5 missed slots")
DISCOVERY_REENTRY_RE = re.compile(r"COLETEC discovery reentry: synchronized")
COLETEC_TIMING_RE = re.compile(
    r"COLETEC timing: responses=(\d+) IRQ-to-arm=(\d+) us "
    r"max-IRQ-to-arm=(\d+) us late=(\d+)")

CSV_FIELDS = [
    "timestamp", "elapsed_s", "event",
    "txns_ok", "txns_failed", "rwd_missing",
    "guard_us", "slot_missed_count", "sync_lost_count",
    "discovery_reentry_count",
    "irq_to_arm_us", "max_irq_to_arm_us", "late_schedules",
    "proto_txns_completed", "proto_txns_started", "proto_attempts",
    "proto_timeouts", "proto_rx_errors", "proto_exhausted", "proto_late",
    "proto_duplicate_payload",
]


def new_state():
    return {
        "txns_ok": 0,
        "txns_failed": 0,
        "rwd_missing": 0,
        "guard_us": 0,
        "slot_missed_count": 0,
        "sync_lost_count": 0,
        "discovery_reentry_count": 0,
        "irq_to_arm_us": 0,
        "max_irq_to_arm_us": 0,
        "late_schedules": 0,
        "proto_txns_completed": 0,
        "proto_txns_started": 0,
        "proto_attempts": 0,
        "proto_timeouts": 0,
        "proto_rx_errors": 0,
        "proto_exhausted": 0,
        "proto_late": 0,
        "proto_duplicate_payload": 0,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--receptec", required=True)
    parser.add_argument("--coletec", required=True)
    parser.add_argument("--duration-hours", type=float, default=None,
                        help="stop after this many hours (default: run "
                             "until Ctrl+C)")
    parser.add_argument("--checkpoint-interval-s", type=int, default=300)
    parser.add_argument("--log", default=None,
                        help="log path (default: logs/soak-<timestamp>.log)")
    parser.add_argument("--csv", default=None,
                        help="csv path (default: logs/soak-<timestamp>.csv)")
    parser.add_argument("--checkpoint", default=None,
                        help="checkpoint path (default: "
                             "logs/soak-<timestamp>.checkpoint.json)")
    args = parser.parse_args()

    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    log_path = Path(args.log) if args.log else Path("logs") / f"soak-{timestamp}.log"
    csv_path = Path(args.csv) if args.csv else Path("logs") / f"soak-{timestamp}.csv"
    checkpoint_path = Path(args.checkpoint) if args.checkpoint else (
        Path("logs") / f"soak-{timestamp}.checkpoint.json")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    checkpoint_path.parent.mkdir(parents=True, exist_ok=True)

    log_file = log_path.open("w", encoding="utf-8")
    csv_file = csv_path.open("w", encoding="utf-8", newline="")
    csv_writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
    csv_writer.writeheader()

    lock = threading.Lock()
    report_lock = threading.Lock()
    stop = threading.Event()
    state = new_state()
    started = time.monotonic()

    def report(message):
        with report_lock:
            print(message, flush=True)
            log_file.write(message + "\n")
            log_file.flush()

    def write_csv_row(event):
        with lock:
            row = {"timestamp": datetime.now().isoformat(),
                   "elapsed_s": round(time.monotonic() - started, 1),
                   "event": event}
            row.update(state)
        with report_lock:
            csv_writer.writerow(row)
            csv_file.flush()

    def write_checkpoint():
        with lock:
            snapshot = dict(state)
        snapshot["elapsed_s"] = round(time.monotonic() - started, 1)
        snapshot["timestamp"] = datetime.now().isoformat()
        checkpoint_path.write_text(json.dumps(snapshot, indent=2))

    def heartbeat():
        while not stop.wait(10.0):
            with lock:
                elapsed = time.monotonic() - started
                report(f"heartbeat: elapsed={elapsed:.1f}s "
                       f"txns_ok={state['txns_ok']} "
                       f"txns_failed={state['txns_failed']} "
                       f"guard_us={state['guard_us']} "
                       f"sync_lost={state['sync_lost_count']} "
                       f"max_irq_to_arm_us={state['max_irq_to_arm_us']}")

    def checkpointer():
        while not stop.wait(args.checkpoint_interval_s):
            write_checkpoint()

    def duration_watchdog():
        if args.duration_hours is None:
            return
        end = started + args.duration_hours * 3600.0
        while not stop.wait(min(30.0, max(0.0, end - time.monotonic()))):
            if time.monotonic() >= end:
                report(f"duration {args.duration_hours}h reached; stopping")
                stop.set()
                return

    def read_lines(port, role):
        try:
            with serial.Serial(port, 115200, timeout=0.2) as uart:
                while not stop.is_set():
                    line = uart.readline().decode(errors="replace").strip()
                    if not line:
                        continue
                    report(f"{role}: {line}")

                    if role == "RECEPTEC":
                        match = DATA_OK_RE.search(line)
                        if match:
                            with lock:
                                state["txns_ok"] += 1
                            write_csv_row("data_req_ok")
                            continue
                        match = DATA_FAILED_RE.search(line)
                        if match:
                            with lock:
                                state["txns_failed"] += 1
                            write_csv_row("data_req_failed")
                            continue
                        match = RWD_MISSING_RE.search(line)
                        if match:
                            with lock:
                                state["rwd_missing"] += 1
                            write_csv_row("rwd_missing")
                            continue
                        match = PROTOCOL_COUNTERS_RE.search(line)
                        if match:
                            with lock:
                                (state["proto_txns_completed"],
                                 state["proto_txns_started"],
                                 state["proto_attempts"],
                                 state["proto_timeouts"],
                                 state["proto_rx_errors"],
                                 state["proto_exhausted"],
                                 state["proto_late"]) = (
                                    int(v) for v in match.groups()[0:7])
                                state["proto_duplicate_payload"] = int(
                                    match.groups()[-1])
                            write_csv_row("protocol_counters")
                            continue

                    if role == "COLETEC":
                        match = SLOT_MISSED_RE.search(line)
                        if match:
                            with lock:
                                state["slot_missed_count"] += 1
                                state["guard_us"] = int(match.group(2))
                            write_csv_row("slot_missed")
                            continue
                        if SYNC_LOST_RE.search(line):
                            with lock:
                                state["sync_lost_count"] += 1
                            write_csv_row("sync_lost")
                            continue
                        if DISCOVERY_REENTRY_RE.search(line):
                            with lock:
                                state["discovery_reentry_count"] += 1
                                state["guard_us"] = 0
                            write_csv_row("discovery_reentry")
                            continue
                        match = COLETEC_TIMING_RE.search(line)
                        if match:
                            with lock:
                                state["irq_to_arm_us"] = int(match.group(2))
                                state["max_irq_to_arm_us"] = int(match.group(3))
                                state["late_schedules"] = int(match.group(4))
                            write_csv_row("coletec_timing")
                            continue
        except Exception as exc:
            report(f"{role} serial monitor error: {exc}")
            stop.set()

    background = [
        threading.Thread(target=heartbeat),
        threading.Thread(target=checkpointer),
        threading.Thread(target=duration_watchdog),
        threading.Thread(target=read_lines, args=(args.receptec, "RECEPTEC")),
        threading.Thread(target=read_lines, args=(args.coletec, "COLETEC")),
    ]
    for thread in background:
        thread.start()

    uart_threads = background[3:]
    interrupted = False
    try:
        while any(thread.is_alive() for thread in uart_threads):
            for thread in uart_threads:
                thread.join(timeout=0.2)
    except KeyboardInterrupt:
        interrupted = True
        report("soak interrupted by user")
    finally:
        stop.set()
        for thread in background:
            thread.join()

    write_checkpoint()
    elapsed = time.monotonic() - started
    with lock:
        summary = dict(state)
    report(f"soak complete: elapsed={elapsed:.1f}s "
           f"txns_ok={summary['txns_ok']} txns_failed={summary['txns_failed']} "
           f"rwd_missing={summary['rwd_missing']} "
           f"slot_missed_count={summary['slot_missed_count']} "
           f"sync_lost_count={summary['sync_lost_count']} "
           f"discovery_reentry_count={summary['discovery_reentry_count']} "
           f"max_irq_to_arm_us={summary['max_irq_to_arm_us']} "
           f"log={log_path} csv={csv_path} checkpoint={checkpoint_path}")
    log_file.close()
    csv_file.close()
    if interrupted:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
