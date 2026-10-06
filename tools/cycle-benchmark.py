#!/usr/bin/env python3
"""Capture cycle metadata using BLE; no console or board configuration.

Example: cycle-benchmark.py --address CC:9B:2A:FA:A1:07 --mode measured \
    --seconds 1800 --output /tmp/atletec-cycle-measured
Writes records.jsonl and summary.json in a new output directory. Exit 0 means
all acceptance checks passed; 1 means failed/incomplete; 2 means capture error.
"""
import argparse
import asyncio
import json
import math
from pathlib import Path
import struct
import time

UUID = "00003104-4550-5453-4154-4c4554454300"
RECORD = struct.Struct("<BBBBQII")
PAIRS = {0: "M-T", 1: "A-T", 2: "M-A"}


def decode(payload):
    if len(payload) not in (20, 60):
        raise ValueError(f"metadata length {len(payload)}")
    records = []
    for offset in range(0, len(payload), RECORD.size):
        version, pair, source, status, round_id, distance, age = RECORD.unpack_from(payload, offset)
        if version != 1 or pair > 2 or source > 2:
            raise ValueError("unsupported metadata record")
        records.append(dict(version=version, pair=pair, source=source, status=status,
                            round=round_id, distance_mm=distance, age_ms=age))
    return records


class Capture:
    def __init__(self, args, stream):
        self.args, self.stream = args, stream
        self.started = time.monotonic()
        self.rows = {pair: {} for pair in PAIRS}
        self.last = {}
        self.duplicates = {pair: 0 for pair in PAIRS}
        self.cadence_errors = {pair: 0 for pair in PAIRS}
        self.deadline_errors = {pair: 0 for pair in PAIRS}
        self.stale_events = {pair: 0 for pair in PAIRS}
        self.stale = {pair: False for pair in PAIRS}
        self.sequence_errors = 0
        self.invalid_payloads = 0

    def receive(self, _sender, payload):
        elapsed = time.monotonic() - self.started
        try:
            records = decode(payload)
        except ValueError as error:
            self.invalid_payloads += 1
            self.stream.write(json.dumps(dict(elapsed_s=elapsed, error=str(error),
                                              raw=bytes(payload).hex())) + "\n")
            return
        for row in records:
            self.stream.write(json.dumps(dict(elapsed_s=elapsed, **row)) + "\n")
            pair, round_id = row["pair"], row["round"]
            if pair not in PAIRS:
                continue
            if round_id in self.rows[pair]:
                self.duplicates[pair] += 1
            if pair in self.last:
                previous = self.last[pair]
                gap = round_id - previous["round"]
                if gap <= 0 or gap > 4:
                    self.sequence_errors += 1
                if gap == 1 and not 0.5 * self.args.period_ms / 1000 <= elapsed - previous["elapsed_s"] <= 1.5 * self.args.period_ms / 1000:
                    self.cadence_errors[pair] += 1
            if row["age_ms"] != 0xFFFFFFFF and row["age_ms"] > 3000:
                self.deadline_errors[pair] += 1
            entry = dict(elapsed_s=elapsed, **row)
            self.rows[pair][round_id] = entry
            self.last[pair] = entry
        self.stream.flush()

    def check_stale(self):
        elapsed = time.monotonic() - self.started
        for pair in PAIRS:
            last = self.last.get(pair)
            expired = elapsed > 5 if last is None else (
                1000 * (elapsed - last["elapsed_s"]) +
                (0 if last["age_ms"] == 0xFFFFFFFF else last["age_ms"]) > 3000)
            if expired and not self.stale[pair]:
                self.stale_events[pair] += 1
            self.stale[pair] = expired

    def summary(self, complete=False, error=None):
        elapsed = time.monotonic() - self.started
        all_rounds = set().union(*(set(rows) for rows in self.rows.values()))
        # Reserve-ID jumps after reboot are intentionally counted as loss and
        # flagged as a sequence discontinuity; never allocate the gap in memory.
        span = max(all_rounds) - min(all_rounds) + 1 if all_rounds else 0
        # Count silent intervals at either boundary too; reserve one partial round.
        expected = max(span, max(0, math.floor(min(elapsed, self.args.seconds) * 1000 / self.args.period_ms) - 1))
        aligned = len(set.intersection(*(set(rows) for rows in self.rows.values())))
        stats = {}
        for pair, rows in self.rows.items():
            source = 1 if pair == 2 and self.args.mode == "fixed" else 0
            success = sum(row["status"] == 0 and row["distance_mm"] != 0xFFFFFFFF
                          and row["source"] == source
                          and (source != 1 or row["distance_mm"] == self.args.baseline_mm)
                          for row in rows.values())
            stats[PAIRS[pair]] = dict(received=len(rows), successes=success,
                failures=len(rows) - success, missing=max(0, expected - len(rows)),
                success_percent=100 * success / expected if expected else 0,
                duplicates=self.duplicates[pair], cadence_errors=self.cadence_errors[pair],
                deadline_errors=self.deadline_errors[pair], stale_events=self.stale_events[pair])
        enough_rounds = expected >= math.floor(self.args.seconds * 1000 / self.args.period_ms) - 3
        accepted = (complete and elapsed >= self.args.minimum_seconds and enough_rounds and expected > 0
                    and not error and not self.sequence_errors and not self.invalid_payloads
                    and aligned / expected >= 0.99
                    and all(item["success_percent"] >= self.args.minimum_success and not item["duplicates"]
                            and not item["cadence_errors"] and not item["deadline_errors"]
                            and not item["stale_events"] for item in stats.values()))
        return dict(mode=self.args.mode, baseline_mm=self.args.baseline_mm,
            address=self.args.address, requested_seconds=self.args.seconds,
            period_ms=self.args.period_ms, minimum_success_percent=self.args.minimum_success, minimum_seconds=self.args.minimum_seconds,
            elapsed_seconds=elapsed, complete=complete, accepted=accepted,
            expected_rounds=expected, aligned_rounds=aligned,
            sequence_errors=self.sequence_errors, invalid_payloads=self.invalid_payloads,
            pairs=stats, error=error,
            limitations="Host arrival cadence includes BLE latency. Shared rounds do not prove radio exclusion. "
                         "Constant mode cannot prove absence of UWB traffic; verify firmware counters separately.")


