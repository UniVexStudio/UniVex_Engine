#!/usr/bin/env python3
"""Frame-time percentiles from the engine's UVE_FRAME_TRACE CSV.

The engine writes one row per frame when UVE_FRAME_TRACE names a path (see
Engine/Runtime/Engine/Internal/frame_trace.cpp). This tool runs a command a number of times,
collects those rows and reports p50/p95/p99, 1% low FPS and hitches, optionally against a
baseline so a regression fails the run.
"""
import argparse
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import time

COLUMNS = ('frame', 'frame_ms', 'delta_ms', 'fps', 'update_ms', 'lateupdate_ms', 'render_ms')
PHASES = ('update_ms', 'lateupdate_ms', 'render_ms')


def percentile(values, fraction):
    """Linear-interpolated percentile, fraction in [0, 1]. Empty input yields 0.0."""
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = fraction * (len(ordered) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[int(position)]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def parse_trace(path, warmup_frames=0):
    """Reads the CSV, dropping the first `warmup_frames` rows (startup and shader compilation)."""
    rows = []
    text = Path(path).read_text()
    lines = [line for line in text.splitlines() if line.strip()]
    if not lines:
        return rows
    header = lines[0].split(',')
    if tuple(header) != COLUMNS:
        raise ValueError(f'unexpected trace header: {header}')
    for line in lines[1 + warmup_frames:]:
        fields = line.split(',')
        if len(fields) != len(COLUMNS):
            raise ValueError(f'malformed trace row: {line}')
        row = {}
        for name, field in zip(COLUMNS, fields):
            row[name] = int(field) if name == 'frame' else float(field)
        rows.append(row)
    return rows


def summarize(rows):
    frame_ms = [row['frame_ms'] for row in rows]
    delta_ms = [row['delta_ms'] for row in rows if row['delta_ms'] > 0.0]
    median_frame_ms = percentile(frame_ms, 0.5)
    hitch_threshold_ms = median_frame_ms * 2.0
    hitches = sum(1 for value in frame_ms if value > hitch_threshold_ms)
    summary = {
        'frames': len(frame_ms),
        'frame_ms': {
            'mean': statistics.fmean(frame_ms) if frame_ms else 0.0,
            'min': min(frame_ms) if frame_ms else 0.0,
            'p50': median_frame_ms,
            'p90': percentile(frame_ms, 0.90),
            'p95': percentile(frame_ms, 0.95),
            'p99': percentile(frame_ms, 0.99),
            'p99_9': percentile(frame_ms, 0.999),
            'max': max(frame_ms) if frame_ms else 0.0,
            'stddev': statistics.pstdev(frame_ms) if len(frame_ms) > 1 else 0.0,
        },
        'fps': {
            'mean': 1000.0 / statistics.fmean(delta_ms) if delta_ms else 0.0,
            'median': 1000.0 / percentile(delta_ms, 0.5) if delta_ms else 0.0,
            'p95_low': 1000.0 / percentile(frame_ms, 0.95) if frame_ms else 0.0,
            'one_percent_low': 1000.0 / percentile(frame_ms, 0.99) if frame_ms else 0.0,
        },
        'hitches': {
            'threshold_ms': hitch_threshold_ms,
            'count': hitches,
            'ratio': hitches / len(frame_ms) if frame_ms else 0.0,
        },
    }
    summary['phase_ms'] = {}
    for phase in PHASES:
        values = [row[phase] for row in rows]
        summary['phase_ms'][phase[:-3]] = {
            'mean': statistics.fmean(values) if values else 0.0,
            'p95': percentile(values, 0.95),
        }
    return summary


def run_once(command, timeout, trace_path, maximum_frames, warmup_frames):
    environment = dict(os.environ)
    environment['UVE_FRAME_TRACE'] = str(trace_path)
    environment['UVE_FRAME_TRACE_MAX'] = str(maximum_frames)
    started = time.monotonic()
    timed_out = False
    with tempfile.TemporaryFile() as output:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=output,
                                   stderr=subprocess.STDOUT, env=environment)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            code = process.wait()
        output.seek(0)
        tail = output.read().decode('utf-8', 'replace').splitlines()[-20:]
    rows = parse_trace(trace_path, warmup_frames) if trace_path.exists() else []
    return {
        'exit_code': code,
        'timed_out': timed_out,
        'elapsed_seconds': time.monotonic() - started,
        'output_tail': tail,
        'rows': rows,
    }


def compare_baseline(report, baseline, maximum_regression_percent):
    reference = baseline.get('frame_ms', {})
    comparison = {'baseline_p95_ms': reference.get('p95', 0.0),
                  'baseline_p99_ms': reference.get('p99', 0.0)}
    passed = True
    for key in ('p95', 'p99'):
        current = report['frame_ms'][key]
        previous = reference.get(key, 0.0)
        if previous <= 0.0:
            comparison[f'{key}_regression_percent'] = 0.0
            continue
        regression = (current - previous) / previous * 100.0
        comparison[f'{key}_regression_percent'] = regression
        if regression > maximum_regression_percent:
            passed = False
    comparison['passed'] = passed
    return comparison


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--timeout', type=float, default=120.0)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--warmup-frames', type=int, default=0,
                        help='frames dropped from the start of each run (default: 0)')
    parser.add_argument('--maximum-frames', type=int, default=20000)
    parser.add_argument('--min-fps', type=float, help='fail when the 1%% low FPS is below this')
    parser.add_argument('--max-hitch-ratio', type=float,
                        help='fail when the share of frames over 2x the median exceeds this')
    parser.add_argument('--baseline', type=Path, help='previous report to compare against')
    parser.add_argument('--max-regression-percent', type=float, default=10.0)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if (not command or args.runs < 1 or not math.isfinite(args.timeout) or args.timeout <= 0
            or args.warmup_frames < 0 or args.maximum_frames <= 0):
        parser.error('command, positive runs, a positive timeout and a positive frame bound are required')

    failed = False
    runs = []
    rows = []
    with tempfile.TemporaryDirectory(prefix='uve-frame-trace-') as temporary:
        for index in range(args.runs):
            trace_path = Path(temporary) / f'run-{index}.csv'
            result = run_once(command, args.timeout, trace_path, args.maximum_frames,
                              args.warmup_frames)
            run_rows = result.pop('rows')
            rows.extend(run_rows)
            runs.append(result)
            if result['timed_out'] or result['exit_code'] != 0 or not run_rows:
                failed = True
                break

    report = {
        'schema_version': 1,
        'measurement': 'frame-time-percentiles',
        'platform': platform.platform(),
        'command': command,
        'runs': runs,
    }
    if rows:
        report.update(summarize(rows))
    else:
        report['error'] = 'the command produced no frame trace rows'
        failed = True

    if rows and args.min_fps is not None:
        report['min_fps_passed'] = report['fps']['one_percent_low'] >= args.min_fps
        failed |= not report['min_fps_passed']
    if rows and args.max_hitch_ratio is not None:
        report['hitch_budget_passed'] = report['hitches']['ratio'] <= args.max_hitch_ratio
        failed |= not report['hitch_budget_passed']
    if rows and args.baseline is not None:
        baseline = json.loads(args.baseline.read_text())
        report['baseline'] = compare_baseline(report, baseline, args.max_regression_percent)
        failed |= not report['baseline']['passed']

    report['passed'] = not failed
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
