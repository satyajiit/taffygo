#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Validate and project taffy-core/build/components.toml.

The manifest is deliberately data-only. This tool is the one interpretation of
its process, trust, platform, source-ownership, and dependency rules. It uses
only the Python standard library and writes generated projections to stdout.
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from pathlib import Path
from typing import Any

from component_graph_outputs import (
    format_android_modules,
    format_gn,
    format_gn_component_graph,
    format_gradle,
    format_json,
)
from component_graph_selftest import run_self_test
from component_graph_schema import (
    GN_TARGET_PATTERN,
    GRADLE_API_SURFACES,
    GRADLE_PATH_PATTERN,
    ID_PATTERN,
    PLATFORMS,
    PROCESSES,
    PROCESS_DEPS,
    PROCESS_TRUST,
    ROOT_INFRASTRUCTURE,
    SOURCE_SUFFIXES,
    STATES,
    TRUSTS,
    TRUST_DEPS,
    Component,
    decode_components,
    load_manifest,
)

GENERATED_PROJECTIONS = {
    "components.gni": "gn",
    "components.json": "json",
    "gn-component-graph.json": "gn-graph",
    "android-modules.settings.gradle.kts": "gradle",
    "android-modules.tsv": "gradle-data",
}

LEGACY_NAMESPACE_MARKERS = (
    "//components/taffy",
    "components/taffy/",
    "src/components/taffy",
)
LEGACY_SCAN_EXEMPT = {
    "build/component_graph.py",
    "build/component_graph_selftest.py",
}
IGNORED_GENERATED_DIRS = {".gradle", ".idea", "__pycache__", "target"}
# A checksum-verified upstream checkout populated by tools/chromium/sync.  The
# exact path is excluded; first-party cpython tooling beside it remains owned.
FOREIGN_SOURCE_ROOTS = (("third_party", "cpython", "src"),)


#: An include of another product-root header, which is the only kind this
#: rule can attribute to a component.
_PRODUCT_INCLUDE = re.compile(r'^#include\s+"taffy/([^"]+)"', re.MULTILINE)


def _is_generated_path(path: Path, core_root: Path) -> bool:
    relative_parts = path.relative_to(core_root).parts
    return any(part in IGNORED_GENERATED_DIRS for part in relative_parts) or (
        "build" in relative_parts[1:]
    )


def _is_foreign_source_path(path: Path, core_root: Path) -> bool:
    relative_parts = path.relative_to(core_root).parts
    return any(
        relative_parts[: len(root)] == root for root in FOREIGN_SOURCE_ROOTS
    )


def _source_root(pattern: str) -> tuple[str, bool] | None:
    is_directory = pattern.endswith("/**")
    root = pattern[:-3] if is_directory else pattern
    if not root or root.startswith("/") or root.startswith(".") or ".." in root.split("/"):
        return None
    if any(character in root for character in "*?[]\\"):
        return None
    if not is_directory and "/" not in root:
        return None
    return root.rstrip("/"), is_directory


def _cycle(components: dict[str, Component]) -> list[str] | None:
    visited: set[str] = set()
    visiting: list[str] = []

    def visit(component_id: str) -> list[str] | None:
        if component_id in visiting:
            start = visiting.index(component_id)
            return visiting[start:] + [component_id]
        if component_id in visited:
            return None
        visiting.append(component_id)
        for dependency in components[component_id].direct_deps:
            if dependency in components:
                found = visit(dependency)
                if found:
                    return found
        visiting.pop()
        visited.add(component_id)
        return None

    for component_id in sorted(components):
        found = visit(component_id)
        if found:
            return found
    return None


def _portable_include_findings(core_root: Path, components: list[Component]) -> list[str]:
    """Hold the `platforms` column against what the sources actually include.

    The column declares where a component is meant to run, and the manifest
    already requires a component's platforms to be a subset of every component
    it *declares* a dependency on. That leaves the case this closes: a source
    file in a portable component including a header owned by an Android-only
    one without a declared edge. Neither the subset rule nor `checkdeps` sees
    it — the first because there is no edge, the second because DEPS reports
    what a rule forbids and knows nothing about platforms — so the column
    stays green while the code stops being portable.

    A desktop component may reach anything that also builds for desktop, and
    `host` components are build-time tools that ship nowhere and are skipped.
    """
    desktop = {"macos", "windows"}
    owners = _source_owners(core_root, components)
    by_id = {component.id: component for component in components}
    findings: list[str] = []
    for path, owner_id in sorted(owners.items()):
        if not path.endswith((".cc", ".h", ".mm")):
            continue
        owner = by_id.get(owner_id)
        if owner is None or owner.test_only or not desktop & set(owner.platforms):
            continue
        text = (core_root / path).read_text(encoding="utf-8", errors="replace")
        for match in _PRODUCT_INCLUDE.finditer(text):
            included = match.group(1)
            target = by_id.get(owners.get(included, ""))
            if target is None or desktop & set(target.platforms):
                continue
            findings.append(
                f"{path}: {owner.id} declares {sorted(desktop & set(owner.platforms))} but "
                f"includes taffy/{included}, owned by {target.id}, which declares "
                f"{list(target.platforms)}"
            )
    return findings


