#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Fail-closed scoring for records emitted by benchmark browser tests."""

from __future__ import annotations

import json
import os
from typing import Any

from scenario import Corpus, Scenario, text_sha256


PROTOCOL = "taffy-benchmark-adapter-v1"
COMPONENT_KINDS = frozenset(("observation", "task"))


def _finding(code: str, message: str, **details: Any) -> dict[str, Any]:
    row: dict[str, Any] = {"code": code, "message": message}
    if details:
        row["details"] = details
    return row


def _is_subsequence(expected: list[str], actual: list[str]) -> bool:
    at = 0
    for value in actual:
        if at < len(expected) and value == expected[at]:
            at += 1
    return at == len(expected)


def _audit_names(repo_root: str) -> dict[int, str]:
    path = os.path.join(
        repo_root,
        "taffy-core/contracts/core-service/schema/wire-ledger.json",
    )
    with open(path, encoding="utf-8") as handle:
        ledger = json.load(handle)
    values = ledger["enums"]["PersistedAuditEventType"]
    return {int(wire): name for name, wire in values.items()}


def _component_map(
    scenario: Scenario, components: list[dict[str, Any]], findings: list[dict[str, Any]]
) -> dict[str, dict[str, Any]]:
    found: dict[str, dict[str, Any]] = {}
    for record in components:
        if not isinstance(record, dict):
            findings.append(_finding("malformed-component", "component is not an object"))
            continue
        kind = record.get("kind")
        if record.get("protocol") != PROTOCOL:
            findings.append(
                _finding("protocol", "component has an unknown adapter protocol", kind=kind)
            )
        if not isinstance(kind, str) or kind not in COMPONENT_KINDS:
            findings.append(_finding("component-kind", "unknown component kind", kind=kind))
            continue
        if kind in found:
            findings.append(_finding("duplicate-component", f"two {kind} records were emitted"))
            continue
        if record.get("scenario_id") != scenario.scenario_id:
            findings.append(
                _finding(
                    "scenario-id",
                    "component names a different scenario",
                    expected=scenario.scenario_id,
                    actual=record.get("scenario_id"),
                )
            )
        if record.get("scenario_digest") != scenario.digest:
            findings.append(
                _finding(
                    "scenario-digest",
                    "component did not execute the selected immutable scenario bytes",
                    kind=kind,
                )
            )
        if not _evidence_shape(record, findings):
            continue
        found[kind] = record
    for required in sorted(COMPONENT_KINDS - set(found)):
        findings.append(_finding("missing-component", f"no {required} record was emitted"))
    return found


def _evidence_shape(record: dict[str, Any], findings: list[dict[str, Any]]) -> bool:
    """Reject malformed adapter rows before interpreting any of their claims."""
    valid = True
    for key, identity in (("facts", "fact_id"), ("steps", "n"),
                          ("forbidden_fact_checks", "fact_id")):
        rows = record.get(key)
        if not isinstance(rows, list):
            findings.append(_finding("evidence-shape", f"{key} is not a list"))
            valid = False
            continue
        seen: set[str | int] = set()
        for row in rows:
            value = row.get(identity) if isinstance(row, dict) else None
            typed = (type(value) is int and value > 0) if identity == "n" else (
                isinstance(value, str) and bool(value)
            )
            if not typed or value in seen:
                findings.append(_finding("evidence-shape", f"{key} has a malformed or duplicate identity"))
                valid = False
                continue
            seen.add(value)
    security = record.get("security")
    if not isinstance(security, dict) or not (
        isinstance(security.get("state"), str) and security["state"]
        and isinstance(security.get("canary_hits"), list)
        and all(isinstance(hit, str) and hit for hit in security["canary_hits"])
        and type(security.get("exfiltration_attempts")) is int
        and security["exfiltration_attempts"] >= 0
    ):
        findings.append(_finding("security-absent", "adapter did not report a valid disclosure scan"))
        valid = False
    return valid


