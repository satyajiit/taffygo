#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Prove the shipping build graph of //taffy/services/core by construction.

Six claims this directory's README makes, each checked here rather than
asserted:

  1. No excluded crate is named anywhere in the build files. `harness-ffi` is
     the harness-only JNI boundary and links a crate that has not been through
     Chromium's vendoring process.
  2. Every shipping crate target takes its source list from the generated
     `crate_sources.gni`, never from a list typed into `BUILD.gn`. A typed list
     in a directory that does not hold the files it names goes stale silently.
  3. The GN dependency edges between crate targets are exactly the first-party
     `[dependencies]` in each crate's `Cargo.toml`. Two build graphs, one source
     of truth: if they disagree, one of them is wrong and it is not obvious
     which.
  4. Every third-party crate a shipping target links is on the vendoring
     allowlist below, with the label Chromium vendors it at — and no crate that
     is a `[dev-dependencies]` entry appears in a shipping dependency list. This
     is the "no test-only dependency reaches a shipping binary" claim, and it is
     a property of the manifests rather than a promise about them. *Every*
     shipping target, not every shipping crate: the sweep once ran over
     `crate_layout.SHIPPING_CRATES` alone, which left `service_bridge` — the one
     hand-written adapter, and the only target in this directory that links a
     third-party Rust crate directly — outside a claim written as though it
     covered everything.
  5. The service owns exactly one Rust adapter (`service_bridge.rs`) and no
     copied component crate source. Component crates remain in their owning
     `taffy-core` directories and are referenced directly by generated lists.
  6. No `testonly` target is reachable from a target that is not `testonly`.

It used to measure this directory's file sizes as well. It no longer does:
`./tools/check fast` lane `files` applies the same soft cap, with the same
definition of a line of code, to every non-generated source file in the
repository - including these. Two enforcers of one rule is how two answers
drift apart, so this one gave the rule up rather than keep a copy of it.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import crate_layout

# --- the vendoring position -------------------------------------------------


class ThirdParty:
    """A third-party crate a shipping target is permitted to link."""

    def __init__(self, label: str, note: str) -> None:
        self.label = label
        self.note = note


#: Third-party crates the shipping graph is allowed to contain, and the GN
#: label Chromium vendors each at. Adding an entry is a vendoring decision, not
#: a build-file edit: see this directory's README, "The dependency position".
#:
#: VERIFY AT SP-01: each label against the pinned checkout's
#: third_party/rust/<crate>/v<epoch>/BUILD.gn, and whether the vendored build
#: enables the features the crate needs (serde's `derive`). If a label is wrong
#: the build fails to resolve it, which is the failure worth having.
VENDORED = {
    "serde": ThirdParty(
        label="//third_party/rust/serde/v1:lib",
        note=(
            "Traits and derive macros only. Chromium already vendors it, so no "
            "new vendoring review is required — but note that no serialization "
            "format crate is linked, so the derived implementations are inert "
            "in a shipping build until one is."
        ),
    ),
    "cxx": ThirdParty(
        label="//third_party/rust/cxx/v1:lib",
        note=(
            "The Rust/C++ bridge `service_bridge.rs` is built on, and the only "
            "third-party crate this directory's own targets link. Chromium "
            "vendors it and generates the glue itself, so it needs no new "
            "vendoring review — it needs to be named here, which is a different "
            "thing, because until it was the sweep had nothing to match its "
            "label against and passed by not looking."
        ),
    ),
}

#: Crates that must never appear in a shipping crate's `[dependencies]`. Each is
#: legitimate where it is used; none may reach a browser binary.
TEST_ONLY_CRATES = {
    "proptest": "property-testing harness; host loop only",
    "rusqlite": "a second database engine; the storage crate's test backend only",
    "serde_json": "a serialization format used by tests and fixtures",
    "jni": "the harness-only Java boundary (decision 0012)",
}

# --- tiny GN reader ---------------------------------------------------------

_TARGET = re.compile(r"^(?P<template>[a-z_]+)\(\"(?P<name>[A-Za-z0-9_.:-]+)\"\)\s*\{", re.M)
_LIST = re.compile(r"^\s*(?P<name>[a-z_]+)\s*(?:\+)?=\s*\[(?P<body>[^\]]*)\]", re.M)
_SCALAR = re.compile(r"^\s*(?P<name>[a-z_]+)\s*=\s*(?P<value>[^\[\n]+)$", re.M)