async def run(args):
    from bleak import BleakClient
    args.output.mkdir(parents=True, exist_ok=False)
    disconnected = asyncio.Event()
    error, complete = None, False
    with (args.output / "records.jsonl").open("x") as stream:
        capture = Capture(args, stream)
        try:
            async with BleakClient(args.address, disconnected_callback=lambda _: disconnected.set()) as client:
                # Baseline read is evidence only: its old samples are not counted
                # as new rounds or fresh arrivals in the measurement window.
                initial = await client.read_gatt_char(UUID)
                for row in decode(initial):
                    stream.write(json.dumps(dict(initial=True, **row)) + "\n")
                capture.started = time.monotonic()
                await client.start_notify(UUID, capture.receive)
                next_report = 60
                while time.monotonic() - capture.started < args.seconds:
                    await asyncio.sleep(min(0.25, args.seconds))
                    if disconnected.is_set():
                        raise RuntimeError("BLE disconnected during capture")
                    capture.check_stale()
                    elapsed = time.monotonic() - capture.started
                    if elapsed >= next_report:
                        print(json.dumps(capture.summary()), flush=True)
                        next_report += 60
                await client.stop_notify(UUID)
                complete = True
        except asyncio.CancelledError:
            error = "Capture interrupted"
        except Exception as exc:
            error = f"{type(exc).__name__}: {exc}"
        result = capture.summary(complete=complete, error=error)
    (args.output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)
    return 2 if error else 0 if result["accepted"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True, help="Explicit BLE address; no scan")
    parser.add_argument("--seconds", type=float, default=1800)
    parser.add_argument("--output", required=True, type=Path, help="New capture directory")
    parser.add_argument("--mode", choices=("measured", "fixed"), required=True)
    parser.add_argument("--baseline-mm", type=int)
    parser.add_argument("--period-ms", type=int, default=1000)
    parser.add_argument("--minimum-success", type=float, default=99)
    parser.add_argument("--minimum-seconds", type=float, default=1800)
    args = parser.parse_args()
    if not 300 <= args.period_ms <= 1000:
        parser.error("--period-ms must be in 300..1000")
    if not math.isfinite(args.minimum_success) or not 90 <= args.minimum_success <= 100:
        parser.error("--minimum-success must be in 90..100")
    if not math.isfinite(args.minimum_seconds) or args.minimum_seconds < 60:
        parser.error("--minimum-seconds must be finite and >=60")
    if not math.isfinite(args.seconds) or args.seconds <= 0:
        parser.error("--seconds must be finite and positive")
    if args.mode == "fixed" and (args.baseline_mm is None or not 1 <= args.baseline_mm <= 2147483647):
        parser.error("fixed mode requires --baseline-mm in 1..2147483647")
    if args.mode == "measured" and args.baseline_mm is not None:
        parser.error("--baseline-mm is only valid in fixed mode")
    try:
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
