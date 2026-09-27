#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Prove every Core Service command kind reaches a projection that carries it.

`RustCore::Submit` routes a command to one plane by naming its kind in an `if`
condition, and every kind it does not name falls through to the account
projection at the end. Each projection switches over the whole enumeration with
no `default`, so a kind it does not carry sits in a run of case labels whose
whole body is `return std::nullopt;` — and a projection answering `std::nullopt`
is `AdmissionStatus::kInvalidCommand`.

Those two facts meet in the failure this checker exists for. Leave a kind out of
the dispatch and the compiler says nothing: the switch is still exhaustive
because the refusal group already lists the kind, the fallthrough is still
well-formed, and the browser receives a refusal for a command the core
implements. It happened to `kSupplyFieldValues`, which the Rust reducer, the
bridge and the browser all carried while the C++ seam between them refused it,
so every task that asked a person for field values waited for an answer that
could not arrive.

A kind no plane carries yet belongs in `KNOWN_UNROUTED` with a reason and an
owner. The register is checked in both directions, so a debt that is paid off
cannot be left on the books.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

BRIDGE_DIR = Path(__file__).resolve().parent.parent
REPOSITORY_ROOT = BRIDGE_DIR.parent.parent.parent
ENUM_FILE = (
    REPOSITORY_ROOT
    / "taffy-core/contracts/core-service/generated/mojom/core_service.mojom"
)
DISPATCH_FILE = BRIDGE_DIR / "rust_core_dispatch.cc"
DISPATCH_ENTRY = "CoreResponseBatch RustCore::Submit("

# Kinds no plane carries yet, each with a reason and an owner. Stated here
# rather than left to the fallthrough, so the seam's silence is a decision with
# a name on it and the day one is wired up it replaces an entry instead of
# removing a finding. It is empty today: the two entries it would have held
# when this checker was written — `kSupplyFieldValues` and
# `kSetProviderModelPreference` — were paid off rather than registered, and the
# register is read in both directions so neither can be left on the books.
KNOWN_UNROUTED: dict[str, tuple[str, str]] = {}


def strip_comments(text: str) -> str:
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)


def matched_span(text: str, start: int, opening: str, closing: str) -> int | None:
    """Index one past the `closing` that balances the `opening` at `start`."""
    if start >= len(text) or text[start] != opening:
        return None
    depth = 0
    for index in range(start, len(text)):
        if text[index] == opening:
            depth += 1
        elif text[index] == closing:
            depth -= 1
            if depth == 0:
                return index + 1
    return None


def function_body(text: str, marker: str) -> str | None:
    start = text.find(marker)
    if start < 0:
        return None
    brace = text.find("{", start + len(marker))
    if brace < 0:
        return None
    end = matched_span(text, brace, "{", "}")
    return None if end is None else text[brace + 1 : end - 1]


def enum_members(mojom_source: str) -> list[str]:
    body = function_body(mojom_source, "enum CoreServiceCommandKind")
    if body is None:
        return []
    return re.findall(r"(?m)^\s*(k\w+)\s*=\s*\d+\s*,", body)


def handler_of(block: str) -> str | None:
    """The projection or submit helper a dispatch branch hands the command to."""
    projection = re.search(r"\bToBridge(\w+)\b", block)
    if projection:
        return f"ToBridge{projection.group(1)}"
    inline = re.search(r"core_service_internal::(\w+)", block)
    return inline.group(1) if inline else None


def dispatch_branches(submit_body: str) -> tuple[list[tuple[list[str], str | None]], str | None]:
    """Every `if` branch that names a kind, and the trailing fallback's handler."""
    branches: list[tuple[list[str], str | None]] = []
    index = 0
    depth = 0
    end_of_last_branch = 0
    while index < len(submit_body):
        character = submit_body[index]
        if character in "{(":
            depth += 1
            index += 1
            continue
        if character in "})":
            depth -= 1
            index += 1
            continue
        if depth == 0 and submit_body.startswith("if", index):
            after = index + 2
            while after < len(submit_body) and submit_body[after].isspace():
                after += 1
            condition_end = matched_span(submit_body, after, "(", ")")
            if condition_end is None:
                index += 1
                continue
            condition = submit_body[after:condition_end]
            block_start = condition_end
            while block_start < len(submit_body) and submit_body[block_start].isspace():
                block_start += 1
            block_end = matched_span(submit_body, block_start, "{", "}")
            if block_end is None:
                index = condition_end
                continue
            kinds = re.findall(r"CoreServiceCommandKind::(k\w+)", condition)
            if kinds:
                branches.append(
                    (kinds, handler_of(submit_body[block_start:block_end]))
                )
                end_of_last_branch = block_end
            index = block_end
            continue
        index += 1
    return branches, handler_of(submit_body[end_of_last_branch:])


