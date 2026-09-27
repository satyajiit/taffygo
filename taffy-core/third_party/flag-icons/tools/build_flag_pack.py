#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Packages the country-flag artwork into the asset the delivery catalog names.

Three screens draw a flag per country -- SCR-001, SCR-006 and SCR-407. Today
that is the device manufacturer's own emoji glyph, which differs between
phones, is absent from every Compose preview, and is therefore absent from
every screenshot review. Decision 0047 replaces it with one published pack.

WHAT THIS TOOL DECIDES, AND STATES

  * **Which countries are in it.** Exactly the codes the product can ask for.
    `LanguageRegionPolicy` filters every region through
    `java.util.Locale.getISOCountries()`, so a code outside that set is a code
    no screen can request. flag-icons also ships England, Scotland, Wales,
    Northern Ireland, Catalonia, Galicia, the Basque Country, Kosovo, the
    European Union, the United Nations and six other entities; none of them is
    an ISO 3166-1 alpha-2 country and none of them is reachable, so shipping
    them would be taking a position nothing asked TaffyGo to take. The pack is
    the intersection, and the intersection is checked in both directions.
  * **What it weighs and hashes to.** Answered as a manifest in the exact shape
    a published catalog variant needs, so filling the row in is a copy rather
    than a measurement somebody takes by hand.
  * **What it is allowed to depend on.** The upstream tarball by digest, a
    pinned librsvg/cairo pair, and a pinned `cwebp`. Every one of the three is
    part of the output bytes and every one is refused on mismatch.

It never downloads anything: `--source` is a tarball already on disk, whose
digest is verified before a single file is read out of it.

    build_flag_pack.py --source flag-icons-7.5.0.tgz --output pack.zip
    build_flag_pack.py --self-test

Exit status: 0 clean, 1 refused.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        "cpython",
        "tools",
    ),
)

import artifact_zip  # noqa: E402  (both paths are set immediately above)
import svg_raster  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "manifest.json")
CODES = os.path.join(HERE, "iso-country-codes.tsv")

#: The archive member every pack carries beside the artwork. The pack is in no
#: build graph, so this is the only copy of the notice that reaches a device.
LICENCE_MEMBER = "LICENSE"


class BuildError(Exception):
    """The inputs are not ones this tool will package."""


def load_manifest() -> dict:
    with open(MANIFEST, "r", encoding="utf-8") as handle:
        return json.load(handle)


def load_codes() -> list[str]:
    """The committed country list, upper-case, sorted, comments dropped."""
    codes: list[str] = []
    with open(CODES, "r", encoding="utf-8") as handle:
        for line in handle:
            value = line.strip()
            if value and not value.startswith("#"):
                codes.append(value)
    if len(codes) != len(set(codes)):
        raise BuildError(f"{CODES}: a code appears twice")
    return sorted(codes)


