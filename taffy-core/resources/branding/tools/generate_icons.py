#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Derive every TaffyGo raster brand asset from the committed marks.

No artwork is committed under `branding/`. Every raster asset is derived,
here, from the masters `brand/` already owns, at the geometries named in
`icons/manifest.json`. That keeps one master per treatment, makes a brand
change a one-file change, and keeps binary blobs that nobody can review out
of the overlay.

The manifest names two kinds of asset by `role`:

  launcher   TaffyGo's own resource names (taffygo_launcher), consumed by
             the :branding_resources target;
  override   upstream resource names (app_icon, layered_app_icon,
             fre_product_logo, ...), consumed by the :image_override_resources target
             with resource-overlay precedence, so the product packages no
             Chromium artwork (decision 0019; patch specification 0002
             records the mechanism).

Rendering per asset: the named source master is fitted into
`canvas_px * content_scale` preserving aspect, box-downscaled, optionally
reduced to a white-over-alpha mask (`monochrome_alpha`, for the themed-icon
and notification layers Android recolours), and centred on a transparent
canvas. An asset whose fit exactly fills its canvas is written as the plain
downscale, bit-for-bit the same output the version-1 manifest produced for
the launcher set.

Modes:

  --generate --out-dir DIR   write every asset in the manifest under DIR
  --check [--out-dir DIR]    verify DIR already holds byte-identical assets
  --list [--role ROLE]       print the manifest's output paths, one per line

`--check` is deterministic: `png_io.encode` writes filter-0 rows and a fixed
zlib level, and every geometry step is integer arithmetic, so the same
masters and the same manifest produce the same bytes on every host. A
mismatch therefore means a master changed or the manifest changed, never
that the encoder drifted.

Exit status: 0 clean, 1 on any finding. Stdlib only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import png_io  # noqa: E402  (path set above so the module resolves under GN)
import repo_root  # noqa: E402

MANIFEST = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "icons", "manifest.json"
)


class Finding(Exception):
    """Something the caller must fix; never worked around."""


