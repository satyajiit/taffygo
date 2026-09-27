#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Orchestrate a measured, device-side Task Benchmark execution."""

from __future__ import annotations

import argparse
import datetime
import json
import math
import os
import socket
import sys
import tempfile
import time
from typing import Any

from device import AdbDevice, DeviceError, build_facts
from launcher import COMPONENTS, LauncherError, install_test_packages, run_component
from manifest_fragment import measured_fragment
from scenario import Corpus, CorpusReadError
from score import score_run


EVIDENCE_VERSION = 3
REPORT_FLOOR = 50


def _timestamp() -> str:
    return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z"
    )


def _read_selection(path: str) -> list[str]:
    try:
        with open(path, encoding="utf-8") as handle:
            selected = [line.strip() for line in handle if line.strip()]
    except OSError as error:
        raise CorpusReadError(f"cannot read benchmark selection {path}: {error}") from error
    if not selected:
        raise CorpusReadError("the selected suite contains no scenarios")
    if len(selected) != len(set(selected)):
        raise CorpusReadError("the selected suite contains duplicate scenario ids")
    return selected


def _atomic_json(path: str, value: Any) -> None:
    directory = os.path.dirname(os.path.abspath(path))
    os.makedirs(directory, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".benchmark-", suffix=".json", dir=directory)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            json.dump(value, handle, ensure_ascii=False, indent=2, sort_keys=True)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def _load_matrix(path: str, device: dict[str, Any]) -> dict[str, Any]:
    if not path:
        return {"state": "unratified-local", "version": None, "path": None}
    try:
        with open(path, encoding="utf-8") as handle:
            matrix = json.load(handle)
    except (OSError, ValueError) as error:
        raise DeviceError(f"cannot read device matrix {path}: {error}") from error
    if not isinstance(matrix, dict) or matrix.get("state") != "ratified":
        raise DeviceError("the device matrix must be a JSON object with state=ratified")
    if not isinstance(matrix.get("version"), str) or not matrix["version"]:
        raise DeviceError("the ratified device matrix has no version")
    rows = matrix.get("devices")
    if not isinstance(rows, list):
        raise DeviceError("the ratified device matrix has no devices list")
    matching = [row for row in rows if row.get("fingerprint") == device["fingerprint"]]
    if len(matching) != 1:
        raise DeviceError("attached device fingerprint is not a unique member of the matrix")
    return {
        "state": "ratified",
        "version": matrix["version"],
        "path": os.path.abspath(path),
        "device_entry": matching[0],
    }


def _metrics(runs: list[dict[str, Any]]) -> dict[str, Any]:
    complete = [run for run in runs if run.get("score")]
    total_facts = sum(run["score"]["required_facts"] for run in complete)
    verified_facts = sum(run["score"]["verified_facts"] for run in complete)
    passed = sum(1 for run in complete if run["score"]["passed"])
    exact_outcomes = sum(
        1
        for run in complete
        if run["score"]["actual_outcome"] == run["score"]["expected_outcome"]
    )
    return {
        "scenario_runs": len(complete),
        "scenario_contract_passes": passed,
        "scenario_contract_pass_rate": passed / len(complete) if complete else None,
        "declared_outcomes_matched": exact_outcomes,
        "declared_outcome_match_rate": exact_outcomes / len(complete) if complete else None,
        "required_facts": total_facts,
        "verified_facts": verified_facts,
        "required_fact_recall": verified_facts / total_facts if total_facts else None,
        "seeded_secret_findings": sum(
            1
            for run in complete
            for finding in run["score"]["findings"]
            if finding["code"] in ("seeded-secret", "security")
        ),
        "live_model_findings": sum(
            1
            for run in complete
            for finding in run["score"]["findings"]
            if finding["code"] == "live-model"
        ),
    }


def _raw_run_path(raw_dir: str, scenario_id: str, sample: int) -> str:
    return os.path.join(raw_dir, "runs", scenario_id, f"sample-{sample}.json")


def _evidence_paths(args: argparse.Namespace) -> tuple[str, str]:
    if args.evidence:
        evidence = os.path.abspath(args.evidence)
        stem = os.path.splitext(os.path.basename(evidence))[0]
        raw = os.path.join(os.path.dirname(evidence), stem + ".d")
        return evidence, raw
    run_id = f"{datetime.datetime.now(datetime.timezone.utc):%Y%m%dT%H%M%SZ}-{args.suite}"
    raw = os.path.abspath(os.path.join(args.reports, run_id))
    return os.path.join(raw, "evidence.json"), raw


