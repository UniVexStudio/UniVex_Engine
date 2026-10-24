"""Integration checks against real short-lived child processes."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == 'linux', 'Linux RSS measurements')
class ProcessMeasurements(unittest.TestCase):
    def test_real_process_outcomes(self):
        runner = Path(__file__).with_name('measure_process.py')
        cases = [
            ('success', ['true'], [], 0),
            ('failure', ['false'], [], 1),
            ('timeout', ['sleep', '2'], ['--timeout', '0.05'], 1),
            ('budget', ['true'], ['--max-peak-mib', '0.001'], 1),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            for name, command, extra, expected in cases:
                with self.subTest(name=name):
                    output = Path(temporary) / (name + '.json')
                    result = subprocess.run([sys.executable, str(runner), '--runs', '1',
                                             '--output', str(output), *extra, '--', *command],
                                            timeout=10, check=False)
                    self.assertEqual(result.returncode, expected)
                    report = json.loads(output.read_text())
                    self.assertEqual(report['passed'], expected == 0)
                    self.assertFalse(report['frame_time_measured'])
                    if name == 'timeout':
                        self.assertTrue(report['samples'][0]['timed_out'])


if __name__ == '__main__':
    unittest.main()
