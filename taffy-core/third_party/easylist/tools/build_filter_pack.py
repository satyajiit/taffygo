#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Builds the base filter-list pack from the committed snapshots.

The pack is one deterministic zip: the two pinned rule-set snapshots under
`lists/`, beside the licence they travel under as `LICENSE`. Its bytes are a
function of the committed tree and nothing else — `manifest.json` pins each
snapshot's SHA-256 and the builder refuses to package bytes that differ, so a
quietly edited list cannot become a pack that claims this revision.

On success the builder prints the four facts a published catalog variant
requires, in the exact shape `catalog/source/assets.json` takes, so publishing
is a paste rather than a transcription. Nothing here uploads: publish is
`./tools/assets`, which verifies what the origin serves against what this
printed.

Stdlib only. Exit status: 0 built (or self-test passed), 1 refused.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TOOLS_DIR, "..", "..", "..", ".."))
MANIFEST_PATH = os.path.join(TOOLS_DIR, "manifest.json")

sys.path.insert(
    0, os.path.join(REPO_ROOT, "taffy-core", "third_party", "cpython", "tools")
)
import artifact_zip  # noqa: E402

#: The archive member each snapshot becomes. The upstream version stays in the
#: manifest and the provenance record rather than in the member name, so a
#: reader of the pack addresses a stable path and the revision names the pair.
LIST_MEMBERS = {"easylist": "lists/easylist.txt", "easyprivacy": "lists/easyprivacy.txt"}
LICENCE_MEMBER = "LICENSE"


class Refusal(Exception):
    """The tree cannot be packaged as it stands."""


def sha256_of(path: str) -> str:
    hasher = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def load_manifest(path: str) -> dict:
    try:
        with open(path, encoding="utf-8") as handle:
            manifest = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        raise Refusal(f"manifest unreadable: {error}") from error
    for name in LIST_MEMBERS:
        entry = manifest.get("upstream", {}).get(name)
        if not isinstance(entry, dict):
            raise Refusal(f"manifest names no upstream.{name} entry")
        for key in ("snapshot", "sha256", "bytes", "version", "url"):
            if key not in entry:
                raise Refusal(f"upstream.{name} is missing {key}")
    if "licence_file" not in manifest.get("upstream", {}):
        raise Refusal("manifest names no licence file")
    asset = manifest.get("asset")
    if not isinstance(asset, dict) or "path" not in asset or "members" not in asset:
        raise Refusal("manifest names no asset row")
    return manifest


def verified_input(repo_root: str, entry: dict, name: str) -> str:
    """One snapshot's absolute path, after its pinned facts held."""
    path = os.path.join(repo_root, entry["snapshot"])
    if not os.path.isfile(path):
        raise Refusal(f"{name} snapshot is not in the tree: {entry['snapshot']}")
    size = os.path.getsize(path)
    if size != entry["bytes"]:
        raise Refusal(
            f"{name} snapshot is {size} bytes where the manifest pinned "
            f"{entry['bytes']}; a changed list needs a new revision, not a rebuild"
        )
    measured = sha256_of(path)
    if measured != entry["sha256"]:
        raise Refusal(
            f"{name} snapshot hashes to {measured} where the manifest pinned "
            f"{entry['sha256']}; a changed list needs a new revision, not a rebuild"
        )
    return path


def build(manifest: dict, repo_root: str, output: str) -> dict:
    """Packages the pack and returns the published-variant facts."""
    entries = []
    licence = os.path.join(repo_root, manifest["upstream"]["licence_file"])
    if not os.path.isfile(licence):
        raise Refusal(f"licence text is not in the tree: {manifest['upstream']['licence_file']}")
    entries.append((LICENCE_MEMBER, licence))
    for name, member in LIST_MEMBERS.items():
        entries.append((member, verified_input(repo_root, manifest["upstream"][name], name)))
    expected = sorted(manifest["asset"]["members"])
    packaged = sorted(member for member, _ in entries)
    if packaged != expected:
        raise Refusal(f"member set {packaged} differs from the manifest's {expected}")
    artifact_zip.write(output, sorted(entries))
    return artifact_zip.describe(output)


def publication_fragment(manifest: dict, facts: dict) -> str:
    """The catalog-variant fragment, one platform per line to paste."""
    row = {
        "publication": "published",
        "path": manifest["asset"]["path"],
        "transfer_bytes": facts["transfer_bytes"],
        "installed_bytes": facts["installed_bytes"],
        "digest": facts["digest"],
    }
    return json.dumps(row, indent=2)


def concat_text(manifest: dict, repo_root: str, output: str) -> int:
    """Writes EasyList then EasyPrivacy, each terminated by a newline."""
    chunks: list[bytes] = []
    for name in ("easylist", "easyprivacy"):
        path = verified_input(repo_root, manifest["upstream"][name], name)
        with open(path, "rb") as handle:
            payload = handle.read()
        chunks.append(payload.rstrip(b"\n") + b"\n")
    os.makedirs(os.path.dirname(os.path.abspath(output)) or ".", exist_ok=True)
    with open(output, "wb") as handle:
        handle.write(b"".join(chunks))
    return 0


