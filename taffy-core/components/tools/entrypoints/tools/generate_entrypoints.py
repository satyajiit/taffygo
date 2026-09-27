#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate the frozen tool-entrypoint registry from its committed source.

WHAT THIS REGISTRY IS

It is the complete list of things a sandboxed worker may ever be asked to do.
Not the list of things one is asked to do today, and not a list a caller
contributes to: the list. A name that is not a row in it is refused before a
process is spent on it, and there is no code path that adds a row after the
build.

WHY IT IS GENERATED RATHER THAN LOADED

A registry read from disk is a registry an attacker with a write to that disk
can extend, and a registry supplied by a caller is not a registry at all. So
the rows become a C++ table and a Rust table, both compiled in, and the source
is a build input rather than a run-time input. Nothing in the product opens it.

That leaves exactly one failure mode - a generated table that no longer matches
its source - and `--check` is what closes it.

WHERE THE RULES AND THE RENDERERS LIVE

This file drives; it does not decide. `entrypoint_source.py` holds every rule a
source document must satisfy, including the one that refuses a key naming a
location or a body of code, and `entrypoint_projections.py` holds the three
renderers. A rule therefore has exactly one home, and a renderer cannot quietly
accept something the rules refuse.

Stdlib only. Deterministic: nothing reads a clock, a host name, or the
environment. `REGISTRY_FINGERPRINT` is derived from the rows themselves, so it
cannot be forgotten and cannot be stale.

    generate_entrypoints.py --write       rewrite all three artifacts
    generate_entrypoints.py --check       fail if any is stale
    generate_entrypoints.py --self-test   check that the rules above still fire

