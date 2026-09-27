#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Mount the canonical Android UI sources into Chromium's ``//taffy`` tree."""

from __future__ import annotations

import argparse
import os
import re
import sys

import dagger_contract
import resource_mounts


MODULES: dict[str, str] = {
    "app": "com.taffygo.browser.ui.app",
    "core/analytics": "com.taffygo.browser.ui.core.analytics",
    "core/api": "com.taffygo.browser.ui.core.api",
    "core/assets": "com.taffygo.browser.ui.core.assets",
    "core/browser": "com.taffygo.browser.ui.core.browser",
    "core/common": "com.taffygo.browser.ui.core.common",
    "core/credentials": "com.taffygo.browser.ui.core.credentials",
    "core/designsystem": "com.taffygo.browser.ui.core.designsystem",
    "core/model": "com.taffygo.browser.ui.core.model",
    "core/page-intelligence": "com.taffygo.browser.ui.core.page",
    "core/preferences": "com.taffygo.browser.ui.core.preferences",
    "core/providerauth": "com.taffygo.browser.ui.core.providerauth",
    "core/providers": "com.taffygo.browser.ui.core.providers",
    "core/task": "com.taffygo.browser.ui.core.task",
    "core/ui": "com.taffygo.browser.ui.core.ui",
    "core/workspace": "com.taffygo.browser.ui.core.workspace",
    "feature/assistant": "com.taffygo.browser.ui.feature.assistant",
    "feature/browsing": "com.taffygo.browser.ui.feature.browsing",
    "feature/downloads": "com.taffygo.browser.ui.feature.downloads",
    "feature/onboarding": "com.taffygo.browser.ui.feature.onboarding",
    "feature/providers": "com.taffygo.browser.ui.feature.providers",
    "feature/settings": "com.taffygo.browser.ui.feature.settings",
    "feature/workspaces": "com.taffygo.browser.ui.feature.workspaces",
}

MOUNTED_MODULES = tuple(MODULES)
DAGGER_MODULES = dagger_contract.DAGGER_MODULES
BLOCKED_MODULES: dict[str, str] = {}

# Only Core API appears here, and BIP deliberately does not. BIP terminates at
# the browser-side page intelligence adapter: raw BIP enumerations, encoded
# graph bytes and capability references are not things the Android layer is
# trusted to hold, so Kotlin consumes the separate, profile-scoped Core API
# instead. `taffy-core/contracts/bip/mojom/BUILD.gn` records the same decision
# from the other end, where `generate_java` is absent on purpose. Mounting a
# generated BIP source set here would hand Android the contract that decision
# keeps away from it.
GENERATED_SOURCE_SETS: dict[str, str] = {
    "taffy-core/contracts/core-api/generated/kotlin": "taffy.core_api",
}

RESOURCE_MODULES = resource_mounts.RESOURCE_MODULES
RESOURCE_NAME_PREFIX = resource_mounts.RESOURCE_NAME_PREFIX
UNMOUNTABLE_RESOURCE_MODULES = resource_mounts.UNMOUNTABLE_RESOURCE_MODULES
module_resource_dir = resource_mounts.module_resource_dir

# Chromium supplies the Application and Activity owners. The canonical UI tree
# therefore contains no Gradle-only platform owner that could become a second
# process-global profile graph.
REPLACED_FILES: dict[str, dict[str, str]] = {}

KOTLIN_MOUNT_DIR = os.path.join("android", "kotlin")
GITIGNORE_ENTRY = "/taffy-core/app/android/kotlin/"

_DAGGER_IMPORT = re.compile(r"^\s*import\s+dagger\b", re.MULTILINE)
_PREVIEW_ONLY = re.compile(r"^\s*import\s+androidx\.compose\.ui\.tooling\b", re.MULTILINE)
_UNAVAILABLE_IN_GN = re.compile(
    r"^\s*import\s+kotlinx\.serialization\b|^\s*import\s+androidx\.datastore\b",
    re.MULTILINE,
)
_ROOT_MARKERS = ("chromium/REVISION", "TOOLCHAIN.md", "Cargo.toml")


class ContractError(Exception):
    """The source/build mount contract is inconsistent."""


def repository_root() -> str:
    root = os.path.dirname(os.path.realpath(__file__))
    for _ in range(4):
        root = os.path.dirname(root)
    for marker in _ROOT_MARKERS:
        if not os.path.exists(os.path.join(root, marker)):
            raise ContractError(f"{root} is missing {marker}")
    return root


def overlay_component_dir() -> str:
    directory = os.path.dirname(os.path.realpath(__file__))
    return os.path.dirname(os.path.dirname(directory))


def package_path(package: str) -> str:
    return package.replace(".", os.sep)


def module_source_dir(root: str, module: str, package: str) -> str:
    return os.path.join(
        root,
        "taffy-core",
        "ui",
        "android",
        module,
        "src",
        "main",
        "kotlin",
        package_path(package),
    )


def mount_path(component_dir: str, package: str) -> str:
    return os.path.join(component_dir, KOTLIN_MOUNT_DIR, package_path(package))


def resource_mount_path(component_dir: str, module: str) -> str:
    return resource_mounts.resource_mount_path(component_dir, KOTLIN_MOUNT_DIR, module)


def generated_source_dir(root: str, directory: str) -> str:
    return os.path.join(root, directory)


def kotlin_files(directory: str) -> list[str]:
    found: list[str] = []
    for current, _directories, files in os.walk(directory):
        for name in sorted(files):
            if name.endswith(".kt"):
                found.append(os.path.relpath(os.path.join(current, name), directory))
    return sorted(found)


def _type_name(relative: str) -> str:
    return os.path.splitext(os.path.basename(relative))[0]


