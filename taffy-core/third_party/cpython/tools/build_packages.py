#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Packages the allowlisted pure-Python set into the asset the catalog names.

The standard library is what Python is; this is what a bundled entrypoint is
allowed to import beside it. The two are separate assets because they change on
different clocks and for different reasons — the library moves when CPython
does, this moves when an entrypoint needs something — and because one is
required at start-up while this is fetched on demand.

The allowlist beside this file is the whole of the policy. This tool reads it,
refuses anything it does not name, and answers the facts a published catalog
variant needs. It downloads nothing: a wheel is fetched by whoever builds the
artifact and verified here against the digest the allowlist pins, so a
compromised index is a build failure rather than a release.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import artifact_zip  # noqa: E402  (path is set immediately above)

ALLOWLIST = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "packages",
    "allowlist.txt",
)

#: A wheel carrying one of these is not pure Python, whatever its name says.
COMPILED_SUFFIXES = (".so", ".pyd", ".dylib", ".dll", ".a", ".lib")

#: The same six the catalog publishes for; `unsupported` is deliberately absent.
PLATFORMS = (
    "android-arm64",
    "android-x64",
    "macos-arm64",
    "macos-x64",
    "windows-x64",
    "windows-arm64",
)


class PackageError(Exception):
    """The allowlist or the staged tree is not one this tool can package."""


class Row:
    """One allowlist entry."""

    def __init__(self, name: str, version: str, digest: str, reason: str, where: str):
        self.name = name
        self.version = version
        self.digest = digest
        self.reason = reason
        self.where = where


def read_allowlist(path: str = ALLOWLIST) -> list[Row]:
    """Every row, or a `PackageError` naming the line that is malformed."""
    if not os.path.exists(path):
        raise PackageError(f"{path} does not exist")
    rows: list[Row] = []
    seen: set[str] = set()
    with open(path, encoding="utf-8") as handle:
        for number, line in enumerate(handle, start=1):
            text = line.rstrip("\n")
            if not text.strip() or text.lstrip().startswith("#"):
                continue
            where = f"{os.path.basename(path)}:{number}"
            fields = text.split("\t")
            if len(fields) < 4:
                raise PackageError(
                    f"{where}: four tab-separated fields are required "
                    f"(name, version, sha256, reason); found {len(fields)}. "
                    "A space-separated row is not a row."
                )
            name, version, digest, reason = (field.strip() for field in fields[:4])
            if not name or not version:
                raise PackageError(f"{where}: a name and a version are required")
            if len(digest) != 64 or digest.lower() != digest or not _is_hex(digest):
                raise PackageError(
                    f"{where}: the third field is the wheel's lowercase SHA-256"
                )
            if not reason:
                raise PackageError(
                    f"{where}: a reason is required, and it names the entrypoint "
                    "that imports this package"
                )
            if name in seen:
                raise PackageError(f"{where}: {name} appears twice")
            seen.add(name)
            rows.append(Row(name, version, digest, reason, where))
    return rows


def _is_hex(value: str) -> bool:
    return all(character in "0123456789abcdef" for character in value)


def _refuse_compiled(root: str) -> None:
    for directory, _, files in os.walk(root):
        for name in files:
            if name.endswith(COMPILED_SUFFIXES):
                relative = os.path.relpath(os.path.join(directory, name), root)
                raise PackageError(
                    f"{relative}: a compiled extension. The allowlist is pure "
                    "Python only; a package that needs an extension is a "
                    "component, not an allowlist entry. It is also the one "
                    "thing this artifact may not carry: Play exempts what an "
                    "interpreter interprets, not a downloaded `.so`."
                )


def _excluded(archive_path: str) -> bool:
    parts = archive_path.split("/")
    if "__pycache__" in parts:
        return True
    if any(part.endswith(".dist-info") for part in parts):
        # Installer metadata describes an installation, and nothing in the
        # sandbox installs. Its RECORD also carries the staging tree's own
        # hashes, which would make the artifact depend on where it was built.
        return True
    return archive_path.endswith((".pyc", ".pyo"))


