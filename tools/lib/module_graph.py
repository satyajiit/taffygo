#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Verify the generated Android module graph without a JDK.

`taffy-core/build/components.toml` is the only source of module ownership and
direct dependencies. Its committed `android-modules.tsv` projection is read by
this host check and by Gradle build logic. Module build scripts may configure a
module, but may not declare project-to-project edges of their own.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass

GRAPH = "taffy-core/build/generated/android-modules.tsv"
SETTINGS_PROJECTION = "taffy-core/build/generated/android-modules.settings.gradle.kts"
MODULE_ROOT = "taffy-core/ui/android"
BUILD_LOGIC_ROOT = "build-logic/convention"

_MODULE_PATH = re.compile(r"^:[a-z][a-z0-9-]*(?::[a-z][a-z0-9-]*)*$")
_REGISTERED_MODULE = re.compile(r'registerTaffyAndroidModule\("([^"]+)"\)')
_PACKAGE = re.compile(r"^package\s+([A-Za-z0-9_.]+)", re.MULTILINE)
_PROJECT_DEPENDENCY = re.compile(r'project\(\s*"(:[A-Za-z0-9_:-]+)"\s*\)')


class GraphError(ValueError):
    """A malformed generated projection."""


@dataclass(frozen=True)
class Module:
    """One exact generated module row."""

    path: str
    component: str
    layer: int
    project_dir: str
    api_surface: str
    api_deps: tuple[str, ...]
    implementation_deps: tuple[str, ...]

    @property
    def dependencies(self) -> tuple[str, ...]:
        return self.api_deps + self.implementation_deps


def _read(path: str) -> str:
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return ""


def _dependency_list(value: str, where: str) -> tuple[str, ...]:
    if not value:
        return ()
    dependencies = tuple(value.split(","))
    if any(not _MODULE_PATH.fullmatch(item) for item in dependencies):
        raise GraphError(f"{where}: malformed Gradle dependency list {value!r}")
    if dependencies != tuple(sorted(set(dependencies))):
        raise GraphError(f"{where}: dependencies must be unique and sorted")
    return dependencies


def load_graph(repo_root: str) -> dict[str, Module]:
    """Load the committed component-manifest projection."""
    path = os.path.join(repo_root, GRAPH)
    try:
        with open(path, encoding="utf-8") as handle:
            raw_lines = list(handle)
    except OSError as error:
        raise GraphError(f"cannot read {GRAPH}: {error}") from error

    modules: dict[str, Module] = {}
    for number, raw in enumerate(raw_lines, start=1):
        line = raw.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        where = f"{GRAPH}:{number}"
        if len(fields) != 7:
            raise GraphError(f"{where}: expected 7 tab-separated fields, got {len(fields)}")
        path_value, component, layer_raw, project_dir, api_surface, api_raw, implementation_raw = fields
        if not layer_raw.isdigit() or int(layer_raw) <= 0:
            raise GraphError(f"{where}: layer must be a positive integer, got {layer_raw!r}")
        if not _MODULE_PATH.fullmatch(path_value):
            raise GraphError(f"{where}: malformed Gradle path {path_value!r}")
        relative_module = path_value.lstrip(":").replace(":", "/")
        expected_dir = MODULE_ROOT.removeprefix("taffy-core/") + "/" + relative_module
        if project_dir != expected_dir:
            raise GraphError(f"{where}: project directory must be {expected_dir!r}")
        if api_surface not in {"flat", "split"}:
            raise GraphError(f"{where}: api surface must be flat or split")
        if path_value in modules:
            raise GraphError(f"{where}: duplicate module {path_value}")
        api_deps = _dependency_list(api_raw, where)
        implementation_deps = _dependency_list(implementation_raw, where)
        if set(api_deps) & set(implementation_deps):
            raise GraphError(f"{where}: a dependency cannot be both api and implementation")
        modules[path_value] = Module(
            path=path_value,
            component=component,
            layer=int(layer_raw),
            project_dir=project_dir,
            api_surface=api_surface,
            api_deps=api_deps,
            implementation_deps=implementation_deps,
        )
    if not modules:
        raise GraphError(f"{GRAPH}: no Android modules")
    for module in modules.values():
        for dependency in module.dependencies:
            if dependency not in modules:
                raise GraphError(f"{GRAPH}: {module.path} names unknown dependency {dependency}")
            if dependency == module.path:
                raise GraphError(f"{GRAPH}: {module.path} depends on itself")
    return modules


def _physical_modules(repo_root: str) -> set[str]:
    root = os.path.join(repo_root, MODULE_ROOT)
    modules: set[str] = set()
    for directory, dirnames, filenames in os.walk(root):
        dirnames[:] = [
            name for name in dirnames if name not in {".gradle", ".idea", "build", "out"}
        ]
        if "build.gradle.kts" not in filenames:
            continue
        relative = os.path.relpath(directory, root)
        modules.add(":" + relative.replace(os.sep, ":"))
    return modules


