#!/usr/bin/env python3
"""Tests for annotate_ctest_failures.py - the log a maintainer reads instead of the job log."""

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import annotate_ctest_failures as annotate  # noqa: E402


class CollectUVETest(unittest.TestCase):
    def collect(self, text: str, keep_lines: int = 8):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "ctest-output.txt"
            path.write_text(text)
            return annotate.collect(path, keep_lines)

    def test_one_case_per_failure_and_only_its_own_lines(self):
        log = """1/2 Test #1: A.CaseOne ...***Failed    0.03 sec
[ RUN      ] A.CaseOne
/t/x.cpp:461: Failure
Expected equality of these values:
  entries.size()
    Which is: 4
  5
[  FAILED  ] A.CaseOne (0 ms)
2/2 Test #2: B.CaseTwo ...***Failed    0.01 sec
[ RUN      ] B.CaseTwo
/t/y.cpp:77: Failure
Expected: attached
  Actual: parked
[  FAILED  ] B.CaseTwo (0 ms)
"""
        failures = self.collect(log)
        self.assertEqual([name for name, _ in failures], ["A.CaseOne", "B.CaseTwo"])
        self.assertIn("Which is: 4", failures[0][1])
        self.assertNotIn("A.CaseOne (0 ms)", failures[0][1], "the verdict line is not part of the story")
        self.assertNotIn("B.CaseTwo", failures[0][1], "one case never borrows the next one's lines")

    def test_a_test_that_never_reached_gtest_is_not_invented(self):
        log = """1/1 Test #1: Slow.Case ...***Timeout  60.0 sec
"""
        self.assertEqual(self.collect(log), [])

    def test_detail_lines_are_bounded(self):
        lines = "".join(f"  line {index}\n" for index in range(20))
        log = f"""1/1 Test #1: A.Case ...***Failed    1 sec
[ RUN      ] A.Case
/t/x.cpp:1: Failure
Expected: something
{lines}[  FAILED  ] A.Case (0 ms)
"""
        failures = self.collect(log, keep_lines=3)
        self.assertEqual(len(failures[0][1].splitlines()), 3, "the header counts, and the cap holds")


class EscapeUVETest(unittest.TestCase):
    def test_workflow_commands_survive_newlines_and_percent_signs(self):
        self.assertEqual(annotate.escape("50% a\r\nb"), "50%25 a%0D%0Ab")


if __name__ == "__main__":
    unittest.main()
