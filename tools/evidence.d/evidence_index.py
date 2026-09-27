# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Evidence-index identity, derived state, and integrity validation."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "release.d"))

from schema_validate import Finding, Validator  # noqa: E402

MILESTONES = tuple(f"M{number}" for number in range(9))
SCHEMA_PATH = HERE / "schema.json"
PROJECTION_PATH = Path("taffy-core/build/capabilities/generated/product-capabilities.json")
CAPABILITY_SOURCE_PATH = Path("taffy-core/build/product-capabilities.json")
CANDIDATE_RELEASE_CAPABILITY = {
    "delegated_task_start": True,
    "task_milestone": "M8",
    "policy_milestone": "M7",
}


def load_json(path: Path):
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def capability_identity(path: Path, profile: str) -> dict:
    projection = load_json(path)
    if not isinstance(projection, dict):
        raise ValueError("capability projection root is not an object")
    profiles = projection.get("profiles", {})
    if not isinstance(profiles, dict) or profile not in profiles:
        raise ValueError(f"capability projection has no {profile!r} profile")
    selected = profiles[profile]
    if not isinstance(selected, dict):
        raise ValueError(f"capability projection profile {profile!r} is not an object")
    return {
        "profile": profile,
        "projection_sha256": sha256(path),
        "configuration_fingerprint": projection["configuration_fingerprint"],
        "accepted_milestone": projection["accepted_milestone"],
        "delegated_task_start": selected.get("delegated_task_start"),
        "task_milestone": selected.get("task_milestone"),
        "policy_milestone": selected.get("policy_milestone"),
    }


