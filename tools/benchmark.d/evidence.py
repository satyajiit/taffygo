#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Write the Task Benchmark's input-integrity record and its manifest fragment.

Authority boundary: this module records what the benchmark verified and what it
could not run, and nothing more. It never produces a score: scoring belongs to
a run against an installed build on the ratified device matrix, and the whole
purpose of this record is to keep "verified inputs" and "measured results"
distinguishable in an evidence trail.

Owning milestone: M1 (WP-M1-07), so a release manifest can cite a benchmark
state instead of leaving the field blank or, worse, filling it with a run that
did not happen.

Inputs arrive in the environment from tools/benchmark. Stdlib only, no network.
"""

from __future__ import annotations

import datetime
import json
import os
import sys

from manifest_fragment import preflight_fragment

EVIDENCE_VERSION = 2


def value(name: str) -> str:
    return os.environ.get(name, "").strip()


def timestamp() -> str:
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    moment = (
        datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc)
        if epoch and epoch.isdigit()
        else datetime.datetime.now(datetime.timezone.utc)
    )
    return moment.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def suites() -> list:
    """One row per suite: whether it could select, and how much it selected.

    `inputs_complete` and `selected` answer a weaker question than the record's
    `measured` field. A suite can hold every input and select every scenario in
    the corpus while still executing none of them, which is exactly the state
    this repository is in, and the record keeps the two apart on purpose.
    """
    entries = []
    for line in value("B_BLOCKERS").splitlines():
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        name, blockers = parts[0], parts[1]
        selected = parts[2] if len(parts) > 2 else ""
        reasons = [reason for reason in blockers.split(";") if reason.strip()]
        entries.append({
            "name": name,
            "inputs_complete": not reasons,
            "blockers": reasons,
            "selected_scenarios": int(selected) if selected.strip().isdigit() else None,
        })
    return entries


def scenario_corpus() -> dict:
    """The scenario corpus as tools/benchmark.d/corpus.py resolved it.

    Absent or unreadable, the record says so rather than omitting the field: a
    missing key reads as an older evidence version, while `present: false` is a
    statement about this run.
    """
    summary = value("B_SCENARIO_SUMMARY")
    record = {"path": value("B_CORPUS_DIR"), "present": bool(summary), "needs": []}
    for line in summary.splitlines():
        if "\t" not in line:
            continue
        key, item = line.split("\t", 1)
        if key == "execution_needs":
            record["needs"].append(item)
        elif key in ("corpus", "version", "scenarios", "job_families",
                     "page_corpus_version", "page_fixtures_used",
                     "scripted_model_outputs", "execution_state"):
            record[key] = item
    return record


def build() -> dict:
    manifest = value("B_FIXTURES_MANIFEST")
    return {
        "evidence_version": EVIDENCE_VERSION,
        "generated_at": timestamp(),
        "host": value("B_HOST"),
        "requested_suite": value("B_SUITE"),
        "page_fixtures": {
            "corpus": value("B_CORPUS_NAME"),
            "version": value("B_CORPUS_VERSION"),
            "manifest": os.path.relpath(manifest, os.getcwd()) if manifest else "",
            "self_check": value("B_SELF_CHECK") or "not-run",
        },
        "scenario_corpus": scenario_corpus(),
        "suites": suites(),
        "devices": [device for device in value("B_DEVICES").split() if device],
        "measured": False,
        "measured_note": (
            "No scenario was executed. This record proves which corpus versions were "
            "verified on which host and what each suite would have selected; a "
            "benchmark result additionally requires an installed build on the device "
            "matrix (OD-017), and the run's audit stream read back."
        ),
    }


def main() -> int:
    record = build()
    evidence_path = value("B_EVIDENCE")
    fragment_path = value("B_FRAGMENT")
    if evidence_path:
        os.makedirs(os.path.dirname(os.path.abspath(evidence_path)), exist_ok=True)
        with open(evidence_path, "w", encoding="utf-8") as handle:
            json.dump(record, handle, indent=2)
            handle.write("\n")
    if fragment_path:
        fragment = preflight_fragment(evidence_path, record)
        os.makedirs(os.path.dirname(os.path.abspath(fragment_path)), exist_ok=True)
        with open(fragment_path, "w", encoding="utf-8") as handle:
            json.dump(fragment, handle, indent=2)
            handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
