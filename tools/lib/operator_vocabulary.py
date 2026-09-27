#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Refuse operator identifiers in anything a public export would carry.

Authority boundary: reading `tools/check.d/operator-vocabulary.tsv` and its
allow table, and deciding whether a set of files carries a listed identifier.
It holds no identifier of its own; every pattern is a row of that list, so the
secrets lane and `./tools/export-public verify` cannot disagree about one.

Two scopes, one scanner:

  --repo ROOT  every file an export of the working tree would carry: what Git
               tracks or would track, less the rows of the export's exclusion
               table, plus the overlay under the public path it is published
               at. The planning documents are not in it on purpose; they are
               the history of these identifiers and never leave. An allow row
               that excuses nothing any more is a finding here, so a paid-off
               excuse cannot stay on the books.
  --tree DIR   every file below an exported tree, with no Git and no table.

Exit status: 0 clean, 1 findings or a malformed table, 3 the list is not in
this tree (a published tree does not carry it), 2 usage.

Stdlib only. Read-only.
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from exemption_table import TableError, read_table  # noqa: E402

LIST = "tools/check.d/operator-vocabulary.tsv"
ALLOW = "tools/check.d/operator-vocabulary-allow.tsv"
EXPORT_LIB = "tools/export-public.d/lib"
EXPORT_TABLE = "tools/export-public.d/exclude.tsv"
EXIT_CLEAN, EXIT_FINDINGS, EXIT_USAGE, EXIT_ABSENT = 0, 1, 2, 3
_ID = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
_INLINE_FLAGS = re.compile(r"\(\?[aiLmsux]")


class Rule:
    """One row of the list, compiled."""

    def __init__(self, row) -> None:
        self.id, allow, self.pattern, sample = row.fields
        # `\xNN` in a sample is that byte, so a sample can match its pattern
        # without the table itself carrying text another gate refuses.
        self.sample = re.sub(r"\\x([0-9a-fA-F]{2})", lambda m: chr(int(m.group(1), 16)), sample)
        self.reason = row.reason
        self.where = row.where
        if not _ID.match(self.id):
            raise TableError(f"{self.where}: id {self.id!r} is not lower-case-with-hyphens")
        if allow not in ("allowable", "never"):
            raise TableError(f"{self.where}: the allow column is `allowable` or `never`, not {allow!r}")
        self.allowable = allow == "allowable"
        try:
            self.regex = re.compile(self.pattern.encode("utf-8"))
        except re.error as error:
            raise TableError(f"{self.where}: {self.id}: the pattern does not compile: {error}") from error
        if not self.regex.search(self.sample.encode("utf-8")):
            raise TableError(f"{self.where}: {self.id}: the pattern does not match its own sample")
        self.needle = literal_needle(self.pattern)
        if self.needle is not None and self.needle not in self.sample.encode("utf-8"):
            raise TableError(f"{self.where}: {self.id}: the prefilter {self.needle!r} read out of "
                             "the pattern is not in the sample; tools/lib/operator_vocabulary.py "
                             "misread the expression")

    def excerpt(self, text: bytes) -> str:
        shown = text.decode("utf-8", "replace")
        # A value row may be a real credential; name where it is, not what it is.
        return shown[:14] + "…" if not self.allowable else shown[:80]


def literal_needle(pattern: str) -> bytes | None:
    """The longest literal every match of `pattern` must contain, or None.

    Only a prefilter: a file without the needle cannot match, so the regular
    expression runs only over files that carry it. Anything this reader does
    not understand ends the literal run it is in, which errs toward a shorter
    needle rather than an unsound one.
    """
    if _INLINE_FLAGS.search(pattern):
        return None
    runs: list[str] = []
    run: list[str] = []
    depth = 0
    index = 0

    def flush() -> None:
        runs.append("".join(run))
        run.clear()

    while index < len(pattern):
        char = pattern[index]
        if char == "\\":
            escaped = pattern[index + 1 : index + 2]
            index += 2
            if escaped and not escaped.isalnum() and depth == 0:
                run.append(escaped)
            else:
                flush()
            continue
        if char in "?*{":
            if run:
                run.pop()  # the quantified character is optional
            flush()
            if char == "{":
                index = pattern.find("}", index) + 1 or len(pattern)
            else:
                index += 1
            continue
        if char == "[":
            flush()
            end = index + 1
            while end < len(pattern) and (pattern[end] != "]" or end == index + 1):
                end += 2 if pattern[end] == "\\" else 1
            index = end + 1
            continue
        if char == "|" and depth == 0:
            return None
        if char in "()":
            depth += 1 if char == "(" else -1
            flush()
        elif char in ".^$+|":
            flush()
        elif depth == 0:
            run.append(char)
        index += 1
    flush()
    best = max(runs, key=len)
    return best.encode("utf-8") if len(best) >= 3 else None


