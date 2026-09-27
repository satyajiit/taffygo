#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Check the committed listing images against Google Play's published rules.

The images are the half of a store listing no text gate can see, and they are
produced by a renderer that lives outside this repository. So the repository
checks the artifact rather than trusting the tool that made it: every rule here
is one Play publishes and enforces at upload time, and finding a 1024x501
feature graphic on this machine costs a second, while finding it at upload
costs a round trip through a rejection.

It reads PNG and JPEG headers by hand rather than importing an image library,
because this repository installs nothing at run time and a lane that needs
Pillow is a lane that skips on most hosts. A PNG's IHDR is the first chunk
after the signature and carries width, height, bit depth and colour type in a
fixed 13-byte layout; a JPEG's dimensions come from its first SOFn marker.

Exit codes: 0 = clean, 1 = a finding, 2 = it could not run.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

# tools/lib/play_images.py -> tools/lib -> tools -> the repository root. Three
# dirnames, not two: two lands on tools/ and every path below resolves under it,
# which reads as "no images are committed" rather than as a wrong root.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
METADATA = os.path.join("tools", "play.d", "metadata", "android")

#: Play's published limits, per asset kind.
#:
#: `count` is (minimum, maximum) files. `exact` pins a size Play requires to the
#: pixel. `bounds` is the (min, max) length of a side. `ratio` is the permitted
#: range of long-side / short-side. `alpha` demands a real alpha channel, which
#: is what Play means by "32-bit PNG" for the icon.
RULES = {
    "phoneScreenshots": {
        "count": (2, 8),
        "bounds": (320, 3840),
        "ratio": (1.0, 2.4),
        "alpha": False,
    },
    "sevenInchScreenshots": {"count": (0, 8), "bounds": (320, 3840), "ratio": (1.0, 2.4), "alpha": False},
    "tenInchScreenshots": {"count": (0, 8), "bounds": (320, 3840), "ratio": (1.0, 2.4), "alpha": False},
    "featureGraphic": {"count": (1, 1), "exact": (1024, 500), "alpha": False},
    "icon": {"count": (1, 1), "exact": (512, 512), "alpha": True},
    "promoGraphic": {"count": (0, 1), "exact": (180, 120), "alpha": False},
    "tvBanner": {"count": (0, 1), "exact": (1280, 720), "alpha": False},
}

#: Play rejects an image file over 15MB, and is markedly happier under 8.
MAX_BYTES = 15 * 1024 * 1024
WARN_BYTES = 8 * 1024 * 1024

#: A single file under `images/`, rather than a directory, names its own kind.
SINGLE = {"featureGraphic", "icon", "promoGraphic", "tvBanner"}

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
#: PNG colour types that carry an alpha channel (greyscale+alpha, truecolour+alpha).
PNG_ALPHA_TYPES = {4, 6}


def read_png(head: bytes):
    """Return (width, height, has_alpha) for a PNG, or None if it is not one."""
    if not head.startswith(PNG_SIGNATURE):
        return None
    # 8-byte signature, then a chunk: 4-byte length, 4-byte type, then IHDR's
    # 13 bytes — width, height, bit depth, colour type, and three more.
    if len(head) < 33 or head[12:16] != b"IHDR":
        return None
    width, height = struct.unpack(">II", head[16:24])
    colour_type = head[25]
    return width, height, colour_type in PNG_ALPHA_TYPES


def read_jpeg(path: str):
    """Return (width, height, False) for a JPEG, or None if it is not one."""
    with open(path, "rb") as handle:
        if handle.read(2) != b"\xff\xd8":
            return None
        while True:
            marker = handle.read(2)
            if len(marker) < 2 or marker[0] != 0xFF:
                return None
            kind = marker[1]
            # SOF0..SOF15, excluding the four that are not frame headers.
            if 0xC0 <= kind <= 0xCF and kind not in (0xC4, 0xC8, 0xCC):
                handle.read(3)  # segment length, then sample precision
                height, width = struct.unpack(">HH", handle.read(4))
                return width, height, False
            length = struct.unpack(">H", handle.read(2))[0]
            handle.seek(length - 2, os.SEEK_CUR)


