#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The Mojo projection of the contract, checked rather than generated.

Authority boundary: agreement between ``taffy-core/contracts/bip/schema`` and
the main BIP Mojo interface plus its imported identity vocabulary. Nothing
here writes a file. The Mojo definitions are hand-written on purpose — they
carry the trust-boundary reasoning a generator cannot express, and they live in
the Chromium overlay where a reader needs that reasoning in front of them — so
the thing that must be mechanical is the *comparison*, not the text.

Why this module exists at all: work package WP-M2-01 is verified by
"name-for-name agreement with `taffy-core/contracts/bip/schema/`". A claim in a comment is
not agreement; this is what turns it into a gate the docs host can run, with no
Chromium checkout and no build.

Three questions are answered, and a declaration that answers none of them is a
finding:

1. A Mojo enumeration or structure whose name matches a schema definition must
   project it exactly: the same members, in the same order, spelled by the one
   rule in :mod:`naming` (``STALE_PAGE_EPOCH`` -> ``kStalePageEpoch``), and the
   same field names in the same order for a structure.
2. A Mojo declaration with no schema definition of that name must say why, in
   the file itself, with a ``bip-transport-only:`` annotation. Those are the
   types that exist because Mojo needs a reply message where the schema needs
   none.
3. A schema definition with no Mojo projection must be listed in the file's
   ``bip-not-projected:`` block with a reason. Those are the types that never
   cross the renderer boundary.

