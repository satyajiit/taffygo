#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Enforce the component manifest against static and configured GN graphs."""

from __future__ import annotations

import argparse
import fnmatch
import json
import platform
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from component_gn_parser import GnSourceReader, normalize_dependency, resolve_source
from component_gn_graph_selftest import run_self_test
from component_graph_schema import Component, decode_components, load_manifest


KNOWN_GENERATED_SUFFIXES = (
    "__",
    "_blink",
    "_cxx_generated",
    "_grd",
    "_grit",
    "_headers",
    "_java",
    "_java_sources",
    "_js",
    "_shared",
)
IMPLEMENTATION_SUFFIXES = frozenset((".cc", ".m", ".mm"))
GENERATED_PATH_SEGMENTS = frozenset(
    (
        ".gradle",
        ".kotlin",
        "__pycache__",
        "dist",
        "generated",
        "node_modules",
        "out",
        "target",
    )
)
GENERATED_MARKER = "@generated"
GENERATED_MARKER_WINDOW = 10
FOREIGN_SOURCE_ROOTS = (("third_party", "cpython", "src"),)


@dataclass(frozen=True)
class TargetView:
    label: str
    deps: frozenset[str]
    sources: frozenset[str]
    declaration: str
    test_only: bool


def _generated_parent(label: str, targets: dict[str, TargetView]) -> str | None:
    if ":" not in label:
        return None
    directory, name = label.rsplit(":", 1)
    candidates = []
    for candidate in targets:
        if not candidate.startswith(directory + ":"):
            continue
        candidate_name = candidate.rsplit(":", 1)[-1]
        suffix = name[len(candidate_name) :] if name.startswith(candidate_name) else ""
        if suffix and any(suffix.startswith(marker) for marker in KNOWN_GENERATED_SUFFIXES):
            candidates.append(candidate)
    return max(candidates, key=len) if candidates else None


def _source_owners(
    sources: frozenset[str],
    declaration: str,
    components: list[Component],
    core_root: Path,
    excluded: frozenset[str] = frozenset(),
) -> set[str]:
    owners: set[str] = set()
    for source in sources:
        if source in excluded:
            continue
        relative = resolve_source(source, declaration, core_root)
        if relative is None:
            continue
        for component in components:
            if any(fnmatch.fnmatchcase(relative, pattern) for pattern in component.sources):
                owners.add(component.id)
    return owners


def _path_owners(path: str, components: list[Component]) -> set[str]:
    return {
        component.id
        for component in components
        if any(fnmatch.fnmatchcase(path, pattern) for pattern in component.sources)
    }


def _target_owners(
    targets: dict[str, TargetView],
    components: list[Component],
    core_root: Path,
    owner_hints: dict[str, str] | None = None,
    excluded_sources: frozenset[str] = frozenset(),
) -> tuple[dict[str, str], list[str]]:
    findings: list[str] = []
    ambiguous: set[str] = set()
    explicit: dict[str, str] = dict(owner_hints or {})
    for component in components:
        for label in component.gn_targets:
            if label in explicit and explicit[label] != component.id:
                findings.append(
                    f"{label}: GN target is owned by both {explicit[label]} and {component.id}"
                )
            explicit[label] = component.id

    owners = dict(explicit)
    changed = True
    while changed:
        changed = False
        for label, target in targets.items():
            if label in owners:
                continue
            parent = _generated_parent(label, targets)
            if parent in owners:
                owners[label] = owners[parent]
                changed = True
                continue
            source_owners = _source_owners(
                target.sources, target.declaration, components, core_root, excluded_sources
            )
            if len(source_owners) == 1:
                owners[label] = next(iter(source_owners))
                changed = True
                continue
            if len(source_owners) > 1:
                declaration_owners = _path_owners(target.declaration, components)
                if len(declaration_owners) == 1:
                    declaration_owner = next(iter(declaration_owners))
                    if declaration_owner in source_owners:
                        owners[label] = declaration_owner
                        changed = True
                        continue
                if label not in ambiguous:
                    findings.append(
                        f"{label}: target mixes sources owned by "
                        f"{', '.join(sorted(source_owners))}; declare it as a "
                        "component GN target or split it"
                    )
                    ambiguous.add(label)
                continue
            declaration_owners = _path_owners(target.declaration, components)
            if len(declaration_owners) == 1:
                owners[label] = next(iter(declaration_owners))
                changed = True
                continue
            if len(declaration_owners) > 1 and label not in ambiguous:
                findings.append(
                    f"{label}: declaration is owned by "
                    f"{', '.join(sorted(declaration_owners))}; declare the target explicitly"
                )
                ambiguous.add(label)
                continue
    return owners, findings