def _fragment(path: str, evidence: str, record: dict[str, Any]) -> None:
    if not path:
        return
    _atomic_json(path, measured_fragment(evidence, record))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--chromium-src", required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--web", required=True)
    parser.add_argument("--selection", required=True)
    parser.add_argument("--suite", choices=("production", "affected"), required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--samples", type=int, default=0)
    parser.add_argument("--matrix", default="")
    parser.add_argument("--reports", required=True)
    parser.add_argument("--evidence", default="")
    parser.add_argument("--fragment", default="")
    parser.add_argument("--timeout-seconds", type=int, default=1800)
    return parser


def execute(args: argparse.Namespace) -> tuple[int, str, dict[str, Any]]:
    generated = _timestamp()
    evidence_path, raw_dir = _evidence_paths(args)
    record: dict[str, Any] = {
        "evidence_version": EVIDENCE_VERSION,
        "generated_at": generated,
        "host": {"hostname": socket.gethostname(), "platform": sys.platform},
        "requested_suite": args.suite,
        "selected_scenarios": [],
        "sample_count": 0,
        "measured": False,
        "suite_complete": False,
        "suite_passed": False,
        "report_floor_met": False,
        "release_eligible": False,
        "blockers": [],
        "runs": [],
    }
    try:
        corpus = Corpus(args.repo_root, args.corpus, args.web)
        selected = _read_selection(args.selection)
        scenarios = corpus.scenarios(selected)
        samples = args.samples or max(1, math.ceil(REPORT_FLOOR / len(selected)))
        if samples <= 0:
            raise CorpusReadError("--samples must be a positive integer")
        if args.suite == "production" and len(selected) * samples < REPORT_FLOOR:
            raise CorpusReadError(
                f"production selects {len(selected) * samples} runs; report floor is {REPORT_FLOOR}"
            )
        if args.suite == "production" and not args.matrix:
            raise DeviceError("production requires the ratified device matrix; pass --matrix FILE")
        if corpus.scripted_outputs_state != "not-recorded":
            raise CorpusReadError(
                "this runner only permits the corpus's no-live-model state; scripted state changed"
            )

        device = AdbDevice(args.device)
        device.require_ready(args.profile)
        device_facts = device.facts()
        matrix = _load_matrix(args.matrix, device_facts)
        build = build_facts(args.repo_root, args.chromium_src, args.profile)
        product = device.package("com.taffygo.browser")
        record.update(
            {
                "selected_scenarios": selected,
                "sample_count": samples,
                "scenario_corpus": {
                    "corpus": corpus.manifest["corpus"],
                    "version": corpus.version,
                    "digest": corpus.digest,
                    "scripted_model_outputs": corpus.scripted_outputs_state,
                },
                "page_fixtures": {
                    "corpus": corpus.web_manifest["corpus"],
                    "version": corpus.web_version,
                },
                "device": device_facts,
                "matrix": matrix,
                "build": build,
                "installed_product": product,
                "test_packages": install_test_packages(device, args.chromium_src, args.profile),
            }
        )
        os.makedirs(raw_dir, exist_ok=True)
        for sample in range(1, samples + 1):
            sample_thermal_before = device.thermal()
            sample_battery_before = device.battery()
            launched: dict[str, dict[str, Any]] = {}
            for component in COMPONENTS:
                launched[component.kind] = run_component(
                    args.repo_root,
                    args.profile,
                    args.device,
                    component,
                    selected,
                    raw_dir,
                    sample,
                    args.timeout_seconds,
                )
            sample_thermal_after = device.thermal()
            sample_battery_after = device.battery()
            for scenario in scenarios:
                components = [
                    launched[kind]["records"][scenario.scenario_id]
                    for kind in ("observation", "task")
                ]
                score = score_run(corpus, scenario, components)
                raw_run = {
                    "sample": sample,
                    "scenario_id": scenario.scenario_id,
                    "components": components,
                    "score": score,
                    "test_elapsed_ms": {
                        kind: launched[kind]["test_elapsed_ms"][scenario.scenario_id]
                        for kind in launched
                    },
                    "thermal_before": sample_thermal_before,
                    "thermal_after": sample_thermal_after,
                    "battery_before": sample_battery_before,
                    "battery_after": sample_battery_after,
                }
                raw_path = _raw_run_path(raw_dir, scenario.scenario_id, sample)
                _atomic_json(raw_path, raw_run)
                record["runs"].append(
                    {
                        "scenario_id": scenario.scenario_id,
                        "sample": sample,
                        "passed": score["passed"],
                        "actual_outcome": score["actual_outcome"],
                        "finding_codes": [item["code"] for item in score["findings"]],
                        "raw": os.path.relpath(raw_path, os.path.dirname(evidence_path)).replace(
                            os.sep, "/"
                        ),
                    }
                )
                record["measured"] = True
        record["suite_complete"] = len(record["runs"]) == len(selected) * samples
        record["report_floor_met"] = len(record["runs"]) >= REPORT_FLOOR
        record["metrics"] = _metrics(
            [json.load(open(_raw_run_path(raw_dir, row["scenario_id"], row["sample"]), encoding="utf-8")) for row in record["runs"]]
        )
        record["suite_passed"] = record["suite_complete"] and all(
            row["passed"] for row in record["runs"]
        )
        record["release_eligible"] = (
            args.suite == "production"
            and matrix["state"] == "ratified"
            and record["report_floor_met"]
            and record["suite_passed"]
        )
    except (CorpusReadError, DeviceError, LauncherError, OSError, ValueError) as error:
        record["blockers"].append(str(error))
    _atomic_json(evidence_path, record)
    _fragment(args.fragment, evidence_path, record)
    return (0 if record["suite_passed"] else 1), evidence_path, record


def main(argv: list[str]) -> int:
    args = _parser().parse_args(argv)
    status, evidence, record = execute(args)
    print(f"benchmark evidence: {evidence}")
    print(
        f"measured={str(record['measured']).lower()} "
        f"runs={len(record['runs'])} passed={str(record['suite_passed']).lower()}"
    )
    for blocker in record["blockers"]:
        print(f"blocked: {blocker}", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