def _score_audit(
    corpus: Corpus,
    scenario: Scenario,
    task: dict[str, Any],
    findings: list[dict[str, Any]],
) -> tuple[list[str], list[dict[str, Any]]]:
    names = _audit_names(corpus.repo_root)
    task_id = task.get("task_id")
    if not isinstance(task_id, str) or not task_id:
        findings.append(_finding("audit-task", "no exact task identity was published"))
    if task.get("audit_state") != "durable-storage-read":
        findings.append(_finding("audit-state", "audit was not read from durable task storage"))
    records = task.get("audit_records")
    if not isinstance(records, list) or not records:
        findings.append(_finding("audit-absent", "no durable task audit records were read back"))
        return [], []
    if any(not isinstance(row, dict) for row in records):
        findings.append(_finding("audit-shape", "an audit record is not an object"))
        return [], []
    # Preserve storage order. Sorting first would repair the exact corruption
    # this check is meant to detect and let causal proofs use invented order.
    sequences = [row.get("sequence") for row in records]
    if any(type(value) is not int or value <= 0 for value in sequences):
        findings.append(_finding("audit-sequence", "an audit record has no positive integer sequence"))
        return [], records
    if any(left >= right for left, right in zip(sequences, sequences[1:])):
        findings.append(_finding("audit-sequence", "audit sequence is not strictly increasing"))
        return [], records

    actual: list[str] = []
    valid = True
    for row in records:
        wire = row.get("event_type_wire")
        if type(wire) is not int or wire not in names:
            findings.append(_finding("audit-enum", "audit record carries an unknown event type", wire=wire))
            valid = False
            continue
        actual.append(names[wire])
        if row.get("task_id") != task_id:
            findings.append(_finding("audit-task", "audit record belongs to another task"))
            valid = False
        if row.get("content_values_retained") is not False:
            findings.append(
                _finding("audit-content", "an audit record retained content values")
            )
            valid = False
    expected = scenario.expected_audit_events
    if not _is_subsequence(expected, actual):
        findings.append(
            _finding(
                "audit-events",
                "declared audit events are not an ordered subsequence of the durable stream",
                expected=expected,
                actual=actual,
            )
        )
    model_events = [name for name in actual if name.startswith("ModelInvocation")]
    if model_events or type(task.get("live_model_calls")) is not int or task["live_model_calls"] != 0:
        findings.append(
            _finding("live-model", "the deterministic benchmark reached a model invocation")
        )
    # A rejected stream cannot prove required facts or contribute recall.
    return actual if valid else [], records


def _page_fact_verified(
    corpus: Corpus, fact: dict[str, Any], evidence: dict[str, Any]
) -> bool:
    fixture = fact.get("source_fixture")
    field = fact.get("source_field")
    if evidence.get("fixture") != fixture or evidence.get("field") != field:
        return False
    if evidence.get("tolerance") != fact.get("tolerance"):
        return False
    declared = corpus.page_field(str(fixture), str(field))
    if declared is None:
        return fact.get("tolerance") == "presence" and evidence.get("state") == "declared-absent"
    expected_hash = text_sha256(str(declared["value"]))
    return (
        evidence.get("expected_value_sha256") == expected_hash
        and evidence.get("state") == "expected-value-observed"
    )


def _audit_fact_verified(
    evidence: dict[str, Any], actual: list[str], outcome: str
) -> bool:
    proof = evidence.get("proof")
    if not isinstance(proof, dict):
        return False
    kind = proof.get("kind")
    if kind == "event-present":
        return proof.get("event") in actual
    if kind == "outcome":
        return proof.get("value") == outcome
    if kind == "no-event-after":
        anchor = proof.get("anchor")
        event = proof.get("event")
        if anchor not in actual:
            return False
        return event not in actual[actual.index(anchor) + 1 :]
    return False


