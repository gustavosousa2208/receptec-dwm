#!/usr/bin/env python3
"""Long-duration RECEPTEC/COLETEC clock-drift soak monitor.

The recommended traffic source for one real production COLETEC is
``uwb sync start 1`` on the RECEPTEC. The monitor also accepts the normal
four-peer DATA_REQ/DATA schedule and both PoC and production COLETEC telemetry.
"""

import argparse
import csv
import json
import re
import shutil
import threading
import time
from datetime import datetime
from pathlib import Path

import serial
import pylink
from elftools.elf.elffile import ELFFile

DATA_OK_RE = re.compile(
    r"DATA_REQ/DATA OK: cycle=(\d+) slot=(\d+) peer=(\S+) seq=(\d+) "
    r"sample=(\d+) bytes=(\d+) attempt=(\d+) offset=(\d+)")
DATA_FAILED_RE = re.compile(r"DATA_REQ/DATA failed: seq=(\d+) error=(-?\d+)")
SYNC_OK_RE = re.compile(
    r"SYNC/ACK OK: seq=(\d+) attempt=(\d+) offset=(\d+)")
SYNC_FAILED_RE = re.compile(r"SYNC/ACK failed: seq=(\d+) error=(-?\d+)")
RWD_MISSING_RE = re.compile(
    r"RWD missing peer: cycle=(\d+) slot=(\d+) peer=(\S+) seq=(\d+) "
    r"attempts=(\d+) result=timeout")
PROTOCOL_COUNTERS_RE = re.compile(
    r"Protocol counters: txns=(\d+)/(\d+) attempts=(\d+) timeouts=(\d+) "
    r"rx_errors=(\d+) exhausted=(\d+) late=(\d+) "
    r"invalid=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+) duplicate_payload=(\d+)")
SLOT_MISSED_RE = re.compile(
    r"(?:COLETEC|UWB) slot missed: count=(\d+) next-guard=(\d+) us")
SYNC_LOST_RE = re.compile(
    r"(?:COLETEC synchronization lost after 5 missed slots|"
    r"UWB synchronization lost: entering discovery)")
DISCOVERY_REENTRY_RE = re.compile(
    r"(?:COLETEC discovery reentry: synchronized|"
    r"UWB discovery reentry: sequence=)")
COLETEC_TIMING_RE = re.compile(
    r"COLETEC timing: responses=(\d+) IRQ-to-arm=(\d+) us "
    r"max-IRQ-to-arm=(\d+) us late=(\d+)")
DRIFT_RE = re.compile(
    r"UWB drift: samples=(\d+) phase=(-?\d+) us "
    r"min=(-?\d+) us max=(-?\d+) us sum=(-?\d+) us "
    r"average=(-?\d+) milli-ppm")

CSV_FIELDS = [
    "timestamp", "elapsed_s", "event",
    "txns_ok", "txns_failed", "sync_ok", "sync_failed", "rwd_missing",
    "guard_us", "slot_missed_count", "sync_lost_count",
    "discovery_reentry_count",
    "irq_to_arm_us", "max_irq_to_arm_us", "late_schedules",
    "proto_txns_completed", "proto_txns_started", "proto_attempts",
    "proto_timeouts", "proto_rx_errors", "proto_exhausted", "proto_late",
    "proto_duplicate_payload",
    "phase_samples", "phase_error_us", "phase_min_us", "phase_max_us",
    "phase_sum_us", "relative_ppm",
]


