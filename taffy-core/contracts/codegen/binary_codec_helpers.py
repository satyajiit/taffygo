# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Type traversal and field-expression helpers for Rust binary codecs."""

from __future__ import annotations

from typing import Any, Callable

def _snake(value: str) -> str:
    result: list[str] = []
    for index, character in enumerate(value):
        if character.isupper() and index:
            result.append("_")
        result.append(character.lower())
    return "".join(result)


def _rust_type(reference: Any) -> str:
    if reference.kind == "optional":
        return f"Option<{_rust_type(reference.inner)}>"
    if reference.kind == "list":
        return f"Vec<{_rust_type(reference.inner)}>"
    mapping = {
        "bool": "bool",
        "bytes": "Vec<u8>",
        "bytes32": "[u8; 32]",
        "i64": "i64",
        "string": "String",
        "u32": "u32",
        "u64": "u64",
    }
    return mapping.get(reference.name, reference.name)


def _reachable_types(
    codec: dict[str, Any],
    root: str,
    parse_type: Callable[[str], Any],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]:
    types = codec["types"]
    enums = {item["name"]: item for item in types["enums"]}
    structs = {item["name"]: item for item in types["structs"]}
    unions = {item["name"]: item for item in types["unions"]}
    seen: set[str] = set()

    def visit(name: str) -> None:
        if name in seen:
            return
        seen.add(name)
        owner = structs.get(name) or unions.get(name)
        if owner is None:
            return
        field_lists = [owner["fields"]] if name in structs else [
            variant["fields"] for variant in owner["variants"]
        ]
        for fields in field_lists:
            for field in fields:
                reference = parse_type(field["type"])
                inner = reference.inner if reference.inner is not None else reference
                if inner.kind == "named":
                    visit(inner.name)

    visit(root)
    return (
        [item for item in types["enums"] if item["name"] in seen],
        [item for item in types["structs"] if item["name"] in seen],
        [item for item in types["unions"] if item["name"] in seen],
    )


def _emit_encode(
    reference: Any,
    expression: str,
    indent: str,
    enum_names: set[str],
    referenced: bool = False,
) -> list[str]:
    if reference.kind == "optional":
        lines = [f"{indent}match &{expression} {{", f"{indent}    Some(value) => {{"]
        lines.append(f"{indent}        encoder.put_u8(1)?;")
        lines += _emit_encode(reference.inner, "value", indent + "        ", enum_names, True)
        lines += [f"{indent}    }}", f"{indent}    None => encoder.put_u8(0)?,", f"{indent}}}"]
        return lines
    if reference.kind == "list":
        lines = [f"{indent}encoder.put_len({expression}.len())?;", f"{indent}for item in &{expression} {{"]
        lines += _emit_encode(reference.inner, "item", indent + "    ", enum_names, True)
        lines.append(f"{indent}}}")
        return lines
    if reference.kind == "named":
        argument = expression if referenced else f"&{expression}"
        return [f"{indent}encode_{_snake(reference.name)}(encoder, {argument})?;"]
    methods = {
        "bool": "put_bool",
        "bytes": "put_bytes",
        "bytes32": "put_bytes32",
        "i64": "put_i64",
        "string": "put_string",
        "u32": "put_u32",
        "u64": "put_u64",
    }
    argument = expression
    if reference.name in {"bool", "i64", "u32", "u64"} and referenced:
        argument = f"*{expression}"
    if reference.name in {"bytes", "bytes32", "string"} and not referenced:
        argument = f"&{expression}"
    return [f"{indent}encoder.{methods[reference.name]}({argument})?;"]


def _decode_expression(reference: Any, enum_names: set[str]) -> str:
    if reference.kind == "optional":
        return f"decoder.read_optional(|decoder| Ok({_decode_expression(reference.inner, enum_names)}))?"
    if reference.kind == "list":
        return f"decoder.read_list(|decoder| Ok({_decode_expression(reference.inner, enum_names)}))?"
    if reference.kind == "named":
        return f"decode_{_snake(reference.name)}(decoder)?"
    methods = {
        "bool": "read_bool",
        "bytes": "read_bytes",
        "bytes32": "read_bytes32",
        "i64": "read_i64",
        "string": "read_string",
        "u32": "read_u32",
        "u64": "read_u64",
    }
    return f"decoder.{methods[reference.name]}()?"

