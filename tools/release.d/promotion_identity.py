#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Verify capability and evidence identities for promotion_policy."""

from __future__ import annotations

import json
import os
import subprocess

import repo_facts

CAPABILITY_PROJECTION = os.path.join(
    "taffy-core", "build", "capabilities", "generated", "product-capabilities.json"
)
EVIDENCE_TOOL = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "evidence"))
CANDIDATE_RELEASE_CAPABILITY = {
    "delegated_task_start": True,
    "task_milestone": "M8",
    "policy_milestone": "M7",
}


def _get(node, *path, default=None):
    for key in path:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node


def _attests(manifest, kind: str, path, digest) -> bool:
    return any(
        artifact.get("kind") == kind
        and artifact.get("path") == path
        and artifact.get("sha256") == digest
        for artifact in manifest.get("artifacts", [])
    )


def check(manifest, root: str, artifacts_dir: str, report) -> None:
    """Bind the candidate to this checkout's capability and evidence facts."""
    capability = _get(manifest, "identity", "capability_profile", default={}) or {}
    if capability.get("name") != "candidate":
        report.fail(
            "identity.capability_profile.name",
            f'is "{capability.get("name")}" rather than "candidate"',
            "Build release-arm64 from the generated candidate capability profile. "
            "Development authority is never promotable.",
        )
    if capability.get("accepted_milestone") != "M8":
        report.fail(
            "identity.capability_profile.accepted_milestone",
            f'is "{capability.get("accepted_milestone")}" rather than "M8"',
            "TaffyGo ships once. Accept every M0-M8 exit before producing the public "
            "candidate; an earlier internal milestone is not a release.",
        )
    for field, expected in CANDIDATE_RELEASE_CAPABILITY.items():
        if capability.get(field) != expected:
            report.fail(
                f"identity.capability_profile.{field}",
                f"is {capability.get(field)!r} rather than {expected!r} for the M8 candidate",
                "The public candidate must expose the accepted M8 task surface on "
                "the ratified M7 policy. Keep the current M0 candidate quarantined "
                "until every milestone exit is accepted; never promote a task-disabled build.",
            )

    projection_path = os.path.join(root, CAPABILITY_PROJECTION)
    try:
        with open(projection_path, encoding="utf-8") as handle:
            projection = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        report.fail(
            "identity.capability_profile",
            f"cannot read {CAPABILITY_PROJECTION}: {error}",
            "Regenerate the product-capability projection before composing the "
            "manifest. Missing authority is a closed candidate profile.",
        )
    else:
        if not isinstance(projection, dict):
            report.fail(
                "identity.capability_profile",
                f"{CAPABILITY_PROJECTION} does not contain a JSON object",
                "Regenerate the product-capability projection before composing the "
                "manifest. Malformed authority is a closed candidate profile.",
            )
            projection = {}
        actual_sha = repo_facts.sha256_file(projection_path)
        comparisons = (
            ("projection_sha256", actual_sha),
            ("configuration_fingerprint", projection.get("configuration_fingerprint")),
            ("accepted_milestone", projection.get("accepted_milestone")),
            (
                "delegated_task_start",
                _get(
                    projection, "profiles", capability.get("name"),
                    "delegated_task_start",
                ),
            ),
            (
                "task_milestone",
                _get(projection, "profiles", capability.get("name"), "task_milestone"),
            ),
            (
                "policy_milestone",
                _get(projection, "profiles", capability.get("name"), "policy_milestone"),
            ),
        )
        for field, actual in comparisons:
            if capability.get(field) != actual:
                report.fail(
                    f"identity.capability_profile.{field}",
                    "does not match the generated product-capability projection",
                    "Regenerate the manifest from the checkout that built the artifact; "
                    "never edit a capability identity into agreement.",
                )
        build_profile = _get(manifest, "build", "profile")
        selected = _get(projection, "chromium_profiles", build_profile)
        if selected != capability.get("name"):
            report.fail(
                "identity.capability_profile.name",
                f'{build_profile} selects "{selected}" in the generated projection',
                "Use the committed release-arm64 mapping and rebuild. The manifest "
                "cannot relabel a development capability profile.",
            )

    evidence = _get(manifest, "identity", "evidence_index", default={}) or {}
    source_revision = _get(manifest, "source", "taffy", "revision")
    if evidence.get("source_revision") != source_revision:
        report.fail(
            "identity.evidence_index.source_revision",
            "does not match the artifact source revision",
            "Generate the evidence index from the same immutable revision as the "
            "candidate. Evidence from another checkout does not transfer.",
        )
    if not _attests(
        manifest, "evidence-index", evidence.get("path"), evidence.get("sha256")
    ):
        report.fail(
            "identity.evidence_index",
            "does not identify one attested evidence-index artifact by path and digest",
            "Attach the index as --artifact evidence-index:<path> and record that "
            "artifact's exact path and SHA-256. Two unlinked claims are not evidence.",
        )
        return

    index_path = repo_facts.path_beneath(artifacts_dir, evidence.get("path"))
    if index_path is None or not os.path.isfile(index_path):
        report.fail(
            "identity.evidence_index.path",
            "is not a readable file beneath the artifact directory",
            "Attach the evidence index beside the candidate and use its relative path.",
        )
        return
    if repo_facts.sha256_file(index_path) != evidence.get("sha256"):
        report.fail(
            "identity.evidence_index.sha256",
            "does not match the attached evidence index",
            "Reassemble the candidate from the unchanged index; never repair its "
            "digest by hand.",
        )
        return
    try:
        completed = subprocess.run(
            [
                EVIDENCE_TOOL,
                "verify",
                index_path,
                "--artifacts-dir",
                os.path.realpath(artifacts_dir),
                "--root",
                os.path.realpath(root),
                "--candidate",
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except OSError as error:
        report.fail(
            "identity.evidence_index",
            f"could not run tools/evidence: {error}",
            "Restore the repository-owned evidence verifier and run its self-test.",
        )
        return
    if completed.returncode != 0:
        detail = next(
            (line.strip() for line in reversed(completed.stdout.splitlines()) if line.strip()),
            "the evidence verifier returned no detail",
        )
        report.fail(
            "identity.evidence_index",
            f"tools/evidence rejected candidate readiness: {detail}",
            "Run ./tools/evidence verify <index> --artifacts-dir <dir> --root . "
            "--candidate and close every named milestone or integrity finding.",
        )
