#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Check the Android shell's hand-written GN source lists in both directions.

The retained Compose modules have a generated source projection, but the fork
adapter and JUnit sources beside ``shell/BUILD.gn`` intentionally follow
Chromium's explicit-list convention.  That convention still needs a census: a
Kotlin or Java file present on disk but absent from the matching target
otherwise exists without ever reaching the product or its claimed test suite,
while a stale listed path fails only on a Chromium builder.

Stdlib only.  The mount lane runs both ``--self-test`` and the real check.
"""

from __future__ import annotations

import argparse
from collections import Counter
import os
from pathlib import Path
import re
import sys


SOURCES_RE = re.compile(r"\bsources\s*=\s*\[(?P<body>.*?)\]", re.DOTALL)
QUOTED_RE = re.compile(r'"(?P<path>[^"\n]+)"')
SOURCE_SUFFIXES = (".java", ".kt")


class CheckError(ValueError):
    """The BUILD.gn shape is not the one this checker can prove."""


def declared_sources(build_text: str, target: str) -> list[str]:
    """Return every Java/Kotlin path in one Android library's sources."""
    target_pattern = re.compile(
        rf'android_library\("{re.escape(target)}"\)\s*\{{(?P<body>.*?)^\}}',
        re.MULTILINE | re.DOTALL,
    )
    targets = list(target_pattern.finditer(build_text))
    if len(targets) != 1:
        raise CheckError(
            f'expected exactly one android_library("{target}"), found {len(targets)}'
        )
    source_lists = list(SOURCES_RE.finditer(targets[0].group("body")))
    if len(source_lists) != 1:
        raise CheckError(
            f"expected exactly one direct sources list in {target}, "
            f"found {len(source_lists)}"
        )
    paths = [
        match.group("path")
        for match in QUOTED_RE.finditer(source_lists[0].group("body"))
    ]
    non_sources = [path for path in paths if not path.endswith(SOURCE_SUFFIXES)]
    if non_sources:
        raise CheckError(
            f"{target}.sources contains non-Java/Kotlin paths: "
            + ", ".join(non_sources)
        )
    return paths


def disk_sources(shell_dir: Path, source_roots: tuple[Path, ...]) -> list[str]:
    """Return Java/Kotlin paths below ``source_roots``, relative to shell/."""
    return sorted(
        Path(os.path.relpath(path, shell_dir)).as_posix()
        for source_root in source_roots
        for path in source_root.rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def findings(target: str, declared: list[str], present: list[str]) -> list[str]:
    """Describe every disagreement between a source list and the tree."""
    result: list[str] = []
    counts = Counter(declared)
    duplicates = sorted(path for path, count in counts.items() if count != 1)
    if duplicates:
        result.append(f"{target}.sources lists more than once: " + ", ".join(duplicates))

    declared_set = set(declared)
    present_set = set(present)
    missing = sorted(present_set - declared_set)
    stale = sorted(declared_set - present_set)
    if missing:
        result.append(f"on disk but absent from {target}.sources: " + ", ".join(missing))
    if stale:
        result.append(f"in {target}.sources but absent from disk: " + ", ".join(stale))
    return result


def self_test() -> int:
    """Exercise exactness, duplicate detection, and malformed target refusal."""
    fixture = '''
android_library("shell_java") {
  sources = [
    "java/src/A.java",
    "java/src/B.kt",
  ]
  deps = [ "//base" ]
}
'''
    failures: list[str] = []
    try:
        parsed = declared_sources(fixture, "shell_java")
    except CheckError as error:
        failures.append(f"valid fixture was refused: {error}")
        parsed = []
    if parsed != ["java/src/A.java", "java/src/B.kt"]:
        failures.append(f"valid fixture parsed as {parsed!r}")
    if findings("shell_java", parsed, list(parsed)):
        failures.append("an exact source census produced a finding")
    if len(findings("shell_java", parsed, [*parsed, "java/src/C.kt"])) != 1:
        failures.append("an unlisted disk source was not detected")
    if len(findings("shell_java", [*parsed, "java/src/Gone.java"], parsed)) != 1:
        failures.append("a stale GN source was not detected")
    if len(findings("shell_java", [*parsed, parsed[0]], parsed)) != 1:
        failures.append("a duplicate GN source was not detected")
    try:
        declared_sources('android_library("other") { sources = [] }', "shell_java")
        failures.append("a missing shell_java target was accepted")
    except CheckError:
        pass

    for failure in failures:
        print(f"check_shell_sources self-test: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("check_shell_sources self-test: exact, missing, stale, and duplicate cases pass")
    return 0


def repository_root() -> Path:
    return Path(__file__).resolve().parents[4]


def check() -> int:
    shell_dir = repository_root() / "taffy-core" / "app" / "android" / "shell"
    build_path = shell_dir / "BUILD.gn"
    try:
        build_text = build_path.read_text(encoding="utf-8")
        shipping_declared = declared_sources(build_text, "shell_java")
        junit_declared = declared_sources(build_text, "junit")
    except (OSError, CheckError) as error:
        print(f"check_shell_sources: {error}", file=sys.stderr)
        return 1
    shipping_present = disk_sources(shell_dir, (shell_dir / "java" / "src",))
    junit_present = disk_sources(
        shell_dir,
        (shell_dir / "junit" / "src", shell_dir.parent / "junit" / "src"),
    )
    disagreements = [
        *findings("shell_java", shipping_declared, shipping_present),
        *findings("junit", junit_declared, junit_present),
    ]
    for disagreement in disagreements:
        print(f"check_shell_sources: {disagreement}", file=sys.stderr)
    if disagreements:
        return 1
    print(
        "check_shell_sources: "
        f"{len(shipping_present)} shipping and {len(junit_present)} JUnit sources are exact"
    )
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args(argv)
    return self_test() if arguments.self_test else check()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