Both annotations live in the ``.mojom`` file rather than in this checker, so a
new exception is a visible edit to the contract surface, reviewed by whoever
owns that surface, and not a line in a tool nobody reads.
"""

from __future__ import annotations

import os
import re

from layout import CONTRACT_DIR, relative
from naming import pascal_case

#: The main Mojo interface, retained as a public name for focused checker tests.
MOJOM_PATH = os.path.join(CONTRACT_DIR, "mojom", "page_intelligence.mojom")

#: Every hand-written file that together projects the one BIP schema.
MOJOM_PATHS = (
    os.path.join(CONTRACT_DIR, "mojom", "page_intelligence_identity.mojom"),
    MOJOM_PATH,
)

#: ``// bip-transport-only: <reason>`` above a declaration.
_TRANSPORT_ONLY = re.compile(r"^\s*//\s*bip-transport-only:\s*(?P<reason>.+?)\s*$")

#: ``// bip-not-projected: <Name> — <reason>`` anywhere in the file.
_NOT_PROJECTED = re.compile(
    r"^\s*//\s*bip-not-projected:\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*[-—:]\s*(?P<reason>.+?)\s*$"
)

_DECLARATION = re.compile(r"^(?P<kind>enum|struct|union)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\{")
_ENUM_MEMBER = re.compile(r"^\s*(?P<name>k[A-Za-z0-9_]*)\s*(=\s*[^,]+)?,\s*$")
_STRUCT_FIELD = re.compile(
    r"^\s*(?:\[[^\]]*\]\s*)?(?P<type>[A-Za-z_][A-Za-z0-9_<>., ]*?)(?P<nullable>\?)?"
    r"\s+(?P<name>[a-z_][a-z0-9_]*)\s*(=\s*[^;]+)?;\s*$"
)


class Declaration:
    """One ``enum`` or ``struct`` as the Mojo file spells it."""

    def __init__(self, kind: str, name: str, line: int, transport_only: str | None) -> None:
        self.kind = kind
        self.name = name
        self.line = line
        self.transport_only = transport_only
        self.members: list[str] = []

    def __repr__(self) -> str:  # pragma: no cover - diagnostics only
        return f"<{self.kind} {self.name} at line {self.line}>"


def parse(path: str) -> tuple[dict[str, Declaration], dict[str, str]]:
    """Return the declarations in ``path`` and its ``bip-not-projected`` block.

    A hand-rolled reader rather than the Mojo parser, which lives in a Chromium
    checkout this host does not have. It understands exactly the subset this
    contract uses — no unions in use, no imports, no interface bodies compared —
    and treats a declaration body it cannot read as a finding rather than as an
    empty one.
    """
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().split("\n")

    declarations: dict[str, Declaration] = {}
    not_projected: dict[str, str] = {}
    pending_reason: str | None = None
    current: Declaration | None = None

    for index, raw in enumerate(lines, start=1):
        skipped = _NOT_PROJECTED.match(raw)
        if skipped:
            not_projected[skipped.group("name")] = skipped.group("reason")
            continue

        if current is None:
            annotation = _TRANSPORT_ONLY.match(raw)
            if annotation:
                pending_reason = annotation.group("reason")
                continue
            declaration = _DECLARATION.match(raw)
            if declaration:
                current = Declaration(
                    declaration.group("kind"),
                    declaration.group("name"),
                    index,
                    pending_reason,
                )
                pending_reason = None
                continue
            if raw.strip() and not raw.lstrip().startswith("//"):
                pending_reason = None
            continue

        if raw.startswith("};"):
            declarations[current.name] = current
            current = None
            continue
        if not raw.strip() or raw.lstrip().startswith("//"):
            continue
        member = _ENUM_MEMBER.match(raw) if current.kind == "enum" else _STRUCT_FIELD.match(raw)
        if member:
            name = member.group("name")
            if current.kind == "struct" and member.group("nullable"):
                name = f"{name}?"
            current.members.append(name)

    return declarations, not_projected


def parse_projection() -> tuple[dict[str, Declaration], dict[str, str]]:
    """Return the combined declarations from the main file and its import."""
    declarations: dict[str, Declaration] = {}
    not_projected: dict[str, str] = {}
    for path in MOJOM_PATHS:
        file_declarations, file_not_projected = parse(path)
        declarations.update(file_declarations)
        not_projected.update(file_not_projected)
    return declarations, not_projected


def _expected_enum_members(values: list[str]) -> list[str]:
    return [f"k{pascal_case(value)}" for value in values]


def _expected_struct_fields(contract, node: dict) -> list[str]:
    """The Mojo field list a schema object requires, absence marked with ``?``.

    A field the schema does not list in ``required`` is nullable on this wire —
    including an array and including a bool, because Mojo carries optional
    numerics and nullable arrays. The one exception is a property the schema
    marks ``x-bip-absent-as-empty``: there the schema has said that an absent
    list and an empty list are the same statement, so a plain ``array<T>`` is
    the honest projection and a nullable one would invent a distinction the
    contract does not make.
    """
    required = set(node.get("required", []))
    fields = []
    for name in node["properties"]:
        optional = name not in required and not contract.absent_as_empty(node, name)
        fields.append(f"{name}?" if optional else name)
    return fields


def _compare(kind: str, name: str, expected: list[str], found: list[str]) -> list[str]:
    if expected == found:
        return []
    findings = []
    missing = [m for m in expected if m not in found]
    extra = [m for m in found if m not in expected]
    for member in missing:
        findings.append(f"{name}: the schema has {member!r}; the Mojo {kind} does not")
    for member in extra:
        findings.append(f"{name}: the Mojo {kind} has {member!r}; the schema does not")
    if not missing and not extra:
        findings.append(
            f"{name}: same {kind} members, different order — "
            f"schema {expected}, Mojo {found}"
        )
    return findings


def check(contract, path: str | None = None) -> list[str]:
    """Findings, empty when the Mojo file projects the schema exactly."""
    paths = (path,) if path is not None else MOJOM_PATHS
    missing = [candidate for candidate in paths if not os.path.exists(candidate)]
    if missing:
        return [
            f"{relative(candidate)}: the Mojo projection of the contract is missing"
            for candidate in missing
        ]

    if path is not None:
        declarations, not_projected = parse(path)
    else:
        declarations, not_projected = parse_projection()
    findings: list[str] = []

    for name in sorted(contract.defs):
        node = contract.defs[name]
        kind = contract.kind_of(name)
        declaration = declarations.get(name)
        if declaration is None:
            if name in not_projected:
                continue
            if kind == "newtype":
                # Scalar newtypes are projected inline as their underlying type
                # by every field that uses them, so they need no declaration and
                # no exemption: a TabId is a string on this wire.
                continue
            findings.append(
                f"{name}: the schema defines this {kind} and the Mojo file neither "
                "projects it nor lists it in a bip-not-projected annotation"
            )
            continue
        if declaration.transport_only:
            findings.append(
                f"{name}: annotated bip-transport-only at line {declaration.line}, but the "
                "schema defines a type of that name — one of the two is wrong"
            )
            continue
        if kind == "enum":
            if declaration.kind != "enum":
                findings.append(f"{name}: the schema calls this an enum; Mojo declares a {declaration.kind}")
                continue
            findings += _compare("enum", name, _expected_enum_members(node["enum"]), declaration.members)
        elif kind == "object":
            if declaration.kind != "struct":
                findings.append(f"{name}: the schema calls this an object; Mojo declares a {declaration.kind}")
                continue
            findings += _compare(
                "struct", name, _expected_struct_fields(contract, node), declaration.members
            )
        else:
            findings.append(
                f"{name}: the schema calls this a scalar; Mojo declares a {declaration.kind}, "
                "which makes an identifier a message"
            )

    for name in sorted(declarations):
        declaration = declarations[name]
        if name in contract.defs:
            continue
        if not declaration.transport_only:
            findings.append(
                f"{name}: declared at line {declaration.line} with no schema definition of that "
                "name and no bip-transport-only annotation saying why it exists"
            )
        if not declaration.members:
            findings.append(f"{name}: declared at line {declaration.line} with no members this checker could read")

    for name in sorted(not_projected):
        if name not in contract.defs:
            findings.append(
                f"{name}: listed as bip-not-projected, but no schema definition has that name"
            )
        elif name in declarations:
            findings.append(
                f"{name}: listed as bip-not-projected and also declared at line "
                f"{declarations[name].line}"
            )

    return findings


def report(contract, path: str | None = None) -> int:
    """Print the findings; return a process exit status."""
    findings = check(contract, path)
    description = (
        relative(path)
        if path is not None
        else ", ".join(relative(candidate) for candidate in MOJOM_PATHS)
    )
    for finding in findings:
        print(f"mojom: {finding}")
    if findings:
        print()
        print(f"{len(findings)} disagreement(s) between the schema and {description}")
        return 1
    if path is not None:
        declarations, not_projected = parse(path)
    else:
        declarations, not_projected = parse_projection()
    projected = sum(1 for name in declarations if name in contract.defs)
    transport = sum(1 for d in declarations.values() if d.transport_only)
    print(
        f"{description} project {projected} schema definition(s) name for name; "
        f"{transport} transport-only and {len(not_projected)} not-projected exception(s), each with a reason"
    )
    return 0
