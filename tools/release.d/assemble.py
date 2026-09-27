#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Compose one release manifest out of the fragments each job produced.

Authority boundary: this module owns how fragments combine and nothing else.
It derives no facts, contacts no service, and invents no value: every field in
the output came from a fragment on the command line. Deriving the repository's
own facts is collect.sh; deciding whether the result is complete is the schema
and ./tools/release verify.

Owning milestone: M1 (WP-M1-07).

Why fragments. A manifest describes an artifact, and the facts about that
artifact are observed in different places: the repository knows its revision
and its pins, the build job knows its host and its cache configuration, the
signing job knows the certificate, the test job knows which suites ran. Each
writes what it observed; this composes them in order. A tool that guessed the
signing certificate from the repository would be writing fiction into an
evidence record.

Merge rules, applied fragment by fragment in the order given:

  object   merged key by key, recursively
  array    appended, dropping an entry byte-identical to one already present
  scalar   replaced, and the replacement is reported when it changes a value

  --out FILE        where to write the composed manifest
  --fragment FILE   a fragment; repeat, in order of increasing authority
  --report          print every key a later fragment overwrote
  --self-test       run the merge-semantics suite

Exit status: 0 written, 1 a fragment is unusable, 2 bad arguments.
"""

from __future__ import annotations

import argparse
import json
import sys


class Merge:
    """The result of composing fragments: the document plus what was replaced."""

    def __init__(self) -> None:
        self.document: dict = {}
        self.replacements: list[tuple[str, object, object]] = []

    def add(self, fragment: dict) -> None:
        self.document = self._merge(self.document, fragment, "")

    def _merge(self, base, incoming, pointer: str):
        if isinstance(base, dict) and isinstance(incoming, dict):
            result = dict(base)
            for key, value in incoming.items():
                child = f"{pointer}/{key}"
                result[key] = self._merge(base[key], value, child) if key in base else value
            return result
        if isinstance(base, list) and isinstance(incoming, list):
            result = list(base)
            seen = {json.dumps(item, sort_keys=True) for item in base}
            for item in incoming:
                key = json.dumps(item, sort_keys=True)
                if key not in seen:
                    seen.add(key)
                    result.append(item)
            return result
        if base != incoming:
            self.replacements.append((pointer, base, incoming))
        return incoming


def load(path: str) -> dict:
    with open(path, encoding="utf-8") as handle:
        document = json.load(handle)
    if not isinstance(document, dict):
        raise ValueError(f"{path}: a fragment must be a JSON object, found "
                         f"{type(document).__name__}")
    return document


def cmd_compose(args) -> int:
    merge = Merge()
    for path in args.fragment:
        try:
            merge.add(load(path))
        except FileNotFoundError:
            print(f"{path}: no such fragment.", file=sys.stderr)
            print("    fix: The job that observes those facts must write it before "
                  "the manifest is composed.", file=sys.stderr)
            return 1
        except (json.JSONDecodeError, ValueError) as error:
            print(f"{path}: {error}", file=sys.stderr)
            return 1
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(merge.document, handle, indent=2)
        handle.write("\n")
    if args.report:
        for pointer, was, now in merge.replacements:
            print(f"  replaced {pointer or '/'}: {was!r} -> {now!r}")
    print(f"  composed {len(args.fragment)} fragment(s) into {args.out}")
    return 0


CASES = [
    {
        "name": "objects merge key by key",
        "fragments": [{"a": {"x": 1}}, {"a": {"y": 2}}],
        "expect": {"a": {"x": 1, "y": 2}},
        "replacements": 0,
    },
    {
        "name": "a later scalar replaces an earlier one and the change is reported",
        "fragments": [{"a": 1}, {"a": 2}],
        "expect": {"a": 2},
        "replacements": 1,
    },
    {
        "name": "an identical scalar is not reported as a replacement",
        "fragments": [{"a": 1}, {"a": 1}],
        "expect": {"a": 1},
        "replacements": 0,
    },
    {
        "name": "arrays append",
        "fragments": [{"a": [1]}, {"a": [2]}],
        "expect": {"a": [1, 2]},
        "replacements": 0,
    },
    {
        "name": "an identical array entry is not appended twice",
        "fragments": [{"a": [{"k": 1}]}, {"a": [{"k": 1}, {"k": 2}]}],
        "expect": {"a": [{"k": 1}, {"k": 2}]},
        "replacements": 0,
    },
    {
        "name": "a fragment may introduce a branch that does not exist yet",
        "fragments": [{"a": 1}, {"b": {"c": {"d": 3}}}],
        "expect": {"a": 1, "b": {"c": {"d": 3}}},
        "replacements": 0,
    },
    {
        "name": "a nested scalar replacement names its full pointer",
        "fragments": [{"a": {"b": {"c": 1}}}, {"a": {"b": {"c": 9}}}],
        "expect": {"a": {"b": {"c": 9}}},
        "replacements": 1,
        "pointer": "/a/b/c",
    },
    {
        "name": "a fragment replacing an object with a scalar is reported, not silently dropped",
        "fragments": [{"a": {"b": 1}}, {"a": "flattened"}],
        "expect": {"a": "flattened"},
        "replacements": 1,
    },
    {
        "name": "order is authority: the last fragment wins",
        "fragments": [{"a": 1}, {"a": 2}, {"a": 3}],
        "expect": {"a": 3},
        "replacements": 2,
    },
]


def cmd_self_test(_args) -> int:
    failures = 0
    for case in CASES:
        merge = Merge()
        for fragment in case["fragments"]:
            merge.add(fragment)
        if merge.document != case["expect"]:
            failures += 1
            print(f"FAIL  {case['name']}: got {merge.document!r}, "
                  f"expected {case['expect']!r}")
            continue
        if len(merge.replacements) != case["replacements"]:
            failures += 1
            print(f"FAIL  {case['name']}: {len(merge.replacements)} replacement(s) "
                  f"reported, expected {case['replacements']}")
            continue
        if "pointer" in case and merge.replacements[0][0] != case["pointer"]:
            failures += 1
            print(f"FAIL  {case['name']}: replacement reported at "
                  f"{merge.replacements[0][0]}, expected {case['pointer']}")
    print()
    print(f"manifest composition: {len(CASES)} case(s), {failures} failure(s)")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(prog="assemble.py",
                                     description="Compose a release manifest from fragments.")
    parser.add_argument("--out")
    parser.add_argument("--fragment", action="append", default=[])
    parser.add_argument("--report", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return cmd_self_test(args)
    if not args.out or not args.fragment:
        parser.error("--out and at least one --fragment are required")
    return cmd_compose(args)


if __name__ == "__main__":
    sys.exit(main())
