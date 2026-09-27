#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Turns one SVG into one PNG, through the system librsvg, and nothing else.

WHY CTYPES AND NOT A PYTHON SVG LIBRARY

Rasterising an SVG is not a small job, and every Python binding for it is a
third-party package. This repository installs nothing unpinned, and a build
recipe that pip-installs a renderer would put an unpinned rasteriser between an
upstream corpus and a digest the product later promises. librsvg and cairo are
already on any host that has a desktop stack, they are the pair GNOME renders
SVG with, and they are reachable from the standard library through `ctypes`.
So this module binds the six C entry points it needs and stops there.

WHY THE VERSIONS ARE PINNED AND CHECKED

A rasteriser's output is part of the artifact's bytes. A different librsvg
antialiases an edge differently, and a different edge is a different digest —
which would make a catalog row that was true yesterday false today, with no
input having changed. `require_versions` therefore refuses a host whose
librsvg or cairo is not the pinned pair, in the same way
`taffy-core/ui/android/tools/icons/generate_launcher_icons.py` refuses a host
whose `cwebp` is not the pinned one.

WHY CAIRO WRITES THE PNG

cairo's image surface holds premultiplied BGRA. Converting that to straight
RGBA in Python would be a per-pixel loop over several million pixels and a
second place for a rounding rule to live. `cairo_surface_write_to_png` already
does it, correctly, in C. Nothing here touches a pixel.

Stdlib only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import os
import sys
import tempfile

#: cairo's ARGB32, the only format this module renders into. Named rather than
#: written as 0 at the call site, because 0 is also the value of several other
#: cairo enumerations and the reader cannot tell which one is meant.
CAIRO_FORMAT_ARGB32 = 0

#: cairo's success status.
CAIRO_STATUS_SUCCESS = 0


class RasterError(Exception):
    """A host that cannot rasterise, or an SVG that will not render."""


class RasterUnavailable(RasterError):
    """The rasteriser is not installed on this host.

    Separated from every other `RasterError` because the two mean opposite
    things to a gate. A rasteriser that renders the probe wrongly has drifted
    and the digest it produces can no longer be trusted, which is a failure.
    A rasteriser that is not here has not drifted; nothing was measured at all,
    and a check that reported that as a broken property would be claiming to
    have run something it did not — the repository's honesty rule read
    backwards. It stays a subclass so that a caller which genuinely cannot
    proceed without pixels, such as an actual pack build, still stops on it
    without having to name both.
    """


class _Rectangle(ctypes.Structure):
    """`RsvgRectangle`: the viewport a document is rendered into."""

    _fields_ = [
        ("x", ctypes.c_double),
        ("y", ctypes.c_double),
        ("width", ctypes.c_double),
        ("height", ctypes.c_double),
    ]


def _load() -> tuple:
    """Both libraries with their signatures declared, or a readable refusal."""
    cairo_name = ctypes.util.find_library("cairo")
    rsvg_name = ctypes.util.find_library("rsvg-2")
    if not cairo_name or not rsvg_name:
        raise RasterUnavailable(
            "librsvg and cairo are not on this host. They are the only "
            "rasteriser this recipe uses; install the distribution's librsvg2 "
            "and cairo runtime packages rather than a Python renderer."
        )
    try:
        cairo = ctypes.CDLL(cairo_name)
        rsvg = ctypes.CDLL(rsvg_name)
    except OSError as error:
        raise RasterError(f"cannot load {cairo_name} / {rsvg_name}: {error}") from error

    cairo.cairo_image_surface_create.restype = ctypes.c_void_p
    cairo.cairo_image_surface_create.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
    cairo.cairo_create.restype = ctypes.c_void_p
    cairo.cairo_create.argtypes = [ctypes.c_void_p]
    cairo.cairo_status.restype = ctypes.c_int
    cairo.cairo_status.argtypes = [ctypes.c_void_p]
    cairo.cairo_surface_status.restype = ctypes.c_int
    cairo.cairo_surface_status.argtypes = [ctypes.c_void_p]
    cairo.cairo_surface_write_to_png.restype = ctypes.c_int
    cairo.cairo_surface_write_to_png.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    cairo.cairo_destroy.argtypes = [ctypes.c_void_p]
    cairo.cairo_surface_destroy.argtypes = [ctypes.c_void_p]
    cairo.cairo_version_string.restype = ctypes.c_char_p

    rsvg.rsvg_handle_new_from_data.restype = ctypes.c_void_p
    rsvg.rsvg_handle_new_from_data.argtypes = [
        ctypes.c_char_p,
        ctypes.c_size_t,
        ctypes.c_void_p,
    ]
    rsvg.rsvg_handle_render_document.restype = ctypes.c_int
    rsvg.rsvg_handle_render_document.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.POINTER(_Rectangle),
        ctypes.c_void_p,
    ]
    rsvg.rsvg_major_version = ctypes.c_uint.in_dll(rsvg, "rsvg_major_version")
    rsvg.rsvg_minor_version = ctypes.c_uint.in_dll(rsvg, "rsvg_minor_version")
    rsvg.rsvg_micro_version = ctypes.c_uint.in_dll(rsvg, "rsvg_micro_version")
    return cairo, rsvg


_LIBRARIES: tuple | None = None


def libraries() -> tuple:
    """The loaded pair, loaded once."""
    global _LIBRARIES
    if _LIBRARIES is None:
        _LIBRARIES = _load()
    return _LIBRARIES


