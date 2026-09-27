#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""A zip whose bytes are a function of its contents and nothing else.

An asset's catalog row names a SHA-256. That number is a promise: these exact
bytes, from any machine, at any time. A zip written the ordinary way breaks it
on the first rebuild — `zipfile` stores each entry's modification time, the
order the filesystem happened to hand back, the host system's permission bits
and the creating tool's version, and none of those is a fact about the archive's
contents.

So this module writes one shape and only one:

  * entries sorted by their archive path, byte-wise
  * one fixed timestamp for every entry (the zip epoch, 1980-01-01)
  * one fixed external attribute per kind, so a file that was executable on the
    build host is not executable here and a umask cannot change an artifact
  * `create_system` fixed to 3 (Unix), so a Windows builder and a Linux builder
    write the same header
  * deflate at one pinned level, because a different zlib default is a
    different archive of the same files
  * no directory entries, because a directory carries no content and every
    reader this product has creates parents on demand

Two builds of the same tree therefore produce the same digest, which is what
lets the row be written once and verified anywhere.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import sys
import tempfile
import zipfile

#: The zip format cannot represent a timestamp before this. Using it says the
#: archive has no timestamps rather than pretending to a false one.
FIXED_TIMESTAMP = (1980, 1, 1, 0, 0, 0)

#: `-rw-r--r--`, shifted into the high half where zip keeps Unix permissions.
#: One value for every entry: an artifact is data, and nothing in it is
#: executable no matter what the build host thought.
FIXED_EXTERNAL_ATTR = 0o100644 << 16

#: Deflate, at a level named here rather than left to zlib's default. A level
#: is part of the output bytes, so it is part of the promise.
COMPRESS_LEVEL = 9

#: Unix, so a Windows builder writes the same header as a Linux one.
CREATE_SYSTEM = 3


class ArtifactError(Exception):
    """The tree cannot be packaged as it stands."""


def normalise(path: str) -> str:
    """The archive path for one file, with the separator the format uses."""
    return path.replace(os.sep, "/")


def collect(root: str, include, exclude) -> list[tuple[str, str]]:
    """Every file under `root` that survives the filters, as (archive, disk).

    `include` and `exclude` are called with the archive path. A file both
    accept is packaged; everything else is left out silently, because a tree
    that is mostly excluded is the normal case rather than an anomaly.
    """
    if not os.path.isdir(root):
        raise ArtifactError(f"{root} is not a directory")
    collected: list[tuple[str, str]] = []
    for directory, subdirectories, files in os.walk(root):
        subdirectories.sort()
        for name in sorted(files):
            disk = os.path.join(directory, name)
            if os.path.islink(disk):
                # A symlink's target is a fact about the build host. Following
                # one would package a file from outside the tree; storing one
                # would make the archive depend on where it is unpacked.
                raise ArtifactError(f"{normalise(os.path.relpath(disk, root))}: symbolic link")
            archive = normalise(os.path.relpath(disk, root))
            if exclude(archive) or not include(archive):
                continue
            collected.append((archive, disk))
    collected.sort(key=lambda entry: entry[0].encode("utf-8"))
    return collected


def write(output: str, entries: list[tuple[str, str]]) -> None:
    """Writes the one deterministic shape this module produces."""
    if not entries:
        raise ArtifactError("nothing to package: every file was excluded")
    parent = os.path.dirname(os.path.abspath(output))
    os.makedirs(parent, exist_ok=True)
    with zipfile.ZipFile(
        output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=COMPRESS_LEVEL
    ) as archive:
        for name, disk in entries:
            info = zipfile.ZipInfo(filename=name, date_time=FIXED_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = FIXED_EXTERNAL_ATTR
            info.create_system = CREATE_SYSTEM
            with open(disk, "rb") as handle:
                archive.writestr(info, handle.read(), compresslevel=COMPRESS_LEVEL)


def digest(path: str) -> str:
    """The lowercase SHA-256 of a file, in the shape a catalog row names."""
    hasher = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def describe(path: str) -> dict:
    """The four facts a published catalog variant requires, plus the count."""
    with zipfile.ZipFile(path) as archive:
        entry_count = len(archive.namelist())
    size = os.path.getsize(path)
    return {
        "digest": digest(path),
        # A zip that stays a zip is one file to verify and one descriptor to
        # hand a worker, so what it occupies installed is what it weighed on
        # the wire. The two numbers are still both named, because a container
        # that unpacked would make them differ and the row's shape should not
        # have to change when one does.
        "transfer_bytes": size,
        "installed_bytes": size,
        "entries": entry_count,
    }


def _self_test() -> int:
    """Breaks the guarantees this module exists for, and checks each is kept."""
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as scratch:
        tree = os.path.join(scratch, "tree")
        os.makedirs(os.path.join(tree, "pkg", "inner"))
        os.makedirs(os.path.join(tree, "skipme"))
        for relative, body in (
            ("pkg/__init__.py", b"one\n"),
            ("pkg/inner/deep.py", b"two\n"),
            ("top.py", b"three\n"),
            ("skipme/never.py", b"four\n"),
        ):
            with open(os.path.join(tree, relative), "wb") as handle:
                handle.write(body)
        # Executable on disk, data in the archive.
        os.chmod(os.path.join(tree, "top.py"), 0o755)

        keep = lambda name: True
        drop = lambda name: name.startswith("skipme/")
        entries = collect(tree, keep, drop)
        names = [name for name, _ in entries]
        if names != ["pkg/__init__.py", "pkg/inner/deep.py", "top.py"]:
            failures.append(f"collect: wrong set or order: {names}")

        first = os.path.join(scratch, "first.zip")
        second = os.path.join(scratch, "second.zip")
        write(first, entries)
        # Touch every input, which is what a fresh checkout does, and repackage.
        for _, disk in entries:
            os.utime(disk, (0, 0))
        write(second, entries)
        if digest(first) != digest(second):
            failures.append("two builds of one tree produced different bytes")

        with zipfile.ZipFile(first) as archive:
            for info in archive.infolist():
                if info.date_time != FIXED_TIMESTAMP:
                    failures.append(f"{info.filename}: carries a real timestamp")
                if info.external_attr != FIXED_EXTERNAL_ATTR:
                    failures.append(f"{info.filename}: carries host permissions")
                if info.create_system != CREATE_SYSTEM:
                    failures.append(f"{info.filename}: carries the host system")

        facts = describe(first)
        if facts["entries"] != 3 or facts["transfer_bytes"] != os.path.getsize(first):
            failures.append(f"describe: {facts}")
        if facts["digest"] != facts["digest"].lower() or len(facts["digest"]) != 64:
            failures.append("describe: the digest is not the shape a row names")

        try:
            write(os.path.join(scratch, "empty.zip"), [])
        except ArtifactError:
            pass
        else:
            failures.append("an archive of nothing was written rather than refused")

        os.symlink(os.path.join(tree, "top.py"), os.path.join(tree, "pkg", "link.py"))
        try:
            collect(tree, keep, drop)
        except ArtifactError:
            pass
        else:
            failures.append("a symbolic link was packaged rather than refused")

    for failure in failures:
        print(f"artifact zip: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("artifact zip: deterministic across rebuilds; 6 properties checked")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="break the determinism guarantees and check each is kept",
    )
    arguments = parser.parse_args(argv)
    if not arguments.self_test:
        parser.error("this module is a library; --self-test is its only command")
    return _self_test()


if __name__ == "__main__":
    raise SystemExit(main())
