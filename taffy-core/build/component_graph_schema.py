# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Schema and decoding for the authoritative TaffyGo component manifest."""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 3
ROOT_LABEL = "//taffy"
STATES = {"active", "planned"}
PROCESSES = {"assembly", "browser", "build", "neutral", "renderer", "ui", "utility"}
TRUSTS = {"assembly", "browser", "build", "core", "platform", "sandboxed"}
PLATFORMS = {"android", "host", "macos", "windows"}
COMPONENT_KEYS = {
    "id",
    "state",
    "process",
    "trust",
    "layer",
    "platforms",
    "sources",
    "direct_deps",
    "gn_targets",
    "gradle_path",
    "gradle_api_surface",
    "gradle_api_deps",
    "test_only",
}
PROCESS_DEPS = {
    "assembly": {"assembly", "browser", "neutral", "renderer", "ui", "utility"},
    "browser": {"browser", "neutral"},
    "build": {"build"},
    "neutral": {"neutral"},
    "renderer": {"neutral", "renderer"},
    "ui": {"neutral", "ui"},
    "utility": {"neutral", "utility"},
}
TRUST_DEPS = {
    "assembly": {"assembly", "browser", "core", "platform", "sandboxed"},
    "browser": {"browser", "core"},
    "build": {"build"},
    "core": {"core"},
    "platform": {"core", "platform"},
    "sandboxed": {"core", "sandboxed"},
}
PROCESS_TRUST = {
    "assembly": "assembly",
    "browser": "browser",
    "build": "build",
    "neutral": "core",
    "renderer": "sandboxed",
    "ui": "platform",
    "utility": "sandboxed",
}
SOURCE_SUFFIXES = {
    ".cc",
    ".cpp",
    ".css",
    ".gn",
    ".gni",
    ".grd",
    ".grdp",
    ".h",
    ".hpp",
    ".java",
    ".json",
    ".kt",
    ".kts",
    ".mojom",
    ".proto",
    ".py",
    ".rs",
    ".sql",
    ".toml",
    ".ts",
    ".tsx",
    ".xml",
}
ROOT_INFRASTRUCTURE = {"BUILD.gn"}
ID_PATTERN = re.compile(r"^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$")
GN_TARGET_PATTERN = re.compile(
    r"^//taffy(?:/[A-Za-z0-9_./-]+)?(?::[A-Za-z0-9_.-]+)?$"
)
GRADLE_PATH_PATTERN = re.compile(r"^:[a-z][a-z0-9-]*(?::[a-z][a-z0-9-]*)*$")
GRADLE_API_SURFACES = {"none", "flat", "split"}


@dataclass(frozen=True)
class Component:
    id: str
    state: str
    process: str
    trust: str
    layer: int
    platforms: tuple[str, ...]
    sources: tuple[str, ...]
    direct_deps: tuple[str, ...]
    gn_targets: tuple[str, ...]
    gradle_path: str
    gradle_api_surface: str
    gradle_api_deps: tuple[str, ...]
    test_only: bool


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as handle:
            document = tomllib.load(handle)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error
    if not isinstance(document, dict):
        raise ValueError(f"{path} is not a TOML table")
    return document


def _string_list(
    row: dict[str, Any], key: str, label: str, findings: list[str]
) -> tuple[str, ...]:
    value = row.get(key)
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        findings.append(f"{label}.{key}: expected an array of strings")
        return ()
    return tuple(value)


def decode_components(document: dict[str, Any]) -> tuple[list[Component], list[str]]:
    findings: list[str] = []
    unknown_top = set(document) - {"schema_version", "root_label", "component"}
    if unknown_top:
        findings.append(f"manifest: unknown keys: {', '.join(sorted(unknown_top))}")
    if document.get("schema_version") != SCHEMA_VERSION:
        findings.append(f"manifest.schema_version: expected {SCHEMA_VERSION}")
    if document.get("root_label") != ROOT_LABEL:
        findings.append(
            f"manifest.root_label: expected {ROOT_LABEL}; compatibility labels are forbidden"
        )

    rows = document.get("component")
    if not isinstance(rows, list) or not rows:
        findings.append("manifest.component: expected at least one [[component]] table")
        return [], findings

    components: list[Component] = []
    for index, raw in enumerate(rows, start=1):
        label = f"component[{index}]"
        if not isinstance(raw, dict):
            findings.append(f"{label}: expected a table")
            continue
        missing = COMPONENT_KEYS - set(raw)
        unknown = set(raw) - COMPONENT_KEYS
        if missing:
            findings.append(f"{label}: missing keys: {', '.join(sorted(missing))}")
        if unknown:
            findings.append(f"{label}: unknown keys: {', '.join(sorted(unknown))}")
        scalar_keys = (
            "id",
            "state",
            "process",
            "trust",
            "gradle_path",
            "gradle_api_surface",
        )
        if any(not isinstance(raw.get(key), str) for key in scalar_keys):
            findings.append(f"{label}: scalar component fields must be strings")
            continue
        if not isinstance(raw.get("test_only"), bool):
            findings.append(f"{label}.test_only: expected true or false")
            continue
        # `bool` is a subclass of `int`, so the isinstance order matters: a
        # `layer = true` must be rejected rather than silently read as 1.
        layer = raw.get("layer")
        if isinstance(layer, bool) or not isinstance(layer, int) or layer <= 0:
            findings.append(f"{label}.layer: expected a positive integer tier")
            continue
        components.append(
            Component(
                id=raw["id"],
                state=raw["state"],
                process=raw["process"],
                trust=raw["trust"],
                layer=raw["layer"],
                platforms=_string_list(raw, "platforms", label, findings),
                sources=_string_list(raw, "sources", label, findings),
                direct_deps=_string_list(raw, "direct_deps", label, findings),
                gn_targets=_string_list(raw, "gn_targets", label, findings),
                gradle_path=raw["gradle_path"],
                gradle_api_surface=raw["gradle_api_surface"],
                gradle_api_deps=_string_list(raw, "gradle_api_deps", label, findings),
                test_only=raw["test_only"],
            )
        )
    return components, findings
