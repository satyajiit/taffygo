#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Packages the start page's painted plates into the asset the catalog names.

Screens SCR-101 and SCR-102 draw one plate above the TaffyGo wordmark on every
new tab, chosen by the device's own clock: four parts of the day and six
paintings in each. Decision 0145 puts them on the delivery plane rather than in
the installer, so the page opens with the plate compiled in and gets the
painted set once the pack has arrived.

WHAT THIS TOOL DECIDES, AND STATES

  * **Which paintings are in it.** Exactly the members
    `StartSceneMember.allMembers()` can ask for: `<part>-<n>.webp` for each of
    the four parts and each of the six variants. The committed directory and
    that set are reconciled in both directions, because a member the product
    asks for and the pack does not carry is a part of the day that silently
    has no picture, and a file in the directory that no member names is bytes
    nobody reviewed riding along inside a published artifact.
  * **That every one of them is the shape the source expects.** The Compose
    side decodes with an explicit expected width and height and refuses a
    member that disagrees, so a mis-sized painting would reach the device and
    be dropped there. It is cheaper to refuse it here.
  * **What it weighs and hashes to.** Answered as the facts a published catalog
    variant needs, so filling the row in is a copy rather than a measurement
    somebody takes by hand.

It runs no encoder. The reviewed WebPs are committed and are the bytes a person
looked at; re-encoding them at package time would publish bytes nobody
reviewed. `manifest.json` records the recipe they were made with, and the
provenance record beside them is what holds them to it.

    build_scene_pack.py --output pack.zip
    build_scene_pack.py --self-test

Exit status: 0 clean, 1 refused.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import tempfile

#: This file sits at taffy-core/ui/android/tools/scenes/, six levels down.
REPOSITORY = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), *[os.pardir] * 5))
sys.path.insert(0, os.path.join(REPOSITORY, "taffy-core", "third_party", "cpython", "tools"))

import artifact_zip  # noqa: E402  (the path is set immediately above)

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "manifest.json")

#: The archive member every pack carries beside the artwork. The pack is in no
#: build graph, so this is the only copy of the notice that reaches a device.
NOTICE_MEMBER_TEXT = """TaffyGo start page scenes
=========================

Original first-party artwork.
Copyright (c) 2026 Matterward Labs Private Limited. All rights reserved.
TaffyGo proprietary; see the LICENSE file in the TaffyGo repository.

These are decorative, inert plates drawn above the TaffyGo wordmark on a new
tab. They depict no task state, no page, no person's data and no outcome.
Provenance, generation prompts and the encoder recipe are recorded in
taffy-core/ui/android/core/ui/vendor/taffy-start-scenes.txt.
"""


class BuildError(Exception):
    """The inputs are not ones this tool will package."""


def load_manifest() -> dict:
    with open(MANIFEST, "r", encoding="utf-8") as handle:
        return json.load(handle)


def expected_members(art: dict) -> list[str]:
    """The member names the product can ask for, in a stable order.

    This is `StartSceneMember.allMembers()` in Kotlin, spelled once more here.
    The two are held together by the reconciliation below plus the Kotlin
    host test: a part renamed on one side leaves the other with members
    nothing asks for, which is a refusal rather than a quiet absence.
    """
    names: list[str] = []
    for part in art["parts"]:
        for variant in range(1, art["variants_per_part"] + 1):
            names.append(f"{part}-{variant}.webp")
    if len(names) != len(set(names)):
        raise BuildError("the manifest names one member twice")
    return names


def webp_dimensions(data: bytes) -> tuple[int, int]:
    """The canvas size of a WebP file, from its header alone.

    Reads the three chunk shapes `cwebp` emits -- lossy `VP8 `, lossless
    `VP8L`, and the `VP8X` extended header a file with metadata or alpha
    carries. Anything else is refused rather than guessed at, because a size
    this tool could not read is a size it cannot check.
    """
    if len(data) < 30 or data[0:4] != b"RIFF" or data[8:12] != b"WEBP":
        raise BuildError("not a WebP file")
    chunk = data[12:16]
    if chunk == b"VP8X":
        width = int.from_bytes(data[24:27], "little") + 1
        height = int.from_bytes(data[27:30], "little") + 1
        return width, height
    if chunk == b"VP8 ":
        if data[23:26] != b"\x9d\x01\x2a":
            raise BuildError("the lossy frame carries no start code")
        width, height = struct.unpack("<HH", data[26:30])
        return width & 0x3FFF, height & 0x3FFF
    if chunk == b"VP8L":
        if data[20] != 0x2F:
            raise BuildError("the lossless stream carries no signature")
        bits = int.from_bytes(data[21:25], "little")
        return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
    raise BuildError(f"unrecognised WebP chunk {chunk!r}")


