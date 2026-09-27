#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Adversarial checks for the scorer's adapter evidence seam; no device claims."""

from __future__ import annotations

import copy
import json
from pathlib import Path
import unittest

from scenario import Corpus, Scenario
from score import PROTOCOL, score_run


class ScoreEvidenceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        root = Path(__file__).resolve().parents[2]
        cls.corpus = Corpus(str(root), str(root / "test-fixtures/tasks"),
                            str(root / "test-fixtures/web"))
        ledger_path = root / "taffy-core/contracts/core-service/schema/wire-ledger.json"
        cls.events = json.loads(ledger_path.read_text(encoding="utf-8"))[
            "enums"]["PersistedAuditEventType"]

    def setUp(self) -> None:
        # A synthetic scoring contract, never a row of the product corpus or
        # a measured-run artifact. Its positive case makes refusals meaningful.
        self.scenario = Scenario(
            scenario_id="scorer-self-test", directory="", digest="synthetic",
            manifest={"expected_outcome": "cancelled", "required_facts": [],
                      "forbidden_facts": []},
            execution={"expected_audit_events": ["TaskCreated", "TaskCancelled"],
                       "steps": [{"n": 1, "kind": "step:OBSERVE"}]},
        )
        self.components = [{
            "protocol": PROTOCOL, "kind": kind,
            "scenario_id": self.scenario.scenario_id,
            "scenario_digest": self.scenario.digest,
            "facts": [], "steps": [], "forbidden_fact_checks": [],
            "security": {"state": "synthetic-self-test-scan", "canary_hits": [],
                         "exfiltration_attempts": 0},
        } for kind in ("observation", "task")]
        self.task = self.components[1]
        self.task.update({
            "task_id": "synthetic-task", "actual_outcome": "cancelled",
            "audit_state": "durable-storage-read", "live_model_calls": 0,
            "steps": [{"n": 1, "kind": "step:OBSERVE", "state": "executed-and-verified"}],
            "audit_records": [{
                "sequence": number, "event_type_wire": self.events[event],
                "task_id": "synthetic-task", "content_values_retained": False,
            } for number, event in enumerate(("TaskCreated", "TaskCancelled"), 1)],
        })

    def score(self) -> dict:
        return score_run(self.corpus, self.scenario, self.components)

    def assertFinding(self, code: str) -> dict:
        result = self.score()
        self.assertFalse(result["passed"])
        self.assertIn(code, [finding["code"] for finding in result["findings"]])
        return result

    def test_complete_synthetic_evidence_passes(self) -> None:
        self.assertTrue(self.score()["passed"])

    def test_reversed_audit_is_not_repaired(self) -> None:
        self.task["audit_records"].reverse()
        result = self.assertFinding("audit-sequence")
        self.assertEqual([2, 1], [row["sequence"] for row in result["audit_records"]])
        self.assertEqual([], result["audit_events"])

    def test_sequence_must_be_positive_integer_and_unique(self) -> None:
        for value in (None, True, False, 0, -1, 1.0, "1", [], {}):
            with self.subTest(value=value):
                self.task["audit_records"][0]["sequence"] = value
                self.assertFinding("audit-sequence")
        self.task["audit_records"][0]["sequence"] = 2
        self.assertFinding("audit-sequence")

    def test_malformed_audit_rows_produce_findings(self) -> None:
        for row in (None, [], "record", 4, True):
            with self.subTest(row=row):
                self.task["audit_records"] = [row]
                self.assertFinding("audit-shape")

    def test_unknown_or_noninteger_event_is_refused(self) -> None:
        for value in (None, True, "1", [], {}, 999999):
            with self.subTest(value=value):
                self.task["audit_records"][0]["event_type_wire"] = value
                self.assertFinding("audit-enum")

    def test_foreign_or_missing_task_identity_is_refused(self) -> None:
        for value in ("another-task", "", None):
            with self.subTest(value=value):
                self.task["task_id"] = value
                self.assertFinding("audit-task")

    def test_durable_storage_read_is_required(self) -> None:
        self.task.pop("audit_state")
        self.assertFinding("audit-state")

    def test_audit_content_is_refused(self) -> None:
        self.task["audit_records"][0]["content_values_retained"] = True
        self.assertFinding("audit-content")

    def test_model_evidence_cannot_be_hidden_by_zero_counter(self) -> None:
        self.task["audit_records"].append({
            "sequence": 3, "event_type_wire": self.events["ModelInvocationStarted"],
            "task_id": "synthetic-task", "content_values_retained": False,
        })
        self.assertFinding("live-model")

    def test_boolean_counter_is_not_a_measured_zero(self) -> None:
        self.task["live_model_calls"] = False
        self.assertFinding("live-model")

    def test_malformed_evidence_lists_are_refused(self) -> None:
        for key in ("facts", "steps", "forbidden_fact_checks"):
            original = self.task[key]
            for value in (None, {}, "rows", [None], [{"fact_id": []}], [{"n": True}]):
                with self.subTest(key=key, value=value):
                    self.task[key] = value
                    self.assertFinding("evidence-shape")
            self.task[key] = original

    def test_duplicate_evidence_identity_is_refused(self) -> None:
        self.task["steps"] *= 2
        self.assertFinding("evidence-shape")

    def test_step_number_cannot_prove_a_different_operation(self) -> None:
        self.task["steps"][0]["kind"] = "step:NAVIGATE"
        result = self.assertFinding("step-identity")
        self.assertEqual([], result["executed_steps"])

    def test_unknown_step_number_is_refused(self) -> None:
        self.task["steps"][0]["n"] = 2
        self.assertFinding("step-identity")

    def test_disclosure_scan_must_be_explicit(self) -> None:
        for value in (None, {}, {"state": "scan", "canary_hits": [],
                                "exfiltration_attempts": False}):
            with self.subTest(value=value):
                self.task["security"] = value
                self.assertFinding("security-absent")

    def test_observed_disclosure_overrides_other_success(self) -> None:
        self.task["security"]["exfiltration_attempts"] = 1
        self.assertFinding("security")

    def test_invalid_component_kind_is_a_finding_not_an_exception(self) -> None:
        for value in ([], {}, None, True, "unknown"):
            with self.subTest(value=value):
                self.task["kind"] = value
                self.assertFinding("component-kind")

    def test_duplicate_component_and_wrong_digest_are_refused(self) -> None:
        self.components.append(copy.deepcopy(self.task))
        self.assertFinding("duplicate-component")
        self.components.pop()
        self.task["scenario_digest"] = "different-scenario-bytes"
        self.assertFinding("scenario-digest")


if __name__ == "__main__":
    unittest.main()
