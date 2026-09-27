#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The C++ enumerations that claim to mirror a Mojo enumeration, checked.

Authority boundary: agreement between the Mojo projection and the hand-written
C++ enumerations in the renderer overlay that say, in a comment, that they
mirror one. Nothing here writes a file, and nothing here reads the JSON schema:
the schema-to-Mojo comparison is :mod:`mojom`'s job, and chaining the two is
what makes the whole path checkable — schema, Mojo, C++ — without a Chromium
checkout.

Why the renderer keeps its own enumerations at all: the semantic graph is built
before anything is serialized, and a graph built out of generated Mojo types
would make every adapter depend on the wire. The translation lives in one place
(``renderer/wire_enum_conversions.cc``) and the domain enumerations live in
``renderer/semantic_graph.h``, which is the right shape — but it means the same
closed vocabulary is written twice, and a member added to one and not the other
is a silent hole in a closed enumeration. That is exactly the failure this
module exists to make loud.

Two rules, both self-declaring, so there is no list to maintain here:

1. A C++ ``enum class X`` whose leading comment says ``Mirrors mojom::Y`` must
   have the same members, spelled the same way, in the same order, as the Mojo
   enumeration ``Y``. This is the rule for the renderer's domain vocabularies,
   which are deliberately named differently from the wire — ``ActionKind`` for
   ``mojom::ActionType``, ``EdgeType`` for ``mojom::RelationshipKind`` — so
   that a domain type and a wire type are never confused at a call site.
2. A C++ ``enum class X`` where a Mojo enumeration of the *same name* exists
   must match it, comment or no comment. This is the rule for the browser
   process's public contract headers, where the C++ name is the wire name on
   purpose. It is not opt-in, which is the point: a member added to one of the
   two and not the other is caught without anybody having remembered to
   annotate anything.

   Rule 2 has one escape, because a few vocabularies genuinely share a name
   with a wire enumeration while meaning something narrower or wider — a
   browser-side exclusion reason set that maps down to the six the wire
   carries, an adapter-local budget vocabulary, an adapter set with two members
   the wire has no room for yet. Each of those carries a
   ``// bip-local-vocabulary:`` annotation saying so, in the header itself. The
   annotation exempts the enumeration from rule 2 and from nothing else, and an
   annotation on an enumeration with no same-named wire counterpart is itself a
   finding, so the escapes cannot outlive the collisions they were written for.