def _source_owners(core_root: Path, components: list[Component]) -> dict[str, str]:
    """Map each owned source path to its single owning component id."""
    owners: dict[str, str] = {}
    for path, path_owners in _owner_lists(core_root, components).items():
        if len(path_owners) == 1:
            owners[path] = path_owners[0]
    return owners


def _owner_lists(core_root: Path, components: list[Component]) -> dict[str, list[str]]:
    """Every owned source path mapped to the components claiming it."""
    files = sorted(
        path.relative_to(core_root).as_posix()
        for path in core_root.rglob("*")
        if path.is_file()
        and not _is_generated_path(path, core_root)
        and not _is_foreign_source_path(path, core_root)
        and (path.suffix in SOURCE_SUFFIXES or path.name == "BUILD.gn")
        and path.relative_to(core_root).as_posix() not in ROOT_INFRASTRUCTURE
    )
    owners: dict[str, list[str]] = {path: [] for path in files}
    for component in components:
        for pattern in component.sources:
            for path in files:
                if fnmatch.fnmatchcase(path, pattern):
                    owners[path].append(component.id)
    return owners


def _owned_source_findings(core_root: Path, components: list[Component]) -> list[str]:
    findings: list[str] = []
    owners = _owner_lists(core_root, components)
    for component in components:
        if component.state != "active":
            continue
        if not any(component.id in path_owners for path_owners in owners.values()):
            findings.append(f"{component.id}: active component has no source matching {list(component.sources)}")
    for path, path_owners in owners.items():
        if not path_owners:
            findings.append(f"{path}: source is not owned by a manifest component")
        elif len(path_owners) > 1:
            findings.append(f"{path}: source is owned by multiple components: {', '.join(path_owners)}")
    return findings


def _legacy_namespace_findings(core_root: Path) -> list[str]:
    """Reject live source references to the retired Chromium namespace."""
    findings: list[str] = []
    for path in sorted(candidate for candidate in core_root.rglob("*") if candidate.is_file()):
        relative = path.relative_to(core_root).as_posix()
        if (
            relative in LEGACY_SCAN_EXEMPT
            or _is_generated_path(path, core_root)
            or _is_foreign_source_path(path, core_root)
        ):
            continue
        if path.suffix not in SOURCE_SUFFIXES and path.name not in {"BUILD.gn", "DEPS"}:
            continue
        try:
            contents = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        for marker in LEGACY_NAMESPACE_MARKERS:
            if marker in contents:
                findings.append(f"{relative}: contains retired namespace marker {marker!r}")
                break
    return findings


