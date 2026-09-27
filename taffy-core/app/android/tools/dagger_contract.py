#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Enforce Dagger Kotlin annotation processing for every mounted GN module."""

from __future__ import annotations

import os
import re


DAGGER_MODULES = (
    "core/common",
    "core/analytics",
    "core/assets",
    "core/browser",
    "core/providerauth",
    "core/providers",
    "core/page-intelligence",
    "core/task",
    "core/workspace",
    "core/ui",
    "feature/browsing",
    "feature/downloads",
    "feature/assistant",
    "feature/workspaces",
    "feature/settings",
    "feature/onboarding",
    "feature/providers",
    "app",
)

ANDROID_BUILD_FILE = os.path.join("android", "ui", "BUILD.gn")
DAGGER_TEMPLATE = "taffy_dagger_library"

_DISCOVERY_ANNOTATION = re.compile(
    r"^\s*@(Inject|Module|Provides|Binds|Component|Subcomponent|MapKey|Scope|Qualifier)\b"
    r"|^\s*import\s+dagger\b",
    re.MULTILINE,
)
_GN_TARGET = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\("([^"]+)"\)\s*\{', re.MULTILINE)


def carries_discovery_annotation(text: str) -> bool:
    return bool(_DISCOVERY_ANNOTATION.search(text))


def source_variable(module: str) -> str:
    return "taffy_" + module.replace("/", "_").replace("-", "_") + "_sources"


def _target_templates(build_file: str) -> dict[str, tuple[str, str]]:
    with open(build_file, encoding="utf-8") as handle:
        text = handle.read()
    targets: dict[str, tuple[str, str]] = {}
    for match in _GN_TARGET.finditer(text):
        template, name = match.group(1), match.group(2)
        depth, index = 0, match.end() - 1
        while index < len(text):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        targets[name] = (template, text[match.end() : index])
    return targets


def findings(component_dir: str, carriers: dict[str, list[str]]) -> list[str]:
    """Return missing/stale processor declarations in both directions."""
    build_file = os.path.join(component_dir, ANDROID_BUILD_FILE)
    if not os.path.isfile(build_file):
        return [f"{ANDROID_BUILD_FILE}: missing"]
    targets = _target_templates(build_file)
    found: list[str] = []
    for module in sorted(carriers):
        files = carriers[module]
        if files and module not in DAGGER_MODULES:
            found.append(f"{module}: Dagger/Jakarta injection is compiled without kapt")
        if module in DAGGER_MODULES and not files:
            found.append(f"{module}: stale DAGGER_MODULES entry")
        if not files:
            continue
        variable = source_variable(module)
        owners = [name for name, (_template, body) in targets.items() if variable in body]
        if not owners:
            found.append(f"{module}: no GN target consumes {variable}")
        for name in owners:
            template = targets[name][0]
            if template != DAGGER_TEMPLATE:
                found.append(
                    f'{module}: {template}("{name}") must use {DAGGER_TEMPLATE}'
                )
    return found