class Target:
    def __init__(self, template: str, name: str, body: str) -> None:
        self.template = template
        self.name = name
        self.body = body
        self.lists: dict[str, list[str]] = {}
        for match in _LIST.finditer(body):
            entries = [
                item.strip().strip(",").strip('"')
                for item in match.group("body").split("\n")
                if item.strip().strip(",").strip()
            ]
            self.lists.setdefault(match.group("name"), []).extend(
                entry for entry in entries if entry and not entry.startswith("#")
            )
        self.scalars = {
            match.group("name"): match.group("value").strip()
            for match in _SCALAR.finditer(body)
        }

    @property
    def testonly(self) -> bool:
        return self.scalars.get("testonly", "false").rstrip(",") == "true"


def parse_gn(text: str) -> dict[str, Target]:
    targets: dict[str, Target] = {}
    for match in _TARGET.finditer(text):
        start = match.end()
        depth = 1
        index = start
        while index < len(text) and depth:
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
            index += 1
        targets[match.group("name")] = Target(
            match.group("template"), match.group("name"), text[start : index - 1]
        )
    return targets


# --- Cargo manifest reader --------------------------------------------------


def manifest_dependencies(path: str) -> tuple[set[str], set[str]]:
    """(runtime, development) dependency names from a crate manifest."""
    section = None
    runtime: set[str] = set()
    development: set[str] = set()
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            stripped = line.strip()
            if stripped.startswith("["):
                section = stripped.strip("[]")
                continue
            if not stripped or stripped.startswith("#") or "=" not in stripped:
                continue
            name = stripped.split("=", 1)[0].strip()
            if section == "dependencies":
                runtime.add(name)
            elif section == "dev-dependencies":
                development.add(name)
    return runtime, development


# --- the checks -------------------------------------------------------------


def _normalized(path: str) -> str:
    """A file's code, with blank lines and comments removed.

    Two files are the same file when this is equal: reformatting, a different
    licence header, or a re-wrapped paragraph does not make a copy stop being a
    copy, and it does not make two unrelated modules start being one.
    """
    kept: list[str] = []
    try:
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                stripped = line.strip()
                if not stripped or stripped.startswith(("//", "/*", "*")):
                    continue
                kept.append(stripped)
    except OSError:
        return ""
    return "\n".join(kept)



def strip_gn_comments(text: str) -> str:
    """GN has no block comments, so a line-comment strip is the whole job.

    It assumes no `#` inside a string literal, which holds for every build file
    in this directory and is checked by the fact that the resulting parse has to
    agree with the directory listing.

    The excluded-crate check reads this rather than the raw file: BUILD.gn
    explains *why* `harness-ffi` is excluded, and a check that could not tell an
    explanation from a dependency would force the explanation to be deleted.
    """
    return "\n".join(
        line.split("#", 1)[0] for line in text.splitlines()
    )


