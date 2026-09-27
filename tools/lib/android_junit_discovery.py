#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Reject Chromium host-JUnit tests that its runner cannot discover.

Chromium's ``JunitTestMain`` scans jars for classes ending in ``Test`` and then
keeps only classes carrying JUnit's ``@RunWith`` annotation. Kotlin and Java
compilation does not require that annotation, so an omitted runner produces a
green build containing tests that have never run. Every TaffyGo source below a
``junit`` directory is a Chromium host-JUnit source and must make discovery
explicit.

Gradle tests are deliberately outside this rule: their ``src/test`` runner
discovers ordinary JUnit classes without Chromium's annotation filter.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import source_index


_RUN_WITH = re.compile(r"^\s*@(?:org\.junit\.runner\.)?RunWith\s*\(", re.MULTILINE)
_RUN_WITH_IMPORT = re.compile(
    r"^\s*import\s+org\.junit\.runner\.RunWith\s*;?\s*$", re.MULTILINE
)


def junit_tests(repo_root: str) -> list[str]:
    """Return every first-party Chromium host-JUnit test source."""
    return [
        relative
        for relative in source_index.source_files(repo_root)
        if "junit" in relative.split("/")
        and relative.endswith(("Test.kt", "Test.java"))
        and not source_index.is_generated(os.path.join(repo_root, relative), repo_root)
    ]


def run(repo_root: str) -> tuple[list[str], int]:
    tests = junit_tests(repo_root)
    findings: list[str] = []
    for relative in tests:
        absolute = os.path.join(repo_root, relative)
        with open(absolute, encoding="utf-8", errors="replace") as handle:
            source = handle.read()
        annotation = _RUN_WITH.search(source)
        imported = _RUN_WITH_IMPORT.search(source)
        fully_qualified = "@org.junit.runner.RunWith" in source
        if annotation is None or (imported is None and not fully_qualified):
            findings.append(
                f"{relative}: Chromium's host runner will compile this class but omit it "
                "from every run; import org.junit.runner.RunWith and annotate the test "
                "with its intended Robolectric runner."
            )
    return findings, len(tests)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    args = parser.parse_args(argv)
    repo_root = os.path.abspath(args.root)

    findings, count = run(repo_root)
    for finding in findings:
        print(f"Android JUnit discovery: {finding}", file=sys.stderr)
    if findings:
        return 1
    print(f"Android JUnit discovery: {count} host test classes carry an explicit runner")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
