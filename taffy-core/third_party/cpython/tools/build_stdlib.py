#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Packages one CPython standard library into the asset the catalog names.

The interpreter ships inside the installer and the library is fetched, for the
reasons decision 0046 records: a downloaded `.so` is the shape Play's
restriction names and the sandbox could not load one anyway, while what an
interpreter interprets is the exception that restriction states -- and the
library is the larger half, which most sessions never touch.

This tool turns a CPython `Lib/` directory into that artifact. It decides three
things and states each one:

  * **What is left out.** A phone does not carry CPython's own test suite, its
    IDE, its Tk bindings, or the byte-code caches of whatever built it. The
    exclusions are a table below, each with a reason, and the tool refuses a
    tree that is missing what it expects rather than quietly shipping less.
  * **What the artifact weighs and hashes to.** Answered as a manifest, in the
    exact shape a published catalog variant needs, so filling the row in is a
    copy rather than a measurement someone takes by hand.
  * **Which platform the row is for.** The library is mostly portable, but it
    is packaged per platform because the catalog is: a variant is where a
    platform-specific decision goes when one is needed, and a row that shared
    one artifact across platforms would have nowhere to put the first one.

It never downloads anything and never writes into the source tree.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import artifact_zip  # noqa: E402  (path is set immediately above)

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "manifest.json")
CATALOG = os.path.normpath(
    os.path.join(
        HERE,
        "..",
        "..",
        "..",
        "components",
        "delivery",
        "core",
        "rust",
        "asset-plane",
        "catalog",
        "source",
        "assets.json",
    )
)

#: Directories left out of every variant, and why. Each is a top-level name in
#: CPython's `Lib/`; a nested one would be a rule about someone else's layout.
EXCLUDED_DIRECTORIES = {
    "test": "CPython's own test suite: tens of megabytes that test the interpreter, not the product",
    "idlelib": "the IDLE editor, which is a desktop application",
    "tkinter": "Tk bindings, whose native library is not in the sandbox",
    "turtledemo": "a teaching demo for tkinter, which is not here either",
    "lib2to3": "a Python 2 translator, removed upstream and never used here",
    "ensurepip": "a bundled installer; nothing in the sandbox may install a package",
    "venv": "virtual environments, which a sandboxed worker has no filesystem to make",
    "__pycache__": "byte-code caches of whichever interpreter last read the tree",
}

#: File suffixes left out wherever they appear.
EXCLUDED_SUFFIXES = {
    ".pyc": "compiled for one interpreter build; the worker compiles what it needs",
    ".pyo": "the same, from an older CPython",
    ".whl": "a wheel bundled inside the library is a package, not the library",
}

#: Names that must be present, or the tree is not a standard library. Cheap,
#: and it turns "you pointed at the wrong directory" into a message rather than
#: into an artifact that is missing half of Python.
REQUIRED_ENTRIES = ("os.py", "json", "encodings", "importlib")

#: The platforms a variant may be built for. The same list the catalog
#: generator holds, and deliberately not `unsupported`: that platform exists so
#: a build with no published artifacts can say so, and building one for it
#: would contradict the only thing it means.
# The same alphabet generate_catalog.py holds and the browser enforces.
REVISION = re.compile(r"^[a-z0-9]+([.\-][a-z0-9]+)*$")

PLATFORMS = (
    "android-arm64",
    "android-x64",
    "macos-arm64",
    "macos-x64",
    "windows-x64",
    "windows-arm64",
)


class BuildError(Exception):
    """The source tree is not one this tool can package."""


def _load_json(path: str) -> dict:
    try:
        with open(path, encoding="utf-8") as handle:
            value = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        raise BuildError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise BuildError(f"{path} is not a JSON object")
    return value


def _registry_error(manifest: dict, catalog: dict) -> str | None:
    asset = manifest.get("asset")
    rows = catalog.get("assets")
    if not isinstance(asset, dict) or not isinstance(rows, list):
        return "the CPython asset pin or catalog rows are missing"
    matches = [
        row
        for row in rows
        if isinstance(row, dict) and row.get("id") == asset.get("id")
    ]
    if len(matches) != 1:
        return f"the catalog has {len(matches)} rows for {asset.get('id')!r}"
    row = matches[0]
    for field in ("id", "revision", "kind", "container", "necessity"):
        if asset.get(field) != row.get(field):
            return (
                f"the CPython manifest records {field}={asset.get(field)!r}, "
                f"but the catalog records {row.get(field)!r}"
            )
    return None