Exit status: 0 clean, 1 stale or malformed.
"""

from __future__ import annotations

import argparse
import copy
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from entrypoint_projections import render_cpp, render_doc, render_rust  # noqa: E402
from entrypoint_source import (  # noqa: E402
    MAX_ENTRYPOINT_ID_BYTES,
    RegistryError,
    fingerprint,
    load,
    validate,
)

HERE = os.path.dirname(os.path.abspath(__file__))
REGISTRY = os.path.dirname(HERE)
COMPONENT = os.path.dirname(REGISTRY)
SOURCE = os.path.join(REGISTRY, "source", "entrypoints.json")
CPP = os.path.join(REGISTRY, "generated", "cpp", "tool_entrypoints.h")
DOC = os.path.join(REGISTRY, "generated", "docs", "tool-entrypoints.md")
RUST = os.path.join(
    COMPONENT, "core", "rust", "tool-entrypoints", "src", "generated.rs"
)


ARTIFACTS = (
    ("the C++ table", CPP, render_cpp),
    ("the Rust table", RUST, render_rust),
    ("the documentation", DOC, render_doc),
)


# --- self-test --------------------------------------------------------------


def self_test() -> int:
    """Break the source in each way the rules name and check each one fires."""
    base = load(SOURCE)
    cases: list[tuple[str, object]] = []

    def case(name, mutate):
        cases.append((name, mutate))

    def first(document):
        return document["entrypoints"][0]

    case("an entrypoint with no namespace", lambda d: first(d).update({"id": "build"}))
    case("an entrypoint with a capital", lambda d: first(d).update({"id": "Document.build"}))
    case("an over-long identity",
         lambda d: first(d).update({"id": "a." + "b" * MAX_ENTRYPOINT_ID_BYTES}))
    case("two rows with one identity",
         lambda d: d["entrypoints"].append(copy.deepcopy(first(d))))
    case("rows out of order", lambda d: d["entrypoints"].reverse())
    case("a native alternative nothing declares",
         lambda d: first(d).update({"native_alternative": "core.invented"}))
    case("a declared native path nothing refuses in favour of",
         lambda d: d["native_paths"].append(
             {"id": "core.zzz.unused", "state": "planned", "description": "x"}))
    case("an unknown native state",
         lambda d: d["native_paths"][0].update({"state": "shipping"}))
    case("a port of an undeclared kind",
         lambda d: first(d)["inputs"][0].update({"kind": "audio-wav"}))
    case("a port with no description",
         lambda d: first(d)["inputs"][0].pop("description"))
    case("two ports with one name", lambda d: first(d)["inputs"].append(
        copy.deepcopy(first(d)["inputs"][0])))
    case("an entrypoint with no output", lambda d: first(d).update({"outputs": []}))
    case("an entrypoint with no input", lambda d: first(d).update({"inputs": []}))
    case("an entrypoint with no summary", lambda d: first(d).update({"summary": "  "}))
    case("a row carrying a module name",
         lambda d: first(d).update({"module": "taffy.tools.document"}))
    case("a row carrying a filesystem location",
         lambda d: first(d).update({"path": "tools/document.py"}))
    case("a row carrying a command line", lambda d: first(d).update({"argv": "python -c"}))
    case("a row carrying an origin", lambda d: first(d).update({"url": "example"}))
    case("an unknown key on a row", lambda d: first(d).update({"trusted": True}))
    case("a missing key on a row", lambda d: first(d).pop("native_alternative"))
    case("value kinds out of order", lambda d: d["value_kinds"].reverse())
    case("a value kind with a capital", lambda d: d["value_kinds"][0].update({"name": "Json"}))
    case("a registry version of zero", lambda d: d.update({"registry_version": 0}))
    case("a source naming another registry", lambda d: d.update({"registry": "tools"}))

    failures = []
    for name, mutate in cases:
        document = copy.deepcopy(base)
        mutate(document)
        try:
            validate(document)
        except RegistryError:
            continue
        failures.append(name)

    try:
        validate(base)
    except RegistryError as error:
        failures.append(f"the committed source is itself invalid: {error}")

    # The fingerprint has to move when the field that refuses moves, or a
    # registry that started dispatching a capability it used to refuse would be
    # indistinguishable from the one before it.
    before = fingerprint(base)
    relaxed = copy.deepcopy(base)
    for row in relaxed["entrypoints"]:
        row["native_alternative"] = ""
    if fingerprint(relaxed) == before:
        failures.append("the fingerprint did not change when a refusal was lifted")
    reworded = copy.deepcopy(base)
    reworded["entrypoints"][0]["summary"] = "reworded"
    if fingerprint(reworded) != before:
        failures.append("the fingerprint changed when only prose did")

    # Exactly one row is refused today. This is a fact about the committed
    # source rather than a rule, and it is asserted here because the refusal
    # tests in Rust and C++ both need a real refused row to fire on: a source
    # edit that left none would turn those tests green by emptying them.
    refused = [row for row in base["entrypoints"] if row["native_alternative"]]
    if not refused:
        failures.append("no committed row names a native alternative, so nothing refuses")

    for failure in failures:
        print(f"self-test: {failure} was not caught", file=sys.stderr)
    if failures:
        return 1
    print(f"self-test: {len(cases)} rules fire, and the fingerprint tracks the refusals")
    return 0


# --- entry point ------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true", help="rewrite all three artifacts")
    group.add_argument("--check", action="store_true", help="fail if any is stale")
    group.add_argument("--self-test", action="store_true", help="check the rules")
    arguments = parser.parse_args()

    if arguments.self_test:
        return self_test()

    try:
        source = load(SOURCE)
        validate(source)
        rendered = [(label, path, render(source)) for label, path, render in ARTIFACTS]
    except RegistryError as error:
        print(f"entrypoint registry: {error}", file=sys.stderr)
        return 1

    if arguments.write:
        for _label, path, text in rendered:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text)
            print(f"wrote {os.path.relpath(path)}")
        return 0

    stale = []
    for label, path, text in rendered:
        try:
            with open(path, "r", encoding="utf-8") as handle:
                existing = handle.read()
        except OSError:
            existing = ""
        if existing != text:
            stale.append(f"{label} ({os.path.relpath(path)})")
    if stale:
        print(
            "entrypoint registry: " + ", ".join(stale) + " no longer matches the source. "
            "Regenerate with `python3 taffy-core/components/tools/entrypoints/tools/"
            "generate_entrypoints.py --write` and commit the result.",
            file=sys.stderr,
        )
        return 1
    refused = sum(1 for row in source["entrypoints"] if row["native_alternative"])
    print(
        f"entrypoint registry: {len(source['entrypoints'])} rows "
        f"({refused} refused in favour of a native path), three artifacts up to date"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