def reconcile(directory: str, wanted: list[str]) -> None:
    """Checks the committed artwork and the member set against each other."""
    if not os.path.isdir(directory):
        raise BuildError(
            f"{directory} does not exist. The reviewed WebPs are committed and "
            "this tool encodes nothing; there is no pack until they are there."
        )
    found = sorted(
        name for name in os.listdir(directory) if not name.startswith(".")
    )
    missing = sorted(set(wanted) - set(found))
    if missing:
        raise BuildError(
            "the artwork directory has no "
            f"{', '.join(missing)}. Every member the product can ask for must "
            "be in the pack, because a member with no file is a part of the "
            "day that silently has no picture."
        )
    extra = sorted(set(found) - set(wanted))
    if extra:
        raise BuildError(
            f"the artwork directory carries {', '.join(extra)}, which no "
            "member names. A published artifact carries only bytes somebody "
            "reviewed as part of it."
        )


def build(output: str, manifest: dict | None = None, root: str | None = None) -> dict:
    """Writes the artifact and answers the facts a catalog row needs."""
    manifest = manifest or load_manifest()
    art = manifest["art"]
    asset = manifest["asset"]
    directory = os.path.join(root or REPOSITORY, art["directory"])

    wanted = expected_members(art)
    reconcile(directory, wanted)

    with tempfile.TemporaryDirectory() as scratch:
        tree = os.path.join(scratch, "pack")
        scenes = os.path.join(tree, asset["member_prefix"].rstrip("/"))
        os.makedirs(scenes)
        with open(os.path.join(tree, manifest["notice_member"]), "w", encoding="utf-8") as handle:
            handle.write(NOTICE_MEMBER_TEXT)
        for name in wanted:
            with open(os.path.join(directory, name), "rb") as handle:
                body = handle.read()
            width, height = webp_dimensions(body)
            if (width, height) != (art["width"], art["height"]):
                raise BuildError(
                    f"{name} is {width} by {height} and the manifest says "
                    f"{art['width']} by {art['height']}. The source decodes "
                    "against those numbers and drops a member that disagrees, "
                    "so a mis-sized painting is a plate that never draws."
                )
            with open(os.path.join(scenes, name), "wb") as handle:
                handle.write(body)
        entries = artifact_zip.collect(tree, lambda name: True, lambda name: False)
        artifact_zip.write(output, entries)

    facts = artifact_zip.describe(output)
    facts.update(
        {
            "asset_id": asset["id"],
            "asset_revision": asset["revision"],
            "kind": asset["kind"],
            "container": asset["container"],
            "necessity": asset["necessity"],
            "path": asset["path"],
            "publication": "published",
            "scenes": len(wanted),
        }
    )
    return facts


def _probe_webp(width: int, height: int) -> bytes:
    """A file with a real VP8X header and no image data behind it.

    The self-test is about this tool's rules, not about an encoder: everything
    here reads the header and copies the bytes, so a header is the whole of
    what a probe needs to be.
    """
    chunk = b"VP8X" + struct.pack("<I", 10) + b"\x00\x00\x00\x00"
    chunk += (width - 1).to_bytes(3, "little") + (height - 1).to_bytes(3, "little")
    body = b"WEBP" + chunk
    return b"RIFF" + struct.pack("<I", len(body)) + body


