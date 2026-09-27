#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""What the artifacts carried in the package must agree with.

Authority boundary: the `bundled` block of the catalog source, and nothing
else. Row shape, digests, paths, publication and staleness stay in
`generate_catalog.py`, which imports this module, raises what it returns, and
runs its self-test inside its own — the same arrangement `model_rows.py` has,
for the same reason.

WHY IT IS A SEPARATE FILE

Every other rule in the generator is about a row. These are about the
relationship between a row and bytes that are on this disk: decision 0202
carries every required part inside the package, so the files under
`BUNDLED_DIRECTORY` and the published variants of the rows they satisfy state
one length and one digest twice. Nothing else in this tree compares them. The
embedded catalog is a generated constant carrying no path, and the device is
what enforces the digest — on a phone, at install time, by refusing an
artifact and naming a digest rather than a cause.

BOTH DIRECTIONS, BECAUSE ONLY ONE OF THEM IS LOUD

A row naming a file that is not there fails here immediately and could hardly
do anything else. A file replaced without its catalogue row moving passes
every other gate in this repository, and is then found by a person holding a
phone. So the rules below are totality rules in both directions: every
published variant of a `required` asset must be carried by one of these files,
and every file in that directory other than its own provenance record must be
named by a row.

The provenance record itself is a different question with a different owner.
`tools/lib/vendored_assets.py`, in the `files` lane, holds these same bytes to
`BUNDLED_DIRECTORY/PROVENANCE.txt` — where they came from and under what
terms. This module never reads that record; it reads the catalogue.
"""

from __future__ import annotations

import hashlib
import os
import re

#: Where the package-carried artifacts live, and the record that says where
#: they came from. The record is not an artifact and no row names it.
BUNDLED_DIRECTORY = "taffy-core/components/delivery/bundled"
BUNDLED_RECORD = "PROVENANCE.txt"

BUNDLED_FIELDS = ("asset", "revision", "file", "bytes", "digest", "satisfies")

# A bundled file is one name in one directory of this repository, read by GN
# and by the packager. It is deliberately *not* held to the generator's
# `PATH_SEGMENT`, which is the alphabet a device composes a download path
# from: the two answer different questions, and pinning them to each other
# would be a coincidence rather than a rule. What matters here is that the
# name is one segment and cannot traverse.
BUNDLED_FILE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


def repo_readers(repo_root: str):
    """The two readers the rules take, bound to a repository root.

    Injected rather than reached for directly, so that a fixture and a real
    artifact take one code path and a rule can be broken in a self-test
    without five megabytes being rewritten to break it. Bound here rather
    than in the generator so the repository root stays the generator's one
    fact and the reading stays this module's.
    """

    def measure(relative: str):
        """The byte length and digest of a repository file, or None."""
        try:
            with open(os.path.join(repo_root, *relative.split("/")), "rb") as handle:
                payload = handle.read()
        except OSError:
            return None
        return len(payload), hashlib.sha256(payload).hexdigest()

    def listing(relative: str):
        """Every file directly in a repository directory, or None."""
        directory = os.path.join(repo_root, *relative.split("/"))
        try:
            names = os.listdir(directory)
        except OSError:
            return None
        return sorted(
            name for name in names if os.path.isfile(os.path.join(directory, name))
        )

    return measure, listing


def bundled_finding(source: dict, measure, listing) -> str | None:
    """The first way the bundled artifacts and the catalogue disagree."""
    bundled = source.get("bundled")
    if not isinstance(bundled, list) or not bundled:
        return "bundled: decision 0202 carries the required parts in the package"

    rows = {(asset["id"], asset["revision"]): asset for asset in source["assets"]}
    carried: dict = {}
    files: set = set()
    for entry in bundled:
        if not isinstance(entry, dict):
            return "bundled: every entry is an object"
        missing = [field for field in BUNDLED_FIELDS if field not in entry]
        if missing:
            return f"bundled: an entry is missing {missing}"
        where = f"bundled {entry['file']}"

        asset = rows.get((entry["asset"], entry["revision"]))
        if asset is None:
            return f"{where}: no row is {entry['asset']}@{entry['revision']}"
        if asset["necessity"] != "required":
            return (
                f"{where}: {entry['asset']} is {asset['necessity']}, and the package "
                "carries the required parts"
            )
        if not isinstance(entry["file"], str) or not BUNDLED_FILE.match(entry["file"]):
            return f"{where}: a bundled file is one name in {BUNDLED_DIRECTORY}"
        if entry["file"] in files:
            return f"{where}: two entries name one file"
        files.add(entry["file"])

        measured = measure(f"{BUNDLED_DIRECTORY}/{entry['file']}")
        if measured is None:
            return f"{where}: no such file is in the tree"
        length, digest = measured
        if entry["bytes"] != length:
            return f"{where}: records {entry['bytes']} bytes and measures {length}"
        if entry["digest"] != digest:
            return f"{where}: records digest {entry['digest']} and measures {digest}"

        finding = _carriage_finding(where, entry, asset, length, digest, carried)
        if finding:
            return finding

    return _totality_finding(source, files, carried, listing)


def _carriage_finding(where, entry, asset, length, digest, carried) -> str | None:
    """One bundled file against every variant it claims to satisfy."""
    platforms = entry["satisfies"]
    if not isinstance(platforms, list) or not platforms:
        return f"{where}: a bundled file satisfies at least one variant"
    variants = {variant["platform"]: variant for variant in asset["variants"]}
    for platform in platforms:
        variant = variants.get(platform)
        if variant is None:
            return f"{where}: {entry['asset']} has no {platform} variant"
        if variant["publication"] != "published":
            return f"{where}: the {platform} variant of {entry['asset']} is unpublished"
        carriage = (entry["asset"], entry["revision"], platform)
        if carriage in carried:
            return (
                f"{where}: the {platform} variant of {entry['asset']} is already "
                f"carried by {carried[carriage]}"
            )
        carried[carriage] = entry["file"]
        if variant["transfer_bytes"] != length:
            return (
                f"{where}: the {platform} variant transfers "
                f"{variant['transfer_bytes']} bytes and this file is {length}"
            )
        if variant["digest"] != digest:
            return (
                f"{where}: the {platform} variant pins {variant['digest']} and "
                f"this file measures {digest}"
            )
    return None


def _totality_finding(source, files, carried, listing) -> str | None:
    """Nothing required is left out, and nothing on the disk is unaccounted for."""
    for asset in source["assets"]:
        if asset["necessity"] != "required":
            continue
        for variant in asset["variants"]:
            if variant["publication"] != "published":
                continue
            if (asset["id"], asset["revision"], variant["platform"]) not in carried:
                return (
                    f"bundled: nothing carries the {variant['platform']} variant of "
                    f"{asset['id']}, which is required"
                )

    present = listing(BUNDLED_DIRECTORY)
    if present is None:
        return f"bundled: {BUNDLED_DIRECTORY} is not in the tree"
    if BUNDLED_RECORD not in present:
        return f"bundled: {BUNDLED_DIRECTORY}/{BUNDLED_RECORD} is where those bytes came from"
    for name in present:
        if name == BUNDLED_RECORD or name in files:
            continue
        return f"bundled: {BUNDLED_DIRECTORY}/{name} ships and no row names it"
    return None


# The fixtures. One file and two rows, which is the smallest tree that can
# carry the distinctions these rules make: an asset that is required and
# therefore carried, an asset that is not and therefore is not, and one blob
# satisfying two variants the way the real stdlib pack does.
_FIXTURE_FILES = {"alpha.zip": b"alpha bytes"}


def _fixture_measure(relative):
    name = relative.rsplit("/", 1)[-1]
    payload = _FIXTURE_FILES.get(name)
    if payload is None or relative != f"{BUNDLED_DIRECTORY}/{name}":
        return None
    return len(payload), hashlib.sha256(payload).hexdigest()


def _fixture_listing(_relative):
    return sorted([BUNDLED_RECORD, *_FIXTURE_FILES])


def _fixture_variant(name, platform):
    payload = _FIXTURE_FILES[name]
    return {
        "platform": platform,
        "publication": "published",
        "path": f"alpha/{name}",
        "transfer_bytes": len(payload),
        "installed_bytes": len(payload) * 2,
        "digest": hashlib.sha256(payload).hexdigest(),
    }


def _fixture_source() -> dict:
    """The fixture as it is meant to look, and the accepting case.

    `beta` is on-demand, published nowhere and carried by nothing, which is
    `python-toolkit` in the real catalog: the rules must accept that, or the
    only way to satisfy them would be to bundle an artifact that has no bytes.
    """
    payload = _FIXTURE_FILES["alpha.zip"]
    return {
        "assets": [
            {
                "id": "alpha",
                "revision": "1.0-taffy.1",
                "necessity": "required",
                "variants": [
                    _fixture_variant("alpha.zip", "android-arm64"),
                    _fixture_variant("alpha.zip", "android-x64"),
                    {"platform": "windows-x64", "publication": "unpublished"},
                ],
            },
            {
                "id": "beta",
                "revision": "2.0-taffy.1",
                "necessity": "on-demand",
                "variants": [{"platform": "android-arm64", "publication": "unpublished"}],
            },
        ],
        "bundled": [
            {
                "asset": "alpha",
                "revision": "1.0-taffy.1",
                "file": "alpha.zip",
                "bytes": len(payload),
                "digest": hashlib.sha256(payload).hexdigest(),
                "satisfies": ["android-arm64", "android-x64"],
            }
        ],
    }


def _row(document):
    return document["bundled"][0]


def _cases() -> list:
    """Each way the bundled block can be wrong, as a mutation of the fixture."""
    return [
        ("a row naming no catalogue row", lambda d: _row(d).update({"asset": "absent"})),
        ("a row at the wrong revision", lambda d: _row(d).update({"revision": "9.9"})),
        ("a row missing a field", lambda d: _row(d).pop("digest")),
        ("a row naming an asset that is not required", lambda d: d["assets"][0].update(
            {"necessity": "on-demand"})),
        ("a file that is not in the tree", lambda d: _row(d).update({"file": "gone.zip"})),
        ("a file name with a separator", lambda d: _row(d).update({"file": "a/b.zip"})),
        ("a file name that traverses", lambda d: _row(d).update({"file": "../b.zip"})),
        ("a wrong length", lambda d: _row(d).update({"bytes": 1})),
        ("a wrong digest", lambda d: _row(d).update({"digest": "0" * 64})),
        ("two rows naming one file", lambda d: d["bundled"].append(dict(_row(d)))),
        ("a row satisfying nothing", lambda d: _row(d).update({"satisfies": []})),
        ("a row satisfying no such variant", lambda d: _row(d).update(
            {"satisfies": ["macos-arm64"]})),
        ("a row satisfying an unpublished variant", lambda d: _row(d).update(
            {"satisfies": ["windows-x64"]})),
        ("one variant carried twice", lambda d: _row(d).update(
            {"satisfies": ["android-arm64", "android-arm64"]})),
        ("a variant whose digest moved without the file", lambda d:
            d["assets"][0]["variants"][0].update({"digest": "0" * 64})),
        ("a variant whose transfer length moved", lambda d:
            d["assets"][0]["variants"][0].update({"transfer_bytes": 7})),
        ("a required variant nothing carries", lambda d: d["bundled"].pop(0)),
        ("a source that bundles nothing", lambda d: d.pop("bundled")),
        ("an empty bundled block", lambda d: d.update({"bundled": []})),
    ]


def self_test() -> tuple:
    """Break each rule against the fixture and check every one fires.

    The count is returned rather than written down, so the number the
    generator prints is the number of checks that actually ran.
    """
    failures: list[str] = []
    checks = 1

    finding = bundled_finding(_fixture_source(), _fixture_measure, _fixture_listing)
    if finding:
        failures.append(f"the accepting fixture was refused: {finding}")

    for name, mutate in _cases():
        document = _fixture_source()
        mutate(document)
        if not bundled_finding(document, _fixture_measure, _fixture_listing):
            failures.append(name)
        checks += 1

    # The disk direction takes a listing rather than a broken row: an artifact
    # added to the directory and named by nothing is the case a register-first
    # check cannot see, and it is the one that happens.
    strays = {
        "an unnamed file in the bundled directory":
            lambda _r: sorted([BUNDLED_RECORD, "surprise.zip", *_FIXTURE_FILES]),
        "a missing bundled directory": lambda _r: None,
        "a directory with no provenance record": lambda _r: sorted(_FIXTURE_FILES),
    }
    for name, listing in strays.items():
        if not bundled_finding(_fixture_source(), _fixture_measure, listing):
            failures.append(name)
        checks += 1

    return failures, checks