class Allow:
    """One row of the allow table."""

    def __init__(self, row, rules: dict[str, Rule]) -> None:
        self.glob, self.id = row.fields
        self.reason = row.reason
        self.where = row.where
        self.used = False
        if self.id not in rules:
            raise TableError(f"{self.where}: no vocabulary row is called {self.id!r}")
        if not rules[self.id].allowable:
            raise TableError(f"{self.where}: {self.id} is a `never` row; no allow row may excuse it")


def shown(path: str) -> str:
    """`path` relative to the working directory when it is below it, so a
    finding names `tools/check.d/...:12` rather than a workstation path."""
    relative = os.path.relpath(path)
    return path if relative.startswith("..") else relative


def load(list_path: str, allow_path: str) -> tuple[list[Rule], list[Allow]]:
    list_path, allow_path = shown(list_path), shown(allow_path)
    if not os.path.isfile(list_path):
        raise FileNotFoundError(list_path)
    rules = [Rule(row) for row in read_table(list_path, 4, "operator vocabulary")]
    by_id: dict[str, Rule] = {}
    for rule in rules:
        if rule.id in by_id:
            raise TableError(f"{rule.where}: id {rule.id} is already used at {by_id[rule.id].where}")
        by_id[rule.id] = rule
    allows = [Allow(row, by_id) for row in read_table(allow_path, 2, "operator vocabulary allow")]
    return rules, allows


def _read(path: str) -> bytes | None:
    try:
        if os.path.islink(path):
            return os.readlink(path).encode("utf-8", "surrogateescape")
        with open(path, "rb") as handle:
            return handle.read()
    except OSError:
        return None


def scan(files, rules: list[Rule], allows: list[Allow]) -> list[str]:
    """Findings over `files`, an iterable of (disk path, public path, shown as)."""
    findings: list[str] = []
    for disk, public, label in files:
        data = _read(disk)
        if data is None:
            continue
        hits: list[tuple[int, str]] = []
        for rule in rules:
            if rule.needle is not None and rule.needle not in data:
                continue
            seen: set[int] = set()
            for match in rule.regex.finditer(data):
                line = data.count(b"\n", 0, match.start()) + 1
                if line in seen:
                    continue
                seen.add(line)
                excused = [a for a in allows if a.id == rule.id and fnmatch.fnmatchcase(public, a.glob)]
                for allow in excused:
                    allow.used = True
                if not excused:
                    hits.append((line, f"{rule.id}: {rule.excerpt(match.group(0))} — {rule.reason}"))
        findings += [f"{label}:{line}: {text}" for line, text in sorted(hits)]
    return findings


def tree_files(root: str):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d != ".git" or dirpath != root)
        for name in sorted(filenames + [d for d in dirnames if os.path.islink(os.path.join(dirpath, d))]):
            disk = os.path.join(dirpath, name)
            public = os.path.relpath(disk, root).replace(os.sep, "/")
            yield disk, public, public


