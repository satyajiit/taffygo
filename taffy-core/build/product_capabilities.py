#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Validate and project the product capability profile.

`product-capabilities.json` is the one machine authority for the milestone
surface compiled into each committed Chromium profile. The product never reads
that JSON at runtime. This generator turns it into a GN selection, a C++ view,
a Rust view, and a normalized release-attestation document.

The interface is deliberately small: a profile answers whether delegated task
start exists and, when it does, which task and policy milestones must agree.
All ordering, release quarantine, profile coverage, and cross-language spelling
rules stay behind that interface. The candidate tuple must exactly follow the
accepted milestone, so it can neither expose unaccepted work nor lag behind an
accepted product surface.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable

from product_capability_projections import (
    render_cpp,
    render_cpp_impl,
    render_gni,
    render_release_json,
    render_rust,
)


HERE = Path(__file__).resolve().parent
CORE_ROOT = HERE.parent
REPO_ROOT = CORE_ROOT.parent
SOURCE = HERE / "product-capabilities.json"
ARTIFACTS: tuple[tuple[str, Path, Callable[[dict[str, Any], str], str]], ...] = (
    (
        "release projection",
        HERE / "capabilities/generated/product-capabilities.json",
        render_release_json,
    ),
    (
        "GN projection",
        HERE / "capabilities/generated/product-capabilities.gni",
        render_gni,
    ),
    (
        "C++ interface projection",
        CORE_ROOT / "browser/generated/product_capabilities.h",
        render_cpp,
    ),
    (
        "C++ implementation projection",
        CORE_ROOT / "browser/generated/product_capabilities.cc",
        render_cpp_impl,
    ),
    (
        "Rust projection",
        CORE_ROOT
        / "components/intelligence/core/rust/core-runtime/src/product_capabilities/generated.rs",
        render_rust,
    ),
)

MILESTONES = tuple(f"M{number}" for number in range(9))
POLICY_FOR_TASK = {
    "M2": "M2",
    "M3": "M3",
    "M4": "M3",
    "M5": "M5",
    "M6": "M6",
    "M7": "M7",
    "M8": "M7",
}
PROFILE_KEYS = {"delegated_task_start", "task_milestone", "policy_milestone"}
SOURCE_KEYS = {"schema_version", "accepted_milestone", "profiles", "chromium_profiles"}
ASSIGNMENT = re.compile(r'^taffy_capability_profile\s*=\s*"([a-z][a-z0-9-]*)"\s*$')


class CapabilityProfileError(ValueError):
    """The authority cannot produce a safe capability profile."""


def load(path: Path = SOURCE) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CapabilityProfileError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise CapabilityProfileError("root must be an object")
    return value