def new_state():
    return {
        "txns_ok": 0,
        "txns_failed": 0,
        "sync_ok": 0,
        "sync_failed": 0,
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
        "phase_samples": 0,
        "phase_error_us": 0,
        "phase_min_us": 0,
        "phase_max_us": 0,
        "phase_sum_us": 0,
        "relative_ppm": 0.0,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--receptec", required=True)
    coletec_source = parser.add_mutually_exclusive_group(required=True)
    coletec_source.add_argument(
        "--coletec",
        help="COLETEC UART device (PoC DWM only)")
    coletec_source.add_argument(
        "--coletec-rtt-probe",
        help="J-Link serial number for production COLETEC RTT")
    parser.add_argument(
        "--coletec-elf",
        default="../nrf52-bringup-fw/build/zephyr/zephyr.elf",
        help="production COLETEC ELF used to locate its RTT control block")
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
    rtt_path = Path("logs") / f"soak-{timestamp}.coletec-rtt.log"
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

    def record_raw(role, line):
        """Save an input line without flooding the live terminal."""
        with report_lock:
            log_file.write(f"{role}: {line}\n")
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
                       f"sync_ok={state['sync_ok']} "
                       f"txns_failed={state['txns_failed']} "
                       f"guard_us={state['guard_us']} "
                       f"sync_lost={state['sync_lost_count']} "
                       f"max_irq_to_arm_us={state['max_irq_to_arm_us']} "
                       f"phase_us={state['phase_error_us']} "
                       f"relative_ppm={state['relative_ppm']:.3f}")

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

    def read_rtt_lines():
        jlink = None
        partial_line = ""
        try:
            elf_path = Path(args.coletec_elf).resolve()
            with elf_path.open("rb") as elf_file:
                symbol_table = ELFFile(elf_file).get_section_by_name(".symtab")
                symbols = symbol_table.get_symbol_by_name("_SEGGER_RTT")
                if not symbols:
                    raise RuntimeError(
                        f"_SEGGER_RTT not found in {elf_path}")
                rtt_address = symbols[0]["st_value"]

            commander_name = shutil.which("JLinkExe")
            if commander_name is None:
                raise RuntimeError("JLinkExe not found on PATH")
            commander = Path(commander_name)
            library_path = commander.parent / "libjlinkarm.so"
            if not library_path.is_file():
                raise RuntimeError("real SEGGER J-Link installation not found")
            library = pylink.library.Library(str(library_path))
            jlink = pylink.JLink(lib=library)
            jlink.open(serial_no=int(args.coletec_rtt_probe))
            jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)
            jlink.connect("nRF52840_xxAA", speed=4000)
            jlink.rtt_start(block_address=rtt_address)
            report(f"COLETEC: RTT connected through J-Link "
                   f"{args.coletec_rtt_probe} at 0x{rtt_address:08x}")
            with rtt_path.open("w", encoding="utf-8") as raw_rtt:
                while not stop.is_set():
                    data = jlink.rtt_read(0, 4096)
                    if not data:
                        stop.wait(0.05)
                        continue
                    text = bytes(data).decode(errors="replace")
                    raw_rtt.write(text)
                    raw_rtt.flush()
                    text = partial_line + text
                    lines = text.splitlines(keepends=True)
                    partial_line = ""
                    for line in lines:
                        if line.endswith(("\n", "\r")):
                            handle_line(line.strip(), "COLETEC")
                        else:
                            partial_line = line
                if partial_line:
                    handle_line(partial_line.strip(), "COLETEC")
        except Exception as exc:
            report(f"COLETEC RTT monitor error: {exc}")
            stop.set()
        finally:
            if jlink is not None:
                try:
                    jlink.rtt_stop()
                finally:
                    jlink.close()

    def handle_line(line, role):
        if not line:
            return
        record_raw(role, line)

        if role == "RECEPTEC":
            match = DATA_OK_RE.search(line)
            if match:
                with lock:
                    state["txns_ok"] += 1
                write_csv_row("data_req_ok")
                return
            match = DATA_FAILED_RE.search(line)
            if match:
                with lock:
                    state["txns_failed"] += 1
                write_csv_row("data_req_failed")
                report(f"RECEPTEC failure: {line}")
                return
            match = SYNC_OK_RE.search(line)
            if match:
                with lock:
                    state["sync_ok"] += 1
                write_csv_row("sync_ok")
                return
            match = SYNC_FAILED_RE.search(line)
            if match:
                with lock:
                    state["sync_failed"] += 1
                write_csv_row("sync_failed")
                report(f"RECEPTEC failure: {line}")
                return
            match = RWD_MISSING_RE.search(line)
            if match:
                with lock:
                    state["rwd_missing"] += 1
                write_csv_row("rwd_missing")
                report(f"RECEPTEC missing peer: {line}")
                return
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
                return

        if role == "COLETEC":
            match = SLOT_MISSED_RE.search(line)
            if match:
                with lock:
                    state["slot_missed_count"] += 1
                    state["guard_us"] = int(match.group(2))
                write_csv_row("slot_missed")
                report(f"COLETEC timing warning: {line}")
                return
            if SYNC_LOST_RE.search(line):
                with lock:
                    state["sync_lost_count"] += 1
                write_csv_row("sync_lost")
                report(f"COLETEC synchronization lost: {line}")
                return
            if DISCOVERY_REENTRY_RE.search(line):
                with lock:
                    state["discovery_reentry_count"] += 1
                    state["guard_us"] = 0
                write_csv_row("discovery_reentry")
                report(f"COLETEC synchronized again: {line}")
                return
            match = COLETEC_TIMING_RE.search(line)
            if match:
                with lock:
                    state["irq_to_arm_us"] = int(match.group(2))
                    state["max_irq_to_arm_us"] = int(match.group(3))
                    state["late_schedules"] = int(match.group(4))
                write_csv_row("coletec_timing")
                return
            match = DRIFT_RE.search(line)
            if match:
                with lock:
                    state["phase_samples"] = int(match.group(1))
                    state["phase_error_us"] = int(match.group(2))
                    state["phase_min_us"] = int(match.group(3))
                    state["phase_max_us"] = int(match.group(4))
                    state["phase_sum_us"] = int(match.group(5))
                    state["relative_ppm"] = int(match.group(6)) / 1000.0
                write_csv_row("clock_drift")
                report(
                    f"drift: samples={match.group(1)} "
                    f"phase={match.group(2)}us "
                    f"range={match.group(3)}/{match.group(4)}us "
                    f"average={int(match.group(6)) / 1000.0:.3f}ppm")

    def read_serial_lines(port, role):
        try:
            with serial.Serial(port, 115200, timeout=0.2) as uart:
                while not stop.is_set():
                    line = uart.readline().decode(errors="replace").strip()
                    handle_line(line, role)
        except Exception as exc:
            report(f"{role} serial monitor error: {exc}")
            stop.set()

    coletec_reader = (
        threading.Thread(target=read_rtt_lines)
        if args.coletec_rtt_probe else
        threading.Thread(
            target=read_serial_lines, args=(args.coletec, "COLETEC")))
    background = [
        threading.Thread(target=heartbeat),
        threading.Thread(target=checkpointer),
        threading.Thread(target=duration_watchdog),
        threading.Thread(
            target=read_serial_lines, args=(args.receptec, "RECEPTEC")),
        coletec_reader,
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
           f"sync_ok={summary['sync_ok']} sync_failed={summary['sync_failed']} "
           f"rwd_missing={summary['rwd_missing']} "
           f"slot_missed_count={summary['slot_missed_count']} "
           f"sync_lost_count={summary['sync_lost_count']} "
           f"discovery_reentry_count={summary['discovery_reentry_count']} "
           f"max_irq_to_arm_us={summary['max_irq_to_arm_us']} "
           f"phase_samples={summary['phase_samples']} "
           f"phase_range_us={summary['phase_min_us']}/"
           f"{summary['phase_max_us']} "
           f"relative_ppm={summary['relative_ppm']:.3f} "
           f"log={log_path} csv={csv_path} checkpoint={checkpoint_path}")
    log_file.close()
    csv_file.close()
    if interrupted:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
