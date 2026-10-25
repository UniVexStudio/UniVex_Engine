#!/usr/bin/env python3
"""Turn a ctest log into one GitHub annotation per failing test.

WHY THIS EXISTS. The job log and the artifact host are not reachable from every network - the
people who need to know what broke are often the ones who cannot open either - so a failing test
has to say what it wanted on the Checks tab itself. The old version of this step echoed a single
12 KB blob starting at the first failure, which described exactly one of them: a run with ten
failures reported one test's assertion and nine names with no story.

Each annotation carries the test's own name and the first few lines of its gtest failure, so a
reader sees "this expected 5, that was 4" without leaving the page. Output is bounded twice - per
annotation and in how many are emitted - because GitHub truncates and rate-limits generously sized
annotations, and a truncated annotation is worse than a short one.

Usage: annotate_ctest_failures.py <ctest-output.txt> [--max 10] [--lines 8]
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

# `[ RUN      ] Suite.Case` starts a case; everything until its verdict belongs to it.
RUN_MARKER = re.compile(r"^\[\s*RUN\s*\]\s+(?P<name>\S.*)$")
# gtest's own failure header: "<file>:<line>: Failure", optionally followed by ": <message>".
FAILURE_HEADER = re.compile(r"^\S+\.(?:cc|cpp|cxx|h|hpp):\d+:\s*Failure\b")
# The line ctest prints when a case finishes; its verdict is what makes the case interesting.
CASE_END = re.compile(r"^\[\s*(?:FAILED|PASSED)\s*\]\s+\S")
# ctest's per-case header, used for tests that never reach gtest (timeouts, crashes).
CTEST_CASE = re.compile(r"^\s*\d+/\d+\s+Testing:\s+(?P<name>\S.*)$")

MAX_ANNOTATION_CHARS = 1400


def escape(message: str) -> str:
    """The workflow-command escaping GitHub requires, in the order it requires it."""
    return message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def collect(path: pathlib.Path, keep_lines: int) -> list[tuple[str, str]]:
    """Every failing case in `path`, as (name, detail lines). Names are de-duplicated, in order."""
    failures: dict[str, list[str]] = {}
    current: str | None = None
    capturing = False

    for raw in path.read_text(errors="replace").splitlines():
        line = raw.rstrip()
        case = CTEST_CASE.match(line)
        if case is not None:
            current = case.group("name")
            capturing = False
            continue
        run = RUN_MARKER.match(line)
        if run is not None:
            current = run.group("name")
            capturing = False
            continue
        if current is None:
            continue
        if FAILURE_HEADER.search(line):
            capturing = True
            failures.setdefault(current, []).append(line)
            continue
        if CASE_END.match(line):
            # The verdict line ends the case: what follows belongs to the next one, or to ctest's
            # own summary, and neither is this test's story.
            capturing = False
            current = None
            continue
        if capturing:
            # gtest prints the two values under the header, then a blank line. Stop at the blank so
            # the annotation holds the assertion and nothing else.
            if not line.strip():
                capturing = False
                continue
            detail = failures[current]
            if len(detail) < keep_lines:
                detail.append(line)
    return [(name, "\n".join(lines)) for name, lines in failures.items()]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=pathlib.Path, help="ctest console output (--output-on-failure)")
    parser.add_argument("--max", type=int, default=10, help="how many annotations to emit")
    parser.add_argument("--lines", type=int, default=8,
                        help="lines kept per annotation, counting the failure header")
    arguments = parser.parse_args(argv)

    if not arguments.log.exists():
        print(f"::warning::no ctest output at {arguments.log}; nothing to annotate")
        return 0

    failures = collect(arguments.log, arguments.lines)
    if not failures:
        print("::warning::ctest failed but no gtest failure block was found; see the job log")
        return 0

    for name, detail in failures[: arguments.max]:
        body = f"{name}\n{detail}".strip()
        if len(body) > MAX_ANNOTATION_CHARS:
            body = body[:MAX_ANNOTATION_CHARS] + "..."
        print(f"::error title={escape(name[:180])}::{escape(body)}")
    hidden = len(failures) - arguments.max
    if hidden > 0:
        print(f"::notice::{hidden} more failing case(s) carried no annotation; the summary lists them")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
