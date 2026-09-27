#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Build byte-backed raw benchmark fixtures for promotion-policy self-tests."""

from __future__ import annotations

import json
import os

import repo_facts


def corpus(selected: list[str]) -> tuple[dict, dict, str]:
    tasks = {
        "corpus": "fixture-task-scenarios",
        "version": "fixture-1",
        "scenario_count": len(selected),
        "scenarios": [{"id": identity} for identity in selected],
        "page_corpus": {"required_version": "1"},
        "scripted_model_outputs": {"state": "not-recorded"},
    }
    pages = {"corpus": "web-fixtures", "version": "1"}
    digest = repo_facts.sha256_string(json.dumps(
        {"scenarios": tasks, "pages": pages},
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ))
    return tasks, pages, digest


def stage_corpora(root: str, tasks: dict, pages: dict) -> None:
    for relative, document in (
        (("test-fixtures", "tasks", "manifest.json"), tasks),
        (("test-fixtures", "web", "manifest.json"), pages),
    ):
        path = os.path.join(root, *relative)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(document, handle, sort_keys=True)
            handle.write("\n")


def stage_raw_runs(upload: str, benchmark: dict, manifest: dict) -> None:
    for row in benchmark["runs"]:
        raw_path = os.path.join(upload, row["raw"])
        os.makedirs(os.path.dirname(raw_path), exist_ok=True)
        with open(raw_path, "w", encoding="utf-8") as handle:
            json.dump(
                {
                    "scenario_id": row["scenario_id"],
                    "sample": row["sample"],
                    "score": {
                        "passed": True,
                        "actual_outcome": "verified-complete",
                        "findings": [],
                    },
                },
                handle,
                sort_keys=True,
            )
            handle.write("\n")
        manifest["artifacts"].append({
            "path": row["raw"],
            "kind": "benchmark-run",
            "sha256": repo_facts.sha256_file(raw_path),
            "size_bytes": repo_facts.file_size(raw_path),
        })