def _handwritten_edges(repo_root: str) -> list[str]:
    findings: list[str] = []
    for relative_root in (MODULE_ROOT, BUILD_LOGIC_ROOT):
        root = os.path.join(repo_root, relative_root)
        for directory, dirnames, filenames in os.walk(root):
            dirnames[:] = [
                name for name in dirnames if name not in {".gradle", ".idea", "build", "out"}
            ]
            for filename in filenames:
                if not filename.endswith((".kt", ".kts")):
                    continue
                path = os.path.join(directory, filename)
                for dependency in _PROJECT_DEPENDENCY.findall(_read(path)):
                    display = os.path.relpath(path, repo_root)
                    findings.append(
                        f"{display}: hand-written edge to {dependency}; declare it in "
                        "taffy-core/build/components.toml"
                    )
    return findings


def _semantic_findings(modules: dict[str, Module]) -> list[str]:
    findings: list[str] = []
    for module in modules.values():
        for dependency in module.dependencies:
            if module.path.startswith(":feature:") and dependency.startswith(":feature:"):
                findings.append(
                    f"{module.path} -> {dependency}: features communicate through portable ports, "
                    "not feature-to-feature edges"
                )
            if dependency.startswith(":feature:") and module.path != ":app":
                findings.append(
                    f"{module.path} -> {dependency}: only :app may assemble a feature module"
                )
            other = modules.get(dependency)
            if other is not None and other.layer >= module.layer:
                findings.append(
                    f"{module.path} -> {dependency}: layer {module.layer} cannot depend on "
                    f"layer {other.layer}; every edge falls strictly"
                )
    return findings


def _api_surface_findings(repo_root: str, modules: dict[str, Module]) -> list[str]:
    """Hold each module's declared api surface against the tree.

    `api_surface` was validated as one of two words and then read by nobody, so
    a module could declare a separation it did not have. Only one direction is
    a defect: a `split` module may legitimately have no implementation package
    at all — a pure port or model module is the documented case — but a `flat`
    module claims no api/implementation split, and an implementation package is
    that split. Gradle's ApiSurfaceTask enforces what goes *inside* such a
    package; nothing held the declaration itself against the sources.

    Keyed on the package a source file declares rather than on a directory
    name, so an empty directory left behind by a move is not a finding.
    """
    findings: list[str] = []
    for path, module in sorted(modules.items()):
        if module.api_surface != "flat":
            continue
        main = os.path.join(repo_root, MODULE_ROOT, os.path.relpath(module.project_dir, "ui/android"), "src", "main")
        for directory, _dirnames, filenames in os.walk(main):
            for filename in sorted(filenames):
                if not filename.endswith(".kt"):
                    continue
                declared = _PACKAGE.search(_read(os.path.join(directory, filename)))
                if declared and "internal" in declared.group(1).split("."):
                    display = os.path.relpath(os.path.join(directory, filename), repo_root)
                    findings.append(
                        f"{display}: declares package {declared.group(1)}, but {path} is "
                        "declared flat in taffy-core/build/components.toml; a flat module "
                        "has no implementation package"
                    )
    return findings


def run(repo_root: str) -> list[str]:
    """Return every exact-graph finding in stable order."""
    modules = load_graph(repo_root)
    findings: list[str] = []

    settings = _read(os.path.join(repo_root, "settings.gradle.kts"))
    apply_line = f'apply(from = "{SETTINGS_PROJECTION}")'
    if apply_line not in settings:
        findings.append(f"settings.gradle.kts must consume {SETTINGS_PROJECTION}")

    projection = _read(os.path.join(repo_root, SETTINGS_PROJECTION))
    registered = set(_REGISTERED_MODULE.findall(projection))
    declared = set(modules)
    for path in sorted(registered - declared):
        findings.append(f"{SETTINGS_PROJECTION} registers undeclared module {path}")
    for path in sorted(declared - registered):
        findings.append(f"{GRAPH} declares {path}, but the settings projection omits it")

    physical = _physical_modules(repo_root)
    for path in sorted(physical - declared):
        findings.append(f"{path} has a build script but is absent from {GRAPH}")
    for path in sorted(declared - physical):
        findings.append(f"{path} is declared by {GRAPH} but has no build script")

    findings.extend(_semantic_findings(modules))
    findings.extend(_api_surface_findings(repo_root, modules))
    findings.extend(_handwritten_edges(repo_root))
    return sorted(findings)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    args = parser.parse_args(argv)
    repo_root = os.path.abspath(args.root)
    try:
        findings = run(repo_root)
    except GraphError as error:
        print(f"module graph: {error}", file=sys.stderr)
        return 1
    for finding in findings:
        print(f"module graph: {finding}", file=sys.stderr)
    if findings:
        return 1
    modules = load_graph(repo_root)
    edges = sum(len(module.dependencies) for module in modules.values())
    print(f"module graph: {len(modules)} modules, {edges} exact generated edges")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