A mirror comment naming an enumeration that does not exist is a finding, and so
is a member that appears on one side only, and so is the same set of members in
a different order — a closed vocabulary's order is normative.
"""

from __future__ import annotations

import os
import re

from layout import CONTRACT_DIR, relative

_PRODUCT_ROOT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(CONTRACT_DIR))), "taffy-core"
)

#: The headers searched for mirror declarations.
SEARCH_ROOTS = (
    os.path.join(_PRODUCT_ROOT, "common", "public"),
    os.path.join(_PRODUCT_ROOT, "renderer"),
    os.path.join(_PRODUCT_ROOT, "browser"),
)

_MIRROR = re.compile(r"//\s*Mirrors\s+mojom::(?P<target>[A-Za-z_][A-Za-z0-9_]*)\b")

#: ``// bip-local-vocabulary: <reason>`` above a same-named enumeration.
_LOCAL = re.compile(r"^\s*//\s*bip-local-vocabulary:\s*(?P<reason>.+?)\s*$")
_ENUM = re.compile(r"^enum class (?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(:[^{]*)?\{")
_MEMBER = re.compile(r"^\s*(?P<name>k[A-Za-z0-9_]*)\s*(=\s*[^,]+)?,\s*$")


class Mirror:
    """One C++ enumeration with a Mojo counterpart, declared or same-named."""

    def __init__(
        self, name: str, target: str, path: str, line: int, local: str | None = None
    ) -> None:
        self.name = name
        self.target = target
        self.path = path
        self.line = line
        self.local = local
        self.members: list[str] = []


def _headers(roots: tuple[str, ...]) -> list[str]:
    found: list[str] = []
    for root in roots:
        if not os.path.isdir(root):
            continue
        for directory, _, files in os.walk(root):
            for name in sorted(files):
                if name.endswith(".h"):
                    found.append(os.path.join(directory, name))
    return sorted(found)


def collect(
    roots: tuple[str, ...] = SEARCH_ROOTS, wire_names: frozenset[str] = frozenset()
) -> list[Mirror]:
    """Every C++ enumeration under ``roots`` that has a Mojo counterpart.

    ``wire_names`` is the set of Mojo enumeration names; an enumeration whose
    own name is in it is a mirror by rule 2 even with no comment.
    """
    mirrors: list[Mirror] = []
    for path in _headers(roots):
        with open(path, encoding="utf-8") as handle:
            lines = handle.read().split("\n")
        pending: str | None = None
        local: str | None = None
        current: Mirror | None = None
        for index, raw in enumerate(lines, start=1):
            if current is not None:
                if raw.startswith("};"):
                    mirrors.append(current)
                    current = None
                    continue
                member = _MEMBER.match(raw)
                if member:
                    current.members.append(member.group("name"))
                continue
            declaration = _ENUM.match(raw)
            if declaration:
                name = declaration.group("name")
                target = pending or (name if name in wire_names else None)
                if target or local:
                    current = Mirror(name, target or name, path, index, local)
                pending = None
                local = None
                continue
            mirror = _MIRROR.search(raw)
            local_note = _LOCAL.match(raw)
            if mirror:
                pending = mirror.group("target")
            elif local_note:
                local = local_note.group("reason")
            elif raw.strip() and not raw.lstrip().startswith("//"):
                pending = None
                local = None
    return mirrors


def check(declarations: dict, roots: tuple[str, ...] = SEARCH_ROOTS) -> list[str]:
    """Findings, given the Mojo declarations :func:`mojom.parse` produced."""
    findings: list[str] = []
    wire_names = frozenset(
        name for name, declaration in declarations.items() if declaration.kind == "enum"
    )
    for mirror in collect(roots, wire_names):
        where = f"{relative(mirror.path)}:{mirror.line}"
        same_name = mirror.name == mirror.target
        if mirror.local:
            if not same_name:
                findings.append(
                    f"{where}: {mirror.name} is annotated bip-local-vocabulary and also says "
                    f"it mirrors mojom::{mirror.target}; it cannot be both"
                )
            elif mirror.name not in declarations:
                findings.append(
                    f"{where}: {mirror.name} is annotated bip-local-vocabulary, and no Mojo "
                    "enumeration shares its name, so the annotation exempts it from nothing"
                )
            continue
        target = declarations.get(mirror.target)
        if target is None:
            findings.append(
                f"{where}: {mirror.name} says it mirrors mojom::{mirror.target}, "
                "and no Mojo enumeration has that name"
            )
            continue
        if target.kind != "enum":
            findings.append(
                f"{where}: {mirror.name} says it mirrors mojom::{mirror.target}, "
                f"which is a {target.kind} and not an enumeration"
            )
            continue
        if mirror.members == target.members:
            continue
        missing = [m for m in target.members if m not in mirror.members]
        extra = [m for m in mirror.members if m not in target.members]
        rule = "shares its name with" if same_name else "says it mirrors"
        for member in missing:
            findings.append(
                f"{where}: {mirror.name} {rule} mojom::{mirror.target}, which has "
                f"{member!r}; the C++ enumeration does not"
            )
        for member in extra:
            findings.append(
                f"{where}: {mirror.name} {rule} mojom::{mirror.target} and has {member!r}; "
                "the Mojo enumeration does not"
            )
        if not missing and not extra:
            findings.append(
                f"{where}: {mirror.name} and mojom::{mirror.target} have the same members in "
                "a different order, which makes the two vocabularies disagree about "
                f"precedence — Mojo {target.members}, C++ {mirror.members}"
            )
    return findings


def report(declarations: dict, roots: tuple[str, ...] = SEARCH_ROOTS) -> int:
    """Print the findings; return a process exit status."""
    findings = check(declarations, roots)
    for finding in findings:
        print(f"c++: {finding}")
    if findings:
        print()
        print(f"{len(findings)} disagreement(s) between the Mojo projection and its C++ mirrors")
        return 1
    wire_names = frozenset(
        name for name, declaration in declarations.items() if declaration.kind == "enum"
    )
    mirrors = [m for m in collect(roots, wire_names) if not m.local]
    local = sum(1 for m in collect(roots, wire_names) if m.local)
    declared = sum(1 for m in mirrors if m.name != m.target)
    print(
        f"{len(mirrors)} C++ enumeration(s) agree with the Mojo projection member for member "
        f"({declared} by a Mirrors comment, {len(mirrors) - declared} by name); "
        f"{local} annotated as a local vocabulary with a reason"
    )
    return 0