def _component_edges(
    component: Component,
    targets: dict[str, TargetView],
    owners: dict[str, str],
    components: list[Component],
    core_root: Path,
    excluded_sources: frozenset[str] = frozenset(),
) -> tuple[set[str], list[str]]:
    actual: set[str] = set()
    findings: list[str] = []
    seen: set[str] = set()
    pending = [
        label
        for label, owner in owners.items()
        if owner == component.id
        and label in targets
        and targets[label].test_only == component.test_only
    ]
    while pending:
        label = pending.pop()
        if label in seen:
            continue
        seen.add(label)
        target = targets[label]

        for owner in _source_owners(
            target.sources, target.declaration, components, core_root, excluded_sources
        ):
            if owner != component.id:
                actual.add(owner)

        for dependency in target.deps:
            normalized = normalize_dependency(dependency, target.declaration)
            if normalized is None:
                continue
            if normalized not in targets:
                parent = _generated_parent(normalized, targets)
                if parent is None:
                    findings.append(
                        f"{label}: first-party dependency {normalized} is not a declared GN target"
                    )
                    continue
                normalized = parent
            owner = owners.get(normalized)
            if owner is None or owner == component.id:
                pending.append(normalized)
            else:
                actual.add(owner)
    return actual, findings


def graph_findings(
    targets: dict[str, TargetView],
    components: list[Component],
    core_root: Path,
    *,
    platform_name: str | None = None,
    owner_hints: dict[str, str] | None = None,
    strict_ownership: bool = True,
    excluded_sources: frozenset[str] = frozenset(),
) -> list[str]:
    findings: list[str] = []
    owners, owner_findings = _target_owners(
        targets,
        components,
        core_root,
        owner_hints=owner_hints,
        excluded_sources=excluded_sources,
    )
    findings.extend(owner_findings)
    by_id = {component.id: component for component in components}
    in_scope = {
        component.id
        for component in components
        if component.state == "active"
        and (
            platform_name is None
            or platform_name in component.platforms
            or "host" in component.platforms
        )
    }
    for component in components:
        if component.id not in in_scope:
            continue
        for label in component.gn_targets:
            target = targets.get(label)
            if target is None:
                findings.append(f"{component.id}: declared GN target {label} does not exist")
            elif target.test_only != component.test_only:
                findings.append(
                    f"{component.id}: declared GN target {label} has "
                    f"testonly={str(target.test_only).lower()}"
                )
    for label, target in targets.items():
        owner = owners.get(label)
        if owner is None:
            if strict_ownership:
                findings.append(f"{label}: GN target has no component owner")
            continue
        component = by_id[owner]
        if component.state != "active" and platform_name is None:
            findings.append(f"{label}: planned component {owner} owns an active GN target")
        if component.test_only and not target.test_only:
            findings.append(f"{label}: test-only component {owner} owns a shipping GN target")
    for component in components:
        if component.id not in in_scope or not component.gn_targets:
            continue
        actual, edge_findings = _component_edges(
            component, targets, owners, components, core_root, excluded_sources
        )
        findings.extend(edge_findings)
        expected = set(component.direct_deps)
        for dependency in sorted(expected - actual):
            findings.append(
                f"{component.id}: manifest dependency {dependency} has no GN edge or foreign source"
            )
        for dependency in sorted(actual - expected):
            findings.append(
                f"{component.id}: GN graph has undeclared component dependency {dependency}"
            )
    return findings


