# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The Mojo projection of a contract: its types and the calls that carry them.

Its own module for the reason `cxx_renderer` is: it answers a different
question from the language renderers beside it. Those project a contract into
a language's own value types, and stop there. This one also projects the
*interfaces* — and an interface is the half a surface actually calls, so the
rules it has to keep are about the wire rather than about a type system.

Two of those rules are enforced in `contract_schema` and paid off here. Every
method and every argument carries a written ordinal, because Mojo assigns one
by declaration order otherwise and a reordered list silently reinterprets a
peer's messages. And `pending_remote`/`pending_receiver` reach this renderer
only in a parameter position, because a record is a value and a live connection
is not.
"""

from __future__ import annotations

from typing import Any

from contract_schema import ContractError, TypeRef, parse_type

from contract_renderers import header, pascal


def mojo_type(reference: TypeRef) -> str:
    if reference.kind in {"pending_remote", "pending_receiver"}:
        return f"{reference.kind}<{mojo_type(reference.inner)}>"
    if reference.kind == "optional":
        inner = mojo_type(reference.inner)
        if inner in {"bool", "int64", "uint32", "uint64"}:
            raise ContractError(f"Mojo optional scalar is unsupported: {inner}")
        return f"{inner}?"
    if reference.kind == "list":
        return f"array<{mojo_type(reference.inner)}>"
    mapping = {
        "bool": "bool",
        "bytes": "array<uint8>",
        "bytes32": "array<uint8, 32>",
        "i64": "int64",
        "string": "string",
        "u32": "uint32",
        "u64": "uint64",
    }
    return mapping.get(reference.name, reference.name)


def _mojo_arguments(arguments: list[dict[str, Any]]) -> str:
    return ", ".join(
        f"{mojo_type(parse_type(argument['type']))} {argument['name']}@{argument['ordinal']}"
        for argument in arguments
    )


def _wrap_mojo_method(name: str, ordinal: int, params: str, response: str | None) -> list[str]:
    """One method, wrapped to Chromium's 80 columns without losing its shape."""
    tail = f" => ({response});" if response is not None else ";"
    single = f"  {name}@{ordinal}({params}){tail}"
    if len(single) <= 80:
        return [single]
    lines = [f"  {name}@{ordinal}("]
    for index, argument in enumerate(params.split(", ")) if params else []:
        comma = "," if index < len(params.split(", ")) - 1 else ""
        lines.append(f"      {argument}{comma}")
    if response is None:
        lines.append("  );")
        return lines
    lines.append(f"  ) => ({response});")
    return lines


def render_mojom(schema: dict[str, Any]) -> str:
    lines = header(schema, "//")
    lines.append(f"module {schema['namespace']}.mojom;\n")
    for imported in schema.get("imports", []):
        lines.append(f'import "{imported}";')
    if schema.get("imports"):
        lines.append("")
    for name, value in schema["limits"].items():
        lines.append(f"const uint64 k{pascal(name)} = {value};")
    lines.append("")
    for enum in schema["enums"]:
        lines.append(f"enum {enum['name']} {{")
        lines += [f"  k{pascal(member['name'])} = {member['wire']}," for member in enum["members"]]
        lines += ["};", ""]
    for struct in schema["structs"]:
        lines.append(f"struct {struct['name']} {{")
        for field in struct["fields"]:
            lines.append(f"  {mojo_type(parse_type(field['type']))} {field['name']}@{field['ordinal']};")
        lines += ["};", ""]
    for interface in schema.get("interfaces", []):
        lines.append(f"interface {interface['name']} {{")
        for method in interface["methods"]:
            response = method.get("response")
            lines += _wrap_mojo_method(
                method["name"],
                method["ordinal"],
                _mojo_arguments(method.get("params", [])),
                None if response is None else _mojo_arguments(response),
            )
        lines += ["};", ""]
    return "\n".join(lines).rstrip() + "\n"