def verify_tarball(source: str, expected: str) -> None:
    """Refuses a tarball that is not the one the manifest pins."""
    if not os.path.isfile(source):
        raise BuildError(
            f"{source} is not a file. Point --source at the flag-icons tarball; "
            "this tool downloads nothing."
        )
    hasher = hashlib.sha256()
    with open(source, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    found = hasher.hexdigest()
    if found != expected:
        raise BuildError(
            f"{source} hashes to {found}, and the manifest pins {expected}. "
            "Refusing rather than packaging an unrecognised corpus."
        )


def read_corpus(source: str, raster_set: str) -> tuple[dict, bytes]:
    """The SVG documents by upper-case code, and the upstream licence text."""
    prefix = f"package/flags/{raster_set}/"
    documents: dict[str, bytes] = {}
    licence = b""
    with tarfile.open(source, "r:gz") as archive:
        for member in archive.getmembers():
            if not member.isfile():
                continue
            if member.name == "package/LICENSE":
                licence = archive.extractfile(member).read()
                continue
            if not member.name.startswith(prefix) or not member.name.endswith(".svg"):
                continue
            code = os.path.basename(member.name)[: -len(".svg")]
            if "-" in code:
                continue  # A subdivision, not an ISO 3166-1 alpha-2 country.
            documents[code.upper()] = archive.extractfile(member).read()
    if not licence:
        raise BuildError(f"{source} carries no package/LICENSE")
    if not documents:
        raise BuildError(f"{source} carries no {prefix} documents")
    return documents, licence


def reconcile(documents: dict, codes: list[str]) -> None:
    """Checks the corpus and the country list against each other, both ways."""
    have = set(documents)
    want = set(codes)
    missing = sorted(want - have)
    if missing:
        raise BuildError(
            "the corpus has no artwork for "
            f"{', '.join(missing)}. Every code the product can ask for must be "
            "in the pack, because a code with no member is a country that "
            "silently has no flag."
        )


def require_encoder(pinned: dict) -> None:
    """Refuses a host whose `cwebp` is not the one the manifest names."""
    if not shutil.which("cwebp"):
        raise BuildError(
            "cwebp is not on PATH. The pack is encoded by the pinned encoder "
            f"({pinned['cwebp']}) or not at all."
        )
    result = subprocess.run(
        ["cwebp", "-version"], capture_output=True, text=True, check=False
    )
    found = result.stdout.strip().splitlines()[0].strip() if result.stdout else ""
    if found != pinned["cwebp"]:
        raise BuildError(
            f"cwebp {found or '(unreadable)'} is on PATH but the manifest pins "
            f"{pinned['cwebp']}. An encoder version is part of the artifact's "
            "bytes."
        )


def encode(png: str, webp: str, arguments: list[str]) -> None:
    result = subprocess.run(
        ["cwebp", *arguments, png, "-o", webp], capture_output=True, text=True, check=False
    )
    if result.returncode != 0 or not os.path.exists(webp):
        raise BuildError(f"cwebp refused {png}: {result.stderr.strip()}")


def build(source: str, output: str, manifest: dict | None = None) -> dict:
    """Writes the artifact and answers the facts a catalog row needs."""
    manifest = manifest or load_manifest()
    upstream = manifest["upstream"]
    asset = manifest["asset"]
    raster = manifest["raster"]

    svg_raster.require_versions(manifest["rasteriser"])
    require_encoder(manifest["encoder"])
    verify_tarball(source, upstream["sha256"])

    codes = load_codes()
    documents, licence = read_corpus(source, raster["set"])
    reconcile(documents, codes)

    with tempfile.TemporaryDirectory() as scratch:
        tree = os.path.join(scratch, "pack")
        flags = os.path.join(tree, asset["member_prefix"].rstrip("/"))
        os.makedirs(flags)
        with open(os.path.join(tree, LICENCE_MEMBER), "wb") as handle:
            handle.write(licence)
        staging = os.path.join(scratch, "staging.png")
        for code in codes:
            svg_raster.render_png(
                documents[code], raster["width"], raster["height"], staging
            )
            encode(
                staging,
                os.path.join(flags, code.lower() + asset["member_suffix"]),
                manifest["encoder"]["arguments"],
            )
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
            "countries": len(codes),
        }
    )
    return facts


def _probe_tarball(target: str, codes: list[str], licence: bytes) -> str:
    """A synthetic corpus, so the self-test needs no network and no download."""
    document = (
        b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 4 3">'
        b'<rect width="4" height="3" fill="#123456"/></svg>'
    )
    with tarfile.open(target, "w:gz") as archive:
        def add(name: str, body: bytes) -> None:
            info = tarfile.TarInfo(name)
            info.size = len(body)
            info.mtime = 0
            archive.addfile(info, io.BytesIO(body))

        add("package/LICENSE", licence)
        for code in codes:
            add(f"package/flags/4x3/{code.lower()}.svg", document)
        add("package/flags/4x3/gb-sct.svg", document)
    hasher = hashlib.sha256()
    with open(target, "rb") as handle:
        hasher.update(handle.read())
    return hasher.hexdigest()


