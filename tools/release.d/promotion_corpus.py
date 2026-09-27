#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Resolve the benchmark corpus identity committed at a candidate revision."""

from __future__ import annotations

import hashlib
import json
import os


TASK_CORPUS_MANIFEST = os.path.join("test-fixtures", "tasks", "manifest.json")
PAGE_CORPUS_MANIFEST = os.path.join("test-fixtures", "web", "manifest.json")


def _get(node, *path, default=None):
    for key in path:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node


def load(root: str, report):
    documents = []
    for relative in (TASK_CORPUS_MANIFEST, PAGE_CORPUS_MANIFEST):
        path = os.path.join(root, relative)
        try:
            with open(path, encoding="utf-8") as handle:
                document = json.load(handle)
        except (OSError, json.JSONDecodeError) as error:
            report.fail(
                "tests.benchmark_evidence.scenario_corpus",
                f"cannot read candidate corpus authority {relative}: {error}",
                "Restore and validate both committed fixture manifests before "
                "verifying benchmark evidence.",
            )
            return None
        if not isinstance(document, dict):
            report.fail(
                "tests.benchmark_evidence.scenario_corpus",
                f"candidate corpus authority {relative} is not a JSON object",
                "Restore and validate both committed fixture manifests before "
                "verifying benchmark evidence.",
            )
            return None
        documents.append(document)

    tasks, pages = documents
    rows = tasks.get("scenarios")
    selected = (
        [row.get("id") for row in rows if isinstance(row, dict)]
        if isinstance(rows, list)
        else []
    )
    if (
        not selected
        or len(selected) != len(rows or [])
        or any(not isinstance(identity, str) or not identity for identity in selected)
        or len(selected) != len(set(selected))
        or tasks.get("scenario_count") != len(selected)
    ):
        report.fail(
            "tests.benchmark_evidence.scenario_corpus",
            "the candidate task-corpus authority has no exact unique production selection",
            "Run python3 test-fixtures/tasks/check.py and repair its manifest before "
            "verifying benchmark evidence.",
        )
        return None
    identity = (
        tasks.get("corpus"),
        tasks.get("version"),
        _get(tasks, "scripted_model_outputs", "state"),
        pages.get("corpus"),
        pages.get("version"),
    )
    if (
        any(not isinstance(value, str) or not value for value in identity)
        or _get(tasks, "page_corpus", "required_version") != pages.get("version")
    ):
        report.fail(
            "tests.benchmark_evidence.scenario_corpus",
            "the candidate task/page corpus identity is incomplete or not mutually pinned",
            "Run both fixture-corpus checks and repair their manifests before "
            "verifying benchmark evidence.",
        )
        return None
    encoded = json.dumps(
        {"scenarios": tasks, "pages": pages},
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return {
        "scenario_corpus": identity[0],
        "scenario_version": identity[1],
        "scenario_digest": hashlib.sha256(encoded).hexdigest(),
        "scripted_model_outputs": identity[2],
        "selected_scenarios": selected,
        "page_corpus": identity[3],
        "page_version": identity[4],
    }
