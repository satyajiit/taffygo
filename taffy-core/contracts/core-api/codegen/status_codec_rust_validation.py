# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Render semantic validation shared by Rust CoreStatus encode paths."""

from __future__ import annotations

from typing import Any

from contract_renderers import pascal


def render_validation(
    owner: dict[str, Any], schema: dict[str, Any], codec: dict[str, Any], indent: str
) -> list[str]:
    return _presence_validation(owner, codec, indent) + _ordered_list_validation(
        owner, schema, codec, indent
    )


def _presence_validation(
    owner: dict[str, Any], codec: dict[str, Any], indent: str
) -> list[str]:
    fields = {field["name"]: field for field in owner["fields"]}
    lines: list[str] = []
    for rule in codec["presence_rules"]:
        if rule["struct"] != owner["name"]:
            continue
        tag_type = fields[rule["tag_field"]]["type"]
        members = " | ".join(
            f"{tag_type}::{pascal(member)}" for member in rule["present_for"]
        )
        lines += [
            f"{indent}if matches!(value.{rule['tag_field']}, {members})",
            f"{indent}    != value.{rule['field']}.is_some()",
            f"{indent}{{",
            f"{indent}    return Err(CoreStatusPayloadCodecError::Malformed);",
            f"{indent}}}",
        ]
    return lines


def _ordered_list_validation(
    owner: dict[str, Any], schema: dict[str, Any], codec: dict[str, Any], indent: str
) -> list[str]:
    enum_map = {item["name"]: item for item in schema["enums"]}
    fields = {field["name"]: field for field in owner["fields"]}
    lines: list[str] = []
    for rule in codec["ordered_enum_list_rules"]:
        if rule["struct"] != owner["name"]:
            continue
        tag_type = fields[rule["tag_field"]]["type"]
        enum_type = rule["enum"]
        exact_members = " | ".join(
            f"{tag_type}::{pascal(member)}" for member in rule["exact_for"]
        )
        expected = ", ".join(
            f"{enum_type}::{pascal(member['name'])}"
            for member in enum_map[enum_type]["members"]
        )
        item_path = ".".join(["item", *rule["enum_path"].split(".")])
        lines += [
            f"{indent}if matches!(value.{rule['tag_field']}, {exact_members}) {{",
            f"{indent}    let expected = [{expected}];",
            f"{indent}    if value.{rule['list_field']}.len() != expected.len()",
            f"{indent}        || value.{rule['list_field']}",
            f"{indent}            .iter()",
            f"{indent}            .zip(expected)",
            f"{indent}            .any(|(item, expected)| {item_path} != expected)",
            f"{indent}    {{",
            f"{indent}        return Err(CoreStatusPayloadCodecError::Malformed);",
            f"{indent}    }}",
            f"{indent}}}",
        ]
        if rule["empty_for"]:
            empty_members = " | ".join(
                f"{tag_type}::{pascal(member)}" for member in rule["empty_for"]
            )
            lines += [
                f"{indent}if matches!(value.{rule['tag_field']}, {empty_members})",
                f"{indent}    && !value.{rule['list_field']}.is_empty()",
                f"{indent}{{",
                f"{indent}    return Err(CoreStatusPayloadCodecError::Malformed);",
                f"{indent}}}",
            ]
    return lines