def measure(path: str):
    """Return (width, height, has_alpha, kind) or None when it is neither."""
    with open(path, "rb") as handle:
        head = handle.read(64)
    png = read_png(head)
    if png:
        return png + ("png",)
    jpeg = read_jpeg(path)
    if jpeg:
        return jpeg + ("jpeg",)
    return None


def collect(base: str) -> dict:
    """{locale: {kind: [paths]}} for every locale that has an images directory."""
    found = {}
    if not os.path.isdir(base):
        return found
    for locale in sorted(os.listdir(base)):
        images = os.path.join(base, locale, "images")
        if not os.path.isdir(images):
            continue
        kinds = {}
        for entry in sorted(os.listdir(images)):
            full = os.path.join(images, entry)
            if os.path.isdir(full):
                kinds[entry] = [
                    os.path.join(full, f) for f in sorted(os.listdir(full))
                    if not f.startswith(".")
                ]
            elif not entry.startswith("."):
                kinds[os.path.splitext(entry)[0]] = [full]
        if kinds:
            found[locale] = kinds
    return found


def check_one(kind: str, paths: list, rel) -> list:
    findings = []
    rule = RULES.get(kind)
    if rule is None:
        findings.append(f"{kind}: not an asset kind Play accepts")
        return findings

    low, high = rule["count"]
    if not low <= len(paths) <= high:
        findings.append(
            f"{kind}: {len(paths)} file(s); Play accepts "
            + (f"exactly {low}" if low == high else f"{low} to {high}")
        )

    for path in paths:
        name = rel(path)
        size = os.path.getsize(path)
        if size > MAX_BYTES:
            findings.append(f"{name}: {size / 1e6:.1f}MB, over Play's 15MB limit")
        measured = measure(path)
        if measured is None:
            findings.append(f"{name}: not a PNG or JPEG; Play accepts no other format")
            continue
        width, height, has_alpha, _fmt = measured

        exact = rule.get("exact")
        if exact and (width, height) != exact:
            findings.append(
                f"{name}: {width}x{height}; Play requires exactly {exact[0]}x{exact[1]}"
            )
        bounds = rule.get("bounds")
        if bounds:
            for side, value in (("width", width), ("height", height)):
                if not bounds[0] <= value <= bounds[1]:
                    findings.append(
                        f"{name}: {side} {value}px is outside Play's "
                        f"{bounds[0]}-{bounds[1]}px range"
                    )
        ratio_rule = rule.get("ratio")
        if ratio_rule and width and height:
            ratio = max(width, height) / min(width, height)
            if not ratio_rule[0] <= ratio <= ratio_rule[1]:
                findings.append(
                    f"{name}: side ratio {ratio:.2f}:1 is outside Play's "
                    f"{ratio_rule[0]:.2f}-{ratio_rule[1]:.2f} range"
                )
        if rule.get("alpha") and not has_alpha:
            findings.append(
                f"{name}: no alpha channel; Play requires a 32-bit PNG for this asset"
            )
    return findings


def check(root: str) -> list:
    base = os.path.join(root, METADATA)
    found = collect(base)
    if not found:
        return ["no listing images are committed; Play requires an icon, a "
                "feature graphic and at least two phone screenshots"]

    def rel(path):
        return os.path.relpath(path, root)

    findings = []
    for locale, kinds in found.items():
        for required in ("phoneScreenshots", "featureGraphic", "icon"):
            if required not in kinds:
                findings.append(f"{locale}: no {required}")
        for kind, paths in sorted(kinds.items()):
            findings.extend(check_one(kind, paths, rel))
    return findings