def _static_targets(core_root: Path) -> tuple[dict[str, TargetView], list[str]]:
    parsed, findings = GnSourceReader(core_root).read()
    targets = {
        label: TargetView(
            label=target.label,
            deps=frozenset(
                value
                for value in target.deps
                if value.startswith(("//taffy", ":"))
            ),
            sources=target.sources,
            declaration=target.declaration,
            test_only=target.test_only,
        )
        for label, target in parsed.items()
    }
    return targets, findings


def _is_generated_implementation(path: Path, core_root: Path) -> bool:
    relative_parts = path.relative_to(core_root).parts
    if set(relative_parts) & GENERATED_PATH_SEGMENTS:
        return True
    try:
        with path.open(encoding="utf-8", errors="replace") as source:
            return any(
                GENERATED_MARKER in line
                for _index, line in zip(range(GENERATED_MARKER_WINDOW), source)
            )
    except OSError:
        return False


def _is_foreign_source(path: Path, core_root: Path) -> bool:
    relative_parts = path.relative_to(core_root).parts
    return any(
        relative_parts[: len(root)] == root for root in FOREIGN_SOURCE_ROOTS
    )


def _orphan_implementation_findings(
    targets: dict[str, TargetView], core_root: Path
) -> list[str]:
    referenced: set[str] = set()
    for target in targets.values():
        for source in target.sources:
            relative = resolve_source(source, target.declaration, core_root)
            if relative is not None:
                referenced.add(relative)

    implementations = sorted(
        path.relative_to(core_root).as_posix()
        for path in core_root.rglob("*")
        if path.is_file()
        and path.suffix in IMPLEMENTATION_SUFFIXES
        and not _is_generated_implementation(path, core_root)
        and not _is_foreign_source(path, core_root)
    )
    return [
        f"{path}: "
        "implementation is absent from every parsed GN target sources/inputs list"
        for path in implementations
        if path not in referenced
    ]


def static_findings(core_root: Path, components: list[Component]) -> list[str]:
    targets, findings = _static_targets(core_root)
    findings.extend(_orphan_implementation_findings(targets, core_root))
    findings.extend(graph_findings(targets, components, core_root))
    return findings


def _normalize_live_label(label: str) -> str:
    return label.split("(", 1)[0]


