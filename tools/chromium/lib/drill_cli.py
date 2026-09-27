#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Command line around the urgent-security timing record.

Authority boundary: this module reads and writes the record file and renders
it. The lifecycle, the clock and the intervals are drill.py; the repository
mutations — writing the patch, bumping chromium/SECURITY_PATCH_LEVEL — are
tools/chromium/security-patch, because those need the repository's own helpers
and its refusal rules.

Owning milestone: M1 (WP-M1-08).

Exit status: 0 done, 1 refused with a reason, 2 the record is unusable.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import drill  # noqa: E402  (after sys.path setup)


def refuse(error: drill.RecordError) -> int:
    print(f"error: {error}", file=sys.stderr)
    if error.remediation:
        print(f"       {error.remediation}", file=sys.stderr)
    return 1


def cmd_open(args) -> int:
    if os.path.exists(args.record) and not args.force:
        print(f"error: a record already exists at {args.record}", file=sys.stderr)
        print("       One advisory, one record. Use --record for a different file, "
              "or --force to start over — which discards a measurement.",
              file=sys.stderr)
        return 1
    chromium = json.loads(args.chromium) if args.chromium else {}
    record = drill.new_record(args.advisory, args.severity, args.kind,
                              args.exploited, chromium)
    drill.add_event(record, "detected", args.at, args.actor, args.evidence)
    drill.save(args.record, record)
    print(f"  opened {args.kind} record for {args.advisory} "
          f"({args.severity}{', actively exploited' if args.exploited else ''})")
    print(f"  detected at {drill.event_at(record, 'detected')['at']}")
    print(f"  {args.record}")
    return 0


def cmd_mark(args) -> int:
    record = drill.load(args.record)
    drill.add_event(record, args.event, args.at, args.actor, args.evidence)
    drill.save(args.record, record)
    print(f"  {args.event} at {drill.event_at(record, args.event)['at']}")
    remaining = record["open_events"]
    print(f"  {len(remaining)} event(s) still open: {', '.join(remaining) or 'none'}")
    return 0


def cmd_patch(args) -> int:
    record = drill.load(args.record)
    record["patches"].append({
        "file": args.file,
        "sha256": args.sha256,
        "upstream_commit": args.upstream_commit,
        "subject": args.subject,
    })
    record["chromium"]["security_patch_level_before"] = args.level_before
    record["chromium"]["security_patch_level_after"] = args.level_after
    if not drill.event_at(record, "patch_staged"):
        drill.add_event(record, "patch_staged", args.at, args.actor, args.file)
    else:
        drill.recompute(record)
    drill.save(args.record, record)
    print(f"  recorded {args.file} at security patch level {args.level_after}")
    return 0


def render(record: dict) -> None:
    print(f"  advisory        {record['advisory']} ({record['severity']}"
          f"{', actively exploited' if record['actively_exploited'] else ''})")
    print(f"  record kind     {record['kind']}")
    print(f"  chromium        milestone {record['chromium'].get('milestone', '?')} "
          f"@ {str(record['chromium'].get('commit', ''))[:12]}, security patch level "
          f"{record['chromium'].get('security_patch_level_before', '?')} -> "
          f"{record['chromium'].get('security_patch_level_after', '?')}")
    print(f"  patches         {len(record['patches'])}")
    for patch in record["patches"]:
        print(f"                  {patch['file']}  {patch['sha256'][:12]}")
    print()
    print("  lifecycle")
    for name in drill.EVENTS:
        event = drill.event_at(record, name)
        if event:
            print(f"    {name:<24} {event['at']}"
                  + (f"  {event['actor']}" if event["actor"] else ""))
        else:
            print(f"    {name:<24} —")
    print()
    print("  intervals")
    for key, _start, _end in drill.INTERVALS:
        print(f"    {key:<38} {drill.human_duration(record['intervals'].get(key))}")
    for stage, seconds in record["intervals"].get("stage_seconds", {}).items():
        print(f"    {stage:<38} {drill.human_duration(seconds)}")
    print()
    print(f"  objective       {record['objective']['register_entry']} "
          f"({record['objective']['status']}), from {record['objective']['source']}")
    print(f"                  {record['objective']['note']}")
    print(f"  outcome         {record['outcome']}")