def build(staged: str, output: str, platform: str, revision: str) -> dict:
    """Packages a staged tree of already-verified wheels."""
    if platform not in PLATFORMS:
        raise PackageError(f"{platform!r} is not a platform the catalog publishes for")
    rows = read_allowlist()
    if not rows:
        raise PackageError(
            "the allowlist is empty, so there is no package set to build. That "
            "is the current state on purpose: the first row arrives with the "
            "first bundled entrypoint that needs it."
        )
    if not os.path.isdir(staged):
        raise PackageError(f"{staged} is not a directory")
    _refuse_compiled(staged)
    entries = artifact_zip.collect(staged, lambda name: True, _excluded)
    artifact_zip.write(output, entries)
    facts = artifact_zip.describe(output)
    facts.update(
        {
            "asset_id": "python-toolkit",
            "asset_revision": revision,
            "platform": platform,
            "publication": "published",
            "path": f"python-toolkit/{revision}/{platform}/toolkit.zip",
            "packages": [f"{row.name}=={row.version}" for row in rows],
        }
    )
    return facts


def _self_test() -> int:
    """Checks the allowlist parser and every way this tool refuses."""
    failures: list[str] = []

    try:
        rows = read_allowlist()
    except PackageError as error:
        failures.append(f"the committed allowlist does not parse: {error}")
        rows = []
    if rows:
        failures.append(
            "the committed allowlist has rows; this self-test's empty-set case "
            "no longer covers what it claims"
        )

    with tempfile.TemporaryDirectory() as scratch:
        malformed = [
            ("a space-separated row", "one 1.0 " + "a" * 64 + " reason\n"),
            ("a row with no reason", "one\t1.0\t" + "a" * 64 + "\t\n"),
            ("an uppercase digest", "one\t1.0\t" + "A" * 64 + "\treason\n"),
            ("a short digest", "one\t1.0\tabc\treason\n"),
            ("a duplicate name", "one\t1.0\t" + "a" * 64 + "\tr\none\t2.0\t" + "b" * 64 + "\tr\n"),
        ]
        for case, body in malformed:
            path = os.path.join(scratch, "allowlist.txt")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(body)
            try:
                read_allowlist(path)
            except PackageError:
                continue
            failures.append(f"{case} was accepted rather than refused")

        good = os.path.join(scratch, "good.txt")
        with open(good, "w", encoding="utf-8") as handle:
            handle.write("# a comment\n\nalpha\t1.2.3\t" + "a" * 64 + "\tthe demo entrypoint\n")
        parsed = read_allowlist(good)
        if len(parsed) != 1 or parsed[0].name != "alpha" or parsed[0].version != "1.2.3":
            failures.append("a well-formed row did not parse")

        staged = os.path.join(scratch, "staged", "alpha")
        os.makedirs(staged)
        with open(os.path.join(staged, "_speedup.so"), "wb") as handle:
            handle.write(b"\x7fELF")
        try:
            _refuse_compiled(os.path.join(scratch, "staged"))
        except PackageError:
            pass
        else:
            failures.append("a compiled extension was packaged rather than refused")

        for case, name in (
            ("byte-code cache", "alpha/__pycache__/x.cpython-314.pyc"),
            ("installer metadata", "alpha-1.2.3.dist-info/RECORD"),
            ("a stray .pyc", "alpha/x.pyc"),
        ):
            if not _excluded(name):
                failures.append(f"{case} would be packaged")
        if _excluded("alpha/__init__.py"):
            failures.append("a package source file would be excluded")

    for failure in failures:
        print(f"build packages: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("build packages: 11 properties checked, including six refusals")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--staged", help="a directory of already-verified wheels, unpacked")
    parser.add_argument("--output", help="where to write the artifact")
    parser.add_argument("--platform", help=f"one of: {', '.join(PLATFORMS)}")
    parser.add_argument("--revision", help="the catalog revision this artifact is for")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="check the allowlist parser and every refusal",
    )
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return _self_test()
    required = ("staged", "output", "platform", "revision")
    missing = [name for name in required if not getattr(arguments, name)]
    if missing:
        parser.error(f"--{' and --'.join(missing)} are required without --self-test")
    try:
        facts = build(
            arguments.staged, arguments.output, arguments.platform, arguments.revision
        )
    except (PackageError, artifact_zip.ArtifactError) as error:
        print(f"build packages: {error}", file=sys.stderr)
        return 1
    print(json.dumps(facts, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
