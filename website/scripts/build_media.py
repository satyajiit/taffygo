#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Derive the website's phone screens, paintings and social card.

Host utility, not part of `next build`: the outputs are committed, and this
script records how they were made so they can be made again.

Inputs live outside the repository:

  --screens DIR   a capture directory written by tools/screens/capture; the
                  lossless `cropped/<name>.png` files are read, never the
                  lossy WebP copies beside them
  --art DIR       the reviewed paintings: `website-hero.png`,
                  `section-asks-first.png`, `section-on-your-phone.png` and
                  `og-image.jpg`

Outputs:

  public/screens/<name>-360.webp and -720.webp   one pair per screen below
  public/art/<name>-<width>.webp                 two widths per painting
  public/og-image-<sha256[:12]>.jpg              the social card, byte for byte

Every output's size, byte count and sha256 belongs in
website/taffy-generated-assets.txt; tests/assets.test.ts holds the files to
that record. Requires host Python 3 with Pillow built with WebP support.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import shutil

from PIL import Image

WEB = pathlib.Path(__file__).resolve().parent.parent
PUBLIC = WEB / "public"

#: The captures the site shows. Names are the capture tool's shot names.
SCREENS = (
    "01-start-page",
    "02-taffy-on-a-page",
    "03-ad-blocking",
    "04-errand-running",
    "05-asks-before-acting",
    "06-results",
    "07-your-provider-your-key",
    "08-privacy",
    "09-backup",
    "10-tabs",
    "11-start-page-dark",
    "12-taffy-dark",
    "x-ask-about-this-page",
)
SCREEN_WIDTHS = (360, 720)

#: Painting -> the two widths it is served at.
PAINTINGS = {
    "website-hero": (800, 1600),
    "section-asks-first": (720, 1440),
    "section-on-your-phone": (720, 1440),
}

WEBP_QUALITY = 82


def resized(image: Image.Image, width: int) -> Image.Image:
    height = round(image.height * width / image.width)
    return image.resize((width, height), Image.LANCZOS)


def write_webp(image: Image.Image, target: pathlib.Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    # No metadata travels: the sources are sRGB, which is what a browser
    # assumes for an untagged image.
    image.save(target, "WEBP", quality=WEBP_QUALITY, method=6)
    print(f"wrote {target.relative_to(WEB)}")


def screens(source: pathlib.Path) -> None:
    for name in SCREENS:
        image = Image.open(source / "cropped" / f"{name}.png").convert("RGB")
        for width in SCREEN_WIDTHS:
            write_webp(resized(image, width), PUBLIC / "screens" / f"{name}-{width}.webp")


def paintings(source: pathlib.Path) -> None:
    for name, widths in PAINTINGS.items():
        image = Image.open(source / f"{name}.png").convert("RGB")
        for width in widths:
            write_webp(resized(image, width), PUBLIC / "art" / f"{name}-{width}.webp")


def social_card(source: pathlib.Path) -> None:
    card = source / "og-image.jpg"
    digest = hashlib.sha256(card.read_bytes()).hexdigest()
    for stale in PUBLIC.glob("og-image-*"):
        stale.unlink()
    target = PUBLIC / f"og-image-{digest[:12]}.jpg"
    shutil.copyfile(card, target)
    print(f"wrote {target.relative_to(WEB)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--screens", required=True, type=pathlib.Path)
    parser.add_argument("--art", required=True, type=pathlib.Path)
    args = parser.parse_args()
    screens(args.screens)
    paintings(args.art)
    social_card(args.art)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
