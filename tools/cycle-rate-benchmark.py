#!/usr/bin/env python3
"""Run one bench rate capture, restoring the MASTER configuration afterwards.
Uses lstty and bench; requires explicit probe identity and a free BLE advertiser.
"""
import argparse
import asyncio
import json
from pathlib import Path
import re
import signal
import subprocess
import sys


def command(args):
    result = subprocess.run(args, text=True, capture_output=True, timeout=90)
    print(result.stdout, end='', flush=True)
    if result.stderr:
        print(result.stderr, file=sys.stderr, end='', flush=True)
    result.check_returncode()
    return result.stdout


def state(text):
    matches = re.findall(r'cycle configured=(\d+) running=(\d+) period=(\d+) tag=(\d+) base_a=(\d+) autostart=(\d+) baseline=(\w+) baseline_mm=(\d+)', text)
    if not matches:
        raise RuntimeError('No complete cycle status; refusing to guess configuration')
    fields = matches[-1]
    return dict(zip(('configured', 'running', 'period', 'tag', 'base', 'autostart', 'baseline', 'baseline_mm'), fields))


def console(nick, *commands):
    args = ['bench', 'console', nick, '--transport', 'rtt', '--no-check', '--timeout', '3']
    for line in commands:
        args.extend(['--send', line])
    return command(args)


def set_period(nick, period):
    stopped = state(console(nick, 'uwb range cycle stop', 'uwb range cycle status'))
    if stopped['running'] != '0':
        raise RuntimeError('Scheduler did not stop; refusing period change')
    current = state(console(nick, f'uwb range cycle period {period}', 'uwb range cycle start', 'uwb range cycle status'))
    if current['running'] != '1' or current['period'] != str(period):
        raise RuntimeError('Requested period/start not confirmed')
    return current


async def advertiser_present(address):
    from bleak import BleakScanner
    devices = await BleakScanner.discover(timeout=10)
    return any(device.address.upper() == address.upper() for device in devices)


def capture_process(args, timeout):
    with subprocess.Popen(args) as child:
        try:
            return child.wait(timeout=timeout)
        except BaseException:
            if child.poll() is None:
                child.send_signal(signal.SIGINT)
                try:
                    child.wait(timeout=45)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=10)
            raise


def run(args):
    command(['lstty'])
    probe = json.loads(command(['lstty', 'jlink', 'get', args.nick, '--json']))
    if not probe.get('connected') or str(probe.get('serial')) != args.probe or probe.get('device') != 'nRF52833_xxAA':
        raise RuntimeError('MASTER probe identity/connection mismatch')
    initial = state(console(args.nick, 'uwb range cycle status'))
    if not 300 <= int(initial['period']) <= 1000:
        raise RuntimeError('Stored period outside supported profile; leaving it unchanged')
    if initial['configured'] != '1' or initial['running'] != '1' or initial['baseline'] != 'measured':
        raise RuntimeError('Expected configured/running measured cycle; leaving it unchanged')
    if not asyncio.run(advertiser_present(args.address)):
        raise RuntimeError('MASTER not advertising; leaving configuration unchanged')
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / 'initial-state.json').write_text(json.dumps(initial, indent=2) + '\n')
    # Any failure after the first stop still takes the restoration path.
    try:
        set_period(args.nick, args.period_ms)
        result = capture_process([sys.executable, str(Path(__file__).with_name('cycle-benchmark.py')),
            '--address', args.address, '--mode', 'measured', '--notify-transport', 'bluez-fd',
            '--period-ms', str(args.period_ms), '--seconds', str(args.seconds),
            '--minimum-seconds', str(args.seconds), '--minimum-success', '90',
            '--output', str(args.output / 'capture')], timeout=args.seconds + 120)
        return result
    finally:
        restored = set_period(args.nick, int(initial['period']))
        if any(restored[key] != initial[key] for key in ('configured', 'running', 'period', 'tag', 'base', 'autostart', 'baseline', 'baseline_mm')):
            raise RuntimeError('Restoration differs from initial configuration')
        (args.output / 'restored-state.json').write_text(json.dumps(restored, indent=2) + '\n')


if __name__ == '__main__':
    def terminate(_signal, _frame):
        raise SystemExit(143)
    signal.signal(signal.SIGTERM, terminate)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nick', required=True)
    parser.add_argument('--probe', required=True)
    parser.add_argument('--address', required=True)
    parser.add_argument('--period-ms', type=int, choices=range(300, 1001), required=True)
    parser.add_argument('--seconds', type=int, default=1800)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.seconds < 60:
        parser.error('--seconds must be at least 60')
    try:
        raise SystemExit(run(args))
    except Exception as error:
        print(f'Rate capture failed: {error}', file=sys.stderr)
        raise SystemExit(2)
