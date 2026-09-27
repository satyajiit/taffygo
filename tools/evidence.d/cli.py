#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Assemble and verify the evidence index for one TaffyGo source revision."""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MODULES = ROOT / "tools" / "evidence.d"
sys.path.insert(0, str(MODULES))

from evidence_index import (  # noqa: E402
    MILESTONES,
    capability_identity,
    derive_candidate_state,
    expected_projection,
    load_json,
    validate_index,
)
from fragment import write_fragment  # noqa: E402

DEFAULT_PROJECTION = Path(
    "taffy-core/build/capabilities/generated/product-capabilities.json"
)


def timestamp() -> str:
    epoch = os.environ.get("SOURCE_DATE_EPOCH", "")
    moment = (
        datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc)
        if epoch.isdigit()
        else datetime.datetime.now(datetime.timezone.utc)
    )
    return moment.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def git(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *args],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout.strip()


def assemble(args: argparse.Namespace) -> int:
    root = Path(args.root).resolve()
    output = Path(args.out).resolve()
    artifact_root = Path(args.artifacts_dir).resolve() if args.artifacts_dir else output.parent
    projection_path = Path(args.projection)
    if not projection_path.is_absolute():
        projection_path = root / projection_path

    try:
        revision = git(root, "rev-parse", "HEAD")
        dirty = bool(git(root, "status", "--porcelain", "--untracked-files=all"))
        projection = load_json(projection_path)
        source_capability = load_json(
            root / "taffy-core" / "build" / "product-capabilities.json"
        )
        if projection != expected_projection(source_capability):
            raise ValueError(
                "the capability projection is stale or does not represent its source authority"
            )
        capability = capability_identity(projection_path, args.profile)
    except (
        OSError,
        subprocess.CalledProcessError,
        KeyError,
        TypeError,
        ValueError,
        json.JSONDecodeError,
    ) as error:
        print(f"evidence: cannot resolve repository identity: {error}", file=sys.stderr)
        return 2

    records: list[dict] = []
    for fragment_name in args.fragment:
        fragment_path = Path(fragment_name)
        try:
            fragment = load_json(fragment_path)
        except (OSError, json.JSONDecodeError) as error:
            print(f"evidence: cannot read {fragment_path}: {error}", file=sys.stderr)
            return 2
        if not isinstance(fragment, dict) or set(fragment) != {"source_revision", "records"}:
            print(
                f"evidence: {fragment_path} must contain only source_revision and records",
                file=sys.stderr,
            )
            return 1
        if fragment["source_revision"] != revision:
            print(
                f"evidence: {fragment_path} belongs to {fragment['source_revision']!r}, "
                f"not current revision {revision!r}",
                file=sys.stderr,
            )
            return 1
        if not isinstance(fragment["records"], list):
            print(f"evidence: {fragment_path} records must be an array", file=sys.stderr)
            return 1
        records.extend(fragment["records"])

    records.sort(key=lambda row: str(row.get("id", "")))
    source = {"revision": revision, "dirty": dirty}
    milestones, ready, blockers = derive_candidate_state(source, capability, records)
    document = {
        "schema_version": 1,
        "generated_at": timestamp(),
        "source": source,
        "capability": capability,
        "records": records,
        "milestones": milestones,
        "candidate_ready": ready,
        "candidate_blockers": blockers,
    }
    findings = validate_index(document, output, artifact_root=artifact_root)
    if findings:
        for finding in findings:
            print(finding, file=sys.stderr)
        print(f"evidence: {len(findings)} finding(s); index not written", file=sys.stderr)
        return 1
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, output)
    print(f"evidence index: {output}")
    print(f"source: {revision} ({'dirty' if dirty else 'clean'})")
    print(f"records: {len(records)}; candidate_ready: {str(ready).lower()}")
    for blocker in blockers:
        print(f"blocked: {blocker}")
    return 0


def verify(args: argparse.Namespace) -> int:
    path = Path(args.index).resolve()
    try:
        document = load_json(path)
    except (OSError, json.JSONDecodeError) as error:
        print(f"evidence: cannot read {path}: {error}", file=sys.stderr)
        return 2
    artifact_root = Path(args.artifacts_dir).resolve() if args.artifacts_dir else path.parent
    root = Path(args.root).resolve() if args.root else None
    findings = validate_index(
        document,
        path,
        artifact_root=artifact_root,
        repository_root=root,
        require_candidate=args.candidate,
    )
    for finding in findings:
        print(finding)
    if findings:
        print(f"{path}: {len(findings)} finding(s) — evidence is not promotable")
        return 1
    print(f"{path}: evidence index verified")
    return 0