def refused_kinds(projection_body: str) -> set[str] | None:
    """The kinds a projection answers `std::nullopt` for, or None if unparsable.

    A projection with no per-kind case labels refuses no kind: it carries
    whatever the dispatch hands it, so the empty set is the honest answer and
    not a gap. One that does label kinds must be readable, or the checker says
    so rather than reporting a coverage it could not see.
    """
    if "case mojom::CoreServiceCommandKind::" not in projection_body:
        return set()
    switch = re.search(r"\bswitch\s*\(", projection_body)
    if switch is None:
        return None
    brace = projection_body.find("{", switch.end())
    if brace < 0:
        return None
    end = matched_span(projection_body, brace, "{", "}")
    if end is None:
        return None
    body = projection_body[brace + 1 : end - 1]
    labels = list(re.finditer(r"case\s+mojom::CoreServiceCommandKind::(k\w+)\s*:", body))
    refused: set[str] = set()
    group: list[str] = []
    for position, label in enumerate(labels):
        group.append(label.group(1))
        following = (
            body[label.end() : labels[position + 1].start()]
            if position + 1 < len(labels)
            else body[label.end() :]
        )
        if not following.strip():
            continue
        if re.fullmatch(r"return\s+std::nullopt\s*;\s*", following.strip() + " "):
            refused.update(group)
        group = []
    return refused


def check(
    mojom_source: str,
    dispatch_source: str,
    projection_sources: dict[str, str],
    register: dict[str, tuple[str, str]],
) -> tuple[list[str], dict[str, int]]:
    findings: list[str] = []
    members = enum_members(mojom_source)
    if not members:
        return (
            ["command-dispatch: CoreServiceCommandKind has no members to check"],
            {},
        )
    submit_body = function_body(strip_comments(dispatch_source), DISPATCH_ENTRY)
    if submit_body is None:
        return (
            [f"command-dispatch: `{DISPATCH_ENTRY}` has no implementation"],
            {},
        )
    branches, fallback = dispatch_branches(submit_body)
    if fallback is None:
        findings.append(
            "command-dispatch: the fallthrough after the last branch names no "
            "projection, so an unrouted kind reaches nothing"
        )

    routed: dict[str, str | None] = {}
    for kinds, handler in branches:
        for kind in kinds:
            if kind in routed:
                findings.append(
                    f"command-dispatch: `{kind}` is named by two branches "
                    f"(`{routed[kind]}` and `{handler}`); one command kind is "
                    "carried by one plane"
                )
                continue
            routed[kind] = handler

    named = {handler for _, handler in branches if handler}
    if fallback:
        named.add(fallback)
    refusals: dict[str, set[str]] = {}
    for name in sorted(named):
        source = projection_sources.get(name)
        if source is None:
            findings.append(
                f"command-dispatch: the dispatch hands commands to `{name}`, "
                "whose definition this checker cannot find, so its refusals "
                "cannot be proved"
            )
            continue
        body = function_body(strip_comments(source), f"{name}(")
        parsed = refused_kinds(body) if body is not None else None
        if parsed is None:
            findings.append(
                f"command-dispatch: `{name}` labels command kinds in a switch "
                "this checker cannot read, so its refusals cannot be proved"
            )
            continue
        refusals[name] = parsed

    for kind in members:
        if kind not in members:
            continue
        handler = routed.get(kind, fallback)
        registered = kind in register
        refused = handler in refusals and kind in refusals[handler]
        if refused and not registered:
            where = (
                "falls through to" if kind not in routed else "is routed to"
            )
            findings.append(
                f"command-dispatch: `{kind}` {where} `{handler}`, which refuses "
                "it, so every such command is answered kInvalidCommand. Route "
                "it to the plane that carries it, or register it in "
                "KNOWN_UNROUTED with a reason and an owner."
            )
        if registered and not refused and handler in refusals:
            findings.append(
                f"command-dispatch: `{kind}` is in KNOWN_UNROUTED but "
                f"`{handler}` carries it. The debt is paid; delete the entry."
            )

    for kind, entry in register.items():
        if kind not in members:
            findings.append(
                f"command-dispatch: KNOWN_UNROUTED names `{kind}`, which is not "
                "a member of CoreServiceCommandKind"
            )
            continue
        reason, owner = entry
        if not reason.strip() or not owner.strip():
            findings.append(
                f"command-dispatch: KNOWN_UNROUTED entry `{kind}` needs both a "
                "reason and an owner"
            )

    counts = {
        "members": len(members),
        "projections": len(refusals),
        "registered": len([kind for kind in register if kind in members]),
    }
    return findings, counts


