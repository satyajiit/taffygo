# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Failure fixtures for component_graph.py."""

from __future__ import annotations

import copy
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable


def run_self_test(
    manifest_findings: Callable[..., tuple[list[Any], list[str]]],
    format_gn: Callable[[list[Any]], str],
    format_gradle: Callable[[list[Any]], str],
    projection_contents: Callable[[list[Any]], dict[str, str]],
    projection_findings: Callable[[Path, list[Any]], list[str]],
) -> int:
    base = {
        "schema_version": 3,
        "root_label": "//taffy",
        "component": [
            {
                "id": "browser",
                "state": "active",
                "process": "browser",
                "trust": "browser",
                "layer": 20,
                "platforms": ["android"],
                "sources": ["browser/**"],
                "direct_deps": ["leaf"],
                "gn_targets": ["//taffy/browser:browser"],
                "gradle_path": "",
                "gradle_api_surface": "none",
                "gradle_api_deps": [],
                "test_only": False,
            },
            {
                "id": "leaf",
                "state": "active",
                "process": "neutral",
                "trust": "core",
                "layer": 10,
                "platforms": ["android", "macos", "windows"],
                "sources": ["leaf/**"],
                "direct_deps": [],
                "gn_targets": ["//taffy/leaf:leaf"],
                "gradle_path": "",
                "gradle_api_surface": "none",
                "gradle_api_deps": [],
                "test_only": False,
            },
        ],
    }
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "browser").mkdir()
        (root / "leaf").mkdir()
        (root / "browser" / "host.cc").write_text("// fixture\n", encoding="utf-8")
        (root / "leaf" / "port.h").write_text("// fixture\n", encoding="utf-8")
        components, findings = manifest_findings(base, root)
        if findings:
            print(f"self-test: valid graph failed: {findings}", file=sys.stderr)
            return 1
        foreign = root / "third_party" / "cpython" / "src" / "Include"
        foreign.mkdir(parents=True)
        (foreign / "Python.h").write_text("// inbound fixture\n", encoding="utf-8")
        _components, foreign_findings = manifest_findings(base, root)
        if foreign_findings:
            print(
                f"self-test: exact CPython source mount was scanned: {foreign_findings}",
                file=sys.stderr,
            )
            return 1
        sibling = root / "third_party" / "cpython" / "runtime" / "owned.py"
        sibling.parent.mkdir(parents=True)
        sibling.write_text("/* first-party fixture */\n", encoding="utf-8")
        _components, sibling_findings = manifest_findings(base, root)
        if not any("runtime/owned.py: source is not owned" in finding for finding in sibling_findings):
            print(
                "self-test: CPython mount exclusion hid a first-party sibling",
                file=sys.stderr,
            )
            return 1
        sibling.unlink()
        cases: list[tuple[str, dict[str, Any], str]] = []
        for name, mutate, expected in (
            ("unknown dependency", lambda row: row[0].update(direct_deps=["missing"]), "unknown component"),
            ("cycle", lambda row: row[1].update(direct_deps=["browser"]), "dependency cycle"),
            # An inversion that is not a cycle: the leaf keeps no edge of its
            # own, so nothing points back and the graph stays acyclic. Only the
            # declared tier catches it.
            ("layer inversion", lambda row: row[0].update(layer=5), "falls strictly"),
            ("layer not a tier", lambda row: row[0].update(layer=True), "positive integer tier"),
            ("active-to-planned", lambda row: row[1].update(state="planned"), "depends on planned"),
            ("test-only leak", lambda row: row[1].update(test_only=True), "depends on test-only"),
            ("platform mismatch", lambda row: row[1].update(platforms=["macos", "windows"]), "platforms"),
            ("trust mismatch", lambda row: row[0].update(trust="platform"), "requires trust"),
            (
                "old GN namespace",
                lambda row: row[0].update(gn_targets=["//components/taffy:browser"]),
                "exact //taffy label",
            ),
        ):
            broken = copy.deepcopy(base)
            mutate(broken["component"])
            cases.append((name, broken, expected))
        for name, broken, expected in cases:
            _components, broken_findings = manifest_findings(broken, root, check_files=False)
            if not any(expected in finding for finding in broken_findings):
                print(f"self-test: {name} did not report {expected!r}: {broken_findings}", file=sys.stderr)
                return 1
        (root / "stray.rs").write_text("// fixture\n", encoding="utf-8")
        _components, ownership = manifest_findings(base, root)
        if not any("not owned" in finding for finding in ownership):
            print("self-test: unowned source was not reported", file=sys.stderr)
            return 1
        if format_gn(components) != format_gn(components):
            print("self-test: GN projection is not deterministic", file=sys.stderr)
            return 1
        if "registerTaffyAndroidModule" not in format_gradle(components):
            print("self-test: Gradle settings projection is incomplete", file=sys.stderr)
            return 1
        generated = root / "generated"
        generated.mkdir()
        for name, contents in projection_contents(components).items():
            (generated / name).write_text(contents, encoding="utf-8")
        (generated / "retired-projection.gradle.kts").write_text(
            "// obsolete fixture\n", encoding="utf-8"
        )
        projection_errors = projection_findings(generated, components)
        if not any("unexpected generated projection" in finding for finding in projection_errors):
            print("self-test: retired generated projection was not reported", file=sys.stderr)
            return 1
    print("component graph self-test: passed")
    return 0
