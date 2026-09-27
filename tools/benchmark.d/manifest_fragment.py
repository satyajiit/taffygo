#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Derive release-manifest benchmark claims from exact evidence bytes.

The benchmark runner owns measurement facts. It does not own benchmark-owner
approval, the comparison baseline, or the separately attested test-suite row.
Keeping that boundary here prevents a measured run from minting authority it
does not have, while ensuring every scalar it does emit is copied from and
bound to the report rather than typed again by a release job.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import PurePosixPath
import sys
import tempfile
from typing import Any


class FragmentError(ValueError):
    """The evidence record cannot support a manifest fragment."""


def _sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    try:
        with open(path, "rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise FragmentError(f"cannot hash benchmark evidence {path}: {error}") from error
    return digest.hexdigest()


def _evidence_identity(evidence_path: str) -> dict[str, Any]:
    if not evidence_path:
        return {}
    return {
        "benchmark_evidence": os.path.basename(evidence_path),
        "benchmark_evidence_sha256": _sha256_file(evidence_path),
    }


def _artifact_entry(path: str, relative: str, kind: str) -> dict[str, Any]:
    return {
        "path": relative,
        "kind": kind,
        "sha256": _sha256_file(path),
        "size_bytes": os.path.getsize(path),
    }


def _raw_artifacts(evidence_path: str, runs: list[Any]) -> list[dict[str, Any]]:
    base = os.path.realpath(os.path.dirname(os.path.abspath(evidence_path)))
    artifacts = []
    seen = set()
    for index, row in enumerate(runs):
        raw = row.get("raw") if isinstance(row, dict) else None
        relative = PurePosixPath(raw) if isinstance(raw, str) else None
        if (
            relative is None
            or not raw
            or relative.is_absolute()
            or ".." in relative.parts
            or raw != relative.as_posix()
        ):
            raise FragmentError(f"benchmark run {index} has an unsafe raw evidence path")
        full = os.path.realpath(os.path.join(base, *relative.parts))
        try:
            inside = os.path.commonpath((base, full)) == base
        except ValueError:
            inside = False
        if not inside or not os.path.isfile(full):
            raise FragmentError(f"benchmark raw evidence is missing or escapes its report: {raw}")
        if raw in seen:
            raise FragmentError(f"benchmark raw evidence path occurs more than once: {raw}")
        seen.add(raw)
        artifacts.append(_artifact_entry(full, raw, "benchmark-run"))
    return artifacts


def _corpus_version(record: dict[str, Any]) -> str | None:
    fixtures = record.get("page_fixtures")
    if not isinstance(fixtures, dict):
        return None
    corpus = fixtures.get("corpus")
    version = fixtures.get("version")
    if not isinstance(corpus, str) or not corpus:
        return None
    if not isinstance(version, str) or not version:
        return None
    return f"{corpus}-{version}"


def measured_fragment(evidence_path: str, record: dict[str, Any]) -> dict[str, Any]:
    """Claims emitted by the device runner, including blocked/failed runs."""
    selected = record.get("selected_scenarios")
    runs = record.get("runs")
    if not isinstance(selected, list) or not isinstance(runs, list):
        raise FragmentError("measured benchmark evidence has malformed scenario or run arrays")

    tests = _evidence_identity(evidence_path)
    tests.update(
        {
            "benchmark_runs": len(runs),
            "benchmark_scenarios": len(selected),
            "benchmark_measured": record.get("measured") is True,
        }
    )
    corpus_version = _corpus_version(record)
    if corpus_version:
        tests["corpus_version"] = corpus_version

    matrix = record.get("matrix")
    if isinstance(matrix, dict) and matrix.get("state") == "ratified":
        version = matrix.get("version")
        if isinstance(version, str) and version:
            tests["device_matrix_id"] = version

    # Deliberately absent: benchmark_approved, benchmark_baseline and suites.
    # Their owners must merge separate fragments carrying review and an
    # attested kind=test-report path; evidence.json is a benchmark-report.
    artifacts = [
        _artifact_entry(
            evidence_path,
            os.path.basename(evidence_path),
            "benchmark-report",
        )
    ]
    artifacts.extend(_raw_artifacts(evidence_path, runs))
    return {"tests": tests, "artifacts": artifacts}


def preflight_fragment(evidence_path: str, record: dict[str, Any]) -> dict[str, Any]:
    """Explicitly non-measured claims emitted by --list/input preflight."""
    if record.get("measured") is not False:
        raise FragmentError("preflight evidence must explicitly record measured=false")
    tests = _evidence_identity(evidence_path)
    tests.update(
        {
            "benchmark_runs": 0,
            "benchmark_scenarios": 0,
            "benchmark_measured": False,
            "suites": [],
        }
    )
    corpus_version = _corpus_version(record)
    if corpus_version:
        tests["corpus_version"] = corpus_version
    return {"tests": tests}


def _self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="taffy-benchmark-fragment-") as directory:
        evidence = os.path.join(directory, "evidence.json")
        raw = os.path.join(directory, "runs", "TB-101", "sample-1.json")
        os.makedirs(os.path.dirname(raw), exist_ok=True)
        with open(raw, "w", encoding="utf-8") as handle:
            json.dump({"scenario_id": "TB-101", "sample": 1}, handle, sort_keys=True)
            handle.write("\n")
        with open(evidence, "w", encoding="utf-8") as handle:
            json.dump({"evidence_version": 3}, handle, sort_keys=True)
            handle.write("\n")

        fragment = measured_fragment(
            evidence,
            {
                "measured": True,
                "selected_scenarios": ["TB-101"],
                "runs": [{
                    "scenario_id": "TB-101",
                    "sample": 1,
                    "passed": True,
                    "raw": "runs/TB-101/sample-1.json",
                }],
                "page_fixtures": {"corpus": "taffy-web", "version": "7"},
                "matrix": {"state": "ratified", "version": "matrix-3"},
            },
        )
        measured = fragment["tests"]
        expected = {
            "benchmark_evidence": "evidence.json",
            "benchmark_evidence_sha256": _sha256_file(evidence),
            "benchmark_runs": 1,
            "benchmark_scenarios": 1,
            "benchmark_measured": True,
            "corpus_version": "taffy-web-7",
            "device_matrix_id": "matrix-3",
        }
        if measured != expected:
            raise AssertionError(f"measured fragment mismatch: {measured!r}")
        forbidden = {"benchmark_approved", "benchmark_baseline", "suites"}
        if forbidden & measured.keys():
            raise AssertionError("the runner claimed approval, baseline, or suite authority")
        expected_artifacts = [
            _artifact_entry(evidence, "evidence.json", "benchmark-report"),
            _artifact_entry(raw, "runs/TB-101/sample-1.json", "benchmark-run"),
        ]
        if fragment["artifacts"] != expected_artifacts:
            raise AssertionError("measured fragment did not attest its summary and raw run")

        preflight = preflight_fragment(
            evidence,
            {
                "measured": False,
                "page_fixtures": {"corpus": "taffy-web", "version": "7"},
            },
        )["tests"]
        if (
            preflight["benchmark_measured"] is not False
            or preflight["benchmark_runs"] != 0
            or preflight["benchmark_scenarios"] != 0
            or preflight["suites"] != []
        ):
            raise AssertionError("preflight fragment became promotable")

    print("benchmark manifest-fragment self-test: measured claims bound; approval external")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] != ["--self-test"]:
        print(f"usage: {sys.argv[0]} --self-test", file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(_self_test())