def manifest_findings(
    document: dict[str, Any], core_root: Path | None = None, check_files: bool = True
) -> tuple[list[Component], list[str]]:
    components, findings = decode_components(document)
    ids = [component.id for component in components]
    if ids != sorted(ids):
        findings.append("manifest.component: rows must be sorted by id")
    duplicates = sorted({component_id for component_id in ids if ids.count(component_id) > 1})
    if duplicates:
        findings.append(f"manifest.component: duplicate ids: {', '.join(duplicates)}")
    by_id = {component.id: component for component in components}
    gradle_paths = {
        component.gradle_path: component
        for component in components
        if component.gradle_path
    }
    if len(gradle_paths) != sum(bool(component.gradle_path) for component in components):
        findings.append("manifest.component: Gradle paths must be unique")

    source_roots: list[tuple[str, bool, str]] = []
    for component in components:
        label = component.id
        if not ID_PATTERN.fullmatch(component.id):
            findings.append(f"{label}.id: expected lower-case dotted or hyphenated identifier")
        if component.state not in STATES:
            findings.append(f"{label}.state: expected one of {sorted(STATES)}")
        if component.process not in PROCESSES:
            findings.append(f"{label}.process: expected one of {sorted(PROCESSES)}")
        if component.trust not in TRUSTS:
            findings.append(f"{label}.trust: expected one of {sorted(TRUSTS)}")
        if component.process in PROCESS_TRUST and component.trust != PROCESS_TRUST[component.process]:
            findings.append(
                f"{label}: process {component.process} requires trust {PROCESS_TRUST[component.process]}"
            )
        if not component.platforms or any(platform not in PLATFORMS for platform in component.platforms):
            findings.append(f"{label}.platforms: expected non-empty values from {sorted(PLATFORMS)}")
        if list(component.platforms) != sorted(set(component.platforms)):
            findings.append(f"{label}.platforms: values must be unique and sorted")
        if component.process == "build" and component.platforms != ("host",):
            findings.append(f"{label}: build components must use only the host platform")
        if component.process != "build" and "host" in component.platforms:
            findings.append(f"{label}: product components cannot use the host platform")
        if not component.sources:
            findings.append(f"{label}.sources: at least one owned source root is required")
        for pattern in component.sources:
            root = _source_root(pattern)
            if root is None:
                findings.append(
                    f"{label}.sources: {pattern!r} must be a relative file or directory ending in /**"
                )
            else:
                source_roots.append((root[0], root[1], label))
        if list(component.sources) != sorted(set(component.sources)):
            findings.append(f"{label}.sources: values must be unique and sorted")
        if list(component.direct_deps) != sorted(set(component.direct_deps)):
            findings.append(f"{label}.direct_deps: values must be unique and sorted")
        if list(component.gn_targets) != sorted(set(component.gn_targets)):
            findings.append(f"{label}.gn_targets: values must be unique and sorted")
        for target in component.gn_targets:
            if not GN_TARGET_PATTERN.fullmatch(target):
                findings.append(
                    f"{label}.gn_targets: {target!r} must be an exact //taffy label"
                )
        if component.state == "planned" and component.gn_targets:
            findings.append(f"{label}: planned components cannot publish GN targets")
        if component.gradle_api_surface not in GRADLE_API_SURFACES:
            findings.append(
                f"{label}.gradle_api_surface: expected one of {sorted(GRADLE_API_SURFACES)}"
            )
        if component.gradle_path:
            if not GRADLE_PATH_PATTERN.fullmatch(component.gradle_path):
                findings.append(
                    f"{label}.gradle_path: expected an absolute lower-case Gradle path"
                )
            if component.gradle_api_surface == "none":
                findings.append(f"{label}: a Gradle module must declare flat or split API surface")
            if component.process != "ui" or component.platforms != ("android",):
                findings.append(
                    f"{label}: Gradle modules must be Android-only UI components"
                )
            expected_root = "ui/android/" + component.gradle_path.lstrip(":").replace(":", "/")
            if f"{expected_root}/**" not in component.sources:
                findings.append(
                    f"{label}: {component.gradle_path} must own {expected_root}/**"
                )
        elif component.gradle_api_surface != "none" or component.gradle_api_deps:
            findings.append(
                f"{label}: non-Gradle components require gradle_api_surface=none and no Gradle API deps"
            )
        if list(component.gradle_api_deps) != sorted(set(component.gradle_api_deps)):
            findings.append(f"{label}.gradle_api_deps: values must be unique and sorted")
        for dependency_id in component.gradle_api_deps:
            if dependency_id not in component.direct_deps:
                findings.append(
                    f"{label}.gradle_api_deps: {dependency_id} is not a direct dependency"
                )
            elif not by_id.get(dependency_id) or not by_id[dependency_id].gradle_path:
                findings.append(
                    f"{label}.gradle_api_deps: {dependency_id} is not a Gradle module"
                )
        if label in component.direct_deps:
            findings.append(f"{label}.direct_deps: self-dependency is forbidden")

        for dependency_id in component.direct_deps:
            dependency = by_id.get(dependency_id)
            if dependency is None:
                findings.append(f"{label}.direct_deps: unknown component {dependency_id}")
                continue
            if component.state == "active" and dependency.state != "active":
                findings.append(f"{label}: active component depends on planned component {dependency_id}")
            if not component.test_only and dependency.test_only:
                findings.append(f"{label}: shipping component depends on test-only component {dependency_id}")
            if component.process in PROCESS_DEPS and dependency.process not in PROCESS_DEPS[component.process]:
                findings.append(
                    f"{label}: {component.process} process cannot depend on {dependency.process} component {dependency_id}"
                )
            if component.trust in TRUST_DEPS and dependency.trust not in TRUST_DEPS[component.trust]:
                findings.append(
                    f"{label}: {component.trust} trust cannot depend on {dependency.trust} component {dependency_id}"
                )
            if not set(component.platforms).issubset(dependency.platforms):
                findings.append(
                    f"{label}: platforms {list(component.platforms)} exceed {dependency_id} {list(dependency.platforms)}"
                )
            if dependency.layer >= component.layer:
                findings.append(
                    f"{label}: layer {component.layer} cannot depend on layer {dependency.layer} "
                    f"component {dependency_id}; every edge falls strictly"
                )

    for index, (root, is_directory, owner) in enumerate(source_roots):
        for other_root, other_is_directory, other_owner in source_roots[index + 1 :]:
            overlaps = root == other_root
            overlaps = overlaps or (is_directory and other_root.startswith(f"{root}/"))
            overlaps = overlaps or (other_is_directory and root.startswith(f"{other_root}/"))
            if overlaps:
                findings.append(f"source ownership overlaps: {owner} {root}/ and {other_owner} {other_root}/")
    cycle = _cycle(by_id)
    if cycle:
        findings.append(f"component dependency cycle: {' -> '.join(cycle)}")
    if check_files and core_root is not None and core_root.is_dir():
        findings.extend(_owned_source_findings(core_root, components))
        findings.extend(_portable_include_findings(core_root, components))
        findings.extend(_legacy_namespace_findings(core_root))
    return components, findings


