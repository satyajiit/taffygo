#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The soft 400-line file cap, applied to every source file in the repository.

Authority boundary: this is the *decision* half of the size rule — the cap, the
warning band, the exemption table, and the verdict. It measures nothing itself
(`code_lines.py`) and enumerates nothing itself (`source_index.py`).

Owning milestone: M0 (WP-M0-09). The rule is
`docs/architecture/android-app-architecture.md` section 5, and decision 0014
item 3 puts its enforcement in the fast lane.

Three verdicts, and the middle one is the point of a *soft* cap:

  - **over the cap, no exemption — failure.** The architecture document calls a
    file approaching the cap "a design smell to split"; past it, the smell is a
    finding.
  - **approaching the cap — warning.** A file in the warning band is not broken;
    it is the last moment at which splitting it is cheap.
  - **over the cap with an exemption — pass, up to that exemption's own
    ceiling.** An exemption is not a blank cheque: each entry carries the
    largest the file may be, so a file that keeps growing fails again.

A fourth verdict keeps the table honest: an exemption whose file no longer
needs it is itself a failure. Dead exemptions are how an allowlist becomes a
silencer.

Generated files are excluded by detection, never by exemption — see
`source_index.is_generated`. Splitting generated output edits the wrong
artifact, and the next regeneration undoes it.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings, 2 warnings only.
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import sys

import code_lines
import exemption_table
import source_index

#: The soft cap, in lines of code as `code_lines.py` defines them.
#: `docs/architecture/android-app-architecture.md` section 5 and decision 0014
#: item 3 own the number; this constant is the executable copy of it and the
#: only one in the tools.
SOFT_LINE_CAP = 400

#: Where the warning band starts: 90% of the cap. A file here still passes.
WARNING_FLOOR = SOFT_LINE_CAP * 9 // 10

#: From how many physical lines an inline test block is worth mentioning.
#: This is information, not a verdict: the cap excludes inline tests on
#: purpose, and this number changes neither the count nor the exit status. It
#: exists because a block the cap cannot see is a block nothing else measures,
#: and a module whose tests are three times its code is the moment at which
#: moving them to a sibling `tests.rs` is still cheap.
INLINE_TEST_FLOOR = 300

#: The exemption table, relative to the repository root.
EXEMPTIONS = "tools/check.d/file-size-exemptions.tsv"

EXIT_CLEAN = 0
EXIT_FINDINGS = 1
EXIT_WARNINGS = 2


class Exemption:
    """One entry of the exemption table: a glob, a ceiling, and a reason."""

    def __init__(self, glob: str, ceiling: int, reason: str, where: str) -> None:
        self.glob = glob
        self.ceiling = ceiling
        self.reason = reason
        self.where = where
        self.used = False

    def matches(self, relative: str) -> bool:
        return fnmatch.fnmatch(relative, self.glob)


def load_exemptions(repo_root: str) -> list[Exemption]:
    """The exemption table, or a `TableError` naming the malformed line."""
    path = os.path.join(repo_root, EXEMPTIONS)
    rows = exemption_table.read_table(path, columns=2, table_name="file-size exemption")
    exemptions: list[Exemption] = []
    for row in rows:
        glob, ceiling = row.fields
        if not ceiling.isdigit():
            raise exemption_table.TableError(
                f"{row.where}: the second column is the largest this file may "
                f"be, in lines of code; \"{ceiling}\" is not a number."
            )
        if int(ceiling) <= SOFT_LINE_CAP:
            raise exemption_table.TableError(
                f"{row.where}: a ceiling of {ceiling} is at or under the "
                f"{SOFT_LINE_CAP}-line cap, so this entry exempts nothing. "
                "Delete it."
            )
        exemptions.append(Exemption(glob, int(ceiling), row.reason, row.where))
    return exemptions


class Report:
    """What one run found: failures, warnings, and what it measured."""

    def __init__(self) -> None:
        self.failures: list[str] = []
        self.warnings: list[str] = []
        self.measured = 0
        self.largest = 0
        #: `(inline test lines, path)` for every file at or past
        #: `INLINE_TEST_FLOOR`. Informational only; never a finding.
        self.inline_tests: list[tuple[int, str]] = []

    @property
    def exit_status(self) -> int:
        if self.failures:
            return EXIT_FINDINGS
        return EXIT_WARNINGS if self.warnings else EXIT_CLEAN