def run_concat(output: str) -> int:
    manifest = load_manifest(MANIFEST_PATH)
    concat_text(manifest, REPO_ROOT, output)
    print(f"filter pack: wrote concatenated snapshots to {output}")
    return 0


def run_build(output: str) -> int:
    manifest = load_manifest(MANIFEST_PATH)
    facts = build(manifest, REPO_ROOT, output)
    print(f"filter pack: wrote {output}")
    print(
        f"filter pack: {facts['entries']} members, {facts['transfer_bytes']} bytes, "
        f"sha256 {facts['digest']}"
    )
    print("filter pack: a published catalog variant for it carries:")
    print(publication_fragment(manifest, facts))
    return 0


def _synthetic_tree(root: str) -> dict:
    """A miniature repository the self-test builds against."""
    vendor = os.path.join(root, "vendor")
    os.makedirs(vendor)
    lists = {
        "easylist": b"[Adblock Plus 2.0]\n! Title: EasyList\n||ads.example^\n",
        "easyprivacy": b"[Adblock Plus 1.1]\n! Title: EasyPrivacy\n||track.example^\n",
    }
    upstream: dict = {"licence_file": "vendor/LICENSE"}
    with open(os.path.join(vendor, "LICENSE"), "wb") as handle:
        handle.write(b"synthetic licence text\n")
    for name, payload in lists.items():
        snapshot = f"vendor/{name}.txt"
        with open(os.path.join(root, snapshot), "wb") as handle:
            handle.write(payload)
        upstream[name] = {
            "url": f"https://lists.example/{name}.txt",
            "version": "1",
            "snapshot": snapshot,
            "sha256": hashlib.sha256(payload).hexdigest(),
            "bytes": len(payload),
        }
    return {
        "upstream": upstream,
        "asset": {
            "path": "filter-lists/1-taffy.1/easylist-base.zip",
            "members": sorted([LICENCE_MEMBER, *LIST_MEMBERS.values()]),
        },
    }


def _self_test() -> int:
    failures: list[str] = []

    def check(name: str, condition: bool) -> None:
        if not condition:
            failures.append(name)

    with tempfile.TemporaryDirectory() as root:
        manifest = _synthetic_tree(root)
        first = os.path.join(root, "first.zip")
        second = os.path.join(root, "second.zip")
        facts = build(manifest, root, first)
        check("pack carries exactly the manifest's members", facts["entries"] == 3)
        rebuilt = build(manifest, root, second)
        with open(first, "rb") as a, open(second, "rb") as b:
            check("a rebuild is byte-identical", a.read() == b.read())
        check("a rebuild reports the same digest", facts["digest"] == rebuilt["digest"])
        fragment = json.loads(publication_fragment(manifest, facts))
        check(
            "the printed fragment carries the four published facts",
            sorted(fragment) == ["digest", "installed_bytes", "path", "publication", "transfer_bytes"],
        )

        edited = dict(manifest)
        with open(os.path.join(root, "vendor", "easylist.txt"), "ab") as handle:
            handle.write(b"||late-edit.example^\n")
        try:
            build(edited, root, os.path.join(root, "third.zip"))
            check("an edited snapshot is refused", False)
        except Refusal as refusal:
            check("the refusal names the pinned digest", "pinned" in str(refusal))

        manifest = _synthetic_tree(os.path.join(root, "again"))
        missing = json.loads(json.dumps(manifest))
        missing["upstream"]["easyprivacy"]["snapshot"] = "vendor/not-there.txt"
        try:
            build(missing, os.path.join(root, "again"), os.path.join(root, "fourth.zip"))
            check("a missing snapshot is refused", False)
        except Refusal as refusal:
            check("the refusal names the missing file", "not in the tree" in str(refusal))

        for key in ("snapshot", "sha256", "bytes", "version", "url"):
            broken = json.loads(json.dumps(manifest))
            del broken["upstream"]["easylist"][key]
            try:
                # load_manifest reads from disk, so exercise its checks there.
                path = os.path.join(root, "again", "manifest.json")
                with open(path, "w", encoding="utf-8") as handle:
                    json.dump(broken, handle)
                load_manifest(path)
                check(f"a manifest missing {key} is refused", False)
            except Refusal:
                check(f"a manifest missing {key} is refused", True)

    # The committed manifest itself must load, and its pinned digests must
    # match the committed snapshots — the same claim the build makes, checked
    # without writing a pack.
    try:
        committed = load_manifest(MANIFEST_PATH)
        for name in LIST_MEMBERS:
            verified_input(REPO_ROOT, committed["upstream"][name], name)
        check("the committed snapshots match their pins", True)
    except Refusal as refusal:
        failures.append(f"the committed tree fails its own pins: {refusal}")

    if failures:
        for failure in failures:
            print(f"filter pack self-test: FAILED: {failure}", file=sys.stderr)
        return 1
    print("filter pack self-test: passed")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--output", help="Where to write the pack zip.")
    group.add_argument(
        "--concat",
        help="Write the two snapshots concatenated as UTF-8 text (APK bundle).",
    )
    group.add_argument("--self-test", action="store_true", help="Prove the builder's own properties.")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return _self_test()
    try:
        if arguments.concat:
            return run_concat(arguments.concat)
        return run_build(arguments.output)
    except Refusal as refusal:
        print(f"filter pack: refused: {refusal}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