def handler_sources_on_disk(directory: Path, names: set[str]) -> dict[str, str]:
    """The defining source of each handler the dispatch names, and no other.

    Anchored at column zero, which is what separates a definition from a call:
    every function here is defined at namespace scope, so either the return
    type and the name share a line that starts at column zero or the name
    starts one of its own. A call is always indented inside a function body, so
    it cannot match — and getting that wrong would hand this checker a caller's
    file and let it read a switch that is not the projection's.
    """
    sources: dict[str, str] = {}
    for path in sorted(directory.glob("*.cc")):
        if path.name.endswith("_unittest.cc") or path.name == DISPATCH_FILE.name:
            continue
        text = path.read_text(encoding="utf-8")
        for name in names - set(sources):
            definition = re.compile(
                rf"(?m)^(?:[\w:<>,&*]+[ \t]+)?{re.escape(name)}[ \t]*\("
            )
            if definition.search(text):
                sources[name] = text
    return sources


SELF_TEST_ENUM = """
enum CoreServiceCommandKind {
  kAlpha = 0,
  kBeta = 1,
  kGamma = 2,
  kDelta = 3,
};
"""

SELF_TEST_DISPATCH = """
CoreResponseBatch RustCore::Submit(mojom::CoreServiceCommandPtr command,
                                   uint64_t now) {
  if (!command) {
    return CoreResponseBatch();
  }
  if (command->kind == mojom::CoreServiceCommandKind::kAlpha ||
      command->kind == mojom::CoreServiceCommandKind::kBeta) {
    std::optional<bridge::BridgeWorkCommand> projected =
        core_service_internal::ToBridgeWorkCommand(*command);
    return ToResponseBatch(bridge::SubmitWork(std::move(*projected)));
  }
  std::optional<bridge::BridgeRestCommand> projected =
      core_service_internal::ToBridgeRestCommand(*command);
  return ToResponseBatch(bridge::SubmitRest(std::move(*projected)));
}
"""

SELF_TEST_WORK = """
std::optional<bridge::BridgeWorkCommand> ToBridgeWorkCommand(
    const mojom::CoreServiceCommand& command) {
  switch (command.kind) {
    case mojom::CoreServiceCommandKind::kAlpha:
      projected.task_id = command.alpha->task_id;
      break;
    case mojom::CoreServiceCommandKind::kBeta:
      projected.task_id = command.beta->task_id;
      break;
    case mojom::CoreServiceCommandKind::kGamma:
    case mojom::CoreServiceCommandKind::kDelta:
      return std::nullopt;
  }
  return projected;
}
"""

SELF_TEST_REST = """
std::optional<bridge::BridgeRestCommand> ToBridgeRestCommand(
    const mojom::CoreServiceCommand& command) {
  switch (command.kind) {
    case mojom::CoreServiceCommandKind::kGamma:
      projected.name = command.gamma->name;
      break;
    case mojom::CoreServiceCommandKind::kAlpha:
    case mojom::CoreServiceCommandKind::kBeta:
    case mojom::CoreServiceCommandKind::kDelta:
      return std::nullopt;
  }
  return projected;
}
"""