def _projection_contents(components: list[Component]) -> dict[str, str]:
    return {
        "components.gni": format_gn(components),
        "components.json": format_json(components),
        "gn-component-graph.json": format_gn_component_graph(components),
        "android-modules.settings.gradle.kts": format_gradle(components),
        "android-modules.tsv": format_android_modules(components),
    }


def _projection_findings(output_directory: Path, components: list[Component]) -> list[str]:
    findings: list[str] = []
    expected_projections = _projection_contents(components)
    for name, expected in expected_projections.items():
        path = output_directory / name
        try:
            actual = path.read_text(encoding="utf-8")
        except OSError:
            findings.append(f"{path}: generated projection is missing; run --write")
            continue
        if actual != expected:
            findings.append(f"{path}: generated projection is stale; run --write")
    if output_directory.is_dir():
        for path in sorted(output_directory.iterdir()):
            if path.is_file() and path.name not in expected_projections:
                findings.append(
                    f"{path}: unexpected generated projection; remove the retired output"
                )
    return findings


def _write_projections(output_directory: Path, components: list[Component]) -> None:
    output_directory.mkdir(parents=True, exist_ok=True)
    for name, contents in _projection_contents(components).items():
        (output_directory / name).write_text(contents, encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("components.toml"),
        help="manifest to read (default: beside this script)",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="validate the manifest (default)")
    mode.add_argument("--self-test", action="store_true", help="prove the validator catches broken graphs")
    mode.add_argument("--format", choices=("gn", "gradle", "json"), help="print a deterministic projection")
    mode.add_argument("--write", action="store_true", help="write committed GN, Gradle, and JSON projections")
    args = parser.parse_args(argv)
    if args.self_test:
        return run_self_test(
            manifest_findings,
            format_gn,
            format_gradle,
            _projection_contents,
            _projection_findings,
        )
    try:
        document = load_manifest(args.manifest)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    core_root = args.manifest.resolve().parent.parent
    components, findings = manifest_findings(document, core_root)
    output_directory = args.manifest.resolve().parent / "generated"
    if not findings and not args.format and not args.write:
        findings.extend(_projection_findings(output_directory, components))
    if findings:
        for finding in findings:
            print(f"component graph: {finding}", file=sys.stderr)
        return 1
    if args.format == "gn":
        sys.stdout.write(format_gn(components))
    elif args.format == "gradle":
        sys.stdout.write(format_gradle(components))
    elif args.format == "json":
        sys.stdout.write(format_json(components))
    elif args.write:
        _write_projections(output_directory, components)
        print(f"component graph: wrote {len(GENERATED_PROJECTIONS)} projections")
    else:
        active = sum(component.state == "active" for component in components)
        print(f"component graph: {len(components)} components ({active} active), no findings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
