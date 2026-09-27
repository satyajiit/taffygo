#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Validate a release artifact manifest against the committed schema.

Authority boundary: this module owns the command line around the schema —
reading a manifest, choosing the profile, printing findings a human can act
on, and proving the schema itself still behaves. The rules live in
manifest-schema.json; the keyword machinery lives in schema_validate.py.
Nothing here duplicates a rule the schema already states.

Owning milestone: M1 (WP-M1-07).

  validate <file> [--profile KIND] [--json]   check one manifest
  fields                                      print every field and its profile
  self-test                                   run the fixture suite

The profile defaults to the manifest's own release.kind, so a development
artifact is not judged against release-candidate evidence and a candidate
cannot quietly downgrade itself: the kind is written by the build, and the
verifier that promotes an artifact passes --profile candidate explicitly.

Exit status: 0 clean, 1 findings, 2 the manifest or the schema is unusable.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from schema_validate import Validator  # noqa: E402  (after sys.path setup)

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA_PATH = os.path.join(HERE, "manifest-schema.json")
FIXTURES = os.path.join(HERE, "fixtures")


def load_schema() -> Validator:
    with open(SCHEMA_PATH, encoding="utf-8") as handle:
        return Validator(json.load(handle))


def load_json(path: str):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def profile_of(manifest) -> str:
    if isinstance(manifest, dict):
        release = manifest.get("release")
        if isinstance(release, dict) and isinstance(release.get("kind"), str):
            return release["kind"]
    return "development"


def report(findings, as_json: bool, path: str) -> int:
    if as_json:
        json.dump(
            {
                "manifest": path,
                "ok": not findings,
                "findings": [
                    {"pointer": f.pointer, "message": f.message, "remediation": f.remediation}
                    for f in findings
                ],
            },
            sys.stdout,
            indent=2,
        )
        sys.stdout.write("\n")
        return 1 if findings else 0
    if not findings:
        print(f"{path}: manifest is complete and well-formed")
        return 0
    for finding in findings:
        print(str(finding))
    print()
    print(f"{path}: {len(findings)} finding(s) — the artifact is not attested")
    return 1


def cmd_validate(args) -> int:
    try:
        manifest = load_json(args.file)
    except FileNotFoundError:
        print(f"{args.file}: no such manifest.", file=sys.stderr)
        print("    fix: Produce it with ./tools/release manifest --out <dir>.", file=sys.stderr)
        return 2
    except json.JSONDecodeError as error:
        print(f"{args.file}: is not valid JSON ({error}).", file=sys.stderr)
        print("    fix: A truncated manifest usually means the build was killed "
              "mid-write; rebuild rather than repairing it by hand.", file=sys.stderr)
        return 2
    validator = load_schema()
    profile = args.profile or profile_of(manifest)
    return report(validator.validate(manifest, profile), args.json, args.file)


def cmd_fields(args) -> int:
    """Print the schema as a table, so the document and the gate cannot drift."""
    validator = load_schema()
    schema = validator.schema
    profiles = schema.get("x-profiles", {})
    extra: dict[str, list[str]] = {}
    for name, pointers in profiles.items():
        for pointer in pointers:
            extra.setdefault(pointer, []).append(name)

    rows: list[tuple[str, str, str]] = []

    def walk(node: dict, pointer: str, inherited_required: bool) -> None:
        node = validator.resolve(node)
        required = set(node.get("required", []))
        for name, child in node.get("properties", {}).items():
            child_pointer = f"{pointer}/{name}"
            child = validator.resolve(child)
            when = "always" if (name in required and inherited_required) else "optional"
            if child_pointer in extra:
                when = "always" if when == "always" else "+".join(sorted(extra[child_pointer]))
            kind = child.get("type", "const" if "const" in child else "enum")
            if "enum" in child:
                kind = "enum(" + "|".join(str(v) for v in child["enum"]) + ")"
            rows.append((child_pointer, kind, when))
            walk(child, child_pointer, inherited_required and name in required)
            if child.get("type") == "array" and isinstance(child.get("items"), dict):
                walk(child["items"], f"{child_pointer}/[]", False)

    walk(schema, "", True)
    width = max(len(row[0]) for row in rows)
    for pointer, kind, when in rows:
        print(f"{pointer.ljust(width)}  {kind:<28}  {when}")
    print()
    print(f"{len(rows)} field(s); profiles: {', '.join(sorted(profiles))}")
    return 0


def apply_patch(document, operations):
    """Apply the fixture suite's remove/set operations to a copy."""
    document = json.loads(json.dumps(document))
    for operation in operations:
        tokens = [t for t in operation["pointer"].split("/") if t]
        node = document
        for token in tokens[:-1]:
            node = node[int(token)] if isinstance(node, list) else node[token]
        last = tokens[-1]
        key = int(last) if isinstance(node, list) else last
        if operation["op"] == "remove":
            del node[key]
        elif operation["op"] == "set":
            node[key] = operation["value"]
        else:
            raise ValueError(f"unknown fixture operation: {operation['op']}")
    return document


def cmd_self_test(args) -> int:
    validator = load_schema()
    complete = load_json(os.path.join(FIXTURES, "candidate-complete.json"))
    cases = load_json(os.path.join(FIXTURES, "invalid-cases.json"))
    failures = 0
    checked = 0

    for profile in ("development", "drill", "candidate"):
        findings = validator.validate(complete, profile)
        checked += 1
        if findings:
            failures += 1
            print(f"FAIL  the complete fixture must satisfy every profile, but "
                  f"{profile} reports {len(findings)}:")
            for finding in findings:
                print(f"        {finding.pointer}: {finding.message}")

    for case in cases:
        checked += 1
        instance = apply_patch(complete, case["patch"])
        findings = validator.validate(instance, case.get("profile", "candidate"))
        matched = [
            f for f in findings
            if f.pointer == case["expect"]["pointer"]
            and case["expect"]["contains"] in f.message
        ]
        if not matched:
            failures += 1
            print(f"FAIL  {case['name']}: expected a finding at "
                  f"{case['expect']['pointer']} containing "
                  f"{case['expect']['contains']!r}; got "
                  + (", ".join(f"{f.pointer} ({f.message})" for f in findings) or "nothing"))
            continue
        if not matched[0].remediation:
            failures += 1
            print(f"FAIL  {case['name']}: the finding carries no remediation. "
                  "Every rule in the schema names its next step.")

    print()
    print(f"manifest schema: {checked} case(s), {failures} failure(s)")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="manifest.py",
        description="Validate a TaffyGo release artifact manifest.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    validate = sub.add_parser("validate", help="check one manifest against the schema")
    validate.add_argument("file")
    validate.add_argument("--profile", choices=("development", "drill", "candidate"),
                          help="override the manifest's own release.kind")
    validate.add_argument("--json", action="store_true", help="machine-readable findings")
    validate.set_defaults(handler=cmd_validate)

    fields = sub.add_parser("fields", help="print every schema field and when it is required")
    fields.set_defaults(handler=cmd_fields)

    self_test = sub.add_parser("self-test", help="run the fixture suite over the schema")
    self_test.set_defaults(handler=cmd_self_test)

    args = parser.parse_args()
    return args.handler(args)


if __name__ == "__main__":
    sys.exit(main())