def _imports_one_of(text: str, names: set[str]) -> bool:
    imports = re.findall(r"^\s*import\s+([\w.]+)\s*$", text, re.MULTILINE)
    return any(value.rsplit(".", 1)[-1] in names for value in imports)


def excluded_files(
    directory: str,
    dagger: bool = False,
    replaced: dict[str, str] | None = None,
) -> list[str]:
    """Return non-shipping preview, unavailable-library, and replaced files."""
    texts: dict[str, str] = {}
    for relative in kotlin_files(directory):
        with open(os.path.join(directory, relative), encoding="utf-8") as handle:
            texts[relative] = handle.read()
    named = dict(replaced or {})
    for relative in named:
        if relative not in texts:
            raise ContractError(f"{directory}: replacement {relative} no longer exists")
    excluded = set(named) | {
        relative
        for relative, source in texts.items()
        if (not dagger and _DAGGER_IMPORT.search(source))
        or _PREVIEW_ONLY.search(source)
        or _UNAVAILABLE_IN_GN.search(source)
    }
    while True:
        names = {_type_name(relative) for relative in excluded}
        added = {
            relative
            for relative, source in texts.items()
            if relative not in excluded and _imports_one_of(source, names)
        }
        if not added:
            return sorted(excluded)
        excluded |= added


def discovery_carriers(root: str) -> dict[str, list[str]]:
    carriers: dict[str, list[str]] = {}
    for module in MOUNTED_MODULES:
        package = MODULES[module]
        directory = module_source_dir(root, module, package)
        excluded = set(
            excluded_files(
                directory,
                dagger=module in DAGGER_MODULES,
                replaced=REPLACED_FILES.get(module),
            )
        )
        found: list[str] = []
        for relative in kotlin_files(directory):
            if relative in excluded:
                continue
            with open(os.path.join(directory, relative), encoding="utf-8") as handle:
                if dagger_contract.carries_discovery_annotation(handle.read()):
                    found.append(relative)
        carriers[module] = found
    return carriers


def census(root: str) -> list[tuple[str, int, list[str]]]:
    rows: list[tuple[str, int, list[str]]] = []
    for module, package in MODULES.items():
        directory = module_source_dir(root, module, package)
        if not os.path.isdir(directory):
            raise ContractError(f"{module}: no sources at {directory}")
        excluded = excluded_files(
            directory,
            dagger=module in DAGGER_MODULES,
            replaced=REPLACED_FILES.get(module),
        )
        rows.append((module, len(kotlin_files(directory)), excluded))
    return rows


def _verify_link(link: str, target: str, label: str) -> list[str]:
    if not os.path.islink(link):
        return [f"{label}: no mount at {link}; run --mount"]
    if os.path.realpath(link) != os.path.realpath(target):
        return [f"{label}: mount points at {os.path.realpath(link)}, not {target}"]
    return []


def verify(root: str, component_dir: str) -> list[str]:
    findings: list[str] = []
    findings += dagger_contract.findings(component_dir, discovery_carriers(root))
    for module in MOUNTED_MODULES:
        package = MODULES[module]
        findings += _verify_link(
            mount_path(component_dir, package),
            module_source_dir(root, module, package),
            module,
        )
    for directory, package in GENERATED_SOURCE_SETS.items():
        findings += _verify_link(
            mount_path(component_dir, package),
            generated_source_dir(root, directory),
            directory,
        )
    for module in RESOURCE_MODULES:
        target = module_resource_dir(root, module)
        if not os.path.isdir(target):
            findings.append(f"{module}: declared resource module has no res directory")
            continue
        findings += resource_mounts.unreserved_names(module, target)
        findings += _verify_link(resource_mount_path(component_dir, module), target, module)
    with open(os.path.join(root, ".gitignore"), encoding="utf-8") as handle:
        if GITIGNORE_ENTRY not in {line.strip() for line in handle}:
            findings.append(f".gitignore is missing {GITIGNORE_ENTRY}")
    return findings


def _mount_link(link: str, target: str) -> None:
    os.makedirs(os.path.dirname(link), exist_ok=True)
    if os.path.islink(link):
        if os.path.realpath(link) == os.path.realpath(target):
            return
        os.unlink(link)
    elif os.path.exists(link):
        raise ContractError(f"{link} is not a symlink; refusing to replace it")
    os.symlink(os.path.relpath(target, os.path.dirname(link)), link)


def mount(root: str, component_dir: str) -> None:
    for module in MOUNTED_MODULES:
        package = MODULES[module]
        _mount_link(
            mount_path(component_dir, package),
            module_source_dir(root, module, package),
        )
    for directory, package in GENERATED_SOURCE_SETS.items():
        _mount_link(
            mount_path(component_dir, package),
            generated_source_dir(root, directory),
        )
    for module in RESOURCE_MODULES:
        _mount_link(resource_mount_path(component_dir, module), module_resource_dir(root, module))


def report(root: str) -> None:
    print(f"{'module':<28}{'files':>7}{'excluded':>10}")
    for module, count, excluded in census(root):
        print(f"{module:<28}{count:>7}{len(excluded):>10}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--mount", action="store_true")
    action.add_argument("--verify", action="store_true")
    action.add_argument("--report", action="store_true")
    arguments = parser.parse_args(argv)
    try:
        root = repository_root()
        component_dir = overlay_component_dir()
        if arguments.report:
            report(root)
            return 0
        if arguments.mount:
            mount(root, component_dir)
            print(f"mounted {len(MOUNTED_MODULES)} modules")
            return 0
        findings = verify(root, component_dir)
    except ContractError as error:
        print(f"kotlin_mounts: {error}", file=sys.stderr)
        return 1
    if findings:
        print("\n".join(f"  {finding}" for finding in findings))
        return 1
    print(f"{len(MOUNTED_MODULES)} mounted modules, contract holds")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
