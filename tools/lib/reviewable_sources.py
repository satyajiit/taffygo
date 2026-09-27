#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Reject a source file Git treats as binary.

Git decides a file is binary by looking for a NUL byte near its start, and a
file it calls binary has no diff and no `git grep` output. The consequences are
review consequences rather than build ones, which is why nothing else notices:
the compiler is perfectly happy, every suite passes, and the file simply stops
being readable in the one place changes are looked at. It reaches a reviewer as
`Bin 2308 -> 2624 bytes`.

It happens by accident. A separator written as the byte itself rather than as
the language's escape — `"$a\\u0000$b"` versus a literal NUL between the two —
compiles to the same bytes and hashes the same values, and the difference is
invisible in every editor. One such file was committed to this repository
inside a merge, where being undiffable is exactly the moment it costs the most.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
import os
import sys

import source_index

#: What Git reads before deciding. `xdiff` inspects the first 8000 bytes; the
#: whole file is read here because a NUL further in is the same defect waiting
#: for the file to grow, and a source file is small enough to read whole.
BINARY_MARKER = b"\x00"


def run(repo_root: str) -> tuple[list[str], int]:
    findings: list[str] = []
    sources = source_index.source_files(repo_root)
    for relative in sources:
        absolute = os.path.join(repo_root, relative)
        with open(absolute, "rb") as handle:
            raw = handle.read()
        count = raw.count(BINARY_MARKER)
        if not count:
            continue
        line = raw[: raw.index(BINARY_MARKER)].count(b"\n") + 1
        findings.append(
            f"{relative}:{line}: {count} NUL byte(s) in a source file, so Git "
            "calls it binary: it has no reviewable diff and `git grep` prints "
            "none of its lines. Write the byte as the language's escape "
            "instead — the compiled result is the same."
        )
    return findings, len(sources)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    args = parser.parse_args(argv)
    repo_root = os.path.abspath(args.root)

    findings, count = run(repo_root)
    for finding in findings:
        print(f"reviewable sources: {finding}", file=sys.stderr)
    if findings:
        return 1
    print(f"reviewable sources: {count} source files carry a reviewable diff")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
