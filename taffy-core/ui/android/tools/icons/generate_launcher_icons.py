#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Derive the UI host launcher icons and in-app brand marks from `brand/`.

The UI host needs raster icons for three different jobs, and all three start
from the same committed masters in `brand/png/`:

  * the adaptive launcher icon's foreground and themed layers, which are
    108 dp squares with the mark centred inside the safe zone;
  * the platform splash window's icon, which is a 288 dp square the platform
    masks to a centred 192 dp circle, so the mark is sized to the largest span
    at which no pixel of it reaches that circle — a different padding rule
    again, and one measured from the masters rather than assumed;
  * the in-app brand mark the first-run screens draw, which carries no
    safe-zone padding at all — padding a screen asset would silently shrink
    the mark to two thirds of the size the caller asked for.

The splash sets are the one pair chosen by a resource qualifier rather than by
the app: the platform draws that window before any of this code runs, so the
night variant is picked from the system configuration and there is nothing
here that could consult the stored Appearance choice instead.

Why the outputs are committed rather than derived at build time, unlike the
overlay's `branding/tools/generate_icons.py`: the Android Gradle Plugin has no
step this generator could hang off, so a build-time derivation would mean
either a Gradle task nobody can run without Python or an icon that silently
goes stale. Committing the bytes plus a checksum file makes drift a gate
finding instead. `manifest.json` records the master digests, so replacing a
mark fails loudly here instead of shipping the old artwork under the new name.

Modes, mutually exclusive and one required:

  --generate   re-derive every asset and rewrite `checksums.sha256`
  --check      verify the committed set against the manifest and checksums
  --list       print the manifest's output paths, one per line

`--generate` needs `cwebp`, and refuses to run unless its version is the one
`manifest.json` pins: WebP encoders are not byte-compatible across releases,
and a committed checksum is only meaningful if one encoder wrote it.

`--check` needs no encoder and no image library. It re-reads the master
digests, the committed digests, and the pixel dimensions in each file's own
VP8L bitstream header — which is what proves a file is the density it is
filed under, rather than a correctly named copy of another density. That makes
it runnable on a documentation-only host, which is what the `icons` fast lane
needs.

Exit status: 0 clean, 1 on any finding. Stdlib only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))

# The PNG codec and repository-root walk belong to the shared branding
# pipeline. Import them rather than copying them: two icon pipelines that
# disagree about how a master is decoded would produce different marks from
# one file. The path below only has to be good enough to import; `repo_root`
# then validates the root by its markers.
sys.path.insert(
    0,
    os.path.join(
        os.path.normpath(os.path.join(_HERE, os.pardir, os.pardir, os.pardir, os.pardir)),
        "resources", "branding", "tools",
    ),
)

import png_io  # noqa: E402  (path set above so the shared module resolves)
import repo_root  # noqa: E402

MANIFEST = os.path.join(_HERE, "manifest.json")


class Finding(Exception):
    """Something the caller must fix; never worked around."""


# --- the manifest -----------------------------------------------------------


