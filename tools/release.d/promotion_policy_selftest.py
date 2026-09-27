#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Fixture tests for the promotion-policy interface."""

from __future__ import annotations

import json
import os
import subprocess
import tempfile

import promotion_identity
import promotion_policy
import promotion_selftest_fixtures
import repo_facts

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")


class Report:
    def __init__(self) -> None:
        self.findings: list[dict] = []
        self.checks = 0

    def check(self, _name: str) -> None:
        self.checks += 1

    def fail(self, item: str, message: str, remediation: str) -> None:
        self.findings.append({"item": item, "message": message,
                              "remediation": remediation})


def patch(document, operations):
    document = json.loads(json.dumps(document))
    for operation in operations:
        tokens = [token for token in operation["pointer"].split("/") if token]
        node = document
        for token in tokens[:-1]:
            node = node[int(token)] if isinstance(node, list) else node[token]
        key = int(tokens[-1]) if isinstance(node, list) else tokens[-1]
        if operation["op"] == "remove":
            del node[key]
        elif operation["op"] == "set":
            node[key] = operation["value"]
        else:
            raise ValueError(f"unknown fixture operation: {operation['op']}")
    return document


def evidence_records(path: str, digest: str) -> list[dict]:
    records = []
    for milestone in (f"M{number}" for number in range(9)):
        test_id = f"{milestone.lower()}-test"
        for identity, kind, supports in (
            (test_id, "test", []),
            (f"{milestone.lower()}-exit", "milestone-exit", [test_id]),
        ):
            records.append({
                "id": identity,
                "class": "EV-04",
                "milestone": milestone,
                "kind": kind,
                "result": "pass",
                "required_for_candidate": True,
                "owner": "fixture owner",
                "command": "fixture command",
                "started_at": "2026-09-03T00:00:00Z",
                "completed_at": "2026-09-03T00:00:01Z",
                "environment": {"kind": "host", "identity": "fixture"},
                "requirements": [f"{milestone}-fixture"],
                "evidence": [{"path": path, "sha256": digest, "size_bytes": 3}],
                "supports": supports,
                "note": "synthetic promotion-policy evidence",
            })
    return records


def run(*command: str) -> str:
    return subprocess.run(
        command,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ).stdout.strip()