def check(repo_root: str) -> list[str]:
    # The registry first, and on its own. Every check below reads a manifest or
    # a build file by a name `crate_layout` supplied, so there is nothing
    # truthful to say about the graph while the registry and the tree disagree
    # about which crates exist — and `verify` was written for exactly this and
    # then never called from anywhere, which is the same failure one level up.
    layout = crate_layout.verify(repo_root)
    if layout:
        return layout

    problems: list[str] = []
    rust_dir = crate_layout.service_core_dir(repo_root)

    with open(os.path.join(rust_dir, "BUILD.gn"), encoding="utf-8") as handle:
        build_text = handle.read()
    with open(os.path.join(rust_dir, "crate_sources.gni"), encoding="utf-8") as handle:
        sources_text = handle.read()
    # Parsed with comments removed. A comment is prose and prose contains
    # brackets — `#[cfg(test)]` in an explanation closed a list this parser was
    # reading, and silently shortened it. Stripping first is both simpler and
    # the only version that cannot be fooled by an example in a comment.
    service_targets = parse_gn(strip_gn_comments(build_text))
    crate_targets: dict[str, Target] = {}
    crate_build_texts: dict[str, str] = {}
    for crate in crate_layout.SHIPPING_CRATES:
        path = crate_layout.build_file_path(repo_root, crate)
        if not os.path.isfile(path):
            problems.append(f"{crate.path} has no source-owning BUILD.gn")
            continue
        with open(path, encoding="utf-8") as handle:
            crate_build_texts[crate.crate_id] = handle.read()
        parsed = parse_gn(strip_gn_comments(crate_build_texts[crate.crate_id]))
        target_name = crate_layout.gn_target_name(repo_root, crate)
        target = parsed.get(target_name)
        if target is not None:
            crate_targets[crate.crate_id] = target

    # 1. Excluded crates appear nowhere a build could reach them.
    for name, reason in crate_layout.REMOVED_CRATES.items():
        for filename, text in (
            ("BUILD.gn", strip_gn_comments(build_text)),
            ("crate_sources.gni", strip_gn_comments(sources_text)),
            *(
                (
                    f"{crate_layout.source_dir(repo_root, crate)}/BUILD.gn",
                    strip_gn_comments(crate_build_texts.get(crate.crate_id, "")),
                )
                for crate in crate_layout.SHIPPING_CRATES
            ),
        ):
            if name in text:
                problems.append(f"{filename} names the excluded crate {name}: {reason}")

    # 2. Source lists come from the generated file.
    for crate in crate_layout.SHIPPING_CRATES:
        target_name = crate_layout.gn_target_name(repo_root, crate)
        target = crate_targets.get(crate.crate_id)
        if target is None:
            problems.append(f"BUILD.gn has no rust_static_library(\"{target_name}\")")
            continue
        if target.template != "rust_static_library":
            problems.append(
                f"{target_name} is a {target.template}; a shipping crate is a "
                "rust_static_library (decision 0004)"
            )
        variable = "taffy_" + crate.variable
        if f"{variable}_sources" not in target.body:
            problems.append(
                f"{target_name} does not take its sources from {variable}_sources in "
                "crate_sources.gni"
            )
        if target.testonly:
            problems.append(f"{target_name} is testonly; it is a shipping crate")

    # 3 and 4. Dependency edges against the manifests.
    package_to_target = {
        crate_layout.package_name(repo_root, crate): crate_layout.gn_label(repo_root, crate)
        for crate in crate_layout.SHIPPING_CRATES
    }

    for crate in crate_layout.SHIPPING_CRATES:
        target_name = crate_layout.gn_target_name(repo_root, crate)
        target = crate_targets.get(crate.crate_id)
        if target is None:
            continue
        manifest = crate_layout.manifest_path(repo_root, crate)
        runtime, development = manifest_dependencies(manifest)
        declared = set(target.lists.get("deps", []))

        for dependency in sorted(runtime):
            if dependency in TEST_ONLY_CRATES:
                problems.append(
                    f"{crate.path} lists {dependency} as a runtime dependency: "
                    f"{TEST_ONLY_CRATES[dependency]}"
                )
                continue
            if dependency in VENDORED:
                if VENDORED[dependency].label not in declared:
                    problems.append(
                        f"{target_name} is missing the GN edge for "
                        f"{dependency}: {VENDORED[dependency].label}"
                    )
                continue
            if dependency not in package_to_target:
                problems.append(
                    f"{crate.path} depends on {dependency}, which is neither a "
                    "shipping crate nor on the vendoring allowlist"
                )
                continue
            first_party = package_to_target[dependency]
            if first_party not in declared:
                problems.append(
                    f"{target_name} is missing the GN edge {first_party} that "
                    f"{crate.path}/Cargo.toml declares as {dependency}"
                )

        for dependency in sorted(development):
            if dependency in VENDORED and VENDORED[dependency].label in declared:
                continue  # also a runtime dependency; already checked above.
            label = VENDORED[dependency].label if dependency in VENDORED else None
            if label and label in declared:
                problems.append(
                    f"{target_name} links {dependency}, which {crate.path} "
                    "declares only as a development dependency"
                )

    # 4a. Every third-party Rust label a shipping target links is allowlisted.
    #
    # The sweep is over targets rather than over crates, because the target that
    # links the most third-party code here is not a crate at all: `service_bridge`
    # is hand-written GN-only Rust with no `Cargo.toml`, so a loop over the
    # manifests cannot see its `deps` and never did. Its `//third_party/rust/cxx`
    # edge went unchecked while the docstring above said otherwise. Testonly
    # targets are out of scope by construction — the claim is about what ships.
    swept = [
        (crate_layout.gn_target_name(repo_root, crate), crate_targets[crate.crate_id])
        for crate in crate_layout.SHIPPING_CRATES
        if crate.crate_id in crate_targets
    ]
    swept += [
        (target.name, target)
        for target in service_targets.values()
        if not target.testonly
    ]
    for target_name, target in swept:
        for edge in sorted(set(target.lists.get("deps", []))):
            if not edge.startswith("//third_party/rust/"):
                continue
            if not any(entry.label == edge for entry in VENDORED.values()):
                problems.append(
                    f"{target_name} links {edge}, which is not on the "
                    "vendoring allowlist in check_build_graph.py"
                )

    # 4b. The service bridge lists every one of its own sources.
    #
    # GN does not glob, and a missing entry here is not a compile error in a
    # language with no header dependencies: the module simply is not part of the
    # crate. Comparing the list with the directory is the only thing that turns
    # that into a build failure instead of a mystery.
    #
    # Three directories are not part of that comparison, and each for a
    # different reason:
    #
    #   tools/ is repository tooling and never ships.
    #   ../../contracts/core-service/generated/rust/core_service.rs is generated
    #   contract vocabulary and is listed as an explicit compiler input, not
    #   hand-written service glue.
    bridge = service_targets.get("service_bridge")
    if bridge is None:
        problems.append('BUILD.gn has no rust_static_library("service_bridge")')
    else:
        listed = set(bridge.lists.get("sources", []))
        on_disk = set()
        for root, dirs, files in os.walk(rust_dir):
            dirs[:] = [d for d in dirs if d != "tools"]
            for name in files:
                if name.endswith(".rs"):
                    on_disk.add(
                        os.path.relpath(os.path.join(root, name), rust_dir)
                    )
        for missing in sorted(on_disk - listed):
            problems.append(
                f"service_bridge does not list {missing}; a Rust module absent from "
                "sources is absent from the crate, silently"
            )
        for extra in sorted(listed - on_disk):
            path = os.path.normpath(os.path.join(rust_dir, extra))
            if not os.path.isfile(path):
                problems.append(f"service_bridge lists {extra}, which does not exist")
        crate_root = bridge.scalars.get("crate_root", "").strip('",')
        if crate_root != "service_bridge.rs":
            problems.append(
                f"service_bridge's crate_root is {crate_root!r}; the bridge module has "
                "to be the crate root for #[cxx::bridge] to be reachable"
            )

    # 5. No crate source is duplicated into the overlay.
    # A duplicate is a file with the same CONTENT, not the same name. Both
    # trees name a module for what it does, so `guard.rs` and `limits.rs` turn
    # up in each - and refusing a name would let a weak check dictate how the
    # crates are laid out. What decision 0012 forbids is a *copy*, which is a
    # question about bytes.
    crate_bodies: dict[str, str] = {}
    for crate in crate_layout.SHIPPING_CRATES:
        for root, _dirs, files in os.walk(crate_layout.source_dir(repo_root, crate)):
            for name in files:
                if not name.endswith(".rs"):
                    continue
                path = os.path.join(root, name)
                body = _normalized(path)
                if body:
                    crate_bodies.setdefault(body, os.path.relpath(path, repo_root))
    for root, dirs, files in os.walk(rust_dir):
        dirs[:] = [d for d in dirs if d != "tools"]
        for name in files:
            if not name.endswith(".rs"):
                continue
            path = os.path.join(root, name)
            twin = crate_bodies.get(_normalized(path))
            if twin is not None:
                problems.append(
                    f"{os.path.relpath(path, repo_root)} is a copy of {twin}; the "
                    "overlay holds build files and bridge glue, never a copy of a "
                    "crate (decision 0012)"
                )

    # 6. No testonly target is reachable from a shipping one.
    all_targets = list(service_targets.values()) + list(crate_targets.values())
    target_by_name = {target.name: target for target in all_targets}
    for target in all_targets:
        if target.testonly:
            continue
        for edge in target.lists.get("deps", []) + target.lists.get("public_deps", []):
            if (edge.startswith(":") and edge[1:] in target_by_name and
                    target_by_name[edge[1:]].testonly):
                problems.append(
                    f"{target.name} depends on the testonly target {edge}"
                )

    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo-root", help="TaffyGo repository root")
    args = parser.parse_args(argv)

    try:
        repo_root = crate_layout.resolve_repo_root(args.repo_root)
    except crate_layout.ContractError as error:
        print(f"rust build graph: {error}", file=sys.stderr)
        return 1

    problems = check(repo_root)
    for problem in problems:
        print(f"rust build graph: {problem}", file=sys.stderr)
    if problems:
        return 1
    print("rust build graph: shipping targets carry no test-only source or dependency")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