def load_manifest(path: str) -> dict:
    with open(path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    if manifest.get("manifest_version") != 1:
        raise Finding(
            f"{path}: manifest_version {manifest.get('manifest_version')!r} "
            "is not supported by this script"
        )
    for key in ("encoder", "checksums", "sets"):
        if key not in manifest:
            raise Finding(f"{path}: missing required key {key!r}")
    if not manifest["sets"]:
        raise Finding(f"{path}: the set list is empty")
    return manifest


def outputs(manifest: dict) -> list[str]:
    """Every output path the manifest declares, in manifest order."""
    paths = [asset["output"] for group in manifest["sets"] for asset in group["assets"]]
    if len(set(paths)) != len(paths):
        raise Finding("manifest.json declares the same output path twice")
    return paths


def checksums_path(manifest: dict) -> str:
    return os.path.join(_HERE, manifest["checksums"])


def sha256_file(path: str) -> str:
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def verify_source(root: str, group: dict) -> str:
    """Confirm a master is the one the manifest was written against."""
    source = os.path.join(root, group["source_asset"])
    if not os.path.exists(source):
        raise Finding(f"missing brand master: {group['source_asset']} (looked under {root})")
    digest = sha256_file(source)
    if digest != group["source_sha256"]:
        raise Finding(
            f"{group['source_asset']} has changed.\n"
            f"  manifest: {group['source_sha256']}\n"
            f"  on disk:  {digest}\n"
            "The brand master is the input to every derived icon. Update"
            " source_sha256 in this directory's manifest.json in the same change"
            " that updates the artwork, then re-run with --generate."
        )
    return source


# --- rendering --------------------------------------------------------------


def opaque_bounds(image: png_io.Image) -> tuple[int, int, int, int]:
    """The smallest box holding every pixel that is not fully transparent.

    The masters are 4096 px canvases with the mark floating in the middle, so
    the margin is the master's, not a design decision. Cropping it away first
    is what makes `mark_px` in the manifest mean the mark rather than the
    master's framing.
    """
    left, top, right, bottom = image.width, image.height, -1, -1
    for y in range(image.height):
        row = image.pixels[y * image.width * 4 : (y + 1) * image.width * 4]
        opacity = row[3::4]
        first = next((x for x in range(image.width) if opacity[x]), None)
        if first is None:
            continue
        last = image.width - 1
        while not opacity[last]:
            last -= 1
        top = min(top, y)
        bottom = y
        left = min(left, first)
        right = max(right, last)
    if right < 0:
        raise Finding("the master is fully transparent: nothing to derive")
    return left, top, right - left + 1, bottom - top + 1


def fit(width: int, height: int, span: int) -> tuple[int, int]:
    """Scale width x height so its longest side is exactly `span`.

    Integer arithmetic with explicit rounding, so the result cannot depend on
    a host's floating-point mood — a committed checksum has to be reproducible.
    """
    if width >= height:
        return span, max(1, (height * span * 2 + width) // (width * 2))
    return max(1, (width * span * 2 + height) // (height * 2)), span


def place(mark: png_io.Image, canvas_width_px: int, canvas_height_px: int) -> png_io.Image:
    """Centre `mark` on a transparent canvas.

    A straight copy, not `png_io.Image.paste`: compositing over transparent
    black multiplies each colour channel by its own opacity, which is exactly
    the dark halo `png_io.resize` takes care to avoid. The destination is empty
    and the regions do not overlap, so copying the rows is both correct and the
    only thing that preserves the resampler's output byte for byte.
    """
    canvas = png_io.Image(
        canvas_width_px,
        canvas_height_px,
        bytearray(canvas_width_px * canvas_height_px * 4),
    )
    left = (canvas_width_px - mark.width) // 2
    top = (canvas_height_px - mark.height) // 2
    for y in range(mark.height):
        source = y * mark.width * 4
        destination = ((top + y) * canvas_width_px + left) * 4
        canvas.pixels[destination : destination + mark.width * 4] = mark.pixels[
            source : source + mark.width * 4
        ]
    return canvas


def asset_size(asset: dict) -> tuple[int, int]:
    """Read a square legacy size or an explicit rectangular canvas."""
    if "size_px" in asset:
        size = asset["size_px"]
        if not isinstance(size, int) or size <= 0:
            raise Finding(f"{asset.get('output')}: size_px must be a positive integer")
        return size, size
    width = asset.get("width_px")
    height = asset.get("height_px")
    if not isinstance(width, int) or width <= 0:
        raise Finding(f"{asset.get('output')}: width_px must be a positive integer")
    if not isinstance(height, int) or height <= 0:
        raise Finding(f"{asset.get('output')}: height_px must be a positive integer")
    return width, height


def render(cropped: png_io.Image, asset: dict) -> png_io.Image:
    for key in ("mark_px",):
        value = asset.get(key)
        if not isinstance(value, int) or value <= 0:
            raise Finding(f"{asset.get('output')}: {key} must be a positive integer")
    canvas_width, canvas_height = asset_size(asset)
    width, height = fit(cropped.width, cropped.height, asset["mark_px"])
    if width > canvas_width or height > canvas_height:
        raise Finding(f"{asset.get('output')}: the derived mark does not fit its canvas")
    return place(png_io.resize(cropped, width, height), canvas_width, canvas_height)


def master_for(root: str, group: dict, cache: dict) -> png_io.Image:
    """The master this set derives from: decoded once, cropped once, reused."""
    key = (group["source_asset"], group["transform"])
    if key not in cache:
        source = verify_source(root, group)
        image = png_io.read(source)
        left, top, width, height = opaque_bounds(image)
        image = image.crop(left, top, width, height)
        if group["transform"] == "monochrome":
            image = image.to_monochrome_alpha()
        elif group["transform"] != "color":
            raise Finding(f"{group['id']}: unknown transform {group['transform']!r}")
        cache[key] = image
    return cache[key]


# --- the encoder ------------------------------------------------------------


def encoder_version() -> str:
    result = subprocess.run(
        ["cwebp", "-version"], capture_output=True, text=True, check=False
    )
    if result.returncode != 0:
        raise Finding(f"`cwebp -version` exited {result.returncode}: {result.stderr.strip()}")
    return result.stdout.strip().splitlines()[0].strip()


def require_encoder(manifest: dict) -> str:
    """Refuse to generate with anything but the pinned encoder.

    TOOLCHAIN.md carries this pin and `manifest.json` is its machine owner, so
    the two are checked against each other by the `pins` lane. Here it stops a
    second encoder from rewriting committed bytes that nothing visibly changed.
    """
    pinned = manifest["encoder"].get("cwebp")
    if not pinned:
        raise Finding("manifest.json records no cwebp version to pin against")
    if not shutil.which("cwebp"):
        raise Finding(
            "cwebp is not on PATH. --generate needs the pinned encoder "
            f"({pinned}); install libwebp, or run --check, which needs none."
        )
    found = encoder_version()
    if found != pinned:
        raise Finding(
            f"cwebp {found} is on PATH but manifest.json pins {pinned}. "
            "WebP encoders are not byte-compatible across releases, so the "
            "committed checksums would change for no reviewable reason. "
            "Install the pinned version, or move the pin in manifest.json and "
            "TOOLCHAIN.md together and regenerate the whole set."
        )
    return found


def encode_webp(manifest: dict, image: png_io.Image, target: str, scratch: str) -> None:
    staged = os.path.join(scratch, "frame.png")
    png_io.write(staged, image)
    command = ["cwebp", *manifest["encoder"]["arguments"], staged, "-o", target]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise Finding(f"`{' '.join(command)}` exited {result.returncode}: {result.stderr.strip()}")


# --- reading a committed WebP -----------------------------------------------


def webp_dimensions(path: str) -> tuple[int, int]:
    """The pixel size in a simple lossless WebP's own bitstream header.

    A RIFF container holding one VP8L chunk, which is what the pinned encoder
    writes for a lossless file with no metadata. The header is five bytes: a
    0x2F signature, then 14 bits of width-1 and 14 bits of height-1, little
    endian. Anything else is reported rather than guessed at, because a file
    this reader cannot vouch for is a file the gate is not checking.
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WEBP":
        raise Finding(f"{path}: not a RIFF/WEBP file")
    position = 12
    while position + 8 <= len(data):
        kind = data[position : position + 4]
        (length,) = struct.unpack("<I", data[position + 4 : position + 8])
        payload = data[position + 8 : position + 8 + length]
        if kind == b"VP8L":
            if len(payload) < 5 or payload[0] != 0x2F:
                raise Finding(f"{path}: the VP8L chunk has no valid bitstream header")
            bits = int.from_bytes(payload[1:5], "little")
            return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
        position += 8 + length + (length & 1)
    raise Finding(
        f"{path}: no VP8L chunk. The pinned encoder writes simple lossless "
        "files; a lossy or extended-format file did not come from --generate."
    )


def read_checksums(path: str) -> dict[str, str]:
    """A plain `sha256sum` file: one `<digest>  <path>` line per output.

    No header and no comments, so `sha256sum -c` reads it unaided from the
    repository root. A checksum file only this script can read would be one
    more thing to take on trust.
    """
    if not os.path.exists(path):
        raise Finding(f"{path} is missing. Run --generate and commit the result.")
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().splitlines()
    digests: dict[str, str] = {}
    for number, line in enumerate(lines, start=1):
        if not line.strip():
            continue
        parts = line.split("  ", 1)
        if len(parts) != 2 or len(parts[0]) != 64:
            raise Finding(f"{path}:{number}: not a `<sha256>  <path>` line")
        digests[parts[1]] = parts[0]
    return digests


# --- modes ------------------------------------------------------------------


def generate(root: str, manifest: dict) -> int:
    version = require_encoder(manifest)
    print(f"cwebp {version} (pinned by manifest.json)")
    cache: dict = {}
    lines = []
    with tempfile.TemporaryDirectory(prefix="taffy-icons-") as scratch:
        for group in manifest["sets"]:
            cropped = master_for(root, group, cache)
            for asset in group["assets"]:
                target = os.path.join(root, asset["output"])
                os.makedirs(os.path.dirname(target), exist_ok=True)
                encode_webp(manifest, render(cropped, asset), target, scratch)
                lines.append(f"{sha256_file(target)}  {asset['output']}")
                width, height = asset_size(asset)
                print(f"wrote {asset['output']} ({width}x{height}px, {asset['density']})")
    with open(checksums_path(manifest), "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print(f"\nwrote {len(lines)} asset(s) and {manifest['checksums']}")
    return 0


def check(root: str, manifest: dict) -> int:
    findings = 0
    for group in manifest["sets"]:
        verify_source(root, group)
    declared = outputs(manifest)
    recorded = read_checksums(checksums_path(manifest))
    for stale in sorted(set(recorded) - set(declared)):
        print(f"{manifest['checksums']}: records {stale}, which the manifest does not declare")
        findings += 1
    for asset in (asset for group in manifest["sets"] for asset in group["assets"]):
        path = asset["output"]
        expected_size = asset_size(asset)
        target = os.path.join(root, path)
        if path not in recorded:
            print(f"{path}: declared by the manifest but absent from {manifest['checksums']}")
            findings += 1
            continue
        if not os.path.exists(target):
            print(f"{path}: missing. Run --generate on a host with the pinned cwebp.")
            findings += 1
            continue
        digest = sha256_file(target)
        if digest != recorded[path]:
            print(f"{path}: sha256 {digest}, {manifest['checksums']} records {recorded[path]}")
            findings += 1
            continue
        width, height = webp_dimensions(target)
        if (width, height) != expected_size:
            print(
                f"{path}: the file is {width}x{height}, the manifest declares "
                f"{expected_size[0]}x{expected_size[1]}"
            )
            findings += 1
    print()
    print(
        f"checked {len(declared)} committed icon(s) against "
        f"{len(manifest['sets'])} brand master(s), {findings} finding(s)"
    )
    return 1 if findings else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--generate", action="store_true", help="re-derive the committed set")
    mode.add_argument("--check", action="store_true", help="verify the committed set")
    mode.add_argument("--list", action="store_true", help="print output paths")
    parser.add_argument("--repo-root", help="override the repository root")
    parser.add_argument("--manifest", default=MANIFEST, help="icon manifest to read")
    args = parser.parse_args(argv)

    try:
        manifest = load_manifest(args.manifest)
        if args.list:
            for path in outputs(manifest):
                print(path)
            return 0
        root = repo_root.resolve(args.repo_root) if args.repo_root else repo_root.find(__file__)
        return generate(root, manifest) if args.generate else check(root, manifest)
    except (Finding, repo_root.RepoRootError, png_io.PngError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