def fingerprint(document: dict[str, Any]) -> str:
    canonical = json.dumps(document, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return "sha256:" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _candidate_for_accepted_milestone(accepted: str) -> dict[str, Any]:
    if accepted in {"M0", "M1"}:
        return {
            "delegated_task_start": False,
            "task_milestone": None,
            "policy_milestone": None,
        }
    return {
        "delegated_task_start": True,
        "task_milestone": accepted,
        "policy_milestone": POLICY_FOR_TASK[accepted],
    }


def validate(document: dict[str, Any]) -> None:
    if set(document) != SOURCE_KEYS:
        raise CapabilityProfileError(
            f"root keys must be exactly {sorted(SOURCE_KEYS)}"
        )
    if document["schema_version"] != 1:
        raise CapabilityProfileError("schema_version must be 1")
    accepted = document["accepted_milestone"]
    if accepted not in MILESTONES:
        raise CapabilityProfileError("accepted_milestone must be M0 through M8")

    profiles = document["profiles"]
    if not isinstance(profiles, dict) or set(profiles) != {"candidate", "development"}:
        raise CapabilityProfileError("profiles must contain exactly candidate and development")
    for name, profile in profiles.items():
        if not isinstance(profile, dict) or set(profile) != PROFILE_KEYS:
            raise CapabilityProfileError(
                f"profiles.{name} keys must be exactly {sorted(PROFILE_KEYS)}"
            )
        delegated = profile["delegated_task_start"]
        task = profile["task_milestone"]
        policy = profile["policy_milestone"]
        if not isinstance(delegated, bool):
            raise CapabilityProfileError(f"profiles.{name}.delegated_task_start must be boolean")
        if not delegated:
            if task is not None or policy is not None:
                raise CapabilityProfileError(
                    f"profiles.{name}: a disabled task surface carries no task or policy milestone"
                )
            continue
        if task not in POLICY_FOR_TASK:
            raise CapabilityProfileError(
                f"profiles.{name}: delegated task start requires a task milestone from M2 through M8"
            )
        expected_policy = POLICY_FOR_TASK[task]
        if policy != expected_policy:
            raise CapabilityProfileError(
                f"profiles.{name}: task milestone {task} requires policy milestone {expected_policy}"
            )

    candidate = profiles["candidate"]
    expected_candidate = _candidate_for_accepted_milestone(accepted)
    if candidate != expected_candidate:
        raise CapabilityProfileError(
            f"profiles.candidate must exactly match accepted_milestone {accepted}: "
            + json.dumps(expected_candidate, sort_keys=True, separators=(",", ":"))
        )

    chromium_profiles = document["chromium_profiles"]
    if not isinstance(chromium_profiles, dict) or not chromium_profiles:
        raise CapabilityProfileError("chromium_profiles must be a non-empty object")
    if chromium_profiles.get("release-arm64") != "candidate":
        raise CapabilityProfileError("release-arm64 must select candidate")
    for name, selected in chromium_profiles.items():
        if not isinstance(name, str) or not name:
            raise CapabilityProfileError("chromium profile names must be non-empty strings")
        if selected not in profiles:
            raise CapabilityProfileError(
                f"chromium_profiles.{name} selects unknown profile {selected!r}"
            )


def profile_assignment_findings(document: dict[str, Any], args_dir: Path) -> list[str]:
    expected = document["chromium_profiles"]
    actual_files = {path.stem for path in args_dir.glob("*.gn")}
    findings: list[str] = []
    if actual_files != set(expected):
        missing = sorted(actual_files - set(expected))
        retired = sorted(set(expected) - actual_files)
        if missing:
            findings.append("unregistered Chromium profiles: " + ", ".join(missing))
        if retired:
            findings.append("registered profiles with no args file: " + ", ".join(retired))
    for name, selected in sorted(expected.items()):
        path = args_dir / f"{name}.gn"
        if not path.is_file():
            continue
        assignments = []
        for line in path.read_text(encoding="utf-8").splitlines():
            match = ASSIGNMENT.fullmatch(line.split("#", 1)[0].strip())
            if match:
                assignments.append(match.group(1))
        if assignments != [selected]:
            findings.append(
                f"{path}: expected exactly `taffy_capability_profile = \"{selected}\"`"
            )
    return findings


def rendered_artifacts(document: dict[str, Any]) -> list[tuple[str, Path, str]]:
    digest = fingerprint(document)
    return [(label, path, renderer(document, digest)) for label, path, renderer in ARTIFACTS]


def self_test() -> int:
    base = load()

    def set_candidate(
        document: dict[str, Any], accepted: str, delegated: bool,
        task: str | None, policy: str | None,
    ) -> None:
        document["accepted_milestone"] = accepted
        document["profiles"]["candidate"] = {
            "delegated_task_start": delegated,
            "task_milestone": task,
            "policy_milestone": policy,
        }

    cases: list[tuple[str, Callable[[dict[str, Any]], None]]] = [
        ("an unknown root key", lambda value: value.update({"other": True})),
        ("a missing candidate", lambda value: value["profiles"].pop("candidate")),
        (
            "a candidate ahead of the accepted milestone",
            lambda value: set_candidate(value, "M0", True, "M3", "M3"),
        ),
        (
            "a candidate disabled after task milestones are accepted",
            lambda value: set_candidate(value, "M8", False, None, None),
        ),
        (
            "a candidate lagging the accepted milestone",
            lambda value: set_candidate(value, "M8", True, "M7", "M7"),
        ),
        (
            "a disabled surface carrying a milestone",
            lambda value: value["profiles"]["candidate"].update({"task_milestone": "M0"}),
        ),
        (
            "a mismatched policy milestone",
            lambda value: value["profiles"]["development"].update({"policy_milestone": "M5"}),
        ),
        (
            "a release profile selecting development",
            lambda value: value["chromium_profiles"].update({"release-arm64": "development"}),
        ),
        (
            "an unknown selected profile",
            lambda value: value["chromium_profiles"].update({"dev-x64": "nightly"}),
        ),
    ]
    failures: list[str] = []
    for name, mutate in cases:
        document = copy.deepcopy(base)
        mutate(document)
        try:
            validate(document)
        except CapabilityProfileError:
            continue
        failures.append(f"{name} was not caught")

    candidate_shapes = {
        "M0": (False, None, None),
        "M1": (False, None, None),
        "M2": (True, "M2", "M2"),
        "M3": (True, "M3", "M3"),
        "M4": (True, "M4", "M3"),
        "M5": (True, "M5", "M5"),
        "M6": (True, "M6", "M6"),
        "M7": (True, "M7", "M7"),
        "M8": (True, "M8", "M7"),
    }
    for accepted, (delegated, task, policy) in candidate_shapes.items():
        document = copy.deepcopy(base)
        set_candidate(document, accepted, delegated, task, policy)
        try:
            validate(document)
        except CapabilityProfileError as error:
            failures.append(f"the exact {accepted} candidate was refused: {error}")

    m8_document = copy.deepcopy(base)
    set_candidate(m8_document, "M8", True, "M8", "M7")
    m8_digest = fingerprint(m8_document)
    m8_cpp = render_cpp(m8_document, m8_digest)
    m8_rust = render_rust(m8_document, m8_digest)
    if "static_assert(!kCandidate.delegated_task_start" in m8_cpp:
        failures.append("the M8 C++ projection retained an always-disabled assertion")
    if "task_milestone: Some(Milestone::M8)" not in m8_rust or (
        "policy_milestone: Some(PolicyMilestone::M7)" not in m8_rust
    ):
        failures.append("the M8 Rust projection did not retain the M8/M7 tuple")

    if fingerprint(base) == fingerprint({**base, "accepted_milestone": "M1"}):
        failures.append("the fingerprint did not move with accepted_milestone")

    with tempfile.TemporaryDirectory() as directory:
        args_dir = Path(directory)
        for name, selected in base["chromium_profiles"].items():
            (args_dir / f"{name}.gn").write_text(
                f'taffy_capability_profile = "{selected}"\n', encoding="utf-8"
            )
        if profile_assignment_findings(base, args_dir):
            failures.append("the matching profile fixture was refused")
        (args_dir / "release-arm64.gn").write_text(
            'taffy_capability_profile = "development"\n', encoding="utf-8"
        )
        if not profile_assignment_findings(base, args_dir):
            failures.append("a mismatched profile assignment was not caught")
        (args_dir / "new-arm64.gn").write_text(
            'taffy_capability_profile = "candidate"\n', encoding="utf-8"
        )
        if not any(
            finding.startswith("unregistered Chromium profiles:")
            for finding in profile_assignment_findings(base, args_dir)
        ):
            failures.append("an unregistered args file was not caught")

    for failure in failures:
        print(f"self-test: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(
        f"self-test: {len(cases)} refusal cases and "
        f"{len(candidate_shapes)} exact candidate shapes and M8 projections pass"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    mode.add_argument("--format", choices=("release", "gn", "cpp", "cpp-impl", "rust"))
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    try:
        document = load()
        validate(document)
    except CapabilityProfileError as error:
        print(f"product capabilities: {error}", file=sys.stderr)
        return 1

    if arguments.format:
        renderers = {
            "release": render_release_json,
            "gn": render_gni,
            "cpp": render_cpp,
            "cpp-impl": render_cpp_impl,
            "rust": render_rust,
        }
        sys.stdout.write(renderers[arguments.format](document, fingerprint(document)))
        return 0

    if arguments.write:
        for label, path, contents in rendered_artifacts(document):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")
            print(f"wrote {label}: {path.relative_to(REPO_ROOT)}")
        return 0

    findings = profile_assignment_findings(document, REPO_ROOT / "chromium/args")
    for label, path, expected in rendered_artifacts(document):
        try:
            actual = path.read_text(encoding="utf-8")
        except OSError:
            actual = ""
        if actual != expected:
            findings.append(f"stale {label}: {path.relative_to(REPO_ROOT)}")
    for finding in findings:
        print(f"product capabilities: {finding}", file=sys.stderr)
    if findings:
        print(
            "product capabilities: fix profile selections or regenerate with "
            "`python3 taffy-core/build/product_capabilities.py --write`",
            file=sys.stderr,
        )
        return 1
    print(
        "product capabilities: candidate exactly matches accepted "
        f"{document['accepted_milestone']}; "
        f"{len(document['chromium_profiles'])} Chromium profiles and "
        f"{len(ARTIFACTS)} projections agree"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
