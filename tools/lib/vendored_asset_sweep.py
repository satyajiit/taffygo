# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Disk-first discovery helpers for shipping and registered assets."""

from __future__ import annotations

import json
import os
from collections.abc import Mapping, Set
from typing import Any


def icon_manifest_assets(
    repo_root: str,
    manifest: str,
) -> tuple[set[str], list[str]]:
    """Return first-party binary outputs owned by an icon manifest."""
    manifest_path = os.path.join(repo_root, manifest)
    try:
        with open(manifest_path, encoding="utf-8") as handle:
            document = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        return set(), [f"{manifest}: cannot read icon provenance: {error}"]

    sets = document.get("sets")
    if not isinstance(sets, list):
        return set(), [f"{manifest}: `sets` must be an array"]
    outputs: set[str] = set()
    findings: list[str] = []
    for set_index, icon_set in enumerate(sets):
        assets = icon_set.get("assets") if isinstance(icon_set, dict) else None
        if not isinstance(assets, list):
            findings.append(f"{manifest}: sets[{set_index}].assets must be an array")
            continue
        for asset_index, asset in enumerate(assets):
            output = asset.get("output") if isinstance(asset, dict) else None
            if not isinstance(output, str) or not output:
                findings.append(
                    f"{manifest}: sets[{set_index}].assets[{asset_index}] has no output"
                )
                continue
            normalized = os.path.normpath(output)
            if normalized in outputs:
                findings.append(f"{manifest}: output is declared twice: {output}")
            outputs.add(normalized)
    return outputs, findings


def shipping_binary_assets(
    repo_root: str,
    android_ui: str,
    suffixes: Set[str],
) -> set[str]:
    """Return every binary physically present below a shipping Android res tree."""
    root = os.path.join(repo_root, android_ui)
    assets: set[str] = set()
    for base, directories, files in os.walk(root):
        directories[:] = [
            name for name in directories if name not in (".gradle", ".idea", "build")
        ]
        normalized_base = base.replace(os.sep, "/")
        if "/src/main/res/" not in normalized_base + "/":
            continue
        for name in files:
            if os.path.splitext(name)[1].lower() not in suffixes:
                continue
            assets.add(os.path.normpath(os.path.relpath(os.path.join(base, name), repo_root)))
    return assets


def registered_asset_files(
    repo_root: str,
    register: Mapping[str, Mapping[str, Any]],
) -> set[str]:
    """Return every concrete file held by an explicit provenance record."""
    assets: set[str] = set()
    for entry in register.values():
        for relative in entry["assets"]:
            path = os.path.join(repo_root, relative)
            if os.path.isfile(path):
                assets.add(os.path.normpath(relative))
            elif os.path.isdir(path):
                for base, _directories, files in os.walk(path):
                    assets.update(
                        os.path.normpath(os.path.relpath(os.path.join(base, name), repo_root))
                        for name in files
                    )
    return assets


def unregistered_binary_findings(
    shipping: set[str],
    registered: set[str],
) -> list[str]:
    """Find shipping binaries that no source/provenance manifest owns."""
    return [
        f"{asset}: shipping binary has no provenance or derivation record"
        for asset in sorted(shipping - registered)
    ]


def provenance_files(
    repo_root: str,
    register: Mapping[str, Mapping[str, Any]],
) -> list[str]:
    """Return every `vendor/*.txt` in the tree that must be a register key.

    A `vendor/*.txt` is one of two things: a provenance record, which has to
    be a key of the register, or the licence text some record points at. The
    register already says which files are licence texts — every path in an
    entry's `licences` — so that is what decides it here.

    This used to read the filename instead, treating `.OFL.`, `.MIT.` and
    `.GPL3.` as the spellings a licence text may have. Three is not the number
    of licences a vendored tree can arrive under, and the failure was silent
    in the direction that matters: an Apache-2.0 text was reported as an
    unregistered provenance record, and a licence text nobody registered was
    skipped entirely rather than named. Reading the register answers both.
    """
    licence_texts = {
        path for entry in register.values() for path in entry.get("licences", ())
    }
    found: list[str] = []
    for base, directories, files in os.walk(repo_root):
        directories[:] = [
            name for name in directories if name not in (".git", "build", "node_modules")
        ]
        if base == repo_root:
            # Agent worktrees nest whole checkouts of other revisions under
            # .claude/worktrees, excluded through .git/info/exclude; their
            # vendor/ files belong to those revisions, not to this tree.
            directories[:] = [name for name in directories if name != ".claude"]
        if os.path.basename(base) != "vendor":
            continue
        for name in sorted(files):
            if not name.endswith(".txt"):
                continue
            relative = os.path.relpath(os.path.join(base, name), repo_root)
            if relative in register or relative not in licence_texts:
                found.append(relative)
    return found
