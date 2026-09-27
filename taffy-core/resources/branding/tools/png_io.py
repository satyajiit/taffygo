#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Minimal 8-bit RGBA PNG reader, writer and area resampler.

Why this file exists at all: the launcher icon set is derived from the
committed marks in `brand/`, and that derivation has to run on a Chromium
builder, on a developer laptop and in a documentation-only environment. Pillow
is not a dependency of any of those, and Chromium's build must not grow one
(the vendoring cost is real and the payoff here is a box filter). The Python
standard library already ships `zlib` and `struct`, which is everything a PNG
needs.

Scope, deliberately narrow:

  * reads 8-bit non-interlaced truecolor-with-alpha (color type 6) and
    truecolor (color type 2) PNGs, which is what `brand/png/` and
    `brand/source/` contain;
  * writes 8-bit RGBA, non-interlaced, filter type 0;
  * resamples by exact box average, which is the correct filter for reducing
    a large master to icon sizes and is deterministic across hosts.

Anything outside that raises `PngError` instead of guessing. A silent partial
decode would produce artwork that looks almost right, which is worse than a
failure.

A note on vocabulary: "alpha" appears throughout this file as the PNG
specification's name for the transparency channel, and as the identifier that
channel is read into. It is the domain term, never release vocabulary — the
project's plain-language rules ban the latter, and `brand/README.md` carries
the same distinction with the same reason.