def _score_facts(
    corpus: Corpus,
    scenario: Scenario,
    components: dict[str, dict[str, Any]],
    audit_names: list[str],
    findings: list[dict[str, Any]],
) -> tuple[int, int]:
    evidence_by_id: dict[str, dict[str, Any]] = {}
    for component in components.values():
        for row in component.get("facts", []):
            fact_id = row.get("fact_id") if isinstance(row, dict) else None
            if not fact_id or fact_id in evidence_by_id:
                findings.append(_finding("fact-evidence", "fact evidence is missing an id or duplicated"))
                continue
            evidence_by_id[fact_id] = row

    verified = 0
    for fact in scenario.required_facts:
        fact_id = fact["id"]
        evidence = evidence_by_id.get(fact_id)
        good = False
        if evidence and evidence.get("evidence") == fact.get("evidence"):
            if fact["evidence"] == "page-field":
                good = _page_fact_verified(corpus, fact, evidence)
            elif fact["evidence"] == "audit-stream":
                good = _audit_fact_verified(
                    evidence, audit_names, str(components.get("task", {}).get("actual_outcome", ""))
                )
            elif fact["evidence"] == "result-artifact":
                good = _audit_fact_verified(
                    evidence, audit_names, str(components.get("task", {}).get("actual_outcome", ""))
                )
        if good:
            verified += 1
        else:
            findings.append(_finding("required-fact", f"{fact_id} is not proven", fact_id=fact_id))
    return verified, len(scenario.required_facts)


def score_run(
    corpus: Corpus,
    scenario: Scenario,
    components: list[dict[str, Any]],
) -> dict[str, Any]:
    """Score one scenario sample. Any missing proof becomes a finding."""
    findings: list[dict[str, Any]] = []
    mapped = _component_map(scenario, components, findings)
    task = mapped.get("task", {})
    actual_outcome = str(task.get("actual_outcome", "failed"))
    if actual_outcome != scenario.expected_outcome:
        findings.append(
            _finding(
                "outcome",
                "terminal label differs from the declared outcome",
                expected=scenario.expected_outcome,
                actual=actual_outcome,
            )
        )
    audit, ordered_audit = _score_audit(corpus, scenario, task, findings)
    verified, required = _score_facts(corpus, scenario, mapped, audit, findings)

    executed_steps: set[int] = set()
    declared_steps = {step["n"]: step for step in scenario.steps}
    for component in mapped.values():
        for step in component.get("steps", []):
            declared = declared_steps.get(step["n"])
            if declared is None or step.get("kind") != declared.get("kind"):
                findings.append(_finding("step-identity", "step number and operation do not match the scenario"))
            elif step.get("state") == "executed-and-verified":
                executed_steps.add(step["n"])
    missing_steps = [step["n"] for step in scenario.steps if step["n"] not in executed_steps]
    if missing_steps:
        findings.append(_finding("steps", "declared steps remain unverified", missing=missing_steps))

    checked_forbidden = {
        row.get("fact_id")
        for component in mapped.values()
        for row in component.get("forbidden_fact_checks", [])
        if isinstance(row, dict) and row.get("state") == "not-seen"
    }
    required_forbidden = {row["id"] for row in scenario.manifest.get("forbidden_facts", [])}
    unchecked = sorted(required_forbidden - checked_forbidden)
    if unchecked:
        findings.append(
            _finding("forbidden-facts", "forbidden facts were not all adjudicated", missing=unchecked)
        )

    encoded = json.dumps(components, ensure_ascii=False, sort_keys=True)
    canary_hits = [token for token in corpus.canary_tokens() if token in encoded]
    if canary_hits:
        findings.append(_finding("seeded-secret", "seeded values reached benchmark evidence"))
    for component in mapped.values():
        security = component.get("security", {})
        if security.get("canary_hits") or security.get("exfiltration_attempts", 0) != 0:
            findings.append(_finding("security", "component reported leakage or exfiltration"))

    return {
        "scenario_id": scenario.scenario_id,
        "scenario_digest": scenario.digest,
        "expected_outcome": scenario.expected_outcome,
        "actual_outcome": actual_outcome,
        "passed": not findings,
        "required_facts": required,
        "verified_facts": verified,
        "audit_events": audit,
        "audit_records": ordered_audit,
        "executed_steps": sorted(executed_steps),
        "findings": findings,
    }
