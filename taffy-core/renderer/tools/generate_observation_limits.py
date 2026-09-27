#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Turn observation_limits.json into the header the renderer compiles.

Why this exists at all: protocol section 10 leaves every numeric BIP limit
`[Open (OD-031)]`, and code still needs bounds before that decision lands. The
danger is not that the placeholders are wrong - they are, deliberately - but
that they get copied. A bound that lives in four files gets updated in three,
and the file nobody updated is the one a hostile page finds.

So the values live in exactly one committed file and this script is the only
thing allowed to turn them into C++. It also refuses to run when
observation_limits.json and the `ObservationLimitsValues` struct in
observation_limits.h disagree, which makes adding a field to one of them a
build failure rather than a silently defaulted zero.

Host Python 3 only, no third-party imports: it runs from a GN action on the
Chromium track and from `--check` on any host, including the docs host that
cannot compile anything.

Usage:
  generate_observation_limits.py --input observation_limits.json \
      --header observation_limits.h --output <gen>/observation_limits_generated.h
  generate_observation_limits.py --check        # agreement only, writes nothing
  generate_observation_limits.py --selftest     # exercises the checks above

Exit status: 0 agreement (and, unless --check, the header written), 1 findings.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
import textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
COMPONENT_DIR = os.path.dirname(HERE)

DEFAULT_INPUT = os.path.join(COMPONENT_DIR, "observation_limits.json")
DEFAULT_HEADER = os.path.join(COMPONENT_DIR, "observation_limits.h")

STRUCT_RE = re.compile(r"struct\s+ObservationLimitsValues\s*\{(.*?)\n\};", re.S)
MEMBER_RE = re.compile(r"^\s*uint32_t\s+([a-z0-9_]+)\s*;\s*$")

# uint32_t is the only member type the struct uses, so every value has to fit.
# A limit that needs more than this is a design change, not a bigger number.
MAX_VALUE = 2**32 - 1


class Failure(Exception):
    """A disagreement between the configuration and the header."""


def read_declared_fields(header_path: str) -> list[str]:
    """The ObservationLimitsValues member names, in declaration order."""
    with open(header_path, encoding="utf-8") as handle:
        text = handle.read()
    match = STRUCT_RE.search(text)
    if not match:
        raise Failure(
            f"{header_path}: no `struct ObservationLimitsValues {{ ... }};` found. "
            "The generator reads the field set from the header so the two "
            "cannot drift; if the struct moved, point --header at it."
        )
    fields = []
    for line in match.group(1).splitlines():
        member = MEMBER_RE.match(line)
        if member:
            fields.append(member.group(1))
    if not fields:
        raise Failure(f"{header_path}: ObservationLimitsValues declares no uint32_t members.")
    return fields


def read_configured_fields(input_path: str) -> tuple[list[tuple[str, int, str]], dict]:
    """The (name, value, why) triples from the configuration, in file order."""
    with open(input_path, encoding="utf-8") as handle:
        document = json.load(handle)

    for required in ("fields", "owned_by_open_decision", "generated_symbol"):
        if required not in document:
            raise Failure(f"{input_path}: missing required key '{required}'.")
    if not re.fullmatch(r"OD-\d{3}", document["owned_by_open_decision"]):
        raise Failure(
            f"{input_path}: owned_by_open_decision must name a register entry "
            "as OD-nnn; these numbers are not allowed to be unowned."
        )

    entries = []
    seen = set()
    for index, field in enumerate(document["fields"]):
        for required in ("name", "value", "unit", "why"):
            if required not in field:
                raise Failure(f"{input_path}: field {index} is missing '{required}'.")
        name = field["name"]
        value = field["value"]
        if name in seen:
            raise Failure(f"{input_path}: '{name}' is declared twice.")
        seen.add(name)
        if not isinstance(value, int) or isinstance(value, bool):
            raise Failure(f"{input_path}: '{name}' must be an integer, got {value!r}.")
        if value < 0 or value > MAX_VALUE:
            raise Failure(
                f"{input_path}: '{name}' = {value} does not fit the uint32_t "
                "member it initialises."
            )
        if not str(field["why"]).strip():
            raise Failure(
                f"{input_path}: '{name}' has an empty 'why'. A bound nobody can "
                "explain is a bound nobody can replace with a measurement."
            )
        entries.append((name, value, str(field["why"]).strip()))
    return entries, document


def check_agreement(declared: list[str], configured: list[tuple[str, int, str]], *,
                    input_path: str, header_path: str) -> None:
    configured_names = [name for name, _, _ in configured]
    if configured_names == declared:
        return

    missing = [name for name in declared if name not in configured_names]
    extra = [name for name in configured_names if name not in declared]
    lines = [
        f"{input_path} and {header_path} disagree about ObservationLimitsValues."
    ]
    if missing:
        lines.append(f"  declared in the header but not configured: {', '.join(missing)}")
    if extra:
        lines.append(f"  configured but not declared in the header: {', '.join(extra)}")
    if not missing and not extra:
        lines.append("  same field set, different order. The generated header is a "
                     "designated initialiser, so order is part of the contract.")
        for position, (config_name, header_name) in enumerate(zip(configured_names, declared)):
            if config_name != header_name:
                lines.append(f"  first difference at position {position}: "
                             f"configured '{config_name}', declared '{header_name}'")
                break
    raise Failure("\n".join(lines))