Stdlib only. Importable as a module; not executable on its own.
"""

from __future__ import annotations

import struct
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

# Color types this module understands, mapped to their channel count.
_CHANNELS = {2: 3, 6: 4}


class PngError(Exception):
    """A PNG this module refuses to guess at."""


class Image:
    """An 8-bit RGBA raster.

    `pixels` is a flat bytearray of length width * height * 4, row-major, with
    straight (non-premultiplied) alpha — the same convention the committed
    brand assets use.
    """

    __slots__ = ("width", "height", "pixels")

    def __init__(self, width: int, height: int, pixels: bytearray) -> None:
        if len(pixels) != width * height * 4:
            raise PngError(
                f"raster is {len(pixels)} bytes, expected {width * height * 4}"
            )
        self.width = width
        self.height = height
        self.pixels = pixels

    @classmethod
    def opaque(cls, width: int, height: int, rgb: tuple[int, int, int]) -> "Image":
        row = bytes((rgb[0], rgb[1], rgb[2], 255)) * width
        return cls(width, height, bytearray(row * height))

    def pixel(self, x: int, y: int) -> tuple[int, int, int, int]:
        offset = (y * self.width + x) * 4
        return tuple(self.pixels[offset : offset + 4])  # type: ignore[return-value]

    def paste(self, other: "Image", left: int, top: int) -> None:
        """Alpha-composite `other` over this image at (left, top)."""
        for y in range(other.height):
            dst_y = top + y
            if dst_y < 0 or dst_y >= self.height:
                continue
            src_row = y * other.width * 4
            dst_row = dst_y * self.width * 4
            for x in range(other.width):
                dst_x = left + x
                if dst_x < 0 or dst_x >= self.width:
                    continue
                s = src_row + x * 4
                d = dst_row + dst_x * 4
                src_a = other.pixels[s + 3]
                if src_a == 0:
                    continue
                if src_a == 255:
                    self.pixels[d : d + 4] = other.pixels[s : s + 4]
                    continue
                inv = 255 - src_a
                for channel in range(3):
                    self.pixels[d + channel] = (
                        other.pixels[s + channel] * src_a
                        + self.pixels[d + channel] * inv
                    ) // 255
                self.pixels[d + 3] = src_a + self.pixels[d + 3] * inv // 255

    def crop(self, left: int, top: int, width: int, height: int) -> "Image":
        out = bytearray(width * height * 4)
        for y in range(height):
            src = ((top + y) * self.width + left) * 4
            dst = y * width * 4
            out[dst : dst + width * 4] = self.pixels[src : src + width * 4]
        return Image(width, height, out)

    def to_monochrome_alpha(self) -> "Image":
        """Keep the shape, discard the color: opaque white over the alpha mask.

        Android's themed-icon layer is read as a mask — the launcher recolors
        it — so the color channels must not carry the brand gradient into a
        surface the launcher owns.
        """
        out = bytearray(self.pixels)
        for offset in range(0, len(out), 4):
            out[offset] = 255
            out[offset + 1] = 255
            out[offset + 2] = 255
        return Image(self.width, self.height, out)


# --- decoding ---------------------------------------------------------------


def _unfilter(raw: bytes, width: int, height: int, channels: int) -> bytearray:
    """Reverse the five PNG scanline filters into a flat raster."""
    stride = width * channels
    out = bytearray(stride * height)
    previous = bytearray(stride)
    position = 0
    for y in range(height):
        filter_type = raw[position]
        position += 1
        line = bytearray(raw[position : position + stride])
        position += stride
        if filter_type == 0:
            pass
        elif filter_type == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:
            for i in range(channels):
                line[i] = (line[i] + (previous[i] >> 1)) & 0xFF
            for i in range(channels, stride):
                line[i] = (line[i] + ((line[i - channels] + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:
            for i in range(channels):
                line[i] = (line[i] + previous[i]) & 0xFF
            for i in range(channels, stride):
                a = line[i - channels]
                b = previous[i]
                c = previous[i - channels]
                p = a + b - c
                pa = p - a
                if pa < 0:
                    pa = -pa
                pb = p - b
                if pb < 0:
                    pb = -pb
                pc = p - c
                if pc < 0:
                    pc = -pc
                if pa <= pb and pa <= pc:
                    predictor = a
                elif pb <= pc:
                    predictor = b
                else:
                    predictor = c
                line[i] = (line[i] + predictor) & 0xFF
        else:
            raise PngError(f"unknown scanline filter {filter_type} on row {y}")
        out[y * stride : (y + 1) * stride] = line
        previous = line
    return out


def read(path: str) -> Image:
    """Decode an 8-bit non-interlaced PNG of color type 2 or 6."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != PNG_SIGNATURE:
        raise PngError(f"{path}: not a PNG")

    header = None
    idat: list[bytes] = []
    position = 8
    while position + 8 <= len(data):
        (length,) = struct.unpack(">I", data[position : position + 4])
        kind = data[position + 4 : position + 8]
        payload = data[position + 8 : position + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            idat.append(payload)
        elif kind == b"IEND":
            break
        position += 12 + length

    if header is None:
        raise PngError(f"{path}: no IHDR chunk")
    width, height, depth, color_type, compression, filter_method, interlace = header
    if depth != 8:
        raise PngError(f"{path}: bit depth {depth}, only 8 is supported")
    if color_type not in _CHANNELS:
        raise PngError(
            f"{path}: color type {color_type}, only 2 (RGB) and 6 (RGBA) are supported"
        )
    if compression != 0 or filter_method != 0:
        raise PngError(f"{path}: non-standard compression or filter method")
    if interlace != 0:
        raise PngError(f"{path}: interlaced PNGs are not supported")

    channels = _CHANNELS[color_type]
    raster = _unfilter(zlib.decompress(b"".join(idat)), width, height, channels)
    if channels == 4:
        return Image(width, height, raster)

    rgba = bytearray(width * height * 4)
    for index in range(width * height):
        rgba[index * 4 : index * 4 + 3] = raster[index * 3 : index * 3 + 3]
        rgba[index * 4 + 3] = 255
    return Image(width, height, rgba)


# --- resampling -------------------------------------------------------------


def _reduce_integer(image: Image, factor: int) -> Image:
    """Average non-overlapping factor x factor blocks. Exact, no rounding drift."""
    width = image.width // factor
    height = image.height // factor
    out = bytearray(width * height * 4)
    source = image.pixels
    area = factor * factor
    for y in range(height):
        base_rows = [(y * factor + dy) * image.width * 4 for dy in range(factor)]
        for x in range(width):
            offsets = [row + (x * factor + dx) * 4 for row in base_rows for dx in range(factor)]
            # Alpha-weighted color average: averaging color channels without
            # weighting them by alpha drags transparent black into the edges
            # and leaves a dark halo around the mark.
            alpha_sum = 0
            r = g = b = 0
            for offset in offsets:
                a = source[offset + 3]
                alpha_sum += a
                if a:
                    r += source[offset] * a
                    g += source[offset + 1] * a
                    b += source[offset + 2] * a
            destination = (y * width + x) * 4
            if alpha_sum:
                out[destination] = r // alpha_sum
                out[destination + 1] = g // alpha_sum
                out[destination + 2] = b // alpha_sum
            out[destination + 3] = alpha_sum // area
    return Image(width, height, out)


def resize(image: Image, width: int, height: int) -> Image:
    """Box-average `image` down to width x height.

    Reduction only: enlarging a launcher icon from a 4096 px master never
    happens, and an interpolating upscale would be a different, softer filter
    that has no business being chosen implicitly.
    """
    if width > image.width or height > image.height:
        raise PngError(
            f"refusing to enlarge {image.width}x{image.height} to {width}x{height}"
        )
    if width == image.width and height == image.height:
        return Image(image.width, image.height, bytearray(image.pixels))

    # One cheap integer reduction first, so the general path below reads far
    # fewer source pixels. 4096 -> 432 becomes 4096 -> 1024 -> 432.
    working = image
    factor = min(image.width // width, image.height // height)
    while factor >= 2 and working.width % 2 == 0 and working.height % 2 == 0:
        working = _reduce_integer(working, 2)
        factor //= 2

    out = bytearray(width * height * 4)
    source = working.pixels
    source_width = working.width
    for y in range(height):
        y0 = y * working.height // height
        y1 = max(y0 + 1, (y + 1) * working.height // height)
        for x in range(width):
            x0 = x * source_width // width
            x1 = max(x0 + 1, (x + 1) * source_width // width)
            alpha_sum = 0
            count = 0
            r = g = b = 0
            for sy in range(y0, y1):
                row = sy * source_width * 4
                for sx in range(x0, x1):
                    offset = row + sx * 4
                    a = source[offset + 3]
                    alpha_sum += a
                    count += 1
                    if a:
                        r += source[offset] * a
                        g += source[offset + 1] * a
                        b += source[offset + 2] * a
            destination = (y * width + x) * 4
            if alpha_sum:
                out[destination] = r // alpha_sum
                out[destination + 1] = g // alpha_sum
                out[destination + 2] = b // alpha_sum
            out[destination + 3] = alpha_sum // count
    return Image(width, height, out)


# --- encoding ---------------------------------------------------------------


def _chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def encode(image: Image) -> bytes:
    """Serialize as an 8-bit RGBA PNG with filter type 0 on every row.

    Filter 0 everywhere keeps the encoder deterministic: the same raster
    produces the same bytes on every host and in every Python version, which
    is what lets `generate_icons.py --check` compare a committed icon against
    a freshly derived one by digest.
    """
    stride = image.width * 4
    raw = bytearray()
    for y in range(image.height):
        raw.append(0)
        raw += image.pixels[y * stride : (y + 1) * stride]
    return b"".join(
        [
            PNG_SIGNATURE,
            _chunk(b"IHDR", struct.pack(">IIBBBBB", image.width, image.height, 8, 6, 0, 0, 0)),
            _chunk(b"IDAT", zlib.compress(bytes(raw), 9)),
            _chunk(b"IEND", b""),
        ]
    )


def write(path: str, image: Image) -> None:
    with open(path, "wb") as handle:
        handle.write(encode(image))