def cmd_status(args) -> int:
    render(drill.load(args.record))
    return 0


def cmd_report(args) -> int:
    record = drill.load(args.record)
    drill.recompute(record)
    drill.save(args.record, record)
    if args.fragment:
        with open(args.record, "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()
        fragment = drill.manifest_fragment(record, args.record, digest)
        with open(args.fragment, "w", encoding="utf-8") as handle:
            json.dump(fragment, handle, indent=2)
            handle.write("\n")
    if args.json:
        json.dump(record, sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        render(record)
        if record["open_events"]:
            print()
            print(f"  incomplete: {', '.join(record['open_events'])} not recorded. "
                  "The drill is not a data point until the clock is closed.")
    if args.fragment:
        print(f"  manifest fragment: {args.fragment}")
    return 0 if not (args.require_complete and record["open_events"]) else 1


# --- self-test ---------------------------------------------------------------

def cmd_self_test(_args) -> int:
    failures = 0
    checks = 0

    def check(condition, message) -> None:
        nonlocal failures, checks
        checks += 1
        if not condition:
            failures += 1
            print(f"FAIL  {message}")

    def expect_refusal(callable_, message) -> None:
        nonlocal failures, checks
        checks += 1
        try:
            callable_()
        except drill.RecordError as error:
            if not error.remediation:
                failures += 1
                print(f"FAIL  {message}: refused without naming a next step")
            return
        failures += 1
        print(f"FAIL  {message}: was accepted")

    chromium = {"milestone": "152", "tag": "152.0.7977.42", "commit": "d" * 40,
                "security_patch_level_before": 0}
    record = drill.new_record("upstream-issue-1", "critical", "drill", True, chromium)
    check(record["objective"]["register_entry"] == "OD-052",
          "a new record cites the register entry that owns the objective")
    check(record["outcome"] == "open" and len(record["open_events"]) == len(drill.EVENTS),
          "a new record has every lifecycle event open")
    check("48" not in json.dumps(record["objective"]) and "7 day" not in
          json.dumps(record["objective"]).lower(),
          "the record states no numeric target of its own")

    drill.add_event(record, "detected", "2026-08-17T00:00:00Z")
    check(record["intervals"]["detection_to_artifact_seconds"] is None,
          "detection to artifact is null until the artifact is verified")
    expect_refusal(lambda: drill.add_event(record, "detected", "2026-08-17T01:00:00Z"),
                   "a second timestamp for the same event")
    expect_refusal(lambda: drill.add_event(record, "triaged", "2026-08-16T23:00:00Z"),
                   "an event that precedes the one before it")
    expect_refusal(lambda: drill.add_event(record, "triaged", "2026-08-17 01:00:00"),
                   "a timestamp with no time zone offset")
    expect_refusal(lambda: drill.add_event(record, "promoted", "2026-08-17T01:00:00Z"),
                   "an event outside the lifecycle")

    for name, at in (
        ("triaged", "2026-08-17T00:30:00Z"),
        ("patch_staged", "2026-08-17T02:00:00Z"),
        ("build_started", "2026-08-17T02:30:00Z"),
        ("artifact_signed", "2026-08-17T06:00:00Z"),
        ("artifact_verified", "2026-08-17T08:00:00Z"),
        ("distribution_submitted", "2026-08-17T09:00:00Z"),
        ("distribution_live", "2026-08-18T08:00:00Z"),
        ("closed", "2026-08-18T09:00:00Z"),
    ):
        drill.add_event(record, name, at)

    check(record["intervals"]["detection_to_artifact_seconds"] == 8 * 3600,
          "detection to artifact measures detected to artifact_verified")
    check(record["intervals"]["artifact_to_distribution_seconds"] == 24 * 3600,
          "artifact to distribution measures artifact_verified to distribution_live")
    check(record["intervals"]["detection_to_distribution_seconds"] == 32 * 3600,
          "detection to distribution spans the whole response")
    check(record["outcome"] == "complete" and not record["open_events"],
          "a record with every event is complete")
    check(len(record["intervals"]["stage_seconds"]) == len(drill.EVENTS) - 1,
          "every consecutive pair of recorded events has a stage duration")
    check(record["events"] == sorted(record["events"],
                                     key=lambda e: drill.EVENTS.index(e["name"])),
          "events are stored in lifecycle order however they were recorded")

    record["chromium"]["security_patch_level_after"] = 1
    fragment = drill.manifest_fragment(record, "/tmp/record.json", "a" * 64)
    check(fragment["security_drill"]["objective_register_entry"] == "OD-052",
          "the manifest fragment cites the register entry")
    check(fragment["security_drill"]["record_path"] == "record.json",
          "the manifest fragment records a relative path, not a machine path")
    check(fragment["chromium"]["security_patch_level"] == 1,
          "the manifest fragment carries the patch level the drill produced")
    check(fragment["security_drill"]["detection_to_artifact_seconds"] == 8 * 3600,
          "the manifest fragment carries both measured intervals")

    partial = drill.new_record("upstream-issue-2", "high", "incident", False, chromium)
    drill.add_event(partial, "detected", "2026-08-17T00:00:00Z")
    drill.add_event(partial, "artifact_signed", "2026-08-17T04:00:00Z")
    check(partial["intervals"]["stage_seconds"] == {"detected to artifact_signed": 14400},
          "stage durations skip events that were never recorded")
    check("artifact_verified" in partial["open_events"],
          "an unmeasured event is named as open rather than assumed")

    print()
    print(f"security drill record: {checks} check(s), {failures} failure(s)")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(prog="drill_cli.py",
                                     description="Read and write a security-response timing record.")
    sub = parser.add_subparsers(dest="command", required=True)

    opener = sub.add_parser("open")
    opener.add_argument("--record", required=True)
    opener.add_argument("--advisory", required=True)
    opener.add_argument("--severity", required=True)
    opener.add_argument("--kind", required=True)
    opener.add_argument("--exploited", action="store_true")
    opener.add_argument("--chromium", default="")
    opener.add_argument("--at", default="")
    opener.add_argument("--actor", default="")
    opener.add_argument("--evidence", default="")
    opener.add_argument("--force", action="store_true")
    opener.set_defaults(handler=cmd_open)

    marker = sub.add_parser("mark")
    marker.add_argument("--record", required=True)
    marker.add_argument("--event", required=True)
    marker.add_argument("--at", default="")
    marker.add_argument("--actor", default="")
    marker.add_argument("--evidence", default="")
    marker.set_defaults(handler=cmd_mark)

    patcher = sub.add_parser("patch")
    patcher.add_argument("--record", required=True)
    patcher.add_argument("--file", required=True)
    patcher.add_argument("--sha256", required=True)
    patcher.add_argument("--upstream-commit", default="")
    patcher.add_argument("--subject", default="")
    patcher.add_argument("--level-before", type=int, required=True)
    patcher.add_argument("--level-after", type=int, required=True)
    patcher.add_argument("--at", default="")
    patcher.add_argument("--actor", default="")
    patcher.set_defaults(handler=cmd_patch)

    status = sub.add_parser("status")
    status.add_argument("--record", required=True)
    status.set_defaults(handler=cmd_status)

    report = sub.add_parser("report")
    report.add_argument("--record", required=True)
    report.add_argument("--json", action="store_true")
    report.add_argument("--fragment", default="")
    report.add_argument("--require-complete", action="store_true")
    report.set_defaults(handler=cmd_report)

    self_test = sub.add_parser("self-test")
    self_test.set_defaults(handler=cmd_self_test)

    args = parser.parse_args()
    try:
        return args.handler(args)
    except drill.RecordError as error:
        return refuse(error)
    except json.JSONDecodeError as error:
        print(f"error: the record is not valid JSON ({error})", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