def _registered_asset() -> dict:
    manifest = _load_json(MANIFEST)
    error = _registry_error(manifest, _load_json(CATALOG))
    if error:
        raise BuildError(error)
    return manifest["asset"]


def _published_variant_error(facts: dict, catalog: dict) -> str | None:
    rows = catalog.get("assets")
    if not isinstance(rows, list):
        return "the asset catalog has no rows"
    matches = [
        row
        for row in rows
        if isinstance(row, dict)
        and row.get("id") == facts.get("asset_id")
        and row.get("revision") == facts.get("asset_revision")
    ]
    if len(matches) != 1:
        return "the built standard library has no unique catalog row"
    variants = matches[0].get("variants")
    if not isinstance(variants, list):
        return "the standard-library catalog row has no variants"
    variants = [
        variant
        for variant in variants
        if isinstance(variant, dict)
        and variant.get("platform") == facts.get("platform")
    ]
    if len(variants) != 1:
        return "the built platform has no unique catalog variant"
    variant = variants[0]
    if variant.get("publication") != "published":
        return None
    for field in ("path", "transfer_bytes", "installed_bytes", "digest"):
        if facts.get(field) != variant.get(field):
            return (
                f"the rebuilt {facts.get('platform')} artifact has "
                f"{field}={facts.get(field)!r}, but its published variant pins "
                f"{variant.get(field)!r}"
            )
    return None


def _excluded(archive_path: str) -> bool:
    head = archive_path.split("/", 1)[0]
    if head in EXCLUDED_DIRECTORIES:
        return True
    if "__pycache__" in archive_path.split("/"):
        return True
    return any(archive_path.endswith(suffix) for suffix in EXCLUDED_SUFFIXES)


def _verify_source(source: str) -> None:
    if not os.path.isdir(source):
        raise BuildError(
            f"{source} is not a directory. Point --source at a CPython checkout's "
            "Lib/ directory; this tool downloads nothing."
        )
    missing = [
        name for name in REQUIRED_ENTRIES if not os.path.exists(os.path.join(source, name))
    ]
    if missing:
        raise BuildError(
            f"{source} is missing {', '.join(missing)}, so it is not a CPython "
            "standard library. Refusing rather than packaging what is there."
        )


def _verify_revision(revision: str) -> None:
    """Refuses a revision this tool would embed in a path a device rejects.

    The path below quotes the revision verbatim, and `IsRelativePath` in
    taffy-core/browser/core_asset_validation.cc permits only lowercase letters,
    digits and the three separators. A revision outside that alphabet builds an
    artifact nobody can fetch, and the refusal on the device names no character,
    so it is refused here where the composition happens.
    """
    if not REVISION.match(revision):
        raise BuildError(
            f"{revision!r} is outside the alphabet a delivery path may use "
            "(lowercase letters, digits, '.' and '-'), and this tool embeds "
            "the revision in the path"
        )


def build(source: str, output: str, platform: str, revision: str) -> dict:
    """Writes the artifact and answers the facts a catalog row needs."""
    if platform not in PLATFORMS:
        raise BuildError(f"{platform!r} is not a platform the catalog publishes for")
    _verify_revision(revision)
    _verify_source(source)
    entries = artifact_zip.collect(source, lambda name: True, _excluded)
    artifact_zip.write(output, entries)
    facts = artifact_zip.describe(output)
    facts.update(
        {
            "asset_id": "python-stdlib",
            "asset_revision": revision,
            "platform": platform,
            "publication": "published",
            "path": f"python/{revision}/{platform}/stdlib.zip",
        }
    )
    return facts


