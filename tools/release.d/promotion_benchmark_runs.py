#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Bind benchmark summary rows to their attested raw-run records."""

from __future__ import annotations

import json
import os
import posixpath

import promotion_artifacts
import repo_facts


def check(manifest, artifacts_dir: str, report_path: str, runs: list[dict], report) -> None:
    report_directory = posixpath.dirname(report_path)
    for index, row in enumerate(runs):
        raw = row.get("raw")
        if (
            not isinstance(raw, str)
            or not raw
            or posixpath.isabs(raw)
            or raw != posixpath.normpath(raw)
        ):
            report.fail(
                f"tests.benchmark_evidence.runs.{index}.raw",
                "is absent or is not one canonical relative path",
                "Retain the raw path emitted by the benchmark runner without editing it.",
            )
            continue
        artifact_path = posixpath.normpath(posixpath.join(report_directory, raw))
        if not promotion_artifacts.matches(
            manifest, "benchmark-run", path=artifact_path
        ):
            report.fail(
                f"tests.benchmark_evidence.runs.{index}.raw",
                f"does not name an attested benchmark-run artifact ({artifact_path})",
                "Attach every raw run as a benchmark-run artifact. A summary pass with "
                "no retained observation is not release evidence.",
            )
            continue
        path = repo_facts.path_beneath(artifacts_dir, artifact_path)
        if path is None or not os.path.isfile(path):
            report.fail(
                f"tests.benchmark_evidence.runs.{index}.raw",
                "is missing or escapes the artifact directory",
                "Stage the raw run beneath the upload directory and reassemble the manifest.",
            )
            continue
        try:
            with open(path, encoding="utf-8") as handle:
                raw_run = json.load(handle)
        except (OSError, json.JSONDecodeError) as error:
            report.fail(
                f"tests.benchmark_evidence.runs.{index}.raw",
                f"is not a readable raw-run JSON object: {error}",
                "Retain the unchanged JSON written by the benchmark runner.",
            )
            continue
        if not isinstance(raw_run, dict):
            report.fail(
                f"tests.benchmark_evidence.runs.{index}.raw",
                "does not contain a raw-run object",
                "Retain the unchanged JSON written by the benchmark runner.",
            )
            continue
        score = raw_run.get("score") if isinstance(raw_run.get("score"), dict) else {}
        findings = score.get("findings")
        findings_valid = isinstance(findings, list) and all(
            isinstance(item, dict)
            and isinstance(item.get("code"), str)
            and bool(item.get("code"))
            for item in findings
        )
        raw_codes = [item["code"] for item in findings] if findings_valid else None
        if (
            raw_run.get("scenario_id") != row.get("scenario_id")
            or not isinstance(raw_run.get("sample"), int)
            or isinstance(raw_run.get("sample"), bool)
            or raw_run.get("sample") != row.get("sample")
            or score.get("passed") is not row.get("passed")
            or score.get("actual_outcome") != row.get("actual_outcome")
            or raw_codes != row.get("finding_codes")
        ):
            report.fail(
                f"tests.benchmark_evidence.runs.{index}.raw",
                "contradicts its summary scenario, sample, verdict, outcome, or findings",
                "Regenerate the summary from the unchanged raw runs; never reconcile "
                "contradictory evidence by hand.",
            )
