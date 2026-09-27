#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The schema, loaded and indexed.

Authority boundary: the intermediate representation every renderer reads. It
knows what a definition *is* — an enumeration, a structure, or a scalar
newtype — and refuses to classify one it does not recognise. It names the Rust
spelling of a scalar because Rust is the one language generated from this
schema; it knows nothing about how a definition is laid out on the page.
"""

from __future__ import annotations

import copy
import re

import jsonschema_mini
from layout import GeneratorError

#: A ``major.minor`` protocol version, the only form the contract uses.
VERSION_PATTERN = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")


def version_tuple(text: str) -> tuple[int, int]:
    """``"0.2"`` -> ``(0, 2)``, so two versions can be ordered.

    Raises rather than guessing: a version this function cannot read is a
    contract defect, and returning a plausible tuple for it would let a
    compatibility check pass by comparing the wrong numbers.
    """
    match = VERSION_PATTERN.match(text)
    if not match:
        raise GeneratorError(f"{text!r} is not a major.minor protocol version")
    return int(match.group(1)), int(match.group(2))


def _added_in_findings(name: str, node: dict) -> list[str]:
    """Rules for ``x-bip-added-in`` on an enumeration definition.

    The annotation records the protocol version at which each member arrived,
    for the members that did not exist from the beginning. It is what lets a
    compatibility fixture ask the question that matters about an additive
    step — what a reader at the *previous* minor version does with a message
    naming a new member — without a hand-copied snapshot of the old schema
    going stale beside the real one.
    """
    added_in = node.get("x-bip-added-in")
    if added_in is None:
        return []
    if not isinstance(added_in, dict):
        return [f"{name}: x-bip-added-in must map a member to the version it arrived at"]
    findings: list[str] = []
    for member, version in sorted(added_in.items()):
        if member not in node["enum"]:
            findings.append(
                f"{name}: x-bip-added-in names {member!r}, which is not a member"
            )
        if not isinstance(version, str) or not VERSION_PATTERN.match(version):
            findings.append(
                f"{name}: x-bip-added-in gives {member!r} the version {version!r}, "
                "which is not a major.minor protocol version"
            )
    return findings


class Contract:
    """Every definition in every schema document, indexed by name."""

    def __init__(self, schema_dir: str) -> None:
        self.store = jsonschema_mini.SchemaStore.from_directory(schema_dir)
        self.modules: dict[str, str] = {}
        self.owner: dict[str, str] = {}
        self.defs: dict[str, dict] = {}
        for document_name in sorted(self.store.documents):
            document = self.store.documents[document_name]
            module = document.get("x-bip-module")
            if not module:
                raise GeneratorError(f"{document_name} has no x-bip-module")
            self.modules[document_name] = module
            for name, node in document.get("$defs", {}).items():
                if name in self.owner:
                    raise GeneratorError(
                        f"definition {name!r} is declared in both "
                        f"{self.owner[name]} and {document_name}; names must be unique "
                        "across the contract so generated modules never collide"
                    )
                self.owner[name] = document_name
                self.defs[name] = node

    def module_of(self, name: str) -> str:
        return self.modules[self.owner[name]]

    def names_in(self, document_name: str) -> list[str]:
        return sorted(self.store.documents[document_name].get("$defs", {}))

    def reader_view(
        self, protocol_version: str
    ) -> tuple[jsonschema_mini.SchemaStore, dict[str, list[str]]]:
        """The contract as a consumer speaking ``protocol_version`` knows it.

        Every enumeration member that ``x-bip-added-in`` dates later than the
        reader's version is removed, because that reader was built before the
        member existed. Returns the pruned store and, per enumeration, the
        members the reader does not have.

        This is how an additive step is proved additive rather than asserted:
        the previous version's view is derived from the one schema on disk, so
        it cannot go stale beside it, and a step that removed or repurposed
        something would show up here as a reader view that still accepts a
        message it should not.
        """
        reader = version_tuple(protocol_version)
        documents = copy.deepcopy(self.store.documents)
        pruned: dict[str, list[str]] = {}
        for document in documents.values():
            for name, node in document.get("$defs", {}).items():
                added_in = node.get("x-bip-added-in")
                if not added_in:
                    continue
                removed = [
                    member
                    for member in node["enum"]
                    if member in added_in and version_tuple(added_in[member]) > reader
                ]
                if not removed:
                    continue
                remaining = [m for m in node["enum"] if m not in removed]
                if not remaining:
                    raise GeneratorError(
                        f"{name}: a reader at {protocol_version} would know no member of "
                        "this enumeration, so every message carrying it would be rejected "
                        "for the wrong reason"
                    )
                node["enum"] = remaining
                node["x-bip-added-in"] = {
                    member: version
                    for member, version in added_in.items()
                    if member not in removed
                }
                pruned[name] = removed
        return jsonschema_mini.SchemaStore(documents), pruned

    def ref_name(self, ref: str) -> str:
        _, _, pointer = ref.partition("#")
        if not pointer.startswith("/$defs/"):
            raise GeneratorError(f"only #/$defs/Name references are generated, got {ref!r}")
        name = pointer[len("/$defs/") :]
        if name not in self.defs:
            raise GeneratorError(f"$ref {ref!r} names an unknown definition")
        return name

    def absent_as_empty(self, node: dict, field: str) -> bool:
        """Whether an omitted array property means the same as an empty one.

        Marked ``x-bip-absent-as-empty`` in the schema. It exists because two
        different statements were being spelled the same way: for most optional
        properties "the field is absent" says something ("this precondition
        constrains no origin") that "the field is present and empty" does not
        ("no origin is allowed"), and for a handful of list-valued properties it
        says nothing at all. Marking the second group lets every binding drop an
        option that no consumer could ever act on, and lets the Mojo projection
        carry a plain array rather than a nullable one, without any surface
        quietly deciding for itself which group a field is in.
        """
        return node.get("properties", {}).get(field, {}).get("x-bip-absent-as-empty") is True

    def kind_of(self, name: str) -> str:
        node = self.defs[name]
        if "enum" in node:
            return "enum"
        if "properties" in node:
            return "object"
        if node.get("type") in ("string", "integer", "number", "boolean"):
            return "newtype"
        raise GeneratorError(f"definition {name!r} is neither an enum, an object, nor a scalar")

    def audit(self) -> list[str]:
        """Contract rules the schema must satisfy before anything is generated."""
        findings: list[str] = []
        for name in sorted(self.defs):
            node = self.defs[name]
            kind = self.kind_of(name)
            if not node.get("description"):
                findings.append(f"{name}: no description")
            required = set(node.get("required", []))
            for field, property_node in node.get("properties", {}).items():
                if property_node.get("x-bip-absent-as-empty") is not True:
                    continue
                if property_node.get("type") != "array":
                    findings.append(
                        f"{name}.{field}: x-bip-absent-as-empty marks a property that is not "
                        "an array; only a list can decode an absence to an empty value"
                    )
                if field in required:
                    findings.append(
                        f"{name}.{field}: x-bip-absent-as-empty marks a required property, "
                        "which can never be absent"
                    )
            if kind != "enum":
                if "x-bip-added-in" in node:
                    findings.append(
                        f"{name}: x-bip-added-in records when an enumeration member arrived, "
                        f"and this definition is a {kind}"
                    )
                continue
            findings += _added_in_findings(name, node)
            if node.get("x-bip-closed") is not True:
                findings.append(f"{name}: an enumeration must be marked x-bip-closed")
            if "fail closed" not in node.get("description", ""):
                findings.append(
                    f"{name}: the description must state that an unknown member is "
                    "unsupported and fails closed"
                )
            for value in node.get("x-bip-reserved", []):
                if value not in node["enum"]:
                    findings.append(f"{name}: reserved value {value!r} is not a member")
            if node.get("x-bip-reserved") and not node.get("x-bip-reserved-milestone"):
                findings.append(f"{name}: reserved values need x-bip-reserved-milestone")
        return findings


def scalar_type(node: dict) -> str:
    """The Rust spelling of a scalar schema node."""
    schema_type = node.get("type")
    if schema_type == "string":
        return "String"
    if schema_type == "boolean":
        return "bool"
    if schema_type == "integer":
        return "u64" if node.get("minimum", -1) >= 0 else "i64"
    if schema_type == "number":
        return "f64"
    raise GeneratorError(f"unsupported scalar type {schema_type!r}")


def field_type(node: dict, contract: Contract, uses: set[str]) -> str:
    """The Rust spelling of a property schema node, recording cross-module uses."""
    if "$ref" in node:
        name = contract.ref_name(node["$ref"])
        uses.add(name)
        return name
    if node.get("type") == "array":
        items = node.get("items")
        if items is None:
            raise GeneratorError("an array property must declare items")
        return f"Vec<{field_type(items, contract, uses)}>"
    return scalar_type(node)


def property_doc(node: dict, contract: Contract) -> str:
    if node.get("description"):
        return node["description"]
    if "$ref" in node:
        return contract.defs[contract.ref_name(node["$ref"])].get("description", "")
    return ""
