#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Fetch and atomically mount the exact CPython source pinned by TaffyGo.

The Chromium sync is the only networked build-input step.  This helper keeps
that boundary narrow: it accepts one manifest, verifies the archive before
opening it, validates every archive path, and publishes the extracted tree by
one rename.  A verified existing tree is an offline success.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request


MAX_ARCHIVE_BYTES = 128 * 1024 * 1024
MAX_EXPANDED_BYTES = 1024 * 1024 * 1024
MAX_MEMBERS = 100_000
MARKER = ".taffy-source.json"
REQUIRED_SOURCE_MEMBERS = ("Include/Python.h", "LICENSE", "configure")


class SourceError(ValueError):
    """A manifest, download, archive, or existing mount is not trustworthy."""


def _string(value: object, where: str) -> str:
    if not isinstance(value, str) or not value:
        raise SourceError(f"{where} must be a non-empty string")
    return value


def read_pin(manifest: Path) -> tuple[str, str, str]:
    try:
        document = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SourceError(f"cannot read {manifest}: {error}") from error
    upstream = document.get("upstream") if isinstance(document, dict) else None
    if not isinstance(upstream, dict):
        raise SourceError(f"{manifest}: upstream must be an object")
    version = _string(upstream.get("version"), "upstream.version")
    url = _string(upstream.get("tarball"), "upstream.tarball")
    digest = _string(upstream.get("sha256"), "upstream.sha256").lower()
    if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
        raise SourceError("upstream.sha256 must be 64 lower-case hexadecimal characters")
    if not url.startswith(("https://", "file://")):
        raise SourceError("upstream.tarball must use HTTPS")
    return version, url, digest


def _marker(destination: Path) -> Path:
    return destination / MARKER


