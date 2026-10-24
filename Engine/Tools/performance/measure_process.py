#!/usr/bin/env python3
"""Measure isolated Linux process runs; elapsed time is not frame time."""
import argparse
import json
import math
import os
from pathlib import Path
import platform
import signal
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--max-peak-mib', type=float)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if (not command or args.runs < 1 or not math.isfinite(args.timeout) or args.timeout <= 0
            or (args.max_peak_mib is not None and
                (not math.isfinite(args.max_peak_mib) or args.max_peak_mib <= 0))):
        parser.error('command, positive runs, and positive timeout are required')
    if platform.system() != 'Linux':
        parser.error('Linux is required for peak RSS measurements')
    samples = []
    failed = False
    for _ in range(args.runs):
        started = time.monotonic()
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, start_new_session=True)
        timed_out = False
        while True:
            pid, status, usage = os.wait4(process.pid, os.WNOHANG)
            if pid:
                break
            if time.monotonic() - started >= args.timeout:
                timed_out = True
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                _, status, usage = os.wait4(process.pid, 0)
                break
            time.sleep(0.01)
        # Reap the measured process and stop any descendants left in its own group.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        code = os.waitstatus_to_exitcode(status)
        process.returncode = code
        sample = {'exit_code': code, 'timed_out': timed_out,
                  'elapsed_seconds': time.monotonic() - started,
                  'peak_rss_kib': usage.ru_maxrss}
        failed |= timed_out or code != 0
        samples.append(sample)
        if failed:
            break
    report = {'schema_version': 1, 'measurement': 'process-runtime-and-peak-rss',
              'platform': platform.platform(), 'command': command, 'samples': samples,
              'frame_time_measured': False, 'gpu_memory_measured': False,
              'reference_device_verified': False}
    measured = [sample for sample in samples if 'peak_rss_kib' in sample]
    if measured:
        report['peak_rss_mib'] = max(sample['peak_rss_kib'] for sample in measured) / 1024
        report['median_elapsed_seconds'] = statistics.median(sample['elapsed_seconds'] for sample in measured)
        if args.max_peak_mib is not None:
            report['memory_budget_passed'] = report['peak_rss_mib'] <= args.max_peak_mib
            failed |= not report['memory_budget_passed']
    report['passed'] = not failed
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