def fixture_record(milestone: str, kind: str, path: Path, digest: str) -> dict:
    suffix = "exit" if kind == "milestone-exit" else "test"
    record = {
        "id": f"{milestone.lower()}-{suffix}",
        "class": "EV-04",
        "milestone": milestone,
        "kind": kind,
        "result": "pass",
        "required_for_candidate": True,
        "owner": "test owner",
        "command": "fixture command",
        "started_at": "2026-09-03T00:00:00Z",
        "completed_at": "2026-09-03T00:00:01Z",
        "environment": {"kind": "host", "identity": "fixture"},
        "requirements": [f"{milestone}-fixture"],
        "evidence": [{"path": path.name, "sha256": digest, "size_bytes": 3}],
        "supports": [],
        "note": "synthetic self-test record",
    }
    if kind == "milestone-exit":
        record["supports"] = [f"{milestone.lower()}-test"]
    return record


def self_test(_: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory(prefix="taffy-evidence-") as directory:
        base = Path(directory)
        artifacts = base / "artifacts"
        artifacts.mkdir()
        artifact = artifacts / "proof.txt"
        artifact.write_bytes(b"ok\n")
        digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
        records = []
        for milestone in MILESTONES:
            records.append(fixture_record(milestone, "test", artifact, digest))
            records.append(fixture_record(milestone, "milestone-exit", artifact, digest))
        source = {"revision": "a" * 40, "dirty": False}
        capability = {
            "profile": "candidate",
            "projection_sha256": "b" * 64,
            "configuration_fingerprint": "sha256:" + "c" * 64,
            "accepted_milestone": "M8",
            "delegated_task_start": True,
            "task_milestone": "M8",
            "policy_milestone": "M7",
        }

        def make(rows: list[dict]) -> dict:
            milestones, ready, blockers = derive_candidate_state(source, capability, rows)
            return {
                "schema_version": 1,
                "generated_at": "2026-09-03T00:00:02Z",
                "source": source,
                "capability": capability,
                "records": rows,
                "milestones": milestones,
                "candidate_ready": ready,
                "candidate_blockers": blockers,
            }

        checks = 0
        complete = make(records)
        if validate_index(
            complete, artifacts / "index.json", artifact_root=artifacts, require_candidate=True
        ):
            print("FAIL complete candidate fixture did not verify")
            return 1
        checks += 1
        incomplete_rows = [row for row in records if row["milestone"] != "M8"]
        incomplete = make(incomplete_rows)
        if not validate_index(
            incomplete, artifacts / "index.json", artifact_root=artifacts, require_candidate=True
        ):
            print("FAIL missing M8 exit was accepted")
            return 1
        checks += 1
        crosswired_rows = json.loads(json.dumps(records))
        next(
            row for row in crosswired_rows if row["id"] == "m8-exit"
        )["supports"] = ["m0-test"]
        crosswired = make(crosswired_rows)
        if not validate_index(
            crosswired, artifacts / "index.json", artifact_root=artifacts, require_candidate=True
        ):
            print("FAIL M8 exit backed only by M0 evidence was accepted")
            return 1
        checks += 1
        disabled = json.loads(json.dumps(complete))
        disabled["capability"].update(
            {
                "delegated_task_start": False,
                "task_milestone": None,
                "policy_milestone": None,
            }
        )
        disabled_milestones, disabled_ready, disabled_blockers = derive_candidate_state(
            source, disabled["capability"], disabled["records"]
        )
        disabled["milestones"] = disabled_milestones
        disabled["candidate_ready"] = disabled_ready
        disabled["candidate_blockers"] = disabled_blockers
        findings = validate_index(
            disabled, artifacts / "index.json", artifact_root=artifacts, require_candidate=True
        )
        if not findings or disabled_ready:
            print("FAIL task-disabled M8 candidate was accepted")
            return 1
        checks += 1
        missing_capability = json.loads(json.dumps(complete))
        del missing_capability["capability"]["policy_milestone"]
        findings = validate_index(
            missing_capability, artifacts / "index.json", artifact_root=artifacts
        )
        if not any(
            finding.pointer == "/capability/policy_milestone" and finding.remediation
            for finding in findings
        ):
            print("FAIL incomplete capability identity was accepted or had no remediation")
            return 1
        checks += 1
        tampered = json.loads(json.dumps(complete))
        tampered["records"][0]["evidence"][0]["sha256"] = "d" * 64
        if not validate_index(tampered, artifacts / "index.json", artifact_root=artifacts):
            print("FAIL tampered evidence digest was accepted")
            return 1
        checks += 1
        outside = base / "outside.txt"
        outside.write_bytes(b"ok\n")
        (artifacts / "linked.txt").symlink_to(outside)
        escaped = json.loads(json.dumps(complete))
        escaped["records"][0]["evidence"][0]["path"] = "linked.txt"
        if not validate_index(
            escaped, artifacts / "index.json", artifact_root=artifacts
        ):
            print("FAIL evidence symlink escape was accepted")
            return 1
        checks += 1
        duplicated = make(records + [records[0]])
        if not validate_index(duplicated, artifacts / "index.json", artifact_root=artifacts):
            print("FAIL duplicate record id was accepted")
            return 1
        checks += 1
        forged = json.loads(json.dumps(incomplete))
        forged["candidate_ready"] = True
        forged["candidate_blockers"] = []
        if not validate_index(forged, artifacts / "index.json", artifact_root=artifacts):
            print("FAIL forged candidate summary was accepted")
            return 1
        checks += 1
        unknown = json.loads(json.dumps(complete))
        unknown["records"][0]["typo"] = True
        if not validate_index(unknown, artifacts / "index.json", artifact_root=artifacts):
            print("FAIL unknown schema field was accepted")
            return 1
        checks += 1
    print(f"ok {checks} evidence-index self-tests")
    return 0


def schema(_: argparse.Namespace) -> int:
    sys.stdout.write((MODULES / "schema.json").read_text(encoding="utf-8"))
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description="Build and verify one revision-bound TaffyGo evidence index."
    )
    commands = result.add_subparsers(dest="command", required=True)
    build = commands.add_parser("assemble", help="assemble job fragments")
    build.add_argument("--out", required=True)
    build.add_argument("--profile", choices=("development", "candidate"), required=True)
    build.add_argument("--fragment", action="append", default=[])
    build.add_argument("--artifacts-dir")
    build.add_argument("--projection", default=str(DEFAULT_PROJECTION))
    build.add_argument("--root", default=str(ROOT))
    build.set_defaults(run=assemble)

    fragment = commands.add_parser("fragment", help="normalize one observed run or review")
    fragment.add_argument("--out", required=True)
    fragment.add_argument("--artifacts-dir", required=True)
    fragment.add_argument("--id", required=True)
    fragment.add_argument(
        "--class",
        dest="evidence_class",
        choices=tuple(f"EV-{number:02d}" for number in range(1, 9)),
        required=True,
    )
    fragment.add_argument("--milestone", choices=MILESTONES, required=True)
    fragment.add_argument(
        "--kind",
        choices=(
            "build", "test", "benchmark", "measurement", "backend",
            "security-review", "privacy-review", "rollback", "milestone-exit", "other",
        ),
        required=True,
    )
    fragment.add_argument("--result", choices=("pass", "fail", "blocked", "not-run"), required=True)
    fragment.add_argument("--required-for-candidate", action="store_true")
    fragment.add_argument("--owner", required=True)
    fragment.add_argument("--observed-command", required=True)
    fragment.add_argument("--started-at", required=True)
    fragment.add_argument("--completed-at", required=True)
    fragment.add_argument("--environment-kind", choices=("host", "device", "backend", "review"), required=True)
    fragment.add_argument("--environment-id", required=True)
    fragment.add_argument("--requirement", action="append", required=True)
    fragment.add_argument("--evidence", action="append", required=True)
    fragment.add_argument("--supports", action="append", default=[])
    fragment.add_argument("--note", default="")
    fragment.add_argument("--root", default=str(ROOT))
    fragment.set_defaults(run=write_fragment)

    check = commands.add_parser("verify", help="verify schema, identity and digests")
    check.add_argument("index")
    check.add_argument("--artifacts-dir")
    check.add_argument("--root", help="also compare with this checkout")
    check.add_argument("--candidate", action="store_true")
    check.set_defaults(run=verify)

    commands.add_parser("schema", help="print the committed schema").set_defaults(run=schema)
    commands.add_parser("self-test", help="run refusal-path tests").set_defaults(run=self_test)
    return result


if __name__ == "__main__":
    arguments = parser().parse_args()
    sys.exit(arguments.run(arguments))
