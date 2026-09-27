# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Create one normalized evidence fragment from an observed artifact."""

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

from schema_validate import Validator  # noqa: E402


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def relative_evidence(path: Path, artifact_root: Path) -> dict:
    resolved_root = artifact_root.resolve()
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(resolved_root)
    except ValueError as error:
        raise ValueError(f"{path} is outside artifact directory {artifact_root}") from error
    if not resolved.is_file():
        raise ValueError(f"{path} is not a readable evidence file")
    return {
        "path": relative.as_posix(),
        "sha256": digest(resolved),
        "size_bytes": resolved.stat().st_size,
    }


def record_schema() -> dict:
    with (HERE / "schema.json").open(encoding="utf-8") as handle:
        whole = json.load(handle)
    schema = dict(whole["$defs"]["record"])
    schema["$defs"] = whole["$defs"]
    return schema


def write_fragment(args) -> int:
    root = Path(args.root).resolve()
    artifacts = Path(args.artifacts_dir).resolve()
    output = Path(args.out).resolve()
    try:
        revision = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout.strip()
        files = [relative_evidence(Path(item), artifacts) for item in args.evidence]
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"evidence fragment: {error}", file=sys.stderr)
        return 2
    record = {
        "id": args.id,
        "class": args.evidence_class,
        "milestone": args.milestone,
        "kind": args.kind,
        "result": args.result,
        "required_for_candidate": args.required_for_candidate,
        "owner": args.owner,
        "command": args.observed_command,
        "started_at": args.started_at,
        "completed_at": args.completed_at,
        "environment": {"kind": args.environment_kind, "identity": args.environment_id},
        "requirements": args.requirement,
        "evidence": files,
        "supports": args.supports,
        "note": args.note,
    }
    findings = Validator(record_schema()).validate(record)
    if findings:
        for finding in findings:
            print(finding, file=sys.stderr)
        return 1
    fragment = {"source_revision": revision, "records": [record]}
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(json.dumps(fragment, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, output)
    print(f"evidence fragment: {output}")
    print(f"record: {args.id} ({args.result}) at {revision}")
    return 0
