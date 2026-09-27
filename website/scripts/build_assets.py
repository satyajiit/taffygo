#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Regenerate the committed image and font assets under website/.

Host utility, not part of `next build`: the outputs are checked in, and this
script exists so they are reproducible rather than mysterious. It reads the
logo masters in brand/ and a Space Grotesk release, and writes:

  public/fonts/space-grotesk-latin-variable.woff2   latin subset, wght axis
  public/fonts/OFL.txt                              the licence, unmodified
  public/brand/taffygo-lockup-on-light.webp         header/footer lockup
  public/brand/taffygo-lockup-on-dark.webp          the dark-surface lockup
  public/brand/taffygo-lockup-color-on-*-*.webp     width-sized lockup set
  public/brand/taffygo-mark-*-180*.webp             width-sized mark set
  public/brand/google-play-badge.webp               lossless WebP of the badge
  app/icon.png  app/apple-icon.png  app/favicon.ico

The link-preview image and the real phone screens are not made here:
scripts/build_media.py derives them from the publishing art and the phone
captures, and taffy-generated-assets.txt records where each came from.

Requires host Python 3 with Pillow, fontTools and brotli, plus a local copy of
the Space Grotesk variable font passed as --font. Nothing here runs in CI.

Usage:
  python3 scripts/build_assets.py --font /path/to/SpaceGrotesk[wght].ttf \
                                  --licence /path/to/OFL.txt
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
WEB = HERE.parent
REPO = WEB.parent
MASTERS = REPO / "brand" / "png"

# Inputs:
#   - Brand masters: brand/png/taffygo-{lockup,mark}-*.png, the committed
#     artwork. They are padded, so every width-sized derivative is sized by
#     width only, never trimmed and never sized by height.
#   - Google Play badge: the official English generic badge PNG, fetched once
#     from
#     https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png
#     and committed as public/brand/google-play-badge.png. It is self-hosted
#     and must never be redrawn or restyled; this script only derives a
#     lossless WebP from the committed PNG.
PLAY_BADGE = WEB / "public" / "brand" / "google-play-badge.png"

# Latin plus the punctuation the page actually sets.
UNICODES = (
    "U+0000-00FF,U+0131,U+0152-0153,U+02BB-02BC,U+02C6,U+02DA,U+02DC,U+0304,"
    "U+0308,U+0329,U+2000-206F,U+20AC,U+2122,U+2191,U+2193,U+2212,U+2215,"
    "U+FEFF,U+FFFD"
)


def trimmed(path: pathlib.Path) -> Image.Image:
    image = Image.open(path).convert("RGBA")
    return image.crop(image.getbbox())


def fit_height(image: Image.Image, height: int) -> Image.Image:
    width = round(image.width * height / image.height)
    return image.resize((width, height), Image.LANCZOS)


def fit_width(image: Image.Image, width: int) -> Image.Image:
    height = round(image.height * width / image.width)
    return image.resize((width, height), Image.LANCZOS)


def subset_font(source: pathlib.Path, target: pathlib.Path) -> None:
    subprocess.run(
        [
            sys.executable, "-m", "fontTools.subset", str(source),
            f"--output-file={target}",
            "--flavor=woff2",
            "--layout-features=kern,liga,clig,calt,tnum,ccmp,mark,mkmk",
            f"--unicodes={UNICODES}",
            "--name-IDs=*", "--name-legacy", "--name-languages=*",
            "--drop-tables+=DSIG",
        ],
        check=True,
    )


def lockups() -> None:
    out = WEB / "public" / "brand"
    out.mkdir(parents=True, exist_ok=True)
    for variant in ("on-dark", "on-light"):
        image = fit_height(trimmed(MASTERS / f"taffygo-lockup-color-{variant}.png"), 84)
        image.save(out / f"taffygo-lockup-{variant}.webp", "WEBP", quality=90, method=6)


def brand_derivatives() -> None:
    """Width-sized WebP set from the padded brand masters.

    Lockups at the nav (118px) and footer (132px) widths, each with a @2x
    variant; marks at 180px with a @2x variant for icons and structured data.
    Lossless, because the wordmark detail does not survive lossy WebP.
    """
    out = WEB / "public" / "brand"
    out.mkdir(parents=True, exist_ok=True)
    for variant in ("color-on-light", "color-on-dark"):
        master = Image.open(MASTERS / f"taffygo-lockup-{variant}.png").convert("RGBA")
        for base in (118, 132):
            for scale, suffix in ((1, ""), (2, "@2x")):
                image = fit_width(master, base * scale)
                name = f"taffygo-lockup-{variant}-{base}{suffix}.webp"
                image.save(out / name, "WEBP", lossless=True, method=6)
    for variant in ("color-on-light", "color-on-dark", "monochrome-on-dark"):
        master = Image.open(MASTERS / f"taffygo-mark-{variant}.png").convert("RGBA")
        for scale, suffix in ((1, ""), (2, "@2x")):
            image = fit_width(master, 180 * scale)
            image.save(
                out / f"taffygo-mark-{variant}-180{suffix}.webp",
                "WEBP", lossless=True, method=6,
            )


def play_badge() -> None:
    """Lossless WebP of the committed official badge PNG; never redrawn."""
    image = Image.open(PLAY_BADGE).convert("RGBA")
    image.save(PLAY_BADGE.with_suffix(".webp"), "WEBP", lossless=True, method=6)


def icons() -> None:
    mark_light = trimmed(MASTERS / "taffygo-mark-color-on-light.png")
    mark_dark = trimmed(MASTERS / "taffygo-mark-color-on-dark.png")

    icon = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    scaled = fit_height(mark_light, 400)
    icon.alpha_composite(scaled, ((512 - scaled.width) // 2, (512 - scaled.height) // 2))
    icon.resize((192, 192), Image.LANCZOS).save(WEB / "app" / "icon.png", optimize=True)

    apple = Image.new("RGBA", (180, 180), (10, 13, 24, 255))
    scaled = fit_height(mark_dark, 128)
    apple.alpha_composite(scaled, ((180 - scaled.width) // 2, (180 - scaled.height) // 2))
    apple.convert("RGB").save(WEB / "app" / "apple-icon.png", optimize=True)

    icon.resize((64, 64), Image.LANCZOS).save(
        WEB / "app" / "favicon.ico", sizes=[(16, 16), (32, 32), (48, 48)]
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, type=pathlib.Path)
    parser.add_argument("--licence", required=True, type=pathlib.Path)
    args = parser.parse_args()

    fonts = WEB / "public" / "fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    subset_font(args.font, fonts / "space-grotesk-latin-variable.woff2")
    shutil.copyfile(args.licence, fonts / "OFL.txt")

    lockups()
    brand_derivatives()
    play_badge()
    icons()
    print("assets rebuilt")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
