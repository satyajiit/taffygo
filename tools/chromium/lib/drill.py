#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The timing record for the urgent-security fast path.

Authority boundary: this module owns the record — its lifecycle, its clock,
and the two intervals the M1 exit review asks for. Applying a patch and bumping
the patch level is tools/chromium/security-patch; deciding what the intervals
mean is nobody's job here, and deliberately so.

Owning milestone: M1 (WP-M1-08). Procedure:
docs/development/chromium-fork-and-build.md section 1.4 and
docs/development/testing-and-delivery.md section 12.

What it deliberately does not do: it states no target and compares against no
threshold. The response-time values in PAR-SEC-003 are candidate objectives
owned by OD-052, and they are ratified from repeated drills, not asserted by
the instrument that measures them. A tool that printed "within target" would be
deciding an open question by writing a number into a report.

  events    the lifecycle, in order; an event may be recorded at any time but
            its timestamp must not precede the event before it
  intervals detection to artifact, artifact to distribution, and each stage;
            an interval whose endpoint has not happened is null, and the
            missing event is named in open_events

Stdlib only, no network.
"""

from __future__ import annotations

import datetime
import json
import os

RECORD_VERSION = 1

# The lifecycle, in the order it must happen. detection_to_artifact ends at
# artifact_verified, not at artifact_signed: an artifact nobody checked is not
# a response, and testing-and-delivery section 12 says so plainly — a dashboard
# that notices an update but cannot produce a tested artifact does not satisfy
# the requirement.
EVENTS = (
    "detected",
    "triaged",
    "patch_staged",
    "build_started",
    "artifact_signed",
    "artifact_verified",
    "distribution_submitted",
    "distribution_live",
    "closed",
)

INTERVALS = (
    ("detection_to_artifact_seconds", "detected", "artifact_verified"),
    ("artifact_to_distribution_seconds", "artifact_verified", "distribution_live"),
    ("detection_to_distribution_seconds", "detected", "distribution_live"),
)

OBJECTIVE = {
    "register_entry": "OD-052",
    "status": "candidate",
    "source": "PAR-SEC-003 in docs/product/browser-parity-matrix.md",
    "note": (
        "The response-time values in PAR-SEC-003 are candidate objectives, not a "
        "service target. This record is one measurement toward ratifying them; it "
        "asserts nothing about whether the response was fast enough. That "
        "judgement belongs to the owners of OD-052, from repeated drills."
    ),
}

SEVERITIES = ("critical", "high", "medium", "low")
KINDS = ("drill", "incident")


class RecordError(Exception):
    """A refusal that names what to do instead."""

    def __init__(self, message: str, remediation: str = "") -> None:
        super().__init__(message)
        self.remediation = remediation


def now() -> str:
    return (datetime.datetime.now(datetime.timezone.utc)
            .replace(microsecond=0).isoformat().replace("+00:00", "Z"))


def parse(stamp: str) -> datetime.datetime:
    text = stamp.strip()
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    try:
        moment = datetime.datetime.fromisoformat(text)
    except ValueError as error:
        raise RecordError(
            f"{stamp!r} is not an RFC 3339 timestamp ({error})",
            "Write it as 2026-08-17T09:30:00Z, or omit it to use the clock now.",
        ) from error
    if moment.tzinfo is None:
        raise RecordError(
            f"{stamp!r} carries no time zone offset",
            "A response clock spanning two people in two places cannot use local "
            "time. Write the offset, or use Z.",
        )
    return moment


def new_record(advisory: str, severity: str, kind: str, exploited: bool,
               chromium: dict) -> dict:
    if severity not in SEVERITIES:
        raise RecordError(f"unknown severity: {severity}",
                          f"One of: {', '.join(SEVERITIES)}")
    if kind not in KINDS:
        raise RecordError(f"unknown record kind: {kind}",
                          "Use --kind drill for a rehearsal, --kind incident for a "
                          "real advisory. The difference matters when the record is "
                          "read later as evidence.")
    return {
        "record_version": RECORD_VERSION,
        "kind": kind,
        "advisory": advisory,
        "severity": severity,
        "actively_exploited": exploited,
        "objective": dict(OBJECTIVE),
        "chromium": chromium,
        "patches": [],
        "events": [],
        "intervals": {},
        "open_events": list(EVENTS),
        "outcome": "open",
        "notes": [],
    }


def load(path: str) -> dict:
    if not os.path.exists(path):
        raise RecordError(
            f"no timing record at {path}",
            "Open one first: ./tools/chromium/security-patch open --advisory <ref> "
            "--kind drill|incident --severity <severity>",
        )
    with open(path, encoding="utf-8") as handle:
        record = json.load(handle)
    if record.get("record_version") != RECORD_VERSION:
        raise RecordError(
            f"{path} is record version {record.get('record_version')}, this tool "
            f"writes version {RECORD_VERSION}",
            "Finish the drill with the tool that opened it rather than mixing "
            "versions mid-incident.",
        )
    return record


def save(path: str, record: dict) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(record, handle, indent=2)
        handle.write("\n")


def event_at(record: dict, name: str):
    for event in record["events"]:
        if event["name"] == name:
            return event
    return None


def add_event(record: dict, name: str, at: str = "", actor: str = "",
              evidence: str = "") -> dict:
    if name not in EVENTS:
        raise RecordError(f"unknown event: {name}",
                          f"The lifecycle is: {', '.join(EVENTS)}")
    if event_at(record, name):
        raise RecordError(
            f'"{name}" is already recorded at {event_at(record, name)["at"]}',
            "A response clock is written once. If the first value was wrong, say so "
            "in a note rather than rewriting the measurement.",
        )
    stamp = at or now()
    moment = parse(stamp)
    index = EVENTS.index(name)
    for earlier in EVENTS[:index]:
        previous = event_at(record, earlier)
        if previous and parse(previous["at"]) > moment:
            raise RecordError(
                f'"{name}" at {stamp} precedes "{earlier}" at {previous["at"]}',
                "The lifecycle is ordered. Either the timestamp is wrong or the "
                "events were recorded out of sequence; both are worth knowing "
                "before this becomes evidence.",
            )
    record["events"].append({
        "name": name,
        "at": parse(stamp).isoformat().replace("+00:00", "Z"),
        "actor": actor or os.environ.get("USER", ""),
        "evidence": evidence,
    })
    record["events"].sort(key=lambda event: EVENTS.index(event["name"]))
    recompute(record)
    return record


def recompute(record: dict) -> dict:
    intervals: dict = {}
    for name, start, end in INTERVALS:
        first, last = event_at(record, start), event_at(record, end)
        intervals[name] = (
            int((parse(last["at"]) - parse(first["at"])).total_seconds())
            if first and last else None
        )
    stages = {}
    previous = None
    for name in EVENTS:
        event = event_at(record, name)
        if event and previous:
            stages[f"{previous['name']} to {name}"] = int(
                (parse(event["at"]) - parse(previous["at"])).total_seconds()
            )
        if event:
            previous = event
    intervals["stage_seconds"] = stages
    record["intervals"] = intervals
    record["open_events"] = [name for name in EVENTS if not event_at(record, name)]
    record["outcome"] = "complete" if not record["open_events"] else "open"
    return record


def manifest_fragment(record: dict, record_path: str, record_digest: str) -> dict:
    """The part of a release manifest a drill artifact carries."""
    fragment = {
        "release": {"kind": "drill" if record["kind"] == "drill" else "candidate"},
        "chromium": {
            "security_patch_level": record["chromium"].get("security_patch_level_after"),
            "security_advisories": [record["advisory"]],
        },
        "security_drill": {
            "advisory": record["advisory"],
            "record_path": os.path.basename(record_path),
            "record_sha256": record_digest,
            "objective_register_entry": OBJECTIVE["register_entry"],
        },
    }
    for key in ("detection_to_artifact_seconds", "artifact_to_distribution_seconds"):
        if record["intervals"].get(key) is not None:
            fragment["security_drill"][key] = record["intervals"][key]
    return {k: v for k, v in fragment.items() if v}


def human_duration(seconds) -> str:
    if seconds is None:
        return "not yet measured"
    hours, remainder = divmod(int(seconds), 3600)
    minutes = remainder // 60
    return f"{hours}h {minutes:02d}m ({seconds} s)"