def _judge(relative: str, count: int, exemption: Exemption | None, report: Report) -> None:
    if exemption is not None:
        # "Used" means the file needed it. An exemption whose file has come
        # back under the cap is stale, and the run below says so — otherwise a
        # split that fixed a file would leave its excuse behind for the next
        # file that grows into the same glob.
        if count > SOFT_LINE_CAP:
            exemption.used = True
        if count > exemption.ceiling:
            report.failures.append(
                f"{relative}: {count} lines of code, past its own exemption "
                f"ceiling of {exemption.ceiling} ({exemption.where}). The "
                "exemption said this file had stopped growing; it has not. "
                "Split it."
            )
        return
    if count > SOFT_LINE_CAP:
        report.failures.append(
            f"{relative}: {count} lines of code, over the {SOFT_LINE_CAP}-line "
            "cap. Split it along a responsibility seam, or add an entry with a "
            f"reason to {EXEMPTIONS}."
        )
    elif count >= WARNING_FLOOR:
        report.warnings.append(
            f"{relative}: {count} lines of code, approaching the "
            f"{SOFT_LINE_CAP}-line cap. This is the cheapest moment to split it."
        )


def _is_inbound_third_party(relative: str) -> bool:
    """True for vendored crate sources this repository did not write.

    Decision 0086 vendors Brave's adblock-rust (and its cargo vendor tree)
    under taffy-core/third_party/adblock-rust/vendor/. Those files are inbound
    MPL-2.0 sources; splitting them is not a TaffyGo design smell.
    """
    return relative.startswith("taffy-core/third_party/adblock-rust/vendor/")


def run(repo_root: str) -> Report:
    """Measure every non-generated source file and judge it."""
    report = Report()
    exemptions = load_exemptions(repo_root)

    for relative in source_index.source_files(repo_root):
        absolute = os.path.join(repo_root, relative)
        if source_index.is_generated(absolute, repo_root):
            continue
        if _is_inbound_third_party(relative):
            continue
        count = code_lines.code_lines(absolute)
        report.measured += 1
        report.largest = max(report.largest, count)
        match = next((e for e in exemptions if e.matches(relative)), None)
        _judge(relative, count, match, report)
        inline = code_lines.inline_test_lines(absolute)
        if inline >= INLINE_TEST_FLOOR:
            report.inline_tests.append((inline, relative))

    for exemption in exemptions:
        if not exemption.used:
            report.failures.append(
                f"{exemption.where}: nothing matching \"{exemption.glob}\" is "
                f"over the {SOFT_LINE_CAP}-line cap any more. The entry is "
                "stale; delete it."
            )
    return report


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    parser.add_argument(
        "--list",
        action="store_true",
        help="print every measured file with its size, largest first, and exit",
    )
    parser.add_argument(
        "--inline-tests",
        action="store_true",
        help=(
            f"print every file carrying an inline test block of "
            f"{INLINE_TEST_FLOOR}+ lines, largest first, and exit"
        ),
    )
    args = parser.parse_args(argv)
    repo_root = os.path.abspath(args.root)

    if args.list:
        rows = []
        for relative in source_index.source_files(repo_root):
            absolute = os.path.join(repo_root, relative)
            if source_index.is_generated(absolute, repo_root):
                continue
            if _is_inbound_third_party(relative):
                continue
            rows.append((code_lines.code_lines(absolute), relative))
        for count, relative in sorted(rows, reverse=True):
            print(f"{count:6d}  {relative}")
        return EXIT_CLEAN

    try:
        report = run(repo_root)
    except exemption_table.TableError as error:
        print(f"file discipline: {error}", file=sys.stderr)
        return EXIT_FINDINGS

    if args.inline_tests:
        # The listing the informational line below points at. Like `--list`
        # it is a view of what was measured, not a verdict, so it exits clean
        # whatever the sizes say.
        for count, relative in sorted(report.inline_tests, reverse=True):
            print(f"{count:6d}  {relative}")
        if not report.inline_tests:
            print(f"no file carries an inline test block of {INLINE_TEST_FLOOR}+ lines")
        return EXIT_CLEAN

    for warning in report.warnings:
        print(f"file discipline: {warning}", file=sys.stderr)
    for failure in report.failures:
        print(f"file discipline: {failure}", file=sys.stderr)
    if not report.failures:
        print(
            f"file discipline: {report.measured} source files measured, "
            f"largest {report.largest} lines of code (cap {SOFT_LINE_CAP})"
        )
    if report.inline_tests:
        largest, where = max(report.inline_tests)
        print(
            f"file discipline: {len(report.inline_tests)} files carry an inline "
            f"test block of {INLINE_TEST_FLOOR}+ lines, largest {largest} "
            f"({where}); run with --inline-tests to list them",
            file=sys.stderr,
        )
    return report.exit_status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
