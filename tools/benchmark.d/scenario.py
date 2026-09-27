#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Load the immutable Task Benchmark inputs for execution and scoring.

Validation remains owned by ``test-fixtures/tasks/check.py``.  This module is
the execution-side reader: after that check passes it joins the manifest row
with the scenario's step file and provides exact lookup helpers.  It never
repairs, normalizes, or defaults a malformed corpus.
"""

from __future__ import annotations

import hashlib
import json
import os
from dataclasses import dataclass
from typing import Any


class CorpusReadError(ValueError):
    """The already-validated corpus cannot be read as execution input."""


def _read_json(path: str) -> dict[str, Any]:
    try:
        with open(path, encoding="utf-8") as handle:
            value = json.load(handle)
    except (OSError, ValueError) as error:
        raise CorpusReadError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise CorpusReadError(f"{path} is not a JSON object")
    return value


def canonical_sha256(value: Any) -> str:
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def text_sha256(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


@dataclass(frozen=True)
class Scenario:
    """One manifest row joined to its ordered execution declaration."""

    scenario_id: str
    directory: str
    manifest: dict[str, Any]
    execution: dict[str, Any]
    digest: str

    @property
    def expected_outcome(self) -> str:
        return str(self.manifest["expected_outcome"])

    @property
    def required_facts(self) -> list[dict[str, Any]]:
        return list(self.manifest["required_facts"])

    @property
    def expected_audit_events(self) -> list[str]:
        return list(self.execution["expected_audit_events"])

    @property
    def steps(self) -> list[dict[str, Any]]:
        return list(self.execution["steps"])


class Corpus:
    """The exact scenario and page-corpus versions selected for one run."""

    def __init__(self, repo_root: str, corpus_dir: str, web_dir: str) -> None:
        self.repo_root = os.path.abspath(repo_root)
        self.corpus_dir = os.path.abspath(corpus_dir)
        self.web_dir = os.path.abspath(web_dir)
        self.manifest = _read_json(os.path.join(self.corpus_dir, "manifest.json"))
        self.web_manifest = _read_json(os.path.join(self.web_dir, "manifest.json"))
        self._web_fixtures = {
            row["id"]: row for row in self.web_manifest.get("fixtures", [])
        }
        self._scenarios: dict[str, Scenario] = {}
        for row in self.manifest.get("scenarios", []):
            scenario_id = str(row.get("id", ""))
            directory = str(row.get("directory", ""))
            execution = _read_json(
                os.path.join(self.corpus_dir, directory, "scenario.json")
            )
            joined = {"manifest": row, "execution": execution}
            self._scenarios[scenario_id] = Scenario(
                scenario_id=scenario_id,
                directory=directory,
                manifest=row,
                execution=execution,
                digest=canonical_sha256(joined),
            )

        pinned = self.manifest.get("page_corpus", {}).get("required_version")
        actual = self.web_manifest.get("version")
        if pinned != actual:
            raise CorpusReadError(
                f"scenario corpus pins page corpus {pinned!r}, found {actual!r}"
            )
        declared = self.manifest.get("scenario_count")
        if declared != len(self._scenarios):
            raise CorpusReadError(
                f"scenario_count says {declared}, loaded {len(self._scenarios)}"
            )

    @property
    def version(self) -> str:
        return str(self.manifest["version"])

    @property
    def web_version(self) -> str:
        return str(self.web_manifest["version"])

    @property
    def scripted_outputs_state(self) -> str:
        return str(self.manifest["scripted_model_outputs"]["state"])

    @property
    def digest(self) -> str:
        return canonical_sha256(
            {"scenarios": self.manifest, "pages": self.web_manifest}
        )

    def scenario(self, scenario_id: str) -> Scenario:
        try:
            return self._scenarios[scenario_id]
        except KeyError as error:
            raise CorpusReadError(f"unknown scenario id {scenario_id}") from error

    def scenarios(self, selected: list[str]) -> list[Scenario]:
        if not selected:
            raise CorpusReadError("a benchmark selection may not be empty")
        if len(selected) != len(set(selected)):
            raise CorpusReadError("a benchmark selection contains duplicate ids")
        return [self.scenario(scenario_id) for scenario_id in selected]

    def fixture(self, fixture_id: str) -> dict[str, Any]:
        try:
            return self._web_fixtures[fixture_id]
        except KeyError as error:
            raise CorpusReadError(f"unknown page fixture id {fixture_id}") from error

    def page_field(self, fixture_id: str, field: str) -> dict[str, Any] | None:
        rows = self.fixture(fixture_id).get("expected_semantic_fields", [])
        matches = [row for row in rows if row.get("field") == field]
        if len(matches) > 1:
            raise CorpusReadError(
                f"fixture {fixture_id} declares page field {field} more than once"
            )
        return matches[0] if matches else None

    def canary_tokens(self) -> list[str]:
        return [str(row["token"]) for row in self.web_manifest.get("canaries", [])]

    def relative(self, path: str) -> str:
        return os.path.relpath(path, self.repo_root).replace(os.sep, "/")
