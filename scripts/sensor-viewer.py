#!/usr/bin/env python3
"""Live view of the COLETEC sensor data that the real RECEPTEC receives over UWB.

Starts `uwb poll` on the RECEPTEC, turns on `uwb poll stream`, and decodes each
`DATA slot=N n=COUNT len=L HEX` line as UWB sensor payload v1
(nrf52-bringup-fw/docs/uwb-sensor-payload-v1.md): an 8-byte header plus the STM32
`ipc_summary_data_t`. This is the COLETEC to RECEPTEC half of the interface; how the
RECEPTEC hands the payload to the Raspberry Pi is a separate piece of work.

The RECEPTEC shell is on RTT until it has a serial port; pass --port once it does.
Peers must already be registered on the RECEPTEC (`uwb peer register <slot> <eui>`).
"""

import argparse
import json
import re
import struct
import sys
import time
from datetime import datetime
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

DATA_LINE = re.compile(r"DATA slot=(\d+) n=(\d+) len=(\d+) ([0-9A-Fa-f]+)")

# ipc_summary_data_t, packed, 88 bytes (contracts/common/ipc/ipc_protocol.h).
HEADER = struct.Struct("<BB6s")
GPS = struct.Struct("<iihBHHhhh")  # lat, lon (deg x 1e7), alt dm, quality, hdop x0.01, speed cm/s, vN, vE, heading cdeg
IMU = struct.Struct("<hhhhBB")  # |a| mg, fwd, side, up, impact peak g x10, |gyro|
METRICS = struct.Struct("<HHBBBBBHHBBH")
WINDOWS = struct.Struct("<HBBBBHBBBB")
AHRS = struct.Struct("<hhhBbBB")
BIO = struct.Struct("<BBBBBB")
STAMP = struct.Struct("<I")
BLOCKS = ("gps", "imu", "metrics", "windows", "ahrs", "bio")


def load_shells():
    """Reuse the RTT and serial shell helpers of sync-sweep.py."""
    path = Path(__file__).with_name("sync-sweep.py")
    spec = spec_from_file_location("sync_sweep", path)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def decode_payload(payload: bytes) -> dict:
    if len(payload) < 8 or payload[0] != 1:
        return {"version": payload[0] if payload else None, "raw": payload.hex()}
    flags, seq, age = payload[1], int.from_bytes(payload[2:4], "little"), int.from_bytes(payload[4:8], "little")
    out = {"version": 1, "summary_valid": bool(flags & 1), "stale": bool(flags & 2),
           "payload_seq": seq, "age_ms": age}
    if not flags & 1 or len(payload) < 8 + 88:
        return out
    body = payload[8:8 + 88]
    pkt_version, block_valid, versions = HEADER.unpack_from(body, 0)
    out["packet_version"] = pkt_version
    out["block_valid"] = [name for i, name in enumerate(BLOCKS) if block_valid & (1 << i)]
    offset = HEADER.size
    values = {}
    for name, fmt in (("gps", GPS), ("imu", IMU), ("metrics", METRICS),
                      ("windows", WINDOWS), ("ahrs", AHRS), ("bio", BIO)):
        values[name] = fmt.unpack_from(body, offset)
        offset += fmt.size
    (out["session_ts_ms"],) = STAMP.unpack_from(body, offset)
    g = values["gps"]
    if "gps" in out["block_valid"]:
        out["gps"] = {"lat": g[0] / 1e7, "lon": g[1] / 1e7, "alt_m": g[2] / 10.0,
                      "hdop": g[4] / 100.0, "speed_ms": g[5] / 100.0,
                      "heading_deg": g[8] / 100.0}
    m = values["metrics"]
    if "metrics" in out["block_valid"]:
        out["metrics"] = {"distance_m": m[0], "player_load": m[1] / 10.0, "sprints": m[4],
                          "accels": m[5], "decels": m[6], "jumps": m[9], "max_speed_ms": m[11] / 100.0}
    i = values["imu"]
    if "imu" in out["block_valid"]:
        out["imu"] = {"accel_mg": i[0], "fwd": i[1], "side": i[2], "up": i[3],
                      "impact_g": i[4] / 10.0, "gyro": i[5]}
    if "ahrs" in out["block_valid"]:
        a = values["ahrs"]
        out["ahrs"] = {"pitch_deg": a[0] / 100.0, "roll_deg": a[1] / 100.0,
                       "heading_deg": a[2] / 100.0}
    return out


