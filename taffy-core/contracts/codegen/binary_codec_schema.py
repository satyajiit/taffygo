# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Validation for contract-owned deterministic binary codecs."""

from __future__ import annotations

from typing import Any, Callable

def validate_binary_codecs(
    contract: str,
    schema: dict[str, Any],
    parse_type: Callable[[str], Any],
) -> None:
    codecs = schema.get("binary_codecs", [])
    if not isinstance(codecs, list):
        raise ValueError(f"{contract}: binary_codecs must be a list")
    suffixes: set[str] = set()
    for codec in codecs:
        required = {
            "root",
            "function_suffix",
            "schema_version_field",
            "schema_version",
            "magic",
            "max_bytes_limit",
            "max_collection_items",
            "max_string_bytes",
            "types",
        }
        if not isinstance(codec, dict) or set(codec) != required:
            raise ValueError(f"{contract}: invalid binary codec declaration")
        types = codec["types"]
        if not isinstance(types, dict) or set(types) != {"enums", "structs", "unions"}:
            raise ValueError(f"{contract}: binary codec types are incomplete")
        if not all(isinstance(types[kind], list) for kind in types):
            raise ValueError(f"{contract}: binary codec type tables must be lists")
        all_types: set[str] = set()
        for kind in ("enums", "structs", "unions"):
            for item in types[kind]:
                name = item.get("name") if isinstance(item, dict) else None
                if not isinstance(name, str) or not name.isalnum() or name in all_types:
                    raise ValueError(f"{contract}: invalid or duplicate binary type {name!r}")
                all_types.add(name)
                if not isinstance(item.get("description"), str) or not item["description"].strip():
                    raise ValueError(f"{contract}: binary type {name} needs a description")
        structs = {item["name"]: item for item in types["structs"]}
        if codec["root"] not in structs:
            raise ValueError(f"{contract}: binary codec root is not a record")
        for enum in types["enums"]:
            members = enum.get("members")
            if not isinstance(members, list) or not members:
                raise ValueError(f"{contract}: binary enum {enum['name']} is empty")
            names = [member.get("name") for member in members if isinstance(member, dict)]
            wires = [member.get("wire") for member in members if isinstance(member, dict)]
            if len(names) != len(members) or len(set(names)) != len(names):
                raise ValueError(f"{contract}: binary enum {enum['name']} has invalid members")
            if wires != list(range(len(wires))):
                raise ValueError(f"{contract}: binary enum wires must be append-only")
        for union in types["unions"]:
            variants = union.get("variants")
            if not isinstance(variants, list) or not variants:
                raise ValueError(f"{contract}: binary union {union['name']} is empty")
            wires = [variant.get("wire") for variant in variants if isinstance(variant, dict)]
            if wires != list(range(len(wires))):
                raise ValueError(f"{contract}: binary union wires must be append-only")
        for owner in types["structs"] + [
            variant for union in types["unions"] for variant in union["variants"]
        ]:
            fields = owner.get("fields")
            if not isinstance(fields, list):
                raise ValueError(f"{contract}: binary record fields must be a list")
            names: set[str] = set()
            for field in fields:
                name = field.get("name") if isinstance(field, dict) else None
                if not isinstance(name, str) or name in names:
                    raise ValueError(f"{contract}: invalid binary field {name!r}")
                names.add(name)
                reference = parse_type(field.get("type", ""))
                inner = reference.inner if reference.inner is not None else reference
                if inner.kind == "named" and inner.name not in all_types:
                    raise ValueError(f"{contract}: unknown binary type {inner.name}")
        suffix = codec["function_suffix"]
        if not isinstance(suffix, str) or not suffix.replace("_", "a").isalnum():
            raise ValueError(f"{contract}: invalid binary codec function suffix")
        if suffix in suffixes:
            raise ValueError(f"{contract}: duplicate binary codec suffix {suffix}")
        suffixes.add(suffix)
        root = structs[codec["root"]]
        version_field = next(
            (field for field in root["fields"] if field["name"] == codec["schema_version_field"]),
            None,
        )
        if version_field is None or version_field["type"] != "u32":
            raise ValueError(f"{contract}: codec schema version must be a u32 root field")
        if root["fields"][0]["name"] != codec["schema_version_field"]:
            raise ValueError(
                f"{contract}: codec schema version must be the first root field"
            )
        if not isinstance(codec["schema_version"], int) or codec["schema_version"] <= 0:
            raise ValueError(f"{contract}: codec schema version must be positive")
        magic = codec["magic"]
        if not isinstance(magic, str) or not 4 <= len(magic.encode("ascii")) <= 16:
            raise ValueError(f"{contract}: codec magic must be 4..16 ASCII bytes")
        if codec["max_bytes_limit"] not in schema["limits"]:
            raise ValueError(f"{contract}: codec max_bytes_limit is not a contract limit")
        for name in ("max_collection_items", "max_string_bytes"):
            if not isinstance(codec[name], int) or codec[name] <= 0:
                raise ValueError(f"{contract}: codec {name} must be positive")