def summary(root: str) -> str:
    found = collect(os.path.join(root, METADATA))
    parts = []
    for locale, kinds in found.items():
        n = sum(len(v) for v in kinds.values())
        parts.append(f"{locale}: {n} image(s)")
    return "; ".join(parts) if parts else "no images"


def self_test() -> int:
    """Every rule must fire, and a correct set must produce no finding."""
    import tempfile
    import zlib

    def png(path, w, h, colour_type=2):
        depth = 8
        ihdr = struct.pack(">IIBBBBB", w, h, depth, colour_type, 0, 0, 0)
        def chunk(tag, body):
            return (struct.pack(">I", len(body)) + tag + body
                    + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))
        with open(path, "wb") as handle:
            handle.write(PNG_SIGNATURE + chunk(b"IHDR", ihdr)
                         + chunk(b"IDAT", zlib.compress(b"\0" * (w * h)))
                         + chunk(b"IEND", b""))

    failures = 0

    def expect(label, findings, wanted):
        nonlocal failures
        if wanted:
            if not any(wanted in f for f in findings):
                failures += 1
                print(f"  fail {label}: no finding mentioning {wanted!r} "
                      f"(got {findings})", file=sys.stderr)
        elif findings:
            failures += 1
            print(f"  fail {label}: expected none, got {findings}", file=sys.stderr)

    with tempfile.TemporaryDirectory() as root:
        images = os.path.join(root, METADATA, "en-GB", "images")
        shots = os.path.join(images, "phoneScreenshots")
        os.makedirs(shots)

        for i in range(3):
            png(os.path.join(shots, f"{i}.png"), 1080, 1920)
        png(os.path.join(images, "featureGraphic.png"), 1024, 500)
        png(os.path.join(images, "icon.png"), 512, 512, colour_type=6)
        expect("a correct set", check(root), None)

        # A feature graphic Play would reject to the pixel.
        png(os.path.join(images, "featureGraphic.png"), 1024, 501)
        expect("feature graphic size", check(root), "exactly 1024x500")
        png(os.path.join(images, "featureGraphic.png"), 1024, 500)

        # "32-bit PNG" means the alpha channel has to be there.
        png(os.path.join(images, "icon.png"), 512, 512, colour_type=2)
        expect("icon alpha", check(root), "no alpha channel")
        png(os.path.join(images, "icon.png"), 512, 512, colour_type=6)

        # One screenshot is below Play's minimum of two.
        for i in range(1, 3):
            os.remove(os.path.join(shots, f"{i}.png"))
        expect("too few screenshots", check(root), "Play accepts 2 to 8")
        for i in range(1, 3):
            png(os.path.join(shots, f"{i}.png"), 1080, 1920)

        # A side outside Play's range, and a ratio outside it.
        png(os.path.join(shots, "0.png"), 200, 356)
        expect("side too small", check(root), "outside Play's 320-3840px range")
        png(os.path.join(shots, "0.png"), 400, 1600)
        expect("ratio too extreme", check(root), "outside Play's 1.00-2.40 range")
        png(os.path.join(shots, "0.png"), 1080, 1920)

        # A file that is not an image at all.
        with open(os.path.join(shots, "0.png"), "wb") as handle:
            handle.write(b"not a png")
        expect("not an image", check(root), "not a PNG or JPEG")
        png(os.path.join(shots, "0.png"), 1080, 1920)

        # And the required kinds are required.
        os.remove(os.path.join(images, "icon.png"))
        expect("missing icon", check(root), "no icon")

    if failures:
        print(f"play images self-test: {failures} case(s) failed", file=sys.stderr)
        return 1
    print("play images self-test: 8 cases passed")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=REPO_ROOT)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    findings = check(args.root)
    if findings:
        for finding in findings:
            print(f"play images: {finding}", file=sys.stderr)
        return 1
    print(f"play images: {summary(args.root)} match Play's published rules")
    return 0


if __name__ == "__main__":
    sys.exit(main())
