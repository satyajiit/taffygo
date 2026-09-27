# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Validate the contract-owned CoreStatus payload codec declaration."""

from __future__ import annotations

from typing import Any

from contract_schema import ContractError, parse_type


CODEC_KEYS = {
    "root",
    "function_suffix",
    "schema_version",
    "magic",
    "max_bytes_limit",
    "string_limits",
    "list_limits",
    "u32_limits",
    "presence_rules",
    "ordered_enum_list_rules",
}


def snake(value: str) -> str:
    result: list[str] = []
    for index, character in enumerate(value):
        if character.isupper() and index:
            result.append("_")
        result.append(character.lower())
    return "".join(result)


def type_maps(schema: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    return (
        {item["name"]: item for item in schema["enums"]},
        {item["name"]: item for item in schema["structs"]},
    )


def reachable_types(
    schema: dict[str, Any], root: str
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    _enums, structs = type_maps(schema)
    seen: set[str] = set()

    def visit(name: str) -> None:
        if name in seen:
            return
        seen.add(name)
        owner = structs.get(name)
        if owner is None:
            return
        for field in owner["fields"]:
            reference = parse_type(field["type"])
            inner = reference.inner if reference.inner is not None else reference
            if inner.kind == "named":
                visit(inner.name)

    visit(root)
    return (
        [item for item in schema["enums"] if item["name"] in seen],
        [item for item in schema["structs"] if item["name"] in seen],
    )


def _field_paths(
    structs: list[dict[str, Any]], kind: str, scalar: str | None = None
) -> set[str]:
    paths: set[str] = set()
    for owner in structs:
        for field in owner["fields"]:
            reference = parse_type(field["type"])
            inner = reference.inner if reference.inner is not None else reference
            if kind == "list" and reference.kind == "list":
                paths.add(f"{owner['name']}.{field['name']}")
            if kind == "scalar" and inner.kind == "scalar" and inner.name == scalar:
                paths.add(f"{owner['name']}.{field['name']}")
    return paths


def _resolve_enum_path(
    struct_map: dict[str, Any],
    enum_map: dict[str, Any],
    element_type: str,
    path: str,
) -> str | None:
    current = element_type
    parts = path.split(".")
    if not parts or any(not part for part in parts):
        return None
    for index, part in enumerate(parts):
        owner = struct_map.get(current)
        if owner is None:
            return None
        field = next((candidate for candidate in owner["fields"] if candidate["name"] == part), None)
        if field is None:
            return None
        reference = parse_type(field["type"])
        if index == len(parts) - 1:
            return reference.name if reference.kind == "named" and reference.name in enum_map else None
        if reference.kind != "named" or reference.name not in struct_map:
            return None
        current = reference.name
    return None


def validate_codec_schema(schema: dict[str, Any]) -> dict[str, Any]:
    codec = schema.get("state_payload_codec")
    if not isinstance(codec, dict) or set(codec) != CODEC_KEYS:
        raise ContractError("core-api: invalid state_payload_codec declaration")
    root = codec["root"]
    _enums, struct_map = type_maps(schema)
    if root not in struct_map:
        raise ContractError("core-api: state payload root must be a generated record")
    if codec["function_suffix"] != "core_status_payload":
        raise ContractError("core-api: state payload function suffix is fixed")
    if not isinstance(codec["schema_version"], int) or codec["schema_version"] <= 0:
        raise ContractError("core-api: state payload schema version must be positive")
    try:
        magic = codec["magic"].encode("ascii")
    except (AttributeError, UnicodeEncodeError) as error:
        raise ContractError("core-api: state payload magic must be ASCII") from error
    if not 4 <= len(magic) <= 16:
        raise ContractError("core-api: state payload magic must be 4..16 bytes")
    limits = schema["limits"]
    if codec["max_bytes_limit"] not in limits:
        raise ContractError("core-api: state payload byte limit is not declared")
    _reachable_enums, reachable_structs = reachable_types(schema, root)
    expected_strings = _field_paths(reachable_structs, "scalar", "string")
    expected_lists = _field_paths(reachable_structs, "list")
    string_limits = codec["string_limits"]
    list_limits = codec["list_limits"]
    u32_limits = codec["u32_limits"]
    if not isinstance(string_limits, dict) or set(string_limits) != expected_strings:
        raise ContractError("core-api: every reachable state string needs one exact limit")
    if not isinstance(list_limits, dict) or set(list_limits) != expected_lists:
        raise ContractError("core-api: every reachable state list needs one exact limit")
    valid_u32 = _field_paths(reachable_structs, "scalar", "u32")
    if not isinstance(u32_limits, dict) or not set(u32_limits) <= valid_u32:
        raise ContractError("core-api: state payload u32 limit names a non-u32 field")
    for table in (string_limits, list_limits, u32_limits):
        if any(limit not in limits for limit in table.values()):
            raise ContractError("core-api: state payload field limit is not declared")
    rules = codec["presence_rules"]
    if not isinstance(rules, list):
        raise ContractError("core-api: state payload presence rules must be a list")
    enum_map, _ = type_maps(schema)
    seen: set[tuple[str, str]] = set()
    for rule in rules:
        if not isinstance(rule, dict) or set(rule) != {
            "struct",
            "tag_field",
            "field",
            "present_for",
        }:
            raise ContractError("core-api: invalid state payload presence rule")
        owner = struct_map.get(rule["struct"])
        fields = {field["name"]: field for field in owner["fields"]} if owner else {}
        tag = fields.get(rule["tag_field"])
        guarded = fields.get(rule["field"])
        identity = (rule["struct"], rule["field"])
        if identity in seen or tag is None or guarded is None:
            raise ContractError("core-api: invalid or duplicate state presence rule")
        enum = enum_map.get(tag["type"])
        if enum is None or parse_type(guarded["type"]).kind != "optional":
            raise ContractError("core-api: state presence rule has incompatible fields")
        members = {member["name"] for member in enum["members"]}
        present_for = rule["present_for"]
        if not isinstance(present_for, list) or not present_for:
            raise ContractError("core-api: state presence rule has no enum members")
        if not set(present_for) <= members:
            raise ContractError("core-api: state presence rule has unknown enum members")
        seen.add(identity)
    ordered_rules = codec["ordered_enum_list_rules"]
    if not isinstance(ordered_rules, list):
        raise ContractError("core-api: ordered enum list rules must be a list")
    seen_lists: set[tuple[str, str]] = set()
    for rule in ordered_rules:
        if not isinstance(rule, dict) or set(rule) != {
            "struct",
            "tag_field",
            "list_field",
            "enum_path",
            "enum",
            "exact_for",
            "empty_for",
        }:
            raise ContractError("core-api: invalid ordered enum list rule")
        owner = struct_map.get(rule["struct"])
        fields = {field["name"]: field for field in owner["fields"]} if owner else {}
        tag = fields.get(rule["tag_field"])
        listed = fields.get(rule["list_field"])
        identity = (rule["struct"], rule["list_field"])
        if identity in seen_lists or tag is None or listed is None:
            raise ContractError("core-api: invalid or duplicate ordered enum list rule")
        tag_enum = enum_map.get(tag["type"])
        list_reference = parse_type(listed["type"])
        element_type = (
            list_reference.inner.name
            if list_reference.kind == "list" and list_reference.inner is not None
            else None
        )
        expected_enum = rule["enum"]
        resolved_enum = (
            _resolve_enum_path(
                struct_map,
                enum_map,
                element_type,
                rule["enum_path"],
            )
            if element_type is not None
            else None
        )
        if tag_enum is None or expected_enum not in enum_map or resolved_enum != expected_enum:
            raise ContractError("core-api: ordered enum list rule has incompatible fields")
        tag_members = {member["name"] for member in tag_enum["members"]}
        exact_for = rule["exact_for"]
        empty_for = rule["empty_for"]
        if (
            not isinstance(exact_for, list)
            or not isinstance(empty_for, list)
            or not exact_for
            or set(exact_for) & set(empty_for)
            or set(exact_for) | set(empty_for) != tag_members
        ):
            raise ContractError(
                "core-api: ordered enum list rule must classify every tag member exactly once"
            )
        seen_lists.add(identity)
    return codec