def summarize(slot: int, count: int, d: dict) -> str:
    stamp = datetime.now().strftime("%H:%M:%S")
    head = f"{stamp} slot{slot} n={count}"
    if d.get("version") != 1:
        return f"{head} unknown payload version {d.get('version')} {d.get('raw', '')[:32]}"
    if not d["summary_valid"]:
        return f"{head} seq={d['payload_seq']} no STM32 summary yet"
    parts = [f"{head} seq={d['payload_seq']} age={d['age_ms']}ms{' STALE' if d['stale'] else ''}"]
    if "gps" in d:
        g = d["gps"]
        parts.append(f"gps {g['lat']:.6f},{g['lon']:.6f} {g['speed_ms']:.1f}m/s hdop {g['hdop']:.1f}")
    else:
        parts.append("gps no fix")
    if "imu" in d:
        parts.append(f"|a|={d['imu']['accel_mg']}mg gyro={d['imu']['gyro']}")
    if "metrics" in d:
        m = d["metrics"]
        parts.append(f"dist={m['distance_m']}m PL={m['player_load']:.1f} sprints={m['sprints']}")
    if "ahrs" in d:
        parts.append(f"pitch={d['ahrs']['pitch_deg']:.1f} roll={d['ahrs']['roll_deg']:.1f}")
    return " | ".join(parts)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port of the RECEPTEC shell (default: RTT)")
    ap.add_argument("--period-ms", type=int, default=1000, help="uwb poll period (default 1000)")
    ap.add_argument("--seconds", type=int, default=0, help="stop after this long (default: until Ctrl-C)")
    ap.add_argument("--only-peer", metavar="EUI64",
                    help="unregister every slot, then register this EUI-64 in slot 0 "
                         "(production COLETEC: 02:00:00:00:00:00:00:02)")
    ap.add_argument("--cmd", action="append", default=[],
                    help="RECEPTEC shell command to run before polling (repeatable), e.g. 'uwb lna off'")
    ap.add_argument("--no-poll", action="store_true", help="do not start/stop the poll; only stream")
    ap.add_argument("--json", type=Path, help="also append each decoded record to this JSONL file")
    ap.add_argument("--raw", action="store_true", help="print the hex payload too")
    args = ap.parse_args()

    ss = load_shells()
    log = open(Path(__file__).resolve().parent.parent / "logs" /
               f"sensor-viewer-{datetime.now().strftime('%Y%m%dT%H%M%S')}.log", "w")
    shell = ss.SerialShell("receptec", args.port, log) if args.port else ss.RttShell("receptec", "receptec", log)
    jsonl = open(args.json, "a") if args.json else None
    received = 0
    try:
        for command in args.cmd:
            out, _ = shell.run(command, max_wait=2.0)
            print(f"{command}:", " | ".join(l.strip() for l in out.splitlines()[1:-1]) or "(no output)")
        if args.only_peer:
            for slot in range(4):
                shell.run(f"uwb peer unregister {slot}", max_wait=1.5)
            out, _ = shell.run(f"uwb peer register 0 {args.only_peer}", max_wait=2.0)
            print("register:", " | ".join(l.strip() for l in out.splitlines()[1:-1]) or "(no output)")
        if not args.no_poll:
            out, _ = shell.run(f"uwb poll start {args.period_ms}", max_wait=3.0)
            print("poll start:", " | ".join(l.strip() for l in out.splitlines()[1:-1]) or "(no output)")
        shell.run("uwb poll stream on", max_wait=2.0)
        print("streaming; Ctrl-C to stop")
        end = time.monotonic() + args.seconds if args.seconds else None
        buffer = ""
        while end is None or time.monotonic() < end:
            buffer += shell._recv().replace("\r", "")
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                m = DATA_LINE.search(re.sub(r"\x1b\[[0-9;]*m", "", line))
                if not m:
                    continue
                slot, count, length, hexdata = int(m[1]), int(m[2]), int(m[3]), m[4]
                d = decode_payload(bytes.fromhex(hexdata))
                received += 1
                print(summarize(slot, count, d) + (f"  {hexdata}" if args.raw else ""))
                if jsonl:
                    jsonl.write(json.dumps({"t": time.time(), "slot": slot, "n": count, **d}) + "\n")
                    jsonl.flush()
    except KeyboardInterrupt:
        pass
    finally:
        try:
            shell.run("uwb poll stream off", max_wait=2.0)
            if not args.no_poll:
                out, _ = shell.run("uwb poll stats", max_wait=3.0)
                print("\n".join(l for l in out.splitlines() if l.startswith(("poll", "slot"))))
            if not args.no_poll:
                shell.run("uwb poll stop", max_wait=5.0)
        finally:
            shell.close()
    print(f"{received} payloads received")
    return 0 if received else 1


if __name__ == "__main__":
    sys.exit(main())