def render(configured: list[tuple[str, int, str]], document: dict, input_path: str) -> str:
    symbol = document["generated_symbol"]
    owner = document["owned_by_open_decision"]
    relative_input = os.path.relpath(input_path, COMPONENT_DIR)

    out = [
        "// Copyright (c) 2026 Matterward Labs Private Limited.",
        "//",
        "// This Source Code Form is subject to the terms of the Mozilla Public",
        "// License, v. 2.0. If a copy of the MPL was not distributed with this",
        "// file, You can obtain one at https://mozilla.org/MPL/2.0/.",
        "//",
        "// GENERATED FILE - DO NOT EDIT AND DO NOT COMMIT.",
        "//",
        f"// Produced from //taffy/renderer/{relative_input} by",
        "// //taffy/renderer/tools/generate_observation_limits.py.",
        f"// Every value below is provisional and owned by [Open ({owner})].",
        "// None of them is a quality target and none may be quoted as one.",
        "// Change the configuration, never this file.",
        "",
        "#ifndef TAFFY_RENDERER_OBSERVATION_LIMITS_GENERATED_H_",
        "#define TAFFY_RENDERER_OBSERVATION_LIMITS_GENERATED_H_",
        "",
        '#include "taffy/renderer/observation_limits.h"',
        "",
        "namespace taffy::generated {",
        "",
        f"inline constexpr ObservationLimitsValues {symbol} = {{",
    ]
    for name, value, why in configured:
        # Wrapped so the generated file passes the same line-length review a
        # hand-written one would. Chromium C++ is 80 columns.
        for line in textwrap.wrap(why, width=72):
            out.append(f"    // {line}")
        out.append(f"    .{name} = {value},")
    out += [
        "};",
        "",
        "}  // namespace taffy::generated",
        "",
        "#endif  // TAFFY_RENDERER_OBSERVATION_LIMITS_GENERATED_H_",
        "",
    ]
    return "\n".join(out)


def write_if_changed(path: str, text: str) -> None:
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    if os.path.exists(path):
        with open(path, encoding="utf-8") as handle:
            if handle.read() == text:
                return
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def selftest() -> int:
    """Proves the two failures that matter actually fail."""
    entries, document = read_configured_fields(DEFAULT_INPUT)
    declared = read_declared_fields(DEFAULT_HEADER)
    check_agreement(declared, entries, input_path=DEFAULT_INPUT, header_path=DEFAULT_HEADER)

    failures = []

    # 1. A configured field the header does not declare must fail.
    with_extra = entries + [("snapshot_max_invented", 1, "not declared anywhere")]
    try:
        check_agreement(declared, with_extra, input_path="<test>", header_path="<test>")
        failures.append("an undeclared configured field was accepted")
    except Failure:
        pass

    # 2. The same field set in a different order must fail, because the
    #    generated header is a designated initialiser.
    if len(entries) >= 2:
        swapped = [entries[1], entries[0]] + entries[2:]
        try:
            check_agreement(declared, swapped, input_path="<test>", header_path="<test>")
            failures.append("a reordered configuration was accepted")
        except Failure:
            pass

    # 3. The rendered header must mention every field exactly once and must
    #    name the open decision that owns the values.
    rendered = render(entries, document, DEFAULT_INPUT)
    for name, _, _ in entries:
        if rendered.count(f".{name} =") != 1:
            failures.append(f"'{name}' is not initialised exactly once")
    if document["owned_by_open_decision"] not in rendered:
        failures.append("the generated header does not name its owning decision")

    # 4. Writing twice must be idempotent.
    with tempfile.TemporaryDirectory() as directory:
        target = os.path.join(directory, "observation_limits_generated.h")
        write_if_changed(target, rendered)
        first = os.stat(target).st_mtime_ns
        write_if_changed(target, rendered)
        if os.stat(target).st_mtime_ns != first:
            failures.append("write_if_changed rewrote an unchanged file")

    for failure in failures:
        print(f"selftest: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"selftest: ok, {len(entries)} limits agree with {os.path.basename(DEFAULT_HEADER)}")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--input", default=DEFAULT_INPUT)
    parser.add_argument("--header", default=DEFAULT_HEADER)
    parser.add_argument("--output")
    parser.add_argument("--check", action="store_true",
                        help="verify agreement and write nothing")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()

    try:
        configured, document = read_configured_fields(args.input)
        declared = read_declared_fields(args.header)
        check_agreement(declared, configured, input_path=args.input,
                        header_path=args.header)
    except Failure as failure:
        print(str(failure), file=sys.stderr)
        return 1

    if args.check:
        print(f"observation limits: {len(configured)} fields agree")
        return 0

    if not args.output:
        print("--output is required unless --check or --selftest is given",
              file=sys.stderr)
        return 1

    write_if_changed(args.output, render(configured, document, args.input))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