def _existing_matches(destination: Path, version: str, digest: str) -> bool:
    if not destination.exists():
        return False
    if not destination.is_dir() or destination.is_symlink():
        raise SourceError(f"{destination} exists but is not a source directory")
    try:
        state = json.loads(_marker(destination).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SourceError(
            f"{destination} has no valid {MARKER}; move it aside and sync again: {error}"
        ) from error
    expected = {"sha256": digest, "version": version}
    if state != expected:
        raise SourceError(
            f"{destination} is pinned as {state!r}, expected {expected!r}; "
            "move it aside and sync again"
        )
    missing = [name for name in REQUIRED_SOURCE_MEMBERS if not (destination / name).is_file()]
    if missing:
        raise SourceError(
            f"{destination} is incomplete despite its marker; missing {', '.join(missing)}"
        )
    return True


def _download(url: str, output: Path) -> str:
    request = urllib.request.Request(url, headers={"User-Agent": "TaffyGo-source-sync/1"})
    digest = hashlib.sha256()
    total = 0
    try:
        with urllib.request.urlopen(request, timeout=60) as response, output.open("xb") as target:
            declared = response.headers.get("Content-Length")
            if declared is not None:
                try:
                    declared_size = int(declared)
                except ValueError as error:
                    raise SourceError("CPython source response has an invalid Content-Length") from error
                if declared_size < 1 or declared_size > MAX_ARCHIVE_BYTES:
                    raise SourceError(
                        f"CPython source response declares {declared_size} bytes; "
                        f"limit is {MAX_ARCHIVE_BYTES}"
                    )
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > MAX_ARCHIVE_BYTES:
                    raise SourceError(
                        f"CPython source archive exceeded {MAX_ARCHIVE_BYTES} bytes"
                    )
                digest.update(chunk)
                target.write(chunk)
    except (OSError, urllib.error.URLError) as error:
        raise SourceError(f"could not fetch pinned CPython source: {error}") from error
    if total == 0:
        raise SourceError("CPython source response was empty")
    return digest.hexdigest()


def _safe_member(member: tarfile.TarInfo, root_name: str) -> None:
    raw = member.name
    path = PurePosixPath(raw)
    if not raw or path.is_absolute() or "\\" in raw or any(part in ("", ".", "..") for part in path.parts):
        raise SourceError(f"CPython archive contains unsafe path {raw!r}")
    if path.parts[0] != root_name:
        raise SourceError(
            f"CPython archive member {raw!r} is outside expected root {root_name!r}"
        )
    if member.isdev() or member.isfifo():
        raise SourceError(f"CPython archive contains special file {raw!r}")
    if member.issym() or member.islnk():
        link = PurePosixPath(member.linkname)
        if link.is_absolute() or "\\" in member.linkname:
            raise SourceError(f"CPython archive contains unsafe link {raw!r}")
        base = path.parent if member.issym() else PurePosixPath()
        resolved: list[str] = []
        for part in (base / link).parts:
            if part in ("", "."):
                continue
            if part == "..":
                if not resolved:
                    raise SourceError(f"CPython archive link escapes its root: {raw!r}")
                resolved.pop()
            else:
                resolved.append(part)
        if not resolved or resolved[0] != root_name:
            raise SourceError(f"CPython archive link escapes its root: {raw!r}")


def _extract(archive: Path, staging: Path, version: str) -> Path:
    root_name = f"Python-{version}"
    try:
        with tarfile.open(archive, mode="r:xz") as source:
            members = source.getmembers()
            if not members or len(members) > MAX_MEMBERS:
                raise SourceError(
                    f"CPython archive has {len(members)} members; limit is {MAX_MEMBERS}"
                )
            expanded = 0
            for member in members:
                _safe_member(member, root_name)
                if member.isfile():
                    expanded += member.size
                    if member.size < 0 or expanded > MAX_EXPANDED_BYTES:
                        raise SourceError(
                            f"CPython archive expands past {MAX_EXPANDED_BYTES} bytes"
                        )
            # Newer host Pythons add mode/link checks through ``data_filter``.
            # Host Python 3 is intentionally not version-pinned, so older
            # hosts use the complete path/link/device checks above instead of
            # failing on a keyword their tarfile module does not know.
            if hasattr(tarfile, "data_filter"):
                filtered = [
                    checked
                    for member in members
                    if (checked := tarfile.data_filter(member, staging)) is not None
                ]
                source.extractall(staging, members=filtered)
            else:
                source.extractall(staging, members=members)
    except (OSError, tarfile.TarError) as error:
        raise SourceError(f"cannot unpack pinned CPython source: {error}") from error
    extracted = staging / root_name
    missing = [name for name in REQUIRED_SOURCE_MEMBERS if not (extracted / name).is_file()]
    if missing:
        raise SourceError(f"CPython source archive is missing {', '.join(missing)}")
    return extracted


def ensure_source(manifest: Path, destination: Path) -> bool:
    """Ensure ``destination`` contains the pin. Return true when newly populated."""
    version, url, expected_digest = read_pin(manifest)
    if _existing_matches(destination, version, expected_digest):
        return False
    if destination.exists():
        # _existing_matches either returned true or raised, so this is only a
        # defensive guard against a future change weakening that contract.
        raise SourceError(f"refusing to replace existing source at {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".cpython-source-", dir=destination.parent) as raw:
        temporary = Path(raw)
        archive = temporary / "source.tar.xz"
        actual_digest = _download(url, archive)
        if actual_digest != expected_digest:
            raise SourceError(
                "pinned CPython source digest mismatch: "
                f"expected {expected_digest}, got {actual_digest}; nothing was unpacked"
            )
        extracted = _extract(archive, temporary / "unpacked", version)
        _marker(extracted).write_text(
            json.dumps({"sha256": expected_digest, "version": version}, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        os.replace(extracted, destination)
    return True


def _fixture_archive(path: Path, version: str, unsafe: bool = False) -> str:
    root = f"Python-{version}"
    fixture = path.parent / "fixture"
    (fixture / root / "Include").mkdir(parents=True, exist_ok=True)
    for relative in REQUIRED_SOURCE_MEMBERS:
        target = fixture / root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(f"fixture {relative}\n", encoding="utf-8")
    with tarfile.open(path, mode="w:xz") as archive:
        archive.add(fixture / root, arcname=root)
        if unsafe:
            payload = fixture / root / "LICENSE"
            archive.add(payload, arcname=f"{root}/../escape")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def self_test() -> None:
    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw)
        archive = root / "source.tar.xz"
        version = "9.8.7"
        digest = _fixture_archive(archive, version)
        manifest = root / "manifest.json"
        manifest.write_text(
            json.dumps(
                {"upstream": {"version": version, "tarball": archive.as_uri(), "sha256": digest}}
            ),
            encoding="utf-8",
        )
        destination = root / "mounted" / "src"
        if not ensure_source(manifest, destination):
            raise SourceError("first self-test population reported an offline hit")
        archive.unlink()
        if ensure_source(manifest, destination):
            raise SourceError("offline self-test population downloaded twice")

        mismatch_archive = root / "mismatch.tar.xz"
        mismatch_digest = _fixture_archive(mismatch_archive, version)
        mismatch_manifest = root / "mismatch.json"
        mismatch_manifest.write_text(
            json.dumps(
                {
                    "upstream": {
                        "version": version,
                        "tarball": mismatch_archive.as_uri(),
                        "sha256": "0" * 64,
                    }
                }
            ),
            encoding="utf-8",
        )
        try:
            ensure_source(mismatch_manifest, root / "bad-digest")
        except SourceError as error:
            if "digest mismatch" not in str(error):
                raise
        else:
            raise SourceError(f"digest mismatch fixture was accepted ({mismatch_digest})")

        unsafe_archive = root / "unsafe.tar.xz"
        unsafe_digest = _fixture_archive(unsafe_archive, version, unsafe=True)
        unsafe_manifest = root / "unsafe.json"
        unsafe_manifest.write_text(
            json.dumps(
                {
                    "upstream": {
                        "version": version,
                        "tarball": unsafe_archive.as_uri(),
                        "sha256": unsafe_digest,
                    }
                }
            ),
            encoding="utf-8",
        )
        try:
            ensure_source(unsafe_manifest, root / "bad-path")
        except SourceError as error:
            if "unsafe path" not in str(error):
                raise
        else:
            raise SourceError("path-traversal fixture was accepted")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--destination", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.self_test:
            if args.manifest or args.destination:
                raise SourceError("--self-test takes no source arguments")
            self_test()
            print("CPython source sync self-test: passed")
            return 0
        if args.manifest is None or args.destination is None:
            raise SourceError("--manifest and --destination are required")
        populated = ensure_source(args.manifest.resolve(), args.destination.resolve())
        state = "populated" if populated else "already verified"
        print(f"CPython source: {state} at {args.destination.resolve()}")
        return 0
    except SourceError as error:
        print(f"CPython source: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
