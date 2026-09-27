# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Deterministic GN, Gradle, and JSON projections for the component register."""

from __future__ import annotations

import json
from typing import Any


#: The licence notice every generated projection carries. The wording after
#: the copyright line is Exhibit A of the Mozilla Public License and is not
#: editable (decision 0205); it is spelled out here rather than imported
#: because a generator's inputs are declared to GN and an import across the
#: tree is an input nothing declares.
NOTICE = (
    "Copyright (c) 2026 Matterward Labs Private Limited.",
    "",
    "This Source Code Form is subject to the terms of the Mozilla Public",
    "License, v. 2.0. If a copy of the MPL was not distributed with this",
    "file, You can obtain one at https://mozilla.org/MPL/2.0/.",
)


def notice(marker: str) -> list[str]:
    """The notice in one comment syntax."""
    return [f"{marker} {line}".rstrip() for line in NOTICE]


def topological_components(components: list[Any]) -> list[Any]:
    by_id = {component.id: component for component in components}
    ordered: list[Any] = []
    visited: set[str] = set()

    def visit(component_id: str) -> None:
        if component_id in visited:
            return
        for dependency in by_id[component_id].direct_deps:
            if dependency in by_id:
                visit(dependency)
        visited.add(component_id)
        ordered.append(by_id[component_id])

    for component_id in sorted(by_id):
        visit(component_id)
    return ordered


def _quoted(values: tuple[str, ...]) -> str:
    return "[ " + ", ".join(json.dumps(value) for value in values) + " ]" if values else "[]"


def format_gn(components: list[Any]) -> str:
    lines = [
        *notice("#"),
        "#",
        "# Generated from //taffy/build/components.toml. Do not edit.",
        "taffy_components = [",
    ]
    for component in topological_components(components):
        lines.extend(
            [
                "  {",
                f"    id = {json.dumps(component.id)}",
                f"    state = {json.dumps(component.state)}",
                f"    process = {json.dumps(component.process)}",
                f"    trust = {json.dumps(component.trust)}",
                f"    platforms = {_quoted(component.platforms)}",
                f"    sources = {_quoted(component.sources)}",
                f"    direct_deps = {_quoted(component.direct_deps)}",
                f"    gn_targets = {_quoted(component.gn_targets)}",
                f"    gradle_path = {json.dumps(component.gradle_path)}",
                f"    gradle_api_surface = {json.dumps(component.gradle_api_surface)}",
                f"    gradle_api_deps = {_quoted(component.gradle_api_deps)}",
                f"    test_only = {str(component.test_only).lower()}",
                "  },",
            ]
        )
    lines.append("]")
    return "\n".join(lines) + "\n"


def format_gn_component_graph(components: list[Any]) -> str:
    """Machine-readable owner and boundary contract for GN graph checks."""
    rows = []
    for component in sorted(components, key=lambda entry: entry.id):
        for target in component.gn_targets:
            rows.append(
                {
                    "label": target,
                    "component": component.id,
                    "direct_component_deps": list(component.direct_deps),
                    "owned_sources": list(component.sources),
                    "test_only": component.test_only,
                }
            )
    document = {
        "schema_version": 1,
        "root_label": "//taffy",
        "targets": rows,
    }
    return json.dumps(document, indent=2) + "\n"


def format_gradle(components: list[Any]) -> str:
    modules = sorted(
        (component for component in components if component.gradle_path),
        key=lambda component: component.gradle_path,
    )
    lines = [
        *notice("//"),
        "//",
        "// Generated from taffy-core/build/components.toml. Do not edit.",
        "fun registerTaffyAndroidModule(path: String) {",
        "    include(path)",
        "    var current = \"\"",
        "    for (segment in path.removePrefix(\":\").split(':')) {",
        "        current += \":$segment\"",
        "        project(current).projectDir =",
        "            rootDir.resolve(\"taffy-core/ui/android/${current.removePrefix(\":\").replace(':', '/')}\")",
        "    }",
        "}",
        "",
    ]
    lines.extend(
        f"registerTaffyAndroidModule({json.dumps(component.gradle_path)})"
        for component in modules
    )
    return "\n".join(lines) + "\n"


def format_android_modules(components: list[Any]) -> str:
    """Machine-readable Gradle module graph consumed by Python and Gradle."""
    by_id = {component.id: component for component in components}
    lines = [
        "# Generated from taffy-core/build/components.toml. Do not edit.",
        "# path\tcomponent\tlayer\tproject-dir\tapi-surface\tapi-deps\timplementation-deps",
    ]
    for component in sorted(
        (entry for entry in components if entry.gradle_path),
        key=lambda entry: entry.gradle_path,
    ):
        project_dependencies = [
            dependency
            for dependency in component.direct_deps
            if by_id[dependency].gradle_path
        ]
        api_ids = set(component.gradle_api_deps)
        api_paths = sorted(by_id[dependency].gradle_path for dependency in api_ids)
        implementation_paths = sorted(
            by_id[dependency].gradle_path
            for dependency in project_dependencies
            if dependency not in api_ids
        )
        project_dir = "ui/android/" + component.gradle_path.lstrip(":").replace(":", "/")
        lines.append(
            "\t".join(
                (
                    component.gradle_path,
                    component.id,
                    str(component.layer),
                    project_dir,
                    component.gradle_api_surface,
                    ",".join(api_paths),
                    ",".join(implementation_paths),
                )
            )
        )
    return "\n".join(lines) + "\n"


def format_json(components: list[Any]) -> str:
    rows = [
        {
            "id": component.id,
            "state": component.state,
            "process": component.process,
            "trust": component.trust,
            "layer": component.layer,
            "platforms": list(component.platforms),
            "sources": list(component.sources),
            "direct_deps": list(component.direct_deps),
            "gn_targets": list(component.gn_targets),
            "gradle_path": component.gradle_path,
            "gradle_api_surface": component.gradle_api_surface,
            "gradle_api_deps": list(component.gradle_api_deps),
            "test_only": component.test_only,
        }
        for component in topological_components(components)
    ]
    document = {"schema_version": 3, "root_label": "//taffy", "components": rows}
    return json.dumps(document, indent=2) + "\n"