def main() -> int:
    with open(os.path.join(FIXTURES, "candidate-complete.json"), encoding="utf-8") as handle:
        complete = json.load(handle)
    with open(os.path.join(FIXTURES, "promotion-policy-cases.json"), encoding="utf-8") as handle:
        cases = json.load(handle)

    capability_source = {
        "schema_version": 1,
        "accepted_milestone": "M8",
        "profiles": {
            "candidate": {
                "delegated_task_start": True,
                "task_milestone": "M8",
                "policy_milestone": "M7",
            }
        },
        "chromium_profiles": {"release-arm64": "candidate"},
    }
    selected_scenarios = [f"scenario-{number:02d}" for number in range(1, 32)]
    task_corpus, page_corpus, corpus_digest = (
        promotion_selftest_fixtures.corpus(selected_scenarios)
    )
    projection = dict(capability_source)
    projection["configuration_fingerprint"] = "sha256:" + repo_facts.sha256_string(
        json.dumps(
            capability_source, sort_keys=True, separators=(",", ":"), ensure_ascii=True
        )
    )
    complete["identity"]["capability_profile"]["configuration_fingerprint"] = (
        projection["configuration_fingerprint"]
    )
    failures = 0
    with tempfile.TemporaryDirectory(prefix="taffy-release-policy-") as base:
        root = os.path.join(base, "repository")
        upload = os.path.join(base, "upload")
        path = os.path.join(root, promotion_identity.CAPABILITY_PROJECTION)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        os.makedirs(upload, exist_ok=True)
        source_path = os.path.join(root, "taffy-core", "build", "product-capabilities.json")
        with open(source_path, "w", encoding="utf-8") as handle:
            json.dump(capability_source, handle, sort_keys=True)
            handle.write("\n")
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(projection, handle, sort_keys=True)
            handle.write("\n")
        promotion_selftest_fixtures.stage_corpora(root, task_corpus, page_corpus)
        run("git", "init", "-q", root)
        run("git", "-C", root, "config", "user.email", "fixture@taffygo.invalid")
        run("git", "-C", root, "config", "user.name", "TaffyGo fixture")
        run("git", "-C", root, "add", ".")
        run("git", "-C", root, "commit", "-q", "-m", "fixture")
        revision = run("git", "-C", root, "rev-parse", "HEAD")
        complete["identity"]["capability_profile"]["projection_sha256"] = (
            repo_facts.sha256_file(path)
        )
        complete["source"]["taffy"]["revision"] = revision

        benchmark = {
            "evidence_version": 3,
            "requested_suite": "production",
            "measured": True,
            "suite_complete": True,
            "suite_passed": True,
            "report_floor_met": True,
            "release_eligible": True,
            "blockers": [],
            "selected_scenarios": selected_scenarios,
            "sample_count": 2,
            "runs": [
                {
                    "scenario_id": scenario,
                    "sample": sample,
                    "passed": True,
                    "actual_outcome": "verified-complete",
                    "finding_codes": [],
                    "raw": f"benchmark-runs/{scenario}/sample-{sample}.json",
                }
                for sample in (1, 2)
                for scenario in selected_scenarios
            ],
            "matrix": {"state": "ratified", "version": "candidate-matrix-fixture-1"},
            "build": {
                "profile": "release-arm64",
                "git_revision": revision,
                "git_dirty_paths": 0,
            },
            "page_fixtures": {"corpus": "web-fixtures", "version": "1"},
            "scenario_corpus": {
                "corpus": "fixture-task-scenarios",
                "version": "fixture-1",
                "digest": corpus_digest,
                "scripted_model_outputs": "not-recorded",
            },
        }
        benchmark_path = os.path.join(upload, "benchmark-report.json")
        promotion_selftest_fixtures.stage_raw_runs(upload, benchmark, complete)

        def stage_benchmark(document, manifest) -> None:
            with open(benchmark_path, "w", encoding="utf-8") as handle:
                json.dump(document, handle, sort_keys=True)
                handle.write("\n")
            digest = repo_facts.sha256_file(benchmark_path)
            manifest["tests"]["benchmark_evidence_sha256"] = digest
            artifact = next(
                row for row in manifest["artifacts"] if row["kind"] == "benchmark-report"
            )
            artifact["sha256"] = digest
            artifact["size_bytes"] = repo_facts.file_size(benchmark_path)

        stage_benchmark(benchmark, complete)

        proof = os.path.join(upload, "proof.txt")
        with open(proof, "wb") as handle:
            handle.write(b"ok\n")
        proof_digest = repo_facts.sha256_file(proof)
        fragment = os.path.join(upload, "evidence.fragment.json")
        with open(fragment, "w", encoding="utf-8") as handle:
            json.dump({
                "source_revision": revision,
                "records": evidence_records("proof.txt", proof_digest),
            }, handle)
            handle.write("\n")
        index = os.path.join(upload, "evidence-index.json")
        run(
            promotion_identity.EVIDENCE_TOOL,
            "assemble",
            "--profile", "candidate",
            "--out", index,
            "--artifacts-dir", upload,
            "--root", root,
            "--fragment", fragment,
        )
        index_digest = repo_facts.sha256_file(index)
        complete["identity"]["evidence_index"] = {
            "path": "evidence-index.json",
            "sha256": index_digest,
            "source_revision": revision,
        }
        evidence_artifact = next(
            row for row in complete["artifacts"] if row["kind"] == "evidence-index"
        )
        evidence_artifact["sha256"] = index_digest
        evidence_artifact["size_bytes"] = repo_facts.file_size(index)

        with open(path, "rb") as handle:
            projection_bytes = handle.read()
        stale_projection = json.loads(projection_bytes)
        stale_projection["accepted_milestone"] = "M7"
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(stale_projection, handle, sort_keys=True)
            handle.write("\n")
        stale_check = subprocess.run(
            [
                promotion_identity.EVIDENCE_TOOL,
                "assemble",
                "--profile", "candidate",
                "--out", os.path.join(upload, "stale-index.json"),
                "--artifacts-dir", upload,
                "--root", root,
                "--fragment", fragment,
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        with open(path, "wb") as handle:
            handle.write(projection_bytes)
        if stale_check.returncode == 0 or "projection is stale" not in stale_check.stdout:
            failures += 1
            print("FAIL  a projection that disagrees with its source authority was accepted")

        baseline = Report()
        promotion_policy.check(complete, "candidate", root, upload, baseline)
        if baseline.findings:
            failures += 1
            print("FAIL  complete candidate fixture failed promotion policy:")
            for finding in baseline.findings:
                print(f"        {finding['item']}: {finding['message']}")

        for case in cases:
            instance = patch(complete, case["patch"])
            report = Report()
            promotion_policy.check(
                instance, case.get("profile", "candidate"), root, upload, report
            )
            matches = [finding for finding in report.findings
                       if finding["item"] == case["expect"]["item"]
                       and case["expect"]["contains"] in finding["message"]]
            if not matches:
                failures += 1
                actual = ", ".join(
                    f"{finding['item']} ({finding['message']})" for finding in report.findings
                ) or "nothing"
                print(f"FAIL  {case['name']}: expected {case['expect']!r}; got {actual}")
            elif not matches[0]["remediation"]:
                failures += 1
                print(f"FAIL  {case['name']}: finding has no remediation")

        for kind in promotion_policy.REQUIRED_CANDIDATE_ARTIFACT_KINDS:
            instance = patch(complete, [{
                "op": "set",
                "pointer": "/artifacts",
                "value": [row for row in complete["artifacts"] if row["kind"] != kind],
            }])
            report = Report()
            promotion_policy.check(instance, "candidate", root, upload, report)
            if not any(row["item"] == f"artifacts.{kind}" for row in report.findings):
                failures += 1
                print(f"FAIL  missing required artifact kind was accepted: {kind}")

        for name in promotion_policy.REQUIRED_CANDIDATE_SUITES:
            instance = patch(complete, [{
                "op": "set",
                "pointer": "/tests/suites",
                "value": [row for row in complete["tests"]["suites"]
                          if row["name"] != name],
            }])
            report = Report()
            promotion_policy.check(instance, "candidate", root, upload, report)
            if not any(row["item"] == f"tests.suites.{name}" for row in report.findings):
                failures += 1
                print(f"FAIL  missing required suite was accepted: {name}")

        benchmark_cases = (
            (
                "an attested unmeasured report cannot inherit manifest success",
                [{"op": "set", "pointer": "/measured", "value": False}],
                "tests.benchmark_evidence.measured",
                "records False",
            ),
            (
                "an integer cannot stand in for a measured boolean",
                [{"op": "set", "pointer": "/measured", "value": 1}],
                "tests.benchmark_evidence.measured",
                "records 1",
            ),
            (
                "an attested report must be release eligible",
                [{"op": "set", "pointer": "/release_eligible", "value": False}],
                "tests.benchmark_evidence.release_eligible",
                "records False",
            ),
            (
                "an integer cannot stand in for a passing boolean",
                [{"op": "set", "pointer": "/suite_passed", "value": 1}],
                "tests.benchmark_evidence.suite_passed",
                "records 1",
            ),
            (
                "benchmark evidence cannot transfer between revisions",
                [{"op": "set", "pointer": "/build/git_revision", "value": "0" * 40}],
                "tests.benchmark_evidence.build",
                "does not identify",
            ),
            (
                "a boolean cannot stand in for a clean-path count",
                [{"op": "set", "pointer": "/build/git_dirty_paths", "value": False}],
                "tests.benchmark_evidence.build",
                "does not identify",
            ),
            (
                "a boolean cannot stand in for a run sample",
                [{"op": "set", "pointer": "/runs/0/sample", "value": True}],
                "tests.benchmark_evidence.runs",
                "malformed",
            ),
            (
                "every benchmark run must be an object",
                [{"op": "set", "pointer": "/runs/0", "value": "not-a-run"}],
                "tests.benchmark_evidence.runs",
                "malformed",
            ),
            (
                "a report with a missing scenario run is incomplete",
                [{"op": "remove", "pointer": "/runs/0"}],
                "tests.benchmark_runs",
                "report contains 61",
            ),
            (
                "benchmark evidence cannot transfer between task-corpus versions",
                [{"op": "set", "pointer": "/scenario_corpus/version", "value": "stale"}],
                "tests.benchmark_evidence.scenario_corpus",
                "does not match",
            ),
            (
                "the production report must select the complete current corpus",
                [{"op": "set", "pointer": "/selected_scenarios/0", "value": "stale"}],
                "tests.benchmark_evidence.selected_scenarios",
                "does not equal",
            ),
            (
                "every benchmark summary row must retain its raw run",
                [{"op": "remove", "pointer": "/runs/0/raw"}],
                "tests.benchmark_evidence.runs.0.raw",
                "absent",
            ),
            (
                "a benchmark summary cannot contradict its raw run",
                [{"op": "set", "pointer": "/runs/0/actual_outcome", "value": "blocked"}],
                "tests.benchmark_evidence.runs.0.raw",
                "contradicts",
            ),
        )
        for name, operations, item, message in benchmark_cases:
            instance = json.loads(json.dumps(complete))
            stage_benchmark(patch(benchmark, operations), instance)
            report = Report()
            promotion_policy.check(instance, "candidate", root, upload, report)
            if not any(
                row["item"] == item and message in row["message"]
                for row in report.findings
            ):
                failures += 1
                print(f"FAIL  {name}: attested benchmark contradiction was accepted")

    checked = (
        len(cases)
        + len(promotion_policy.REQUIRED_CANDIDATE_ARTIFACT_KINDS)
        + len(promotion_policy.REQUIRED_CANDIDATE_SUITES)
        + len(benchmark_cases)
        + 1
        + 1
    )
    print()
    print(f"promotion policy: {checked} case(s), {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