def expected_projection(source: dict) -> dict:
    if not isinstance(source, dict):
        raise TypeError("capability source root is not an object")
    canonical = json.dumps(
        source, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("utf-8")
    return {
        "schema_version": source["schema_version"],
        "configuration_fingerprint": "sha256:" + hashlib.sha256(canonical).hexdigest(),
        "accepted_milestone": source["accepted_milestone"],
        "profiles": source["profiles"],
        "chromium_profiles": source["chromium_profiles"],
    }


def derive_candidate_state(source: dict, capability: dict, records: list[dict]):
    blockers: list[str] = []
    if source.get("dirty") is not False:
        blockers.append("the source tree was dirty when evidence was assembled")
    if capability.get("profile") != "candidate":
        blockers.append("the evidence was assembled for a development capability profile")
    if capability.get("accepted_milestone") != "M8":
        blockers.append(
            f"the capability authority has accepted {capability.get('accepted_milestone', 'no milestone')}, not M8"
        )
    for field, expected in CANDIDATE_RELEASE_CAPABILITY.items():
        if capability.get(field) != expected:
            blockers.append(
                f"the candidate capability {field} is {capability.get(field)!r}, not {expected!r}"
            )

    by_id: dict[str, dict] = {}
    duplicate_ids: set[str] = set()
    for record in records:
        identity = record.get("id")
        if not isinstance(identity, str):
            continue
        if identity in by_id:
            duplicate_ids.add(identity)
        by_id[identity] = record
        if record.get("required_for_candidate") is True and record.get("result") != "pass":
            blockers.append(f"required record {identity} is {record.get('result', 'invalid')}")
    for identity in sorted(duplicate_ids):
        blockers.append(f"record id {identity} occurs more than once")

    summaries = []
    for milestone in MILESTONES:
        exits = [
            row
            for row in records
            if row.get("milestone") == milestone and row.get("kind") == "milestone-exit"
        ]
        verdict = "not-run"
        exit_id = ""
        support_count = 0
        if len(exits) != 1:
            verdict = "blocked" if not exits else "fail"
            blockers.append(
                f"{milestone} has {'no' if not exits else 'more than one'} milestone-exit record"
            )
        else:
            exit_record = exits[0]
            exit_id = exit_record.get("id", "")
            supports = exit_record.get("supports", [])
            support_count = len(supports) if isinstance(supports, list) else 0
            verdict = exit_record.get("result", "fail")
            if exit_record.get("required_for_candidate") is not True:
                verdict = "fail"
                blockers.append(f"{milestone} exit is not marked required for the candidate")
            if not supports:
                verdict = "fail"
                blockers.append(f"{milestone} exit points to no supporting evidence")
            direct_support = False
            for reference in supports if isinstance(supports, list) else []:
                target = by_id.get(reference)
                if reference == exit_id:
                    verdict = "fail"
                    blockers.append(f"{milestone} exit cites itself")
                elif target is None:
                    verdict = "fail"
                    blockers.append(f"{milestone} exit cites missing record {reference}")
                elif target.get("result") != "pass":
                    verdict = "fail"
                    blockers.append(
                        f"{milestone} exit cites {reference}, whose result is {target.get('result')}"
                    )
                elif (
                    target.get("milestone") == milestone
                    and target.get("kind") != "milestone-exit"
                ):
                    direct_support = True
            if supports and not direct_support:
                verdict = "fail"
                blockers.append(
                    f"{milestone} exit cites no passing non-exit evidence from {milestone}"
                )
            if verdict != "pass":
                blockers.append(f"{milestone} milestone-exit result is not pass")
        summaries.append(
            {
                "milestone": milestone,
                "verdict": verdict,
                "exit_record_id": exit_id,
                "supporting_records": support_count,
            }
        )
    blockers = list(dict.fromkeys(blockers))
    return summaries, not blockers, blockers


def _safe_artifact(root: Path, raw_path: str) -> tuple[Path | None, str]:
    relative = Path(raw_path)
    if relative.is_absolute() or ".." in relative.parts:
        return None, "must be a relative path beneath the evidence artifact directory"
    resolved_root = root.resolve()
    resolved = (resolved_root / relative).resolve()
    try:
        resolved.relative_to(resolved_root)
    except ValueError:
        return None, "resolves outside the evidence artifact directory"
    return resolved, ""


def _semantic_findings(document: dict, artifact_root: Path) -> list[Finding]:
    findings: list[Finding] = []
    records = document.get("records", [])
    if not isinstance(records, list):
        return findings
    seen: set[str] = set()
    identifiers = {row.get("id") for row in records if isinstance(row, dict)}
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            continue
        identity = record.get("id")
        if identity in seen:
            findings.append(Finding(f"/records/{index}/id", "duplicates an earlier record id", "Give every observed run and review one stable id."))
        if isinstance(identity, str):
            seen.add(identity)
        started = record.get("started_at")
        completed = record.get("completed_at")
        if isinstance(started, str) and isinstance(completed, str) and completed < started:
            findings.append(Finding(f"/records/{index}/completed_at", "precedes started_at", "Record the observed UTC interval without rewriting its order."))
        for reference in record.get("supports", []) if isinstance(record.get("supports"), list) else []:
            if reference not in identifiers:
                findings.append(Finding(f"/records/{index}/supports", f"cites missing record {reference!r}", "Stage the supporting record in the same revision-bound index."))
        for item_index, item in enumerate(record.get("evidence", []) if isinstance(record.get("evidence"), list) else []):
            if not isinstance(item, dict) or not isinstance(item.get("path"), str):
                continue
            pointer = f"/records/{index}/evidence/{item_index}"
            path, error = _safe_artifact(artifact_root, item["path"])
            if error:
                findings.append(Finding(pointer + "/path", error, "Stage evidence beneath the artifact directory and record a relative path."))
                continue
            if path is None or not path.is_file():
                findings.append(Finding(pointer + "/path", f"{item['path']!r} is not a readable file", "Produce and stage the evidence before assembling its index."))
                continue
            actual_size = path.stat().st_size
            if item.get("size_bytes") != actual_size:
                findings.append(Finding(pointer + "/size_bytes", f"records {item.get('size_bytes')}, file has {actual_size}", "Reassemble from the unchanged evidence artifact."))
            actual_digest = sha256(path)
            if item.get("sha256") != actual_digest:
                findings.append(Finding(pointer + "/sha256", "does not match the evidence file", "Do not repair the digest by hand; restore or reproduce the observed artifact."))
    expected_milestones, expected_ready, expected_blockers = derive_candidate_state(
        document.get("source", {}), document.get("capability", {}), records
    )
    if document.get("milestones") != expected_milestones:
        findings.append(Finding("/milestones", "does not match the records from which it is derived", "Reassemble the index; milestone summaries are never hand-authored."))
    if document.get("candidate_ready") != expected_ready:
        findings.append(Finding("/candidate_ready", "does not match the derived candidate verdict", "Reassemble the index; readiness is derived from evidence."))
    if document.get("candidate_blockers") != expected_blockers:
        findings.append(Finding("/candidate_blockers", "does not match the derived blocker list", "Reassemble the index; blockers are derived from evidence."))
    return findings


def _repository_findings(document: dict, root: Path) -> list[Finding]:
    findings: list[Finding] = []
    try:
        revision = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout.strip()
        dirty = bool(
            subprocess.run(
                ["git", "-C", str(root), "status", "--porcelain", "--untracked-files=all"],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            ).stdout.strip()
        )
    except (OSError, subprocess.CalledProcessError) as error:
        return [Finding("/source", f"cannot inspect repository: {error}", "Verify from the source checkout that produced the candidate.")]
    source = document.get("source", {})
    if source.get("revision") != revision:
        findings.append(Finding("/source/revision", f"does not match checkout revision {revision}", "Use the exact clean source revision whose evidence was assembled."))
    if source.get("dirty") != dirty:
        findings.append(Finding("/source/dirty", f"does not match current checkout state {dirty}", "Verify from an unchanged checkout; candidate evidence requires a clean tree."))
    projection = root / PROJECTION_PATH
    try:
        actual = capability_identity(projection, document.get("capability", {}).get("profile", ""))
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        findings.append(Finding("/capability", f"cannot resolve projection: {error}", "Regenerate the product capability projection before collecting evidence."))
    else:
        if document.get("capability") != actual:
            findings.append(Finding("/capability", "does not match the generated product capability projection", "Reassemble from the same clean source revision; never edit identity fields."))
    source_path = root / CAPABILITY_SOURCE_PATH
    try:
        source_document = load_json(source_path)
        projection_document = load_json(projection)
        expected = expected_projection(source_document)
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        findings.append(Finding(
            "/capability/projection_sha256",
            f"cannot compare capability source and projection: {error}",
            "Restore the capability authority and regenerate every committed projection.",
        ))
    else:
        if projection_document != expected:
            findings.append(Finding(
                "/capability/projection_sha256",
                "the generated capability projection is stale or does not represent its source authority",
                "Run python3 taffy-core/build/product_capabilities.py --write and rebuild before collecting evidence.",
            ))
    return findings


def validate_index(
    document,
    index_path: Path,
    *,
    artifact_root: Path,
    repository_root: Path | None = None,
    require_candidate: bool = False,
) -> list[Finding]:
    try:
        validator = Validator(load_json(SCHEMA_PATH))
        findings = validator.validate(document)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        return [Finding("/", f"evidence schema is unusable: {error}", "Fix tools/evidence.d/schema.json and its self-test first.")]
    if findings or not isinstance(document, dict):
        return findings
    findings.extend(_semantic_findings(document, artifact_root))
    if repository_root is not None:
        findings.extend(_repository_findings(document, repository_root))
    if require_candidate and document.get("candidate_ready") is not True:
        detail = "; ".join(document.get("candidate_blockers", [])) or "derived readiness is false"
        findings.append(Finding("/candidate_ready", f"candidate evidence is incomplete: {detail}", "Complete and sign every M0–M8 exit against passing evidence from this revision."))
    return findings
