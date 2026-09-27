#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Read every externalized TaffyGo string, whichever catalogue defined it.

TaffyGo strings live in two shapes, for two good reasons rather than by
accident:

  * grit catalogues under `//taffy/resources/catalog` — the shape that
    reaches a translator, and therefore the shape every sentence a person
    reads belongs in;
  * Android `values*/**.xml` resources elsewhere in the overlay — the shape a
    proper noun belongs in, and the shape a resource that is selected by build
    configuration must take, because Android resolves those by resource
    directory rather than by message id.

This module turns both into one list of `Message` so that the gate in
`check_strings.py` applies one set of rules to all of them and cannot quietly
hold one shape to a lower standard than the other.

Read-only, stdlib only. Importable as a module; not executable on its own.
"""

from __future__ import annotations

import os
import re
import xml.etree.ElementTree as ElementTree

# The overlay root, two directories above this file: resources/tools -> taffy.
RESOURCES_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OVERLAY_ROOT = os.path.dirname(RESOURCES_DIR)

# Directories that hold no shipping user-visible text.
SKIP_DIR_NAMES = {"junit", "javatests", "test", "tests"}


class Message:
    """One externalized string, plus enough context to report on it."""

    __slots__ = ("name", "text", "description", "source", "translatable")

    def __init__(
        self, name: str, text: str, description: str, source: str, translatable: bool
    ) -> None:
        self.name = name
        self.text = text
        self.description = description
        self.source = source
        self.translatable = translatable


def relative(path: str) -> str:
    """A path a reader can act on: repository-relative, or absolute if outside."""
    result = os.path.relpath(path, os.path.dirname(OVERLAY_ROOT)).replace(os.sep, "/")
    return os.path.abspath(path) if result.startswith("../") else result


def collapse(text: str) -> str:
    """Grit collapses leading, trailing and repeated whitespace in a message."""
    return re.sub(r"\s+", " ", text).strip()


def element_text(element: ElementTree.Element) -> str:
    """Message text with grit placeholders kept in their wire form.

    `itertext` would flatten `<ph name="COUNT">$1<ex>3</ex></ph>` into `$13`,
    which changes what the runtime substitutes. Placeholders are re-serialized
    instead, so the pseudo-localization rules see the same token a translator
    would.
    """
    parts: list[str] = [element.text or ""]
    for child in element:
        if child.tag == "ph":
            parts.append(ElementTree.tostring(child, encoding="unicode").strip())
        else:
            parts.append(element_text(child))
        parts.append(child.tail or "")
    return "".join(parts)


def walk(root: str, predicate) -> list[str]:
    """Every file under `root` the predicate accepts, in a stable order.

    `followlinks=True` is load-bearing rather than incidental. The GN source
    projection mounts Android UI modules as symlinks, and `os.walk`
    does not descend a symlinked directory by default — so without this the
    gate read four files, reported "0 findings", and had never opened a single
    one of the several hundred strings the shipping screens actually show. A
    check that reports success over what it did not scan is the one thing this
    repository's honesty rule forbids outright.

    Following links needs the visited set: a mount is free to point anywhere,
    and a link that ever resolves to an ancestor would otherwise loop forever.
    """
    found: list[str] = []
    visited: set[str] = set()
    for dirpath, dirnames, filenames in os.walk(root, followlinks=True):
        real = os.path.realpath(dirpath)
        if real in visited:
            dirnames[:] = []
            continue
        visited.add(real)
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIR_NAMES]
        for name in filenames:
            path = os.path.join(dirpath, name)
            if predicate(path, name):
                found.append(path)
    return sorted(found)


def grit_roots() -> list[str]:
    return walk(RESOURCES_DIR, lambda _p, name: name.endswith(".grd"))


def grit_parts() -> list[str]:
    return walk(RESOURCES_DIR, lambda _p, name: name.endswith(".grdp"))


def android_resource_files(overlay_root: str) -> list[str]:
    return walk(
        overlay_root,
        lambda path, name: name.endswith(".xml")
        and os.sep + "res" in path
        and os.sep + "values" in path,
    )


def source_files(overlay_root: str) -> list[str]:
    return walk(
        overlay_root,
        lambda _p, name: name.endswith(".kt") or name.endswith(".java"),
    )


def parts_referenced_by(root_path: str) -> tuple[list[tuple[str, str]], str | None]:
    """(reference, resolved-path) pairs from a .grd, or an XML parse error."""
    try:
        tree = ElementTree.parse(root_path).getroot()
    except ElementTree.ParseError as error:
        return [], str(error)
    references: list[tuple[str, str]] = []
    for part in tree.iter("part"):
        reference = part.get("file", "")
        references.append(
            (
                reference,
                os.path.normpath(os.path.join(os.path.dirname(root_path), reference)),
            )
        )
    return references, None


def read_grit_messages(path: str) -> tuple[list[Message], str | None]:
    """Messages from a .grd or .grdp, or an XML parse error."""
    try:
        root = ElementTree.parse(path).getroot()
    except ElementTree.ParseError as error:
        return [], str(error)
    messages = [
        Message(
            name=element.get("name", ""),
            text=collapse(element_text(element)),
            description=collapse(element.get("desc", "")),
            source=relative(path),
            translatable=element.get("translateable", "true") != "false",
        )
        for element in root.iter("message")
    ]
    return messages, None


def read_android_strings(path: str) -> tuple[list[Message], str | None]:
    """Messages from an Android values XML, or an XML parse error.

    An Android string resource has no `desc` attribute; the comment above it
    is the translator's context, and XML comments are not addressable here.
    Descriptions are therefore required only of the grit catalogue, which is
    where a string that will be translated belongs in the first place.
    """
    try:
        root = ElementTree.parse(path).getroot()
    except ElementTree.ParseError as error:
        return [], str(error)
    messages = [
        Message(
            name=element.get("name", ""),
            text=collapse(element_text(element)),
            description="",
            source=relative(path),
            translatable=element.get("translatable", "true") != "false",
        )
        for element in root.iter("string")
    ]
    return messages, None


def resource_set(source: str) -> str:
    """The resource root a file belongs to, or "" for the grit catalogue.

    Android resolves a resource name inside one resource root and lets a later
    root override an earlier one — that is the whole branding-substitution
    mechanism, and it is why `app_name` is deliberately defined in both
    `java/res` and `java/res-debug`. So uniqueness is a property of a resource
    root, not of the overlay. Two definitions in the *same* root are still a
    defect: one of them silently loses.

    The root is the parent of the `values*` directory, which is Chromium's own
    rule — `resource_utils.DeduceResourceDirsFromFileList` takes exactly this
    `dirname(dirname(path))`. Searching upward for a path component named `res`
    instead, as this did, collapsed every mounted module into one set the
    moment decision 0024's mounts arrived: `res/core_ui` and
    `res/feature_browsing` are two roots, not two directories inside one.
    """
    parent = os.path.dirname(os.path.dirname(source))
    return parent if os.path.basename(os.path.dirname(source)).startswith("values") else ""


def configuration(source: str) -> str:
    """The resource configuration a file is qualified by: `values`, `values-hi`.

    A name carried by two configurations of one root is a *translation* and the
    point of the exercise. Only a name carried twice by the same configuration
    is the silent-loss defect C3 is looking for.

    Empty for the grit catalogue, which has no configurations — a `.grd` and
    the `.grdp` it carries sit in different directories, so returning their
    directory names here would put two definitions of one message id in
    different C3 buckets and stop the duplicate being reported at all.
    """
    parent = os.path.basename(os.path.dirname(source))
    return parent if parent.startswith("values") else ""