def _live_targets(
    gn_binary: str, checkout: Path, out_dir: Path
) -> tuple[dict[str, TargetView], list[str]]:
    try:
        output = subprocess.run(
            [gn_binary, "desc", str(out_dir), "//taffy/*", "--format=json"],
            cwd=checkout,
            check=True,
            capture_output=True,
            text=True,
        )
        document = json.loads(output.stdout)
    except (OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        return {}, [f"configured GN graph could not be read: {error}"]
    targets: dict[str, TargetView] = {}
    for raw_label, raw in document.items():
        label = _normalize_live_label(raw_label)
        if label in targets or not isinstance(raw, dict):
            continue
        targets[label] = TargetView(
            label=label,
            deps=frozenset(_normalize_live_label(value) for value in raw.get("deps", [])),
            # Android's Java/Kotlin/resource templates put canonical compile
            # sources in generated actions' `inputs`, while their `sources`
            # arrays are empty. Both are source ownership facts.
            sources=frozenset(
                _normalize_live_label(value)
                for value in raw.get("sources", []) + raw.get("inputs", [])
            ),
            declaration="BUILD.gn",
            test_only=bool(raw.get("testonly", False)),
        )
    return targets, []


def _host_target_os() -> str:
    system = platform.system().lower()
    if system == "darwin":
        return "mac"
    if system.startswith("win"):
        return "win"
    return system


def _live_target_os(gn_binary: str, checkout: Path, out_dir: Path) -> tuple[str, list[str]]:
    try:
        output = subprocess.run(
            [gn_binary, "args", str(out_dir), "--list=target_os", "--short"],
            cwd=checkout,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        return "", [f"configured GN target_os could not be read: {error}"]
    match = re.search(r'^target_os\s*=\s*"([^"]*)"', output.stdout, re.MULTILINE)
    if not match:
        return "", ["configured GN target_os output was malformed"]
    return match.group(1) or _host_target_os(), []


def _manifest_platform(target_os: str) -> str:
    return {"mac": "macos", "win": "windows"}.get(target_os, target_os)


def _live_signing_key(
    gn_binary: str, checkout: Path, out_dir: Path
) -> tuple[frozenset[str], list[str]]:
    """The configured signing key, which is configuration rather than source.

    Chromium's APK template takes `android_keystore_path` as an `input` of
    every `__apk__create` action, and this checker reads an input as a
    source-ownership fact — deliberately, because Android's Java, Kotlin and
    resource templates leave `sources` empty and put the files they actually
    compile in `inputs`. A signing key is the one //taffy input that reaches
    those actions and is never compiled or packaged as product content: it is
    a credential the packaging step spends. Counting it made every component
    that produces an APK appear to depend on whichever component's glob the key
    happened to fall under, which is a statement about the filing system rather
    than about the architecture.

    The exclusion is exactly one path, and it is the path the build was told to
    sign with, so it cannot hide a source file and it follows the argument if
    the key ever moves. A key the build does not name is not excluded.
    """
    try:
        output = subprocess.run(
            [gn_binary, "args", str(out_dir), "--list=android_keystore_path", "--short"],
            cwd=checkout,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        # The argument is declared inside `if (is_android)`, so a host profile
        # has none. That is not a finding: there is no APK and no key to spend.
        return frozenset(), []
    match = re.search(
        r'^android_keystore_path\s*=\s*"([^"]*)"', output.stdout, re.MULTILINE
    )
    if not match or not match.group(1):
        return frozenset(), []
    return frozenset({match.group(1)}), []


def live_findings(
    core_root: Path,
    components: list[Component],
    gn_binary: str,
    checkout: Path,
    out_dir: Path,
) -> list[str]:
    target_os, findings = _live_target_os(gn_binary, checkout, out_dir)
    if findings:
        return findings
    targets, findings = _live_targets(gn_binary, checkout, out_dir)
    if findings:
        return findings
    # Live sources are checkout-absolute labels. Resolve them against the mounted
    # //taffy root, which is required to be this same repository directory.
    mounted_root = (checkout / "taffy").resolve()
    if mounted_root != core_root.resolve():
        return [
            f"configured //taffy mount resolves to {mounted_root}, expected {core_root.resolve()}"
        ]
    static_targets, static_parse_findings = _static_targets(core_root)
    if static_parse_findings:
        return static_parse_findings
    excluded_sources, key_findings = _live_signing_key(gn_binary, checkout, out_dir)
    if key_findings:
        return key_findings
    static_owners, static_owner_findings = _target_owners(
        static_targets, components, core_root
    )
    if static_owner_findings:
        return static_owner_findings
    return graph_findings(
        targets,
        components,
        mounted_root,
        platform_name=_manifest_platform(target_os),
        owner_hints=static_owners,
        strict_ownership=False,
        excluded_sources=excluded_sources,
    )


def _components(manifest: Path) -> tuple[list[Component], list[str]]:
    try:
        document = load_manifest(manifest)
    except ValueError as error:
        return [], [str(error)]
    return decode_components(document)


def _print_findings(findings: list[str]) -> int:
    for finding in findings:
        print(f"GN component graph: {finding}", file=sys.stderr)
    return 1 if findings else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("components.toml"),
    )
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--gn-out", type=Path)
    parser.add_argument("--checkout", type=Path)
    parser.add_argument("--gn", default="gn")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return run_self_test()
    components, findings = _components(arguments.manifest)
    core_root = arguments.manifest.resolve().parent.parent
    findings.extend(static_findings(core_root, components))
    if arguments.gn_out or arguments.checkout:
        if not arguments.gn_out or not arguments.checkout:
            findings.append("--gn-out and --checkout must be supplied together")
        elif not findings:
            findings.extend(
                live_findings(
                    core_root,
                    components,
                    arguments.gn,
                    arguments.checkout.resolve(),
                    arguments.gn_out.resolve(),
                )
            )
    if findings:
        return _print_findings(findings)
    mode = "static and configured" if arguments.gn_out else "static"
    target_count = sum(len(component.gn_targets) for component in components)
    print(f"GN component graph: {target_count} manifest targets, {mode} boundaries exact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