def versions() -> dict:
    """What this host would rasterise with, as the manifest records it."""
    cairo, rsvg = libraries()
    return {
        "librsvg": (
            f"{rsvg.rsvg_major_version.value}."
            f"{rsvg.rsvg_minor_version.value}."
            f"{rsvg.rsvg_micro_version.value}"
        ),
        "cairo": cairo.cairo_version_string().decode("ascii"),
    }


def require_versions(pinned: dict) -> None:
    """Refuses a host whose rasteriser is not the one the manifest names."""
    found = versions()
    for name in ("librsvg", "cairo"):
        if name not in pinned:
            raise RasterError(f"the manifest records no {name} version to pin against")
        if found[name] != pinned[name]:
            raise RasterError(
                f"{name} {found[name]} is on this host but the manifest pins "
                f"{pinned[name]}. A rasteriser's output is part of the "
                "artifact's bytes, so a different one is a different digest "
                "for unchanged inputs. Build on the pinned pair, or change the "
                "pin deliberately and rebuild every published variant."
            )


def render_png(document: bytes, width: int, height: int, target: str) -> None:
    """Renders one SVG document into a `width` x `height` PNG at `target`."""
    if width <= 0 or height <= 0:
        raise RasterError(f"{width}x{height} is not a size")
    cairo, rsvg = libraries()
    handle = rsvg.rsvg_handle_new_from_data(document, len(document), None)
    if not handle:
        raise RasterError("librsvg refused the document")
    surface = cairo.cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height)
    if cairo.cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS:
        cairo.cairo_surface_destroy(surface)
        raise RasterError(f"cairo refused a {width}x{height} surface")
    context = cairo.cairo_create(surface)
    try:
        viewport = _Rectangle(0.0, 0.0, float(width), float(height))
        if not rsvg.rsvg_handle_render_document(handle, context, ctypes.byref(viewport), None):
            raise RasterError("librsvg could not render the document")
        if cairo.cairo_status(context) != CAIRO_STATUS_SUCCESS:
            raise RasterError("cairo reported an error while rendering")
        if cairo.cairo_surface_write_to_png(surface, target.encode("utf-8")):
            raise RasterError(f"cairo could not write {target}")
    finally:
        cairo.cairo_destroy(context)
        cairo.cairo_surface_destroy(surface)


#: A document whose four quadrants are four flat colours, so a self-test can
#: assert on pixels rather than on the absence of an exception.
_PROBE = (
    b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2 2">'
    b'<rect x="0" y="0" width="1" height="1" fill="#ff0000"/>'
    b'<rect x="1" y="0" width="1" height="1" fill="#00ff00"/>'
    b'<rect x="0" y="1" width="1" height="1" fill="#0000ff"/>'
    b"</svg>"
)


def _self_test() -> int:
    """Renders a known document and checks the pixels and the refusals."""
    failures: list[str] = []
    try:
        found = versions()
    except RasterUnavailable as error:
        # Not run, and said so. Reported before the general refusal below
        # because the two are caught by the same name and only the order
        # separates "nothing was measured" from "what was measured was wrong".
        print(f"svg raster: not run — {error}", file=sys.stderr)
        return 0
    except RasterError as error:
        print(f"svg raster: {error}", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as scratch:
        target = os.path.join(scratch, "probe.png")
        try:
            render_png(_PROBE, 4, 4, target)
        except RasterError as error:
            failures.append(f"the probe document did not render: {error}")
        else:
            with open(target, "rb") as handle:
                header = handle.read(8)
            if header != b"\x89PNG\r\n\x1a\n":
                failures.append("what was written is not a PNG")
            if os.path.getsize(target) < 8:
                failures.append("the PNG is empty")

        again = os.path.join(scratch, "again.png")
        try:
            render_png(_PROBE, 4, 4, again)
        except RasterError as error:
            failures.append(f"the second render failed: {error}")
        else:
            with open(target, "rb") as first, open(again, "rb") as second:
                if first.read() != second.read():
                    failures.append("two renders of one document differ")

        try:
            render_png(_PROBE, 0, 4, os.path.join(scratch, "never.png"))
        except RasterError:
            pass
        else:
            failures.append("a zero-width raster was written rather than refused")

        try:
            render_png(b"not an svg", 4, 4, os.path.join(scratch, "never.png"))
        except RasterError:
            pass
        else:
            failures.append("a document that is not SVG was accepted")

        try:
            require_versions({"librsvg": "0.0.0", "cairo": found["cairo"]})
        except RasterError:
            pass
        else:
            failures.append("a mismatched librsvg pin was accepted")

        try:
            require_versions(found)
        except RasterError as error:
            failures.append(f"this host's own versions were refused: {error}")

    for failure in failures:
        print(f"svg raster: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(
        f"svg raster: librsvg {found['librsvg']}, cairo {found['cairo']}; "
        "6 properties checked"
    )
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="render a known document and check the pixels and the refusals",
    )
    parser.add_argument(
        "--versions",
        action="store_true",
        help="print the rasteriser versions this host would build with",
    )
    arguments = parser.parse_args(argv)
    if arguments.versions:
        try:
            for name, value in versions().items():
                print(f"{name} {value}")
        except RasterError as error:
            print(f"svg raster: {error}", file=sys.stderr)
            return 1
        return 0
    if not arguments.self_test:
        parser.error("this module is a library; --self-test and --versions are its commands")
    return _self_test()


if __name__ == "__main__":
    raise SystemExit(main())
