# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Golden fixture encoding and parity checks for the CoreStatus codec."""

from __future__ import annotations

import copy
import json
import struct
from pathlib import Path
from typing import Any

from contract_schema import ContractError, TypeRef, parse_type
from status_codec_rust import render_status_codec
from status_codec_schema import reachable_types, type_maps, validate_codec_schema


def _validate_presence(
    owner: dict[str, Any],
    value: dict[str, Any],
    codec: dict[str, Any],
    enums: dict[str, Any],
) -> None:
    fields = {field["name"]: field for field in owner["fields"]}
    for rule in codec["presence_rules"]:
        if rule["struct"] != owner["name"]:
            continue
        tag_type = fields[rule["tag_field"]]["type"]
        names = {
            member["wire"]: member["name"] for member in enums[tag_type]["members"]
        }
        tag = names.get(value[rule["tag_field"]])
        present = value[rule["field"]] is not None
        if tag is None or present != (tag in rule["present_for"]):
            raise ContractError(f"{owner['name']}: invalid {rule['field']} presence")
    for rule in codec["ordered_enum_list_rules"]:
        if rule["struct"] != owner["name"]:
            continue
        tag_type = fields[rule["tag_field"]]["type"]
        tag_names = {
            member["wire"]: member["name"] for member in enums[tag_type]["members"]
        }
        tag = tag_names.get(value[rule["tag_field"]])
        if tag is None:
            continue
        listed = value[rule["list_field"]]
        if tag in rule["empty_for"] and listed:
            raise ContractError(f"{owner['name']}: {rule['list_field']} must be empty")
        if tag not in rule["exact_for"]:
            continue
        expected = [member["wire"] for member in enums[rule["enum"]]["members"]]

        def at_path(item: Any) -> Any:
            for part in rule["enum_path"].split("."):
                item = item[part]
            return item

        actual = [at_path(item) for item in listed]
        if actual != expected:
            raise ContractError(
                f"{owner['name']}: {rule['list_field']} must contain the exact enum sequence"
            )


def encode_fixture(schema: dict[str, Any], value: dict[str, Any]) -> bytes:
    codec = validate_codec_schema(schema)
    enums, structs = type_maps(schema)
    output = bytearray(codec["magic"].encode("ascii"))
    output += struct.pack("<I", codec["schema_version"])

    def encode(reference: TypeRef, item: Any, path: str) -> None:
        if reference.kind == "optional":
            output.append(0 if item is None else 1)
            if item is not None:
                encode(reference.inner, item, path)
            return
        if reference.kind == "list":
            limit = schema["limits"][codec["list_limits"][path]]
            if not isinstance(item, list) or len(item) > limit:
                raise ContractError(f"{path}: collection limit")
            output.extend(struct.pack("<I", len(item)))
            for member in item:
                encode(reference.inner, member, path)
            return
        if reference.kind == "named" and reference.name in enums:
            wires = {member["wire"] for member in enums[reference.name]["members"]}
            if item not in wires:
                raise ContractError(f"{path}: closed enum")
            output.extend(struct.pack("<I", item))
            return
        if reference.kind == "named":
            owner = structs[reference.name]
            _validate_presence(owner, item, codec, enums)
            for field in owner["fields"]:
                field_path = f"{owner['name']}.{field['name']}"
                encode(parse_type(field["type"]), item[field["name"]], field_path)
            return
        if reference.name == "string":
            encoded = item.encode("utf-8")
            limit = schema["limits"][codec["string_limits"][path]]
            if len(encoded) > limit:
                raise ContractError(f"{path}: string limit")
            output.extend(struct.pack("<I", len(encoded)))
            output.extend(encoded)
        elif reference.name == "bool":
            output.append(1 if item else 0)
        elif reference.name == "u32":
            if path in codec["u32_limits"]:
                limit = schema["limits"][codec["u32_limits"][path]]
                if item > limit:
                    raise ContractError(f"{path}: value limit")
            output.extend(struct.pack("<I", item))
        elif reference.name == "u64":
            output.extend(struct.pack("<Q", item))
        else:
            raise ContractError(f"{path}: unsupported fixture scalar {reference.name}")

    root = structs[codec["root"]]
    _validate_presence(root, value, codec, enums)
    for field in root["fields"]:
        path = f"{root['name']}.{field['name']}"
        encode(parse_type(field["type"]), value[field["name"]], path)
    if len(output) > schema["limits"][codec["max_bytes_limit"]]:
        raise ContractError("CoreStatus: payload size limit")
    return bytes(output)


def verify_language_parity(schema: dict[str, Any], contract_root: Path) -> None:
    codec = validate_codec_schema(schema)
    enums, structs = reachable_types(schema, codec["root"])
    outputs = {
        "Rust": (contract_root / "generated/rust/core_api.rs").read_text(
            encoding="utf-8"
        ),
        "Kotlin": (contract_root / "generated/kotlin/CoreApi.kt").read_text(
            encoding="utf-8"
        ),
        "TypeScript": (
            contract_root / "generated/typescript/core_api.ts"
        ).read_text(encoding="utf-8"),
    }
    for enum in enums:
        markers = {
            "Rust": f"pub enum {enum['name']} {{",
            "Kotlin": f"enum class {enum['name']}",
            "TypeScript": f"export enum {enum['name']} {{",
        }
        for language, marker in markers.items():
            if marker not in outputs[language]:
                raise ContractError(f"core-api: {language} misses enum {enum['name']}")
    for owner in structs:
        type_markers = {
            "Rust": f"pub struct {owner['name']} {{",
            "Kotlin": f"data class {owner['name']}(",
            "TypeScript": f"export interface {owner['name']} {{",
        }
        for language, marker in type_markers.items():
            if marker not in outputs[language]:
                raise ContractError(f"core-api: {language} misses record {owner['name']}")
        for field in owner["fields"]:
            field_markers = {
                "Rust": f"pub {field['name']}:",
                "Kotlin": f"val {field['name']}:",
                "TypeScript": f"readonly {field['name']}:",
            }
            for language, marker in field_markers.items():
                if marker not in outputs[language]:
                    raise ContractError(
                        f"core-api: {language} misses {owner['name']}.{field['name']}"
                    )


def self_test(schema: dict[str, Any], fixture: dict[str, Any]) -> None:
    rendered = render_status_codec(schema)
    for marker in (
        "CORE_STATUS_PAYLOAD_SCHEMA_VERSION",
        "encode_core_status_payload",
        "measure_core_status_payload",
        "decode_core_status_payload",
        "TrailingBytes",
    ):
        if marker not in rendered:
            raise ContractError(f"core-api codec self-test misses {marker}")
    if not encode_fixture(schema, fixture):
        raise ContractError("core-api codec self-test produced no bytes")
    broken = copy.deepcopy(schema)
    del broken["state_payload_codec"]["string_limits"]["TaskViewState.goal"]
    try:
        validate_codec_schema(broken)
    except ContractError:
        pass
    else:
        raise ContractError("core-api codec self-test accepted an unbounded string")
    oversized = copy.deepcopy(fixture)
    oversized["active_tasks"][0]["goal"] = "x" * (
        schema["limits"]["MAX_TASK_GOAL_BYTES"] + 1
    )
    try:
        encode_fixture(schema, oversized)
    except ContractError:
        pass
    else:
        raise ContractError("core-api codec self-test accepted an oversized goal")


def load_fixture(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ContractError(f"{path}: fixture must be an object")
    return value
