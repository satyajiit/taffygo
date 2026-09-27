#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The image half of ./tools/screens/capture: insets, crop, WebP, manifest.

The shell half talks to the phone; this half never does. It reads what the
shell saved (the raw PNG and the text of `dumpsys window`) and writes three
things beside it: a cropped PNG at full resolution, a WebP of that crop, and
one entry in the run's manifest.json.

The crops come from the phone, not from a constant. The navigation bar is cut
at the top edge of the `navigationBars` insets source that `dumpsys window`
reports, and the status bar, when it goes, at the bottom edge of the
`statusBars` source. A hard-coded pixel count is right for one phone and
silently wrong for the next, and a wrong crop reads as a design choice.

Pillow does the pixels. This repository installs nothing at run time, so a
host without it gets a refusal naming what to install, never a quiet fallback
that writes PNGs and skips the WebP.

Exit codes: 0 = written, 1 = refused (the reason is on stderr), 2 = usage.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import sys

_SOURCE = re.compile(
    r"InsetsSource id=\S+ type=(?P<type>navigationBars|statusBars) "
    r"frame=\[(?P<l>-?\d+),(?P<t>-?\d+)\]\[(?P<r>-?\d+),(?P<b>-?\d+)\]"
)


def refuse(message: str) -> int:
    print(f"frame.py: {message}", file=sys.stderr)
    return 1


def inset_frames(dumpsys_text: str) -> dict[str, tuple[int, int, int, int]]:
    """The first frame `dumpsys window` reports for each bar, as l, t, r, b.

    The first is the display's own insets state; later repeats are the same
    source seen from each window's controller, and they agree.
    """
    frames: dict[str, tuple[int, int, int, int]] = {}
    for match in _SOURCE.finditer(dumpsys_text):
        kind = match.group("type")
        if kind not in frames:
            frames[kind] = tuple(int(match.group(k)) for k in "ltrb")  # type: ignore[assignment]
    return frames


def plan_crops(frames, width: int, height: int, crop_status: bool, status_why: str):
    """Top and bottom cuts in pixels, each with the reason the manifest records."""
    crops = []
    nav = frames.get("navigationBars")
    if nav is None:
        raise ValueError("dumpsys window reported no navigationBars source; nothing to measure the crop from")
    left, top, right, bottom = nav
    if bottom != height or left != 0 or right != width or top <= 0:
        raise ValueError(
            f"the navigation bar is not a full-width strip at the bottom (frame [{left},{top}][{right},{bottom}] "
            f"on a {width}x{height} capture); capture in portrait"
        )
    crops.append({
        "edge": "bottom",
        "px": height - top,
        "why": f"navigation bar inset: navigationBars frame [{left},{top}][{right},{bottom}] from dumpsys window",
    })
    if crop_status:
        status = frames.get("statusBars")
        if status is None:
            raise ValueError("the status bar has to go, and dumpsys window reported no statusBars source")
        s_left, s_top, s_right, s_bottom = status
        if s_top != 0 or s_bottom <= 0 or s_bottom >= top:
            raise ValueError(f"the statusBars frame [{s_left},{s_top}][{s_right},{s_bottom}] is not a strip at the top")
        crops.append({
            "edge": "top",
            "px": s_bottom,
            "why": f"status bar inset: statusBars frame [{s_left},{s_top}][{s_right},{s_bottom}]; {status_why}",
        })
    return crops


def pairs(items: list[str]) -> dict[str, str]:
    """Repeated `--app key=value` arguments, as one mapping."""
    return dict(item.split("=", 1) for item in items if "=" in item)


def load_manifest(path: str) -> dict:
    if not os.path.exists(path):
        return {"screens": []}
    with open(path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    if not isinstance(manifest.get("screens"), list):
        raise ValueError(f"{path} has no screens list; move it aside rather than let this overwrite it")
    return manifest


def write_json(path: str, value: dict) -> None:
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as handle:
        json.dump(value, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    os.replace(temporary, path)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True, help="the run directory holding raw/, cropped/, webp/")
    parser.add_argument("--name", required=True)
    parser.add_argument("--dumpsys", required=True, help="a saved `dumpsys window` text")
    parser.add_argument("--crop-status", action="store_true")
    parser.add_argument("--status-why", default="")
    parser.add_argument("--shows", default="")
    parser.add_argument("--substitute", default="")
    parser.add_argument("--quality", type=int, default=90)
    parser.add_argument("--app", action="append", default=[], help="key=value, repeatable")
    parser.add_argument("--device", action="append", default=[], help="key=value, repeatable")
    args = parser.parse_args(argv)

    try:
        from PIL import Image
    except ImportError:
        return refuse(
            f"Pillow is not importable from {sys.executable}. Make a venv with Pillow in it and "
            "point TAFFY_SCREENS_PYTHON at its python3; this tool installs nothing."
        )

    raw = os.path.join(args.out, "raw", f"{args.name}.png")
    cropped = os.path.join(args.out, "cropped", f"{args.name}.png")
    webp = os.path.join(args.out, "webp", f"{args.name}.webp")
    with open(args.dumpsys, encoding="utf-8", errors="replace") as handle:
        frames = inset_frames(handle.read())

    with Image.open(raw) as image:
        image.load()
        width, height = image.size
        try:
            crops = plan_crops(frames, width, height, args.crop_status, args.status_why)
        except ValueError as problem:
            return refuse(str(problem))
        top = sum(c["px"] for c in crops if c["edge"] == "top")
        bottom = height - sum(c["px"] for c in crops if c["edge"] == "bottom")
        result = image.crop((0, top, width, bottom))
        result.save(cropped, format="PNG", optimize=True)
        result.save(webp, format="WEBP", quality=args.quality, method=6)
        out_size = list(result.size)

    app = pairs(args.app)
    entry = {
        "name": args.name,
        "shows": args.shows,
        "captured_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "raw": {"path": f"raw/{args.name}.png", "size_px": [width, height]},
        "cropped": {"path": f"cropped/{args.name}.png", "size_px": out_size},
        "webp": {"path": f"webp/{args.name}.webp", "size_px": out_size, "quality": args.quality},
        "crops": crops,
        "app": app,
    }
    if args.substitute:
        entry["substitute"] = args.substitute

    manifest_path = os.path.join(args.out, "manifest.json")
    try:
        manifest = load_manifest(manifest_path)
    except (ValueError, json.JSONDecodeError) as problem:
        return refuse(str(problem))
    manifest["device"] = pairs(args.device)
    manifest["screens"] = [s for s in manifest["screens"] if s.get("name") != args.name] + [entry]
    manifest["screens"].sort(key=lambda s: s.get("name", ""))
    write_json(manifest_path, manifest)

    cuts = ", ".join(f"{c['edge']} {c['px']} px" for c in crops)
    print(f"{args.name}: {width}x{height} raw -> {out_size[0]}x{out_size[1]} ({cuts})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
