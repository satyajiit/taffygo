# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Golden and compatibility fixture validation for generated contracts.

Every refusal here carries the verdict a conforming decoder must reach, not
just a message. A golden fixture treats any refusal as a failure; a
compatibility fixture is a deliberate deviation and its whole point is *which*
refusal it draws, so the same traversal has to answer both questions. Keeping
one traversal is deliberate: a second one written for the compatibility corpus
would be free to disagree with the rules the goldens are held to, and then the
corpus would be measuring itself.
"""

from __future__ import annotations

import json
from typing import Any

from generate import CONTRACT_ROOT, ContractError, TypeRef, parse_type, tagged_union_for


class DocumentRefusal(ContractError):
    """One refusal of a fixture document, carrying its verdict."""

    def __init__(self, verdict: str, message: str) -> None:
        super().__init__(message)
        self.verdict = verdict


def _malformed(message: str) -> DocumentRefusal:
    return DocumentRefusal("REJECT_MALFORMED", message)


def _validate_value(
    value: Any,
    reference: TypeRef,
    enums: dict[str, set[int]],
    structs: dict[str, dict[str, Any]],
    owner: str,
) -> None:
    if reference.kind == "optional":
        if value is not None:
            _validate_value(value, reference.inner, enums, structs, owner)
        return
    if reference.kind == "list":
        if not isinstance(value, list):
            raise _malformed(f"{owner}: expected list")
        for index, item in enumerate(value):
            _validate_value(item, reference.inner, enums, structs, f"{owner}[{index}]")
        return
    if reference.name in enums:
        if not isinstance(value, int) or value not in enums[reference.name]:
            raise DocumentRefusal(
                "REJECT_CLOSED_ENUM", f"{owner}: unknown closed enum value"
            )
        return
    if reference.name in structs:
        _validate_instance(value, structs[reference.name], enums, structs, owner)
        return
    checks = {
        "bool": lambda item: isinstance(item, bool),
        "bytes": lambda item: isinstance(item, list)
        and all(isinstance(part, int) and 0 <= part <= 255 for part in item),
        "bytes32": lambda item: isinstance(item, list)
        and len(item) == 32
        and all(isinstance(part, int) and 0 <= part <= 255 for part in item),
        "i64": lambda item: isinstance(item, int) and not isinstance(item, bool),
        "string": lambda item: isinstance(item, str),
        "u32": lambda item: isinstance(item, int)
        and not isinstance(item, bool)
        and 0 <= item <= 0xFFFF_FFFF,
        "u64": lambda item: isinstance(item, int) and not isinstance(item, bool) and item >= 0,
    }
    if not checks[reference.name](value):
        raise _malformed(f"{owner}: wrong scalar type for {reference.name}")


def _validate_instance(
    value: Any,
    struct: dict[str, Any],
    enums: dict[str, set[int]],
    structs: dict[str, dict[str, Any]],
    owner: str,
) -> None:
    if not isinstance(value, dict):
        raise _malformed(f"{owner}: expected object")
    expected = {field["name"] for field in struct["fields"]}
    if set(value) != expected:
        raise _malformed(
            f"{owner}: fields differ: expected {sorted(expected)}, got {sorted(value)}"
        )
    for field in struct["fields"]:
        _validate_value(
            value[field["name"]],
            parse_type(field["type"]),
            enums,
            structs,
            f"{owner}.{field['name']}",
        )


def _validate_tagged_union(
    value: dict[str, Any],
    type_name: str,
    schema: dict[str, Any],
    owner: str,
) -> None:
    union = tagged_union_for(schema, type_name)
    if union is None:
        return
    struct = next(item for item in schema["structs"] if item["name"] == type_name)
    tag_type = next(
        field["type"] for field in struct["fields"] if field["name"] == union["tag_field"]
    )
    tag_enum = next(item for item in schema["enums"] if item["name"] == tag_type)
    tag_by_wire = {member["wire"]: member["name"] for member in tag_enum["members"]}
    selected_tag = tag_by_wire[value[union["tag_field"]]]
    present = [variant for variant in union["variants"] if value[variant["field"]] is not None]
    if len(present) != 1 or present[0]["tag"] != selected_tag:
        raise _malformed(f"{owner}: exactly one body must match {selected_tag}")


def _validate_cross_field_rules(
    value: dict[str, Any],
    type_name: str,
    schema: dict[str, Any],
    owner: str,
) -> None:
    struct = next(item for item in schema["structs"] if item["name"] == type_name)
    fields = {field["name"]: field for field in struct["fields"]}
    enums = {item["name"]: item for item in schema["enums"]}
    for pairing in schema.get("enum_pairings", []):
        if pairing["struct"] != type_name:
            continue
        left_type = fields[pairing["left_field"]]["type"]
        right_type = fields[pairing["right_field"]]["type"]
        left_by_wire = {
            member["wire"]: member["name"] for member in enums[left_type]["members"]
        }
        right_by_wire = {
            member["wire"]: member["name"] for member in enums[right_type]["members"]
        }
        selected_left = left_by_wire[value[pairing["left_field"]]]
        selected_right = right_by_wire[value[pairing["right_field"]]]
        allowed = next(
            pair["right"] for pair in pairing["pairs"] if pair["left"] == selected_left
        )
        if selected_right not in allowed:
            raise _malformed(f"{owner}: {selected_left} cannot execute {selected_right}")
    for rule in schema.get("conditional_presence", []):
        if rule["struct"] != type_name:
            continue
        tag_type = fields[rule["tag_field"]]["type"]
        tag_by_wire = {
            member["wire"]: member["name"] for member in enums[tag_type]["members"]
        }
        selected_tag = tag_by_wire[value[rule["tag_field"]]]
        should_be_present = selected_tag in rule["present_for"]
        if (value[rule["field"]] is not None) != should_be_present:
            raise _malformed(
                f"{owner}: {rule['field']} presence does not match {selected_tag}"
            )


def verify_fixtures(contract: str, schema: dict[str, Any]) -> None:
    """Validate every indexed golden value and compatibility verdict."""
    enums = {
        item["name"]: {member["wire"] for member in item["members"]}
        for item in schema["enums"]
    }
    structs = {item["name"]: item for item in schema["structs"]}
    index_path = CONTRACT_ROOT / contract / "golden" / "index.json"
    index = json.loads(index_path.read_text(encoding="utf-8"))
    if not isinstance(index, list) or not index:
        raise ContractError(f"{contract}: golden/index.json must be a non-empty list")
    for entry in index:
        filename = entry.get("file")
        type_name = entry.get("type")
        if type_name not in structs or not isinstance(filename, str):
            raise ContractError(f"{contract}: invalid golden index entry")
        value = json.loads((index_path.parent / filename).read_text(encoding="utf-8"))
        _validate_instance(value, structs[type_name], enums, structs, filename)
        _validate_tagged_union(value, type_name, schema, filename)
        _validate_cross_field_rules(value, type_name, schema, filename)


def document_verdict(schema: dict[str, Any], type_name: str, value: Any) -> str:
    """The verdict this contract's own shape rules reach for one document."""
    enums = {
        item["name"]: {member["wire"] for member in item["members"]}
        for item in schema["enums"]
    }
    structs = {item["name"]: item for item in schema["structs"]}
    if type_name not in structs:
        raise ContractError(f"no record named {type_name}")
    try:
        _validate_instance(value, structs[type_name], enums, structs, type_name)
        _validate_tagged_union(value, type_name, schema, type_name)
        _validate_cross_field_rules(value, type_name, schema, type_name)
    except DocumentRefusal as refusal:
        return refusal.verdict
    return "ACCEPT"
