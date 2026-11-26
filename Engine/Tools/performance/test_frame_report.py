"""Checks for the frame-time report tool, against synthetic traces and real child processes."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parent))
import frame_report  # noqa: E402


def write_trace(path, frame_ms_values):
    lines = [','.join(frame_report.COLUMNS)]
    for index, milliseconds in enumerate(frame_ms_values):
        lines.append(f'{index + 1},{milliseconds:.6f},{milliseconds:.6f},'
                     f'{1000.0 / milliseconds:.6f},0.5,0.5,0.5')
    Path(path).write_text('\n'.join(lines) + '\n')


class SummaryChecks(unittest.TestCase):
    def test_percentile_interpolates_between_ranks(self):
        self.assertEqual(frame_report.percentile([], 0.95), 0.0)
        self.assertEqual(frame_report.percentile([7.0], 0.99), 7.0)
        self.assertAlmostEqual(frame_report.percentile([0.0, 10.0], 0.5), 5.0)
        self.assertAlmostEqual(frame_report.percentile([0.0, 10.0], 0.95), 9.5)

    def test_parse_drops_warmup_rows(self):
        with tempfile.TemporaryDirectory() as temporary:
            trace = Path(temporary) / 'trace.csv'
            write_trace(trace, [1.0, 2.0, 3.0])
            self.assertEqual([row['frame_ms'] for row in frame_report.parse_trace(trace)],
                             [1.0, 2.0, 3.0])
            self.assertEqual([row['frame_ms'] for row in frame_report.parse_trace(trace, 2)],
                             [3.0])

    def test_summary_counts_hitches_over_twice_the_median(self):
        with tempfile.TemporaryDirectory() as temporary:
            trace = Path(temporary) / 'trace.csv'
            write_trace(trace, [8.0] * 99 + [40.0])
            summary = frame_report.summarize(frame_report.parse_trace(trace))
            self.assertEqual(summary['frames'], 100)
            self.assertEqual(summary['hitches']['count'], 1)
            self.assertAlmostEqual(summary['frame_ms']['p50'], 8.0)
            self.assertAlmostEqual(summary['fps']['median'], 125.0)
            self.assertLess(summary['fps']['one_percent_low'], summary['fps']['median'])


class CliChecks(unittest.TestCase):
    @staticmethod
    def _child(write_trace_rows):
        return (
            'import os\n'
            "path = os.environ['UVE_FRAME_TRACE']\n"
            'lines = ["frame,frame_ms,delta_ms,fps,update_ms,lateupdate_ms,render_ms"]\n'
            + ('for index in range(120):\n'
               '    milliseconds = 8.0 if index % 60 else 40.0\n'
               '    lines.append(f"{index + 1},{milliseconds},{milliseconds},"\n'
               '                 f"{1000.0 / milliseconds},1.0,1.0,1.0")\n'
               if write_trace_rows else '')
            + 'open(path, "w").write("\\n".join(lines) + "\\n")\n'
        )

    def _run(self, temporary, child, extra):
        output = Path(temporary) / 'report.json'
        runner = Path(__file__).with_name('frame_report.py')
        result = subprocess.run([sys.executable, str(runner), '--runs', '1',
                                 '--timeout', '30', '--output', str(output), *extra,
                                 '--', sys.executable, '-c', child],
                                capture_output=True, text=True, timeout=60, check=False)
        return result, json.loads(output.read_text())

    def test_passing_run_reports_percentiles(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, report = self._run(temporary, self._child(True), ['--min-fps', '20'])
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(report['passed'])
            self.assertEqual(report['measurement'], 'frame-time-percentiles')
            self.assertEqual(report['frames'], 120)
            self.assertGreater(report['fps']['one_percent_low'], 20.0)

    def test_frame_budget_gate_fails_the_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, report = self._run(temporary, self._child(True), ['--min-fps', '120'])
            self.assertEqual(result.returncode, 1)
            self.assertFalse(report['passed'])
            self.assertFalse(report['min_fps_passed'])

    def test_missing_trace_fails_with_an_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, report = self._run(temporary, self._child(False), [])
            self.assertEqual(result.returncode, 1)
            self.assertIn('error', report)

    def test_baseline_regression_fails_the_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            baseline = Path(temporary) / 'baseline.json'
            baseline.write_text(json.dumps({'frame_ms': {'p95': 1.0, 'p99': 1.0}}))
            result, report = self._run(temporary, self._child(True),
                                       ['--baseline', str(baseline),
                                        '--max-regression-percent', '10'])
            self.assertEqual(result.returncode, 1)
            self.assertFalse(report['baseline']['passed'])


if __name__ == '__main__':
    unittest.main()