def self_test() -> int:
    sources = {
        "ToBridgeWorkCommand": SELF_TEST_WORK,
        "ToBridgeRestCommand": SELF_TEST_REST,
    }
    register = {"kDelta": ("no plane carries it yet", "an-owner")}
    findings, counts = check(SELF_TEST_ENUM, SELF_TEST_DISPATCH, sources, register)
    if findings:
        print(
            f"command-dispatch self-test: complete fixture was refused: {findings}",
            file=sys.stderr,
        )
        return 1
    if counts["members"] != 4 or counts["projections"] != 2:
        print(
            f"command-dispatch self-test: miscounted the fixture: {counts}",
            file=sys.stderr,
        )
        return 1

    dropped = SELF_TEST_DISPATCH.replace(
        "  if (command->kind == mojom::CoreServiceCommandKind::kAlpha ||\n"
        "      command->kind == mojom::CoreServiceCommandKind::kBeta) {",
        "  if (command->kind == mojom::CoreServiceCommandKind::kAlpha) {",
    )
    findings, _ = check(SELF_TEST_ENUM, dropped, sources, register)
    if len(findings) != 1 or "`kBeta` falls through to `ToBridgeRestCommand`" not in findings[0]:
        print(
            f"command-dispatch self-test: missed a dropped route: {findings}",
            file=sys.stderr,
        )
        return 1

    paid = {
        "ToBridgeWorkCommand": SELF_TEST_WORK.replace(
            "    case mojom::CoreServiceCommandKind::kGamma:\n"
            "    case mojom::CoreServiceCommandKind::kDelta:\n"
            "      return std::nullopt;",
            "    case mojom::CoreServiceCommandKind::kDelta:\n"
            "      projected.name = command.delta->name;\n"
            "      break;\n"
            "    case mojom::CoreServiceCommandKind::kGamma:\n"
            "      return std::nullopt;",
        ),
        "ToBridgeRestCommand": SELF_TEST_REST,
    }
    routed_delta = SELF_TEST_DISPATCH.replace(
        "      command->kind == mojom::CoreServiceCommandKind::kBeta) {",
        "      command->kind == mojom::CoreServiceCommandKind::kBeta ||\n"
        "      command->kind == mojom::CoreServiceCommandKind::kDelta) {",
    )
    findings, _ = check(SELF_TEST_ENUM, routed_delta, paid, register)
    if len(findings) != 1 or "The debt is paid" not in findings[0]:
        print(
            f"command-dispatch self-test: missed a paid register entry: {findings}",
            file=sys.stderr,
        )
        return 1

    twice = SELF_TEST_DISPATCH.replace(
        "  std::optional<bridge::BridgeRestCommand> projected =",
        "  if (command->kind == mojom::CoreServiceCommandKind::kAlpha) {\n"
        "    return CoreResponseBatch();\n"
        "  }\n"
        "  std::optional<bridge::BridgeRestCommand> projected =",
    )
    findings, _ = check(SELF_TEST_ENUM, twice, sources, register)
    if not any("is named by two branches" in finding for finding in findings):
        print(
            f"command-dispatch self-test: missed a doubly routed kind: {findings}",
            file=sys.stderr,
        )
        return 1

    findings, _ = check(
        SELF_TEST_ENUM,
        SELF_TEST_DISPATCH,
        sources,
        {"kNotAKind": ("reason", "owner")},
    )
    if not any("is not a member" in finding for finding in findings):
        print(
            f"command-dispatch self-test: missed a stale register name: {findings}",
            file=sys.stderr,
        )
        return 1

    print("command-dispatch: self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--self-test", action="store_true", help="run the checker against its fixtures"
    )
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    dispatch_source = DISPATCH_FILE.read_text(encoding="utf-8")
    submit_body = function_body(strip_comments(dispatch_source), DISPATCH_ENTRY)
    branches, fallback = dispatch_branches(submit_body or "")
    names = {handler for _, handler in branches if handler}
    if fallback:
        names.add(fallback)
    findings, counts = check(
        ENUM_FILE.read_text(encoding="utf-8"),
        dispatch_source,
        handler_sources_on_disk(BRIDGE_DIR, names),
        KNOWN_UNROUTED,
    )
    for finding in findings:
        print(finding, file=sys.stderr)
    if findings:
        return 1
    print(
        f"command-dispatch: all {counts['members']} CoreServiceCommandKind "
        f"members reach a plane that carries them "
        f"({counts['projections']} projections read, "
        f"{counts['registered']} registered as unrouted)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