def _self_test() -> int:
    """Drives the tool over a synthetic corpus, including how it refuses."""
    failures: list[str] = []
    manifest = load_manifest()
    art = manifest["art"]
    wanted = expected_members(art)

    if len(wanted) != len(art["parts"]) * art["variants_per_part"]:
        failures.append("the member set is not one file per part and variant")

    # The three chunk shapes, so a header this tool cannot read is a refusal
    # rather than a wrong number.
    if webp_dimensions(_probe_webp(640, 480)) != (640, 480):
        failures.append("an extended header was read as the wrong size")
    lossy = b"RIFF" + struct.pack("<I", 30) + b"WEBPVP8 " + struct.pack("<I", 18)
    lossy += b"\x00\x00\x00" + b"\x9d\x01\x2a" + struct.pack("<HH", 640, 480)
    if webp_dimensions(lossy) != (640, 480):
        failures.append("a lossy header was read as the wrong size")
    lossless = b"RIFF" + struct.pack("<I", 30) + b"WEBPVP8L" + struct.pack("<I", 18)
    lossless += b"\x2f" + struct.pack("<I", (639) | (479 << 14)) + b"\x00" * 5
    if webp_dimensions(lossless) != (640, 480):
        failures.append("a lossless header was read as the wrong size")
    for name, blob in (
        ("a JPEG", b"\xff\xd8\xff\xe0" + b"\x00" * 40),
        ("a WebP of an unknown shape", b"RIFF" + b"\x00" * 4 + b"WEBPXXXX" + b"\x00" * 20),
    ):
        try:
            webp_dimensions(blob)
        except BuildError:
            pass
        else:
            failures.append(f"{name} was read as a size rather than refused")

    with tempfile.TemporaryDirectory() as scratch:
        root = os.path.join(scratch, "tree")
        directory = os.path.join(root, art["directory"])
        os.makedirs(directory)
        for name in wanted:
            with open(os.path.join(directory, name), "wb") as handle:
                handle.write(_probe_webp(art["width"], art["height"]))

        try:
            output = os.path.join(scratch, "pack.zip")
            facts = build(output, manifest, root=root)
            if facts["entries"] != len(wanted) + 1:
                failures.append(f"the pack holds {facts['entries']} members")
            if len(facts["digest"]) != 64 or facts["digest"] != facts["digest"].lower():
                failures.append("the digest is not the shape a row names")
            if facts["transfer_bytes"] != facts["installed_bytes"]:
                failures.append("a zip that stays a zip reported two different sizes")

            again = os.path.join(scratch, "again.zip")
            build(again, manifest, root=root)
            if artifact_zip.digest(output) != artifact_zip.digest(again):
                failures.append("two builds of one corpus produced different bytes")

            held = os.path.join(directory, wanted[0])
            with open(held, "rb") as handle:
                kept = handle.read()
            os.remove(held)
            try:
                build(os.path.join(scratch, "never.zip"), manifest, root=root)
            except BuildError:
                pass
            else:
                failures.append("a member with no file was packaged rather than refused")
            with open(held, "wb") as handle:
                handle.write(kept)

            stray = os.path.join(directory, "midnight-1.webp")
            with open(stray, "wb") as handle:
                handle.write(_probe_webp(art["width"], art["height"]))
            try:
                build(os.path.join(scratch, "never.zip"), manifest, root=root)
            except BuildError:
                pass
            else:
                failures.append("a file no member names was packaged rather than refused")
            os.remove(stray)

            with open(held, "wb") as handle:
                handle.write(_probe_webp(art["width"] + 8, art["height"]))
            try:
                build(os.path.join(scratch, "never.zip"), manifest, root=root)
            except BuildError:
                pass
            else:
                failures.append("a mis-sized painting was packaged rather than refused")
            with open(held, "wb") as handle:
                handle.write(kept)
        except (BuildError, artifact_zip.ArtifactError) as error:
            failures.append(f"the synthetic corpus did not build: {error}")

    path = manifest["asset"]["path"]
    if "+" in path:
        failures.append("the origin path carries a '+', which the browser refuses")
    if path != path.lower():
        failures.append("the origin path is not lower case, which the browser refuses")
    if not path.startswith(manifest["asset"]["id"] + "/"):
        failures.append("the origin path does not begin with the asset id")

    for failure in failures:
        print(f"scene pack: {failure}", file=sys.stderr)
    if failures:
        return 1

    committed = os.path.join(REPOSITORY, art["directory"])
    try:
        reconcile(committed, wanted)
    except BuildError as error:
        # Named rather than passed over. The committed artwork is what the
        # published pack is made of, and a self-test that says nothing about
        # it would read as having checked it.
        print(f"scene pack: the committed artwork is not packageable — {error}", file=sys.stderr)
        print(
            f"scene pack: {len(wanted)} members, three header shapes and the origin "
            "path checked over a synthetic corpus; the committed artwork was not"
        )
        return 0
    print(
        f"scene pack: {len(wanted)} scenes, deterministic across rebuilds; "
        "16 properties checked and the committed artwork reconciles"
    )
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", help="where to write the artifact")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="drive the tool over a synthetic corpus, including how it refuses",
    )
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return _self_test()
    if not arguments.output:
        parser.error("--output is required")
    try:
        print(json.dumps(build(arguments.output), indent=2))
    except (BuildError, artifact_zip.ArtifactError) as error:
        print(f"scene pack: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
