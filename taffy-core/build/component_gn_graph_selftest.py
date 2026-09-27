# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Failure fixtures for the exact component-to-GN boundary checker."""

from __future__ import annotations

import tempfile
from pathlib import Path


def _write(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")


def _component(
    component_id: str,
    deps: tuple[str, ...],
    target: str,
    source: str,
    platforms: tuple[str, ...] = ("android",),
    layer: int = 10,
):
    from component_graph_schema import Component

    return Component(
        id=component_id,
        state="active",
        process="neutral",
        trust="core",
        layer=layer,
        platforms=platforms,
        sources=(source,),
        direct_deps=deps,
        gn_targets=(target,),
        gradle_path="",
        gradle_api_surface="none",
        gradle_api_deps=(),
        test_only=False,
    )


def _tree(root: Path) -> list[object]:
    _write(
        root / "a/BUILD.gn",
        'source_set("a") {\n  sources = [ "a.cc" ]\n  deps = [ "//taffy/b:b" ]\n}\n',
    )
    _write(root / "a/a.cc", "// a\n")
    _write(root / "b/BUILD.gn", 'source_set("b") { sources = [ "b.cc" ] }\n')
    _write(root / "b/b.cc", "// b\n")
    return [
        _component("a", ("b",), "//taffy/a:a", "a/**", layer=20),
        _component("b", (), "//taffy/b:b", "b/**"),
    ]


def run_self_test() -> int:
    from component_gn_graph import TargetView, graph_findings, static_findings

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        components = _tree(root)
        if static_findings(root, components):
            print("GN graph self-test: valid graph was rejected")
            return 1

        _write(root / "a/orphan.cc", "// orphan C++ implementation\n")
        _write(root / "a/orphan.m", "// orphan Objective-C implementation\n")
        _write(root / "a/orphan.mm", "// orphan Objective-C++ implementation\n")
        orphan = static_findings(root, components)
        for implementation in ("orphan.cc", "orphan.m", "orphan.mm"):
            if not any(
                finding.startswith(
                    f"a/{implementation}: implementation is absent"
                )
                for finding in orphan
            ):
                print(
                    "GN graph self-test: an orphan "
                    f"{Path(implementation).suffix} implementation was accepted"
                )
                return 1
        _write(
            root / "a/BUILD.gn",
            'source_set("a") {\n'
            '  sources = [ "a.cc", "orphan.cc" ]\n'
            '  inputs = [ "orphan.m", "orphan.mm" ]\n'
            '  deps = [ "//taffy/b:b" ]\n'
            '}\n',
        )
        listed = static_findings(root, components)
        for implementation in ("orphan.cc", "orphan.m", "orphan.mm"):
            if any(
                f"a/{implementation}: implementation is absent" in finding
                for finding in listed
            ):
                print(
                    "GN graph self-test: a parsed GN source/input was treated "
                    "as orphaned"
                )
                return 1
            (root / f"a/{implementation}").unlink()

        _write(
            root / "third_party/example/build/real.cc",
            "// third-party implementation, not generated output\n",
        )
        nested_build_findings = static_findings(root, components)
        if not any(
            finding.startswith(
                "third_party/example/build/real.cc: implementation is absent"
            )
            for finding in nested_build_findings
        ):
            print("GN graph self-test: a real source below build/ was hidden")
            return 1
        (root / "third_party/example/build/real.cc").unlink()

        _write(root / "a/generated/unlisted.cc", "// generated output\n")
        _write(root / "a/marked_generated.cc", "// @generated\n")
        generated_findings = static_findings(root, components)
        if any("generated/unlisted.cc" in finding for finding in generated_findings):
            print("GN graph self-test: generated output was treated as an orphan")
            return 1
        if any("marked_generated.cc" in finding for finding in generated_findings):
            print("GN graph self-test: marked generated output was treated as an orphan")
            return 1

        _write(
            root / "a/BUILD.gn",
            'source_set("a") { sources = [ "a.cc" ] }\n',
        )
        missing = static_findings(root, components)
        if not any("has no GN edge" in finding for finding in missing):
            print("GN graph self-test: a missing component edge was accepted")
            return 1

        _write(
            root / "a/BUILD.gn",
            'hidden_edge = undeclared_variable\n'
            'source_set("a") {\n'
            '  sources = [ "a.cc" ]\n'
            '  deps = hidden_edge\n'
            '}\n',
        )
        unresolved = static_findings(root, components)
        if not any(
            "unresolved deps expression variables: undeclared_variable" in finding
            for finding in unresolved
        ):
            print("GN graph self-test: an unresolved dependency variable was accepted")
            return 1

        _write(root / "c/BUILD.gn", 'source_set("c") { sources = [ "c.cc" ] }\n')
        _write(root / "c/c.cc", "// c\n")
        components.append(_component("c", (), "//taffy/c:c", "c/**"))
        _write(
            root / "a/BUILD.gn",
            'source_set("a") {\n'
            '  sources = [ "a.cc" ]\n'
            '  deps = [ "//taffy/b:b", "//taffy/c:c" ]\n'
            '}\n',
        )
        extra = static_findings(root, components)
        if not any("undeclared component dependency c" in finding for finding in extra):
            print("GN graph self-test: an extra component edge was accepted")
            return 1

        # A signing key is an input of every APK action and is not source.
        # Excluding it must not also excuse a real file in the same list, so
        # the fixture puts both in one target and asserts only the key is
        # forgiven.
        _write(root / "a/BUILD.gn", 'source_set("a") { sources = [ "a.cc" ] }\n')
        _write(root / "b/keystore.jks", "not a real key\n")
        signing_targets = {
            "//taffy/a:a__apk__create": TargetView(
                label="//taffy/a:a__apk__create",
                deps=frozenset(),
                sources=frozenset(
                    {"//taffy/b/keystore.jks", "//taffy/b/b.cc"}
                ),
                declaration="BUILD.gn",
                test_only=False,
            ),
            "//taffy/a:a": TargetView(
                label="//taffy/a:a",
                deps=frozenset({"//taffy/a:a__apk__create"}),
                sources=frozenset({"//taffy/a/a.cc"}),
                declaration="BUILD.gn",
                test_only=False,
            ),
            "//taffy/b:b": TargetView(
                label="//taffy/b:b",
                deps=frozenset(),
                sources=frozenset({"//taffy/b/b.cc"}),
                declaration="BUILD.gn",
                test_only=False,
            ),
        }
        unexcluded = graph_findings(
            signing_targets, [_component("a", (), "//taffy/a:a", "a/**"), components[1]], root
        )
        if not any("undeclared component dependency b" in f for f in unexcluded):
            print("GN graph self-test: an unexcluded foreign input was accepted")
            return 1
        excused = graph_findings(
            signing_targets,
            [_component("a", (), "//taffy/a:a", "a/**"), components[1]],
            root,
            excluded_sources=frozenset({"//taffy/b/keystore.jks"}),
        )
        if not any("undeclared component dependency b" in f for f in excused):
            print(
                "GN graph self-test: excluding the signing key also excused a "
                "real source in the same input list"
            )
            return 1
        key_only = dict(signing_targets)
        key_only["//taffy/a:a__apk__create"] = TargetView(
            label="//taffy/a:a__apk__create",
            deps=frozenset(),
            sources=frozenset({"//taffy/b/keystore.jks"}),
            declaration="BUILD.gn",
            test_only=False,
        )
        forgiven = graph_findings(
            key_only,
            [_component("a", (), "//taffy/a:a", "a/**"), components[1]],
            root,
            excluded_sources=frozenset({"//taffy/b/keystore.jks"}),
        )
        if any("undeclared component dependency b" in f for f in forgiven):
            print("GN graph self-test: the configured signing key was read as source")
            return 1
        (root / "b/keystore.jks").unlink()

        _write(root / "a/helper.cc", "// helper\n")
        _write(
            root / "a/BUILD.gn",
            'source_set("a") {\n'
            '  sources = [ "a.cc" ]\n'
            '  deps = [ "//taffy/b:b" ]\n'
            '}\n'
            'source_set("disconnected_helper") {\n'
            '  sources = [ "helper.cc" ]\n'
            '  deps = [ "//taffy/c:c" ]\n'
            '}\n',
        )
        disconnected = static_findings(root, components)
        if not any("undeclared component dependency c" in finding for finding in disconnected):
            print("GN graph self-test: a disconnected owned target edge was accepted")
            return 1

        _write(
            root / "a/BUILD.gn",
            'source_set("a") { sources = [ "../b/b.cc" ] }\n',
        )
        foreign_components = [
            _component("a", (), "//taffy/a:a", "a/**"),
            components[1],
        ]
        foreign = static_findings(root, foreign_components)
        if not any("undeclared component dependency b" in finding for finding in foreign):
            print("GN graph self-test: a foreign source inclusion was accepted")
            return 1

        _write(
            root / "a/BUILD.gn",
            'source_set("other") { sources = [ "a.cc" ] }\n',
        )
        absent = static_findings(root, components[:2])
        if not any("declared GN target //taffy/a:a does not exist" in finding for finding in absent):
            print("GN graph self-test: a missing manifest target was accepted")
            return 1

        platform_components = [
            _component("android", ("leaf",), "//taffy/android:android", "android/**"),
            _component("leaf", ("wire",), "//taffy/leaf:leaf", "leaf/**"),
            _component("wire", (), "//taffy/wire:wire", "wire/**"),
            _component(
                "host",
                (),
                "//taffy/build:manifest",
                "build/**",
                platforms=("host",),
            ),
        ]
        live_targets = {
            label: TargetView(label, frozenset(deps), frozenset(), "BUILD.gn", False)
            for label, deps in {
                "//taffy/android:android": ("//taffy/leaf:leaf",),
                "//taffy/leaf:leaf": (),
                "//taffy/leaf:leaf_headers": ("//taffy/wire:wire",),
                "//taffy/wire:wire": (),
                "//taffy/build:manifest": (),
            }.items()
        }
        android_findings = graph_findings(
            live_targets,
            platform_components,
            root,
            platform_name="android",
            strict_ownership=False,
        )
        if android_findings:
            print(
                f"GN graph self-test: Android generated ownership failed: {android_findings}"
            )
            return 1
        linux_findings = graph_findings(
            {"//taffy/build:manifest": live_targets["//taffy/build:manifest"]},
            platform_components,
            root,
            platform_name="linux",
            strict_ownership=False,
        )
        if linux_findings:
            print(
                f"GN graph self-test: Linux required Android targets: {linux_findings}"
            )
            return 1

    print(
        "GN graph self-test: drift, orphan implementations, unresolved variables, "
        "generated ownership, and platforms pass"
    )
    return 0
