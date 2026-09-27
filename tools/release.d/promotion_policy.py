#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Promotion policy behind one release-verifier seam.

The manifest schema owns field shape and the file verifiers own bytes on disk.
This module owns the cross-field claim: whether the manifest is eligible for
promotion. Candidate verification always crosses this seam; callers cannot
opt out of approvals, suites, devices, or rollback evidence.

Owning milestone: M1 (WP-M1-07). Policy authority:
docs/development/testing-and-delivery.md sections 9.5, 13, 14 and 16.
"""

from __future__ import annotations

import promotion_approvals
import promotion_artifacts
import promotion_benchmark
import promotion_identity


# Kept on the public seam for the fixture suite and callers that enumerate the
# closed policy set. Implementation and attestation logic live in the owning
# artifact module.
REQUIRED_CANDIDATE_ARTIFACT_KINDS = promotion_artifacts.REQUIRED_KINDS

REQUIRED_CANDIDATE_SUITES = (
    "check-docs",
    "check-fast",
    "taffy_unittests",
    "taffy_browsertests",
    "taffy_public_test_apk",
    "taffy_shell_junit_tests",
    "android-instrumentation",
    "diagnostic-sanitizer",
    "bip-fuzzers",
    "upstream-regression",
    "benchmark-production",
    "accessibility",
    "privacy-data-deletion",
    "release-configuration",
    "rollback-rehearsal",
)


def get(node, *path, default=None):
    for key in path:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node


def check(
    manifest,
    profile: str,
    root: str,
    artifacts_dir: str,
    report,
    require_approvals: bool = False,
) -> None:
    """Evaluate all promotion semantics through one interface."""
    report.check("promotion-policy")
    kind = get(manifest, "release", "kind")
    promotable = get(manifest, "release", "promotable")

    if kind != profile:
        report.fail(
            "release.kind",
            f'the manifest says "{kind}" but verification requested "{profile}"',
            "Verify the artifact under the kind that built it. A development manifest "
            "cannot become a candidate by passing --profile candidate.",
        )

    if profile != "candidate":
        if promotable is not False:
            report.fail(
                "release.promotable",
                f"a {profile} artifact does not explicitly say false",
                "Development and drill manifests record promotable:false. They are "
                "useful evidence, but they are never promotion inputs.",
            )
        if require_approvals:
            promotion_approvals.check(manifest, report)
        return

    if promotable is not True:
        report.fail(
            "release.promotable",
            "the candidate does not explicitly say true",
            "Set this only in the promotion fragment after every candidate check has "
            "produced the evidence carried by this manifest.",
        )

    promotion_artifacts.check(manifest, report)
    _check_suites(manifest, report)
    promotion_benchmark.check(manifest, root, artifacts_dir, report)
    promotion_identity.check(manifest, root, artifacts_dir, report)
    _check_vulnerability_scan(manifest, report)
    _check_rollback(manifest, report)
    promotion_approvals.check(manifest, report)


def _check_suites(manifest, report) -> None:
    suites = get(manifest, "tests", "suites", default=[]) or []
    by_name: dict[str, list[dict]] = {}
    for suite in suites:
        by_name.setdefault(suite.get("name", ""), []).append(suite)
    for name in REQUIRED_CANDIDATE_SUITES:
        if name not in by_name:
            report.fail(
                f"tests.suites.{name}",
                "the required suite has no result",
                f'Run the "{name}" candidate suite and attach its test report.',
            )
    for name, rows in by_name.items():
        if len(rows) > 1:
            report.fail(
                f"tests.suites.{name}",
                f"appears {len(rows)} times",
                "Emit one final result per named suite. Appending contradictory job "
                "fragments must not let a passing row hide a failing one.",
            )
        for row in rows:
            result = row.get("result")
            if result != "pass":
                report.fail(
                    f"tests.suites.{name}.result",
                    f'is "{result}", not "pass"',
                    "A failure, flaky retry, skip, or unrun suite cannot support "
                    "promotion. Fix it and rerun from the candidate artifact.",
                )
            if row.get("retries") != 0:
                report.fail(
                    f"tests.suites.{name}.retries",
                    f'records {row.get("retries")} retry attempt(s)',
                    "A passing retry is flaky evidence, not a candidate pass. Fix the "
                    "flake and produce a clean zero-retry result.",
                )
            if row.get("skipped") != 0:
                report.fail(
                    f"tests.suites.{name}.skipped",
                    f'records {row.get("skipped")} skipped check(s)',
                    "Run every check the suite owns. A suite may not summarize itself "
                    "as passing while any child check was skipped.",
                )
            report_path = row.get("report_path")
            if not report_path or not promotion_artifacts.matches(
                manifest, "test-report", path=report_path
            ):
                report.fail(
                    f"tests.suites.{name}.report_path",
                    "does not name an attested test-report artifact",
                    "Attach the suite report and make report_path equal the artifact "
                    "path. A result with no report is an assertion, not evidence.",
                )


def _check_vulnerability_scan(manifest, report) -> None:
    scan = get(manifest, "dependencies", "vulnerability_scan", default={}) or {}
    if scan.get("status") != "clean" or scan.get("findings") != 0:
        report.fail(
            "dependencies.vulnerability_scan.status",
            f'records status="{scan.get("status")}" and findings={scan.get("findings")}',
            "Triage every finding, rerun the pinned scanner, and promote only a clean "
            "zero-finding report.",
        )
    promotion_artifacts.require_attested(
        manifest,
        "dependencies.vulnerability_scan",
        "vulnerability-report",
        scan.get("path"),
        next(
            (
                artifact.get("sha256")
                for artifact in manifest.get("artifacts", [])
                if artifact.get("kind") == "vulnerability-report"
                and artifact.get("path") == scan.get("path")
            ),
            None,
        ),
        report,
    )


def _check_rollback(manifest, report) -> None:
    rollback = manifest.get("rollback") or {}
    if rollback.get("result") != "pass":
        report.fail(
            "rollback.result",
            f'is "{rollback.get("result")}" rather than "pass"',
            "Complete the rollback rehearsal against the exact candidate and attach "
            "its record. A blocked or unrun step is a candidate failure.",
        )
    promotion_artifacts.require_attested(
        manifest,
        "rollback.record_path",
        "rollback-evidence",
        rollback.get("record_path"),
        rollback.get("record_sha256"),
        report,
    )