def repo_files(root: str) -> list[tuple[str, str, str]]:
    listing = subprocess.run(
        ["git", "-C", root, "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        capture_output=True, check=True,
    ).stdout.decode("utf-8", "surrogateescape").split("\0")
    table_path = os.path.join(root, EXPORT_TABLE)
    rows = None
    overlay_prefix = ""
    if os.path.isfile(table_path):
        sys.path.insert(0, os.path.join(root, EXPORT_LIB))
        import export_table  # noqa: PLC0415  (present only where the exporter is)

        rows = export_table.load(table_path)
        overlay_prefix = export_table.OVERLAY + "/"
    found: list[tuple[str, str, str]] = []
    for rel in sorted(set(filter(None, listing))):
        disk = os.path.join(root, rel)
        if not (os.path.isfile(disk) or os.path.islink(disk)) or rel in (LIST, ALLOW):
            continue
        if rows is not None:
            if rel.startswith(overlay_prefix):
                public = rel[len(overlay_prefix):]
                found.append((disk, public, f"{rel} (published as {public})"))
                continue
            if export_table.match(rows, rel) is not None:
                continue
        found.append((disk, rel, rel))
    return found


def run(scope: str, where: str, list_path: str, allow_path: str) -> int:
    try:
        rules, allows = load(list_path, allow_path)
    except FileNotFoundError:
        print(f"operator vocabulary: {list_path} is not in this tree; nothing was checked")
        return EXIT_ABSENT
    except TableError as error:
        print(f"operator vocabulary: {error}")
        return EXIT_FINDINGS
    try:
        files = repo_files(where) if scope == "repo" else list(tree_files(where))
    except TableError as error:
        print(f"operator vocabulary: {error}")
        return EXIT_FINDINGS
    findings = scan(files, rules, allows)
    if scope == "repo":
        findings += [
            f"{a.where}: the allow row for {a.glob} ({a.id}) excuses nothing any more; delete it"
            for a in allows if not a.used
        ]
    for finding in findings:
        print(finding)
    excused = sum(1 for a in allows if a.used)
    print(f"operator vocabulary: {len(rules)} row(s), {len(files)} file(s), "
          f"{excused} allow row(s) in use, {len(findings)} finding(s)")
    return EXIT_FINDINGS if findings else EXIT_CLEAN


def self_test() -> list[str]:
    """The scanner against fixtures it must refuse and one it must pass."""
    problems: list[str] = []
    if literal_needle(r"\b(?:enzo|edge)\.taffy\.test\b") != b".taffy.test":
        problems.append("the needle of a host pattern is not its domain")
    if literal_needle(r"ab|cd") is not None or literal_needle(r"(?i)abcdef") is not None:
        problems.append("a needle was derived where none is sound")
    if literal_needle(r"abcd?e") != b"abc":
        problems.append("a quantified character was kept in the needle")
    with tempfile.TemporaryDirectory(prefix="taffy-vocabulary-") as work:
        table = os.path.join(work, "list.tsv")
        allow = os.path.join(work, "allow.tsv")
        with open(table, "w", encoding="utf-8") as handle:
            handle.write("fixture-host\tallowable\t\\bops\\.fixture\\.test\\b\tops.fixture.test\tfixture\n")
            handle.write("fixture-key\tnever\tfk_[a-z]{8}\tfk_abcdefgh\tfixture\n")
        with open(allow, "w", encoding="utf-8") as handle:
            handle.write("history/*.txt\tfixture-host\tprovenance\n")
        tree = os.path.join(work, "tree")
        for rel, text in {
            "clean.txt": "nothing here\n",
            "history/old.txt": "fetched from ops.fixture.test\n",
            "code/host.cc": "const char kHost[] = \"https://ops.fixture.test\";\n",
            "code/key.cc": "x\nkey = fk_qwertyui\n",
        }.items():
            os.makedirs(os.path.dirname(os.path.join(tree, rel)) or tree, exist_ok=True)
            with open(os.path.join(tree, rel), "w", encoding="utf-8") as handle:
                handle.write(text)
        rules, allows = load(table, allow)
        findings = scan(tree_files(tree), rules, allows)
        if sorted(f.split(":")[0] for f in findings) != ["code/host.cc", "code/key.cc"]:
            problems.append(f"the fixture findings were {findings!r}")
        if not any("code/key.cc:2: fixture-key: fk_qwertyui" in f for f in findings):
            problems.append("a value finding does not name its file and line")
        if not all(a.used for a in allows):
            problems.append("an allow row that excuses a hit was reported unused")
        with open(allow, "w", encoding="utf-8") as handle:
            handle.write("code/*\tfixture-key\ttrying to excuse a value\n")
        try:
            load(table, allow)
            problems.append("an allow row excused a `never` row")
        except TableError:
            pass
        with open(table, "a", encoding="utf-8") as handle:
            handle.write("broken\tallowable\tnever-matches\tsomething else\tfixture\n")
        try:
            load(table, os.path.join(work, "absent.tsv"))
            problems.append("a row that misses its own sample was accepted")
        except TableError:
            pass
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    scope = parser.add_mutually_exclusive_group(required=True)
    scope.add_argument("--repo", metavar="ROOT", help="an export of this working tree")
    scope.add_argument("--tree", metavar="DIR", help="every file below an exported tree")
    scope.add_argument("--self-test", action="store_true")
    parser.add_argument("--list", help=f"the vocabulary (default: <repo>/{LIST})")
    parser.add_argument("--allow", help=f"the allow table (default: <repo>/{ALLOW})")
    args = parser.parse_args(argv)
    if args.self_test:
        problems = self_test()
        for problem in problems:
            print(f"operator vocabulary self-test: {problem}")
        print(f"operator vocabulary self-test: {'FAILED' if problems else 'passed'}")
        return EXIT_FINDINGS if problems else EXIT_CLEAN
    repo = os.path.abspath(args.repo or os.environ.get("TAFFY_ROOT") or os.getcwd())
    list_path = args.list or os.path.join(repo, LIST)
    allow_path = args.allow or os.path.join(repo, ALLOW)
    if args.repo:
        return run("repo", repo, list_path, allow_path)
    return run("tree", os.path.abspath(args.tree), list_path, allow_path)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
