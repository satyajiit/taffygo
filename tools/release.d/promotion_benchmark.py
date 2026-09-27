#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Candidate device-matrix and measured Task Benchmark policy.

This module reopens the attested version-3 benchmark report and derives every
promotion verdict from its contents. Manifest booleans and counts are only
cross-checks; Python's bool/int equivalence is never accepted as JSON typing.
"""

from __future__ import annotations

import json
import os

import promotion_artifacts
import promotion_benchmark_runs
import promotion_corpus
import repo_facts


PHYSICAL_DEVICE_TIERS = ("low", "mid", "high")


def _get(node, *path, default=None):
    for key in path:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node


def _integer(value) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _positive_integer(value) -> bool:
    return _integer(value) and value > 0


def _claim_matches(actual, expected) -> bool:
    if isinstance(expected, bool):
        return actual is expected
    if isinstance(expected, int):
        return _integer(actual) and actual == expected
    return actual == expected


def check(manifest, root: str, artifacts_dir: str, report) -> None:
    devices = _get(manifest, "tests", "devices", default=[]) or []
    if not devices:
        report.fail(
            "tests.devices",
            "the candidate names no approved device",
            "Run the candidate suites on the approved physical matrix and record each "
            "matrix row, model, API, ABI, and tier.",
        )
    for index, device in enumerate(devices):
        if not isinstance(device, dict) or device.get("approved") is not True:
            report.fail(
                f"tests.devices.{index}.approved",
                "is not an approved matrix observation",
                "Use a ratified device-matrix row and record approved:true from the "
                "matrix owner; an arbitrary attached phone is not release evidence.",
            )
    matrix_rows = [
        device.get("matrix_row")
        for device in devices
        if isinstance(device, dict)
        and isinstance(device.get("matrix_row"), str)
        and device.get("matrix_row")
    ]
    if len(matrix_rows) != len(set(matrix_rows)):
        report.fail(
            "tests.devices",
            "the same approved matrix row appears more than once",
            "Record each physical matrix observation once. Relabeling one device as "
            "several tiers does not exercise the ratified matrix.",
        )
    approved_tiers = {
        device.get("tier")
        for device in devices
        if isinstance(device, dict)
        and device.get("approved") is True
        and device.get("tier") in PHYSICAL_DEVICE_TIERS
    }
    missing_tiers = set(PHYSICAL_DEVICE_TIERS) - approved_tiers
    if devices and missing_tiers:
        detail = (
            "the candidate was tested only on emulators"
            if all(
                isinstance(device, dict) and device.get("tier") == "emulator"
                for device in devices
            )
            else "missing approved physical tier(s): " + ", ".join(sorted(missing_tiers))
        )
        report.fail(
            "tests.devices",
            detail,
            "The release-candidate gate requires approved low-, mid-, and high-tier "
            "physical observations from the ratified matrix.",
        )

    measured = _get(manifest, "tests", "benchmark_measured")
    approved = _get(manifest, "tests", "benchmark_approved")
    runs = _get(manifest, "tests", "benchmark_runs", default=0)
    scenarios = _get(manifest, "tests", "benchmark_scenarios", default=0)
    if (
        measured is not True
        or approved is not True
        or not _positive_integer(runs)
        or not _positive_integer(scenarios)
    ):
        report.fail(
            "tests.benchmark_evidence",
            f"records measured={measured!r}, approved={approved!r}, {runs!r} run(s), "
            f"{scenarios!r} scenario(s)",
            "Run the registered production corpus through the measured adapter, "
            "obtain the benchmark owner's approval, and attach the resulting report.",
        )
    promotion_artifacts.require_attested(
        manifest,
        "tests.benchmark_evidence",
        "benchmark-report",
        _get(manifest, "tests", "benchmark_evidence"),
        _get(manifest, "tests", "benchmark_evidence_sha256"),
        report,
    )
    _check_report(manifest, root, artifacts_dir, report)


def _check_report(manifest, root: str, artifacts_dir: str, report) -> None:
    tests = manifest.get("tests") or {}
    relative = tests.get("benchmark_evidence")
    digest = tests.get("benchmark_evidence_sha256")
    path = repo_facts.path_beneath(artifacts_dir, relative)
    if path is None or not os.path.isfile(path):
        report.fail(
            "tests.benchmark_evidence",
            "is not a readable report beneath the artifact directory",
            "Attach the JSON emitted by ./tools/benchmark --suite production beneath "
            "the upload directory and attest that exact relative path.",
        )
        return
    try:
        actual_digest = repo_facts.sha256_file(path)
    except OSError as error:
        report.fail(
            "tests.benchmark_evidence",
            f"cannot hash the attached report: {error}",
            "Restore the unchanged benchmark report before verifying the candidate.",
        )
        return
    if actual_digest != digest:
        report.fail(
            "tests.benchmark_evidence_sha256",
            "does not match the attached benchmark report",
            "Reassemble the manifest from the unchanged measured report; never edit "
            "its digest into agreement.",
        )
        return
    try:
        with open(path, encoding="utf-8") as handle:
            benchmark = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        report.fail(
            "tests.benchmark_evidence",
            f"is not a readable benchmark JSON report: {error}",
            "Attach the evidence.json emitted by the measured benchmark runner.",
        )
        return
    if not isinstance(benchmark, dict):
        report.fail(
            "tests.benchmark_evidence",
            "does not contain a benchmark report object",
            "Attach the evidence.json emitted by the measured benchmark runner.",
        )
        return

    corpus = promotion_corpus.load(root, report)
    if corpus is None:
        return

    expected_claims = {
        "evidence_version": 3,
        "requested_suite": "production",
        "measured": True,
        "suite_complete": True,
        "suite_passed": True,
        "report_floor_met": True,
        "release_eligible": True,
        "blockers": [],
    }
    for field, expected in expected_claims.items():
        actual = benchmark.get(field)
        if not _claim_matches(actual, expected):
            report.fail(
                f"tests.benchmark_evidence.{field}",
                f"records {actual!r}, expected {expected!r}",
                "Use a completed, passing production-suite report from the measured "
                "runner. Input preflight and affected-suite reports are not release evidence.",
            )

    selected = benchmark.get("selected_scenarios")
    runs = benchmark.get("runs")
    samples = benchmark.get("sample_count")
    selected_valid = (
        isinstance(selected, list)
        and bool(selected)
        and all(isinstance(item, str) and bool(item) for item in selected)
        and len(selected) == len(set(selected))
    )
    runs_array = isinstance(runs, list)
    run_rows_valid = runs_array and all(
        isinstance(row, dict)
        and isinstance(row.get("scenario_id"), str)
        and bool(row.get("scenario_id"))
        and _positive_integer(row.get("sample"))
        and row.get("passed") is True
        and isinstance(row.get("actual_outcome"), str)
        and bool(row.get("actual_outcome"))
        and isinstance(row.get("finding_codes"), list)
        and all(
            isinstance(code, str) and bool(code) for code in row.get("finding_codes", [])
        )
        for row in runs
    )
    samples_valid = _positive_integer(samples)
    if not selected_valid:
        report.fail(
            "tests.benchmark_evidence.selected_scenarios",
            "is empty, malformed, or contains duplicate scenario ids",
            "Rerun the registered production selection; do not hand-edit its scenario set.",
        )
    elif selected != corpus["selected_scenarios"]:
        report.fail(
            "tests.benchmark_evidence.selected_scenarios",
            "does not equal the candidate revision's complete production selection",
            "Rerun the production suite from this candidate checkout. Evidence from "
            "another scenario corpus or a selected subset does not transfer.",
        )
    if not runs_array:
        report.fail(
            "tests.benchmark_evidence.runs",
            "is not an array of measured scenario runs",
            "Attach the measured runner's complete report, including every raw-run link.",
        )
    elif not run_rows_valid:
        report.fail(
            "tests.benchmark_evidence.runs",
            "contains a malformed scenario, sample, verdict, outcome, or finding list",
            "Retain the typed object rows emitted by the measured runner; booleans "
            "are not sample numbers.",
        )
    if not samples_valid:
        report.fail(
            "tests.benchmark_evidence.sample_count",
            f"records {samples!r} rather than a positive sample count",
            "Run enough samples to meet the benchmark report floor.",
        )
    if selected_valid:
        claimed = tests.get("benchmark_scenarios")
        if not _integer(claimed) or claimed != len(selected):
            report.fail(
                "tests.benchmark_scenarios",
                f"claims {claimed!r}, report selected {len(selected)}",
                "Copy the scenario count from the attached report during manifest assembly.",
            )
    if runs_array:
        claimed = tests.get("benchmark_runs")
        if not _integer(claimed) or claimed != len(runs):
            report.fail(
                "tests.benchmark_runs",
                f"claims {claimed!r}, report contains {len(runs)}",
                "Copy the run count from the attached report during manifest assembly.",
            )
        if len(runs) < 50:
            report.fail(
                "tests.benchmark_evidence.runs",
                f"contains {len(runs)} run(s), below the 50-run report floor",
                "Run enough production samples to meet the registered report floor.",
            )
    if selected_valid and run_rows_valid and samples_valid:
        keys = [(row["scenario_id"], row["sample"]) for row in runs]
        expected_keys = {
            (scenario, sample)
            for scenario in selected
            for sample in range(1, samples + 1)
        }
        if set(keys) != expected_keys or len(keys) != len(set(keys)):
            report.fail(
                "tests.benchmark_evidence.runs",
                "does not contain exactly one run for every selected scenario and sample",
                "Retain the measured runner's complete run array; missing, duplicate, or "
                "unselected rows make the suite incomplete.",
            )
        elif any(row.get("passed") is not True for row in runs):
            report.fail(
                "tests.benchmark_evidence.runs",
                "contains a scenario run that did not pass",
                "Fix the scenario failure and produce a new complete production report.",
            )
        promotion_benchmark_runs.check(
            manifest, artifacts_dir, relative, runs, report
        )

    matrix = benchmark.get("matrix") if isinstance(benchmark.get("matrix"), dict) else {}
    if matrix.get("state") != "ratified" or matrix.get("version") != tests.get(
        "device_matrix_id"
    ):
        report.fail(
            "tests.device_matrix_id",
            f"does not match a ratified benchmark matrix ({matrix.get('version')!r})",
            "Run on the ratified matrix and copy its version from the benchmark report.",
        )
    build = benchmark.get("build") if isinstance(benchmark.get("build"), dict) else {}
    source_revision = _get(manifest, "source", "taffy", "revision")
    dirty_paths = build.get("git_dirty_paths")
    if (
        build.get("git_revision") != source_revision
        or not _integer(dirty_paths)
        or dirty_paths != 0
        or build.get("profile") != "release-arm64"
    ):
        report.fail(
            "tests.benchmark_evidence.build",
            "does not identify this clean release-arm64 source revision",
            "Run the production benchmark against the exact clean candidate build; "
            "benchmark evidence does not transfer between revisions or profiles.",
        )
    fixtures = (
        benchmark.get("page_fixtures")
        if isinstance(benchmark.get("page_fixtures"), dict)
        else {}
    )
    report_corpus = (
        f"{fixtures.get('corpus')}-{fixtures.get('version')}"
        if fixtures.get("corpus") and fixtures.get("version")
        else None
    )
    expected_page_corpus = f"{corpus['page_corpus']}-{corpus['page_version']}"
    if report_corpus != expected_page_corpus:
        report.fail(
            "tests.benchmark_evidence.page_fixtures",
            f"records {report_corpus!r}, candidate authority is {expected_page_corpus!r}",
            "Rerun the production suite against the page corpus committed at the "
            "candidate revision.",
        )
    if report_corpus != tests.get("corpus_version"):
        report.fail(
            "tests.corpus_version",
            f"claims {tests.get('corpus_version')!r}, report records {report_corpus!r}",
            "Copy the page-fixture corpus identity from the measured report.",
        )
    scenario_corpus = (
        benchmark.get("scenario_corpus")
        if isinstance(benchmark.get("scenario_corpus"), dict)
        else {}
    )
    expected_scenario = {
        "corpus": corpus["scenario_corpus"],
        "version": corpus["scenario_version"],
        "digest": corpus["scenario_digest"],
        "scripted_model_outputs": corpus["scripted_model_outputs"],
    }
    actual_scenario = {
        field: scenario_corpus.get(field) for field in expected_scenario
    }
    if actual_scenario != expected_scenario:
        report.fail(
            "tests.benchmark_evidence.scenario_corpus",
            "does not match the task corpus committed at the candidate revision",
            "Rerun the production suite from this candidate checkout; corpus name, "
            "version, digest and scripted-output state are one benchmark identity.",
        )
    if scenario_corpus.get("scripted_model_outputs") != "not-recorded":
        report.fail(
            "tests.benchmark_evidence.scenario_corpus",
            "does not record the deterministic no-live-model corpus state",
            "Use the registered fixture corpus and the fail-closed benchmark scorer.",
        )