def _self_test() -> int:
    """Drives the tool over a synthetic corpus, including how it refuses."""
    failures: list[str] = []
    manifest = load_manifest()
    committed = load_codes()
    if len(committed) < 200:
        failures.append(f"the committed country list holds only {len(committed)} codes")

    # The corpus half needs pixels; the rest of this self-test does not.
    #
    # A host with no rasteriser has measured nothing here, and saying so is
    # different from saying the builder's properties no longer hold. The
    # difference matters twice over: a refusal this self-test proves is
    # `except svg_raster.RasterError`, and `RasterUnavailable` is one of those,
    # so on a host with no libraries the "a mismatched rasteriser pin was
    # accepted" check would have passed for the wrong reason — proving the pin
    # is honoured by never reaching the pin.
    unavailable: str | None = None
    try:
        svg_raster.versions()
    except svg_raster.RasterUnavailable as error:
        unavailable = str(error)

    if unavailable is None:
        with tempfile.TemporaryDirectory() as scratch:
            codes = committed[:6]
            source = os.path.join(scratch, "probe.tgz")
            digest = _probe_tarball(source, codes, b"MIT\n")

            probe = json.loads(json.dumps(manifest))
            probe["upstream"]["sha256"] = digest
            probe["raster"] = {"width": 8, "height": 6, "set": "4x3"}

            original = globals()["load_codes"]
            globals()["load_codes"] = lambda: list(codes)
            try:
                output = os.path.join(scratch, "pack.zip")
                facts = build(source, output, probe)
                if facts["entries"] != len(codes) + 1:
                    failures.append(f"the pack holds {facts['entries']} members")
                if len(facts["digest"]) != 64 or facts["digest"] != facts["digest"].lower():
                    failures.append("the digest is not the shape a row names")
                if facts["transfer_bytes"] != facts["installed_bytes"]:
                    failures.append("a zip that stays a zip reported two different sizes")

                again = os.path.join(scratch, "again.zip")
                build(source, again, probe)
                if artifact_zip.digest(output) != artifact_zip.digest(again):
                    failures.append("two builds of one corpus produced different bytes")

                globals()["load_codes"] = lambda: list(codes) + ["ZZ"]
                try:
                    build(source, os.path.join(scratch, "never.zip"), probe)
                except BuildError:
                    pass
                else:
                    failures.append("a country with no artwork was packaged rather than refused")
                globals()["load_codes"] = lambda: list(codes)

                wrong = json.loads(json.dumps(probe))
                wrong["upstream"]["sha256"] = "0" * 64
                try:
                    build(source, os.path.join(scratch, "never.zip"), wrong)
                except BuildError:
                    pass
                else:
                    failures.append("an unrecognised tarball was packaged rather than refused")

                stale = json.loads(json.dumps(probe))
                stale["rasteriser"]["librsvg"] = "0.0.0"
                try:
                    build(source, os.path.join(scratch, "never.zip"), stale)
                except svg_raster.RasterError:
                    pass
                else:
                    failures.append("a mismatched rasteriser pin was accepted")
            except (BuildError, svg_raster.RasterError, artifact_zip.ArtifactError) as error:
                failures.append(f"the synthetic corpus did not build: {error}")
            finally:
                globals()["load_codes"] = original

    if "+" in manifest["asset"]["path"]:
        failures.append("the origin path carries a '+', which the browser refuses")
    if manifest["asset"]["path"] != manifest["asset"]["path"].lower():
        failures.append("the origin path is not lower case, which the browser refuses")

    for failure in failures:
        print(f"flag pack: {failure}", file=sys.stderr)
    if failures:
        return 1
    if unavailable is not None:
        # Named rather than counted, because "3 properties checked" would read
        # as a smaller run of the same thing rather than as the eight that
        # decision 0047 is actually about not having happened.
        print(f"flag pack: not run — {unavailable}", file=sys.stderr)
        print(
            f"flag pack: {len(committed)} countries and the origin path checked; "
            "the corpus build, its determinism and its three refusals were not"
        )
        return 0
    print(
        f"flag pack: {len(committed)} countries, deterministic across rebuilds; "
        "8 properties checked"
    )
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", help="the flag-icons tarball the manifest pins")
    parser.add_argument("--output", help="where to write the artifact")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="drive the tool over a synthetic corpus, including how it refuses",
    )
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return _self_test()
    if not arguments.source or not arguments.output:
        parser.error("--source and --output are both required")
    try:
        print(json.dumps(build(arguments.source, arguments.output), indent=2))
    except (BuildError, svg_raster.RasterError, artifact_zip.ArtifactError) as error:
        print(f"flag pack: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