def _self_test() -> int:
    """Drives the tool over a synthetic tree, including the ways it refuses."""
    failures: list[str] = []
    registered: dict | None = None
    try:
        registered = _registered_asset()
    except BuildError as error:
        failures.append(f"the committed asset pin disagrees with the catalog: {error}")
    else:
        forged = {"assets": [dict(registered, kind="python-runtime")]}
        if _registry_error({"asset": registered}, forged) is None:
            failures.append("a catalog kind that differs from the manifest was accepted")
    with tempfile.TemporaryDirectory() as scratch:
        source = os.path.join(scratch, "Lib")
        for directory in ("json", "encodings", "importlib", "test", "__pycache__"):
            os.makedirs(os.path.join(source, directory))
        for relative, body in (
            ("os.py", b"# os\n"),
            ("json/__init__.py", b"# json\n"),
            ("encodings/__init__.py", b"# encodings\n"),
            ("importlib/__init__.py", b"# importlib\n"),
            ("test/test_os.py", b"# a megabyte of tests, in spirit\n"),
            ("__pycache__/os.cpython-314.pyc", b"\x00\x01"),
            ("json/decoder.pyc", b"\x00\x02"),
        ):
            with open(os.path.join(source, relative), "wb") as handle:
                handle.write(body)

        output = os.path.join(scratch, "stdlib.zip")
        facts = build(source, output, "android-arm64", "3.14.2-taffy.1")
        if registered and facts["asset_id"] != registered["id"]:
            failures.append("the builder's asset id differs from the registered row")
        if facts["entries"] != 4:
            failures.append(f"expected four packaged files, got {facts['entries']}")
        if facts["path"] != "python/3.14.2-taffy.1/android-arm64/stdlib.zip":
            failures.append(f"path: {facts['path']}")
        if facts["transfer_bytes"] != facts["installed_bytes"]:
            failures.append("a zip that stays a zip weighs the same installed")
        variant = {
            field: facts[field]
            for field in (
                "platform",
                "path",
                "transfer_bytes",
                "installed_bytes",
                "digest",
            )
        }
        variant["publication"] = "published"
        row = {
            "id": facts["asset_id"],
            "revision": facts["asset_revision"],
            "variants": [variant],
        }
        if _published_variant_error(facts, {"assets": [row]}) is not None:
            failures.append("an exact rebuild disagreed with its published variant")
        variant["digest"] = "0" * 64
        if _published_variant_error(facts, {"assets": [row]}) is None:
            failures.append("a rebuild with a different digest passed as published")

        again = os.path.join(scratch, "again.zip")
        build(source, again, "android-arm64", "3.14.2-taffy.1")
        if artifact_zip.digest(output) != artifact_zip.digest(again):
            failures.append("two builds of one tree produced different bytes")

        for bad in ("3.14.2+taffy.1", "3.14.2 taffy", "Python-3.14"):
            try:
                build(source, os.path.join(scratch, "bad.zip"), "android-arm64", bad)
            except BuildError:
                pass
            else:
                failures.append(f"a revision of {bad!r} was accepted into a path")

        for case, action in (
            ("a directory that is not a standard library", lambda: build(
                os.path.join(scratch, "empty"), output, "android-arm64", "1")),
            ("a platform the catalog does not publish for", lambda: build(
                source, output, "linux-x64", "1")),
            ("the unsupported platform", lambda: build(
                source, output, "unsupported", "1")),
        ):
            try:
                action()
            except BuildError:
                continue
            except artifact_zip.ArtifactError:
                continue
            failures.append(f"{case} was accepted rather than refused")

        os.makedirs(os.path.join(scratch, "partial"))
        with open(os.path.join(scratch, "partial", "os.py"), "wb") as handle:
            handle.write(b"# os\n")
        try:
            build(os.path.join(scratch, "partial"), output, "android-arm64", "1")
        except BuildError as error:
            if "missing" not in str(error):
                failures.append(f"a partial tree was refused for the wrong reason: {error}")
        else:
            failures.append("a tree with only os.py was packaged as a standard library")

    for failure in failures:
        print(f"build stdlib: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("build stdlib: 14 properties checked, including nine refusals")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", help="a CPython checkout's Lib/ directory")
    parser.add_argument("--output", help="where to write the artifact")
    parser.add_argument("--platform", help=f"one of: {', '.join(PLATFORMS)}")
    parser.add_argument(
        "--revision",
        help="the catalog revision this artifact is for, e.g. 3.14.2-taffy.1",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="drive the tool over a synthetic tree, refusals included",
    )
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return _self_test()
    required = ("source", "output", "platform", "revision")
    missing = [name for name in required if not getattr(arguments, name)]
    if missing:
        parser.error(f"--{' and --'.join(missing)} are required without --self-test")
    try:
        registered = _registered_asset()
        if arguments.revision != registered["revision"]:
            raise BuildError(
                f"--revision is {arguments.revision!r}, but manifest.json pins "
                f"{registered['revision']!r}"
            )
        facts = build(
            arguments.source, arguments.output, arguments.platform, arguments.revision
        )
        error = _published_variant_error(facts, _load_json(CATALOG))
        if error:
            raise BuildError(error)
    except (BuildError, artifact_zip.ArtifactError) as error:
        print(f"build stdlib: {error}", file=sys.stderr)
        return 1
    # Printed as the catalog's own shape, so publishing the row is a copy.
    print(json.dumps(facts, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