def load_manifest(path: str) -> dict:
    with open(path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    if manifest.get("manifest_version") != 2:
        raise Finding(
            f"{path}: manifest_version "
            f"{manifest.get('manifest_version')!r} is not supported by this script"
        )
    for key in ("sources", "assets"):
        if key not in manifest:
            raise Finding(f"{path}: missing required key {key!r}")
    if not manifest["assets"]:
        raise Finding(f"{path}: the asset list is empty")
    for source_id, source in manifest["sources"].items():
        for key in ("asset", "sha256"):
            if key not in source:
                raise Finding(f"{path}: source {source_id!r} is missing {key!r}")
    for asset in manifest["assets"]:
        for key in ("id", "output", "canvas_px", "source", "role"):
            if key not in asset:
                raise Finding(f"{path}: asset {asset.get('id')!r} is missing {key!r}")
        if asset["source"] not in manifest["sources"]:
            raise Finding(
                f"{path}: asset {asset['id']!r} names unknown source "
                f"{asset['source']!r}"
            )
        if asset["role"] not in ("launcher", "override"):
            raise Finding(
                f"{path}: asset {asset['id']!r} has unknown role {asset['role']!r}"
            )
    return manifest


def verify_source(root: str, source_id: str, source: dict) -> str:
    """Confirm a master is the one the manifest was written against."""
    path = os.path.join(root, source["asset"])
    if not os.path.exists(path):
        raise Finding(
            f"missing brand master: {source['asset']} (looked under {root})"
        )
    with open(path, "rb") as handle:
        digest = hashlib.sha256(handle.read()).hexdigest()
    if digest != source["sha256"]:
        raise Finding(
            f"{source['asset']} has changed.\n"
            f"  manifest: {source['sha256']}\n"
            f"  on disk:  {digest}\n"
            "The brand masters are the input to every derived asset. Update"
            f" sources.{source_id}.sha256 in icons/manifest.json in the same"
            " change that updates the artwork, then re-run with --generate."
        )
    return path


def _blit_straight(canvas: png_io.Image, art: png_io.Image, left: int, top: int) -> None:
    """Copy `art` into `canvas` byte-for-byte, alpha included.

    The canvas is fully transparent, so a straight copy preserves the
    masters' straight-alpha convention; png_io's compositing paste() would
    premultiply partially transparent edge pixels against nothing.
    """
    for y in range(art.height):
        src = y * art.width * 4
        dst = ((top + y) * canvas.width + left) * 4
        canvas.pixels[dst : dst + art.width * 4] = art.pixels[src : src + art.width * 4]


def render(masters: dict, asset: dict, _cache: dict = {}) -> png_io.Image:  # noqa: B006
    canvas_w, canvas_h = asset["canvas_px"]
    if (
        not isinstance(canvas_w, int)
        or not isinstance(canvas_h, int)
        or canvas_w <= 0
        or canvas_h <= 0
    ):
        raise Finding(f"{asset.get('id')}: canvas_px must be positive integers")
    # content_scale is a JSON *string* ("0.611"), because GN's read_file json
    # conversion has no floating-point type and refuses the manifest outright
    # if a bare float appears anywhere in it.
    try:
        scale = float(asset.get("content_scale", "1.0"))
    except ValueError as error:
        raise Finding(f"{asset.get('id')}: content_scale: {error}") from None
    if not 0.0 < scale <= 1.0:
        raise Finding(f"{asset.get('id')}: content_scale must be in (0, 1]")

    master = masters[asset["source"]]
    content_w = int(canvas_w * scale)
    content_h = int(canvas_h * scale)
    fit = min(content_w / master.width, content_h / master.height)
    art_w = max(1, int(master.width * fit))
    art_h = max(1, int(master.height * fit))

    # Several assets share a source and geometry (the launcher and app_icon
    # sets, the two adaptive layers), and the pure-Python box resample of a
    # 4096 px master is the whole cost of this script. The memo is on the
    # inputs that determine the pixels, so it cannot change any output.
    mono = asset.get("monochrome_alpha", False)
    key = (asset["source"], art_w, art_h, mono)
    art = _cache.get(key)
    if art is None:
        art = png_io.resize(master, art_w, art_h)
        if mono:
            art = art.to_monochrome_alpha()
        _cache[key] = art

    if art_w == canvas_w and art_h == canvas_h:
        return art

    canvas = png_io.Image(canvas_w, canvas_h, bytearray(canvas_w * canvas_h * 4))
    _blit_straight(canvas, art, (canvas_w - art_w) // 2, (canvas_h - art_h) // 2)
    return canvas


def load_masters(root: str, manifest: dict) -> tuple[dict, list[str]]:
    masters = {}
    paths = []
    for source_id, source in manifest["sources"].items():
        path = verify_source(root, source_id, source)
        masters[source_id] = png_io.read(path)
        paths.append(path)
    return masters, paths


def generate(root: str, manifest: dict, out_dir: str, depfile: str | None) -> int:
    masters, master_paths = load_masters(root, manifest)
    written = []
    for asset in manifest["assets"]:
        target = os.path.join(out_dir, asset["output"])
        os.makedirs(os.path.dirname(target), exist_ok=True)
        png_io.write(target, render(masters, asset))
        written.append(target)
        print(f"wrote {asset['output']} ({asset['canvas_px'][0]}x{asset['canvas_px'][1]}px, {asset.get('density', '?')})")
    if depfile:
        repo_root.write_depfile(depfile, written[0], master_paths + [MANIFEST])
    return 0


def check(root: str, manifest: dict, out_dir: str) -> int:
    masters, _ = load_masters(root, manifest)
    findings = 0
    for asset in manifest["assets"]:
        target = os.path.join(out_dir, asset["output"])
        expected = png_io.encode(render(masters, asset))
        if not os.path.exists(target):
            print(
                f"{asset['output']}: missing. "
                "Run generate_icons.py --generate --out-dir <dir>."
            )
            findings += 1
            continue
        with open(target, "rb") as handle:
            actual = handle.read()
        if actual != expected:
            print(
                f"{asset['output']}: does not match the master "
                f"({len(actual)} bytes on disk, {len(expected)} expected)"
            )
            findings += 1
    print()
    print(
        f"checked {len(manifest['assets'])} asset(s) against "
        f"{len(manifest['sources'])} master(s), {findings} finding(s)"
    )
    return 1 if findings else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--generate", action="store_true", help="write the asset set")
    mode.add_argument("--check", action="store_true", help="verify an existing set")
    mode.add_argument("--list", action="store_true", help="print output paths")
    parser.add_argument("--out-dir", help="directory the assets are written under")
    parser.add_argument("--repo-root", help="override the repository root")
    parser.add_argument("--manifest", default=MANIFEST, help="asset manifest to read")
    parser.add_argument("--depfile", help="Ninja depfile to write (GN action use)")
    parser.add_argument(
        "--role",
        choices=("launcher", "override"),
        help="with --list: print only assets with this role",
    )
    args = parser.parse_args(argv)

    try:
        manifest = load_manifest(args.manifest)
        if args.list:
            for asset in manifest["assets"]:
                if args.role and asset["role"] != args.role:
                    continue
                print(asset["output"])
            return 0
        if not args.out_dir:
            parser.error("--out-dir is required with --generate and --check")
        root = repo_root.resolve(args.repo_root)
        if args.generate:
            return generate(root, manifest, args.out_dir, args.depfile)
        return check(root, manifest, args.out_dir)
    except (Finding, repo_root.RepoRootError, png_io.PngError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
