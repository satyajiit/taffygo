# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Rust, Kotlin, TypeScript, and Mojo contract renderers."""

from __future__ import annotations

from typing import Any

from contract_schema import ContractError, TypeRef, parse_type
from rust_binary_codec import render_rust_codecs

def words(value: str) -> list[str]:
    return value.lower().split("_")


def tagged_union_for(schema: dict[str, Any], struct_name: str) -> dict[str, Any] | None:
    return next(
        (union for union in schema.get("tagged_unions", []) if union["struct"] == struct_name),
        None,
    )


def enum_pairing_for(schema: dict[str, Any], struct_name: str) -> dict[str, Any] | None:
    return next(
        (pairing for pairing in schema.get("enum_pairings", []) if pairing["struct"] == struct_name),
        None,
    )


def presence_rules_for(schema: dict[str, Any], struct_name: str) -> list[dict[str, Any]]:
    return [
        rule
        for rule in schema.get("conditional_presence", [])
        if rule["struct"] == struct_name
    ]


def pascal(value: str) -> str:
    return "".join(word.capitalize() for word in words(value))


def camel(value: str) -> str:
    result = pascal(value)
    return result[:1].lower() + result[1:]


def screaming(value: str) -> str:
    return "_".join(words(value)).upper()


def rust_type(reference: TypeRef) -> str:
    if reference.kind == "optional":
        return f"Option<{rust_type(reference.inner)}>"
    if reference.kind == "list":
        return f"Vec<{rust_type(reference.inner)}>"
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


def kotlin_type(reference: TypeRef) -> str:
    if reference.kind == "optional":
        return f"{kotlin_type(reference.inner)}?"
    if reference.kind == "list":
        return f"List<{kotlin_type(reference.inner)}>"
    mapping = {
        "bool": "Boolean",
        "bytes": "ByteArray",
        "bytes32": "ByteArray",
        "i64": "Long",
        "string": "String",
        "u32": "UInt",
        "u64": "ULong",
    }
    return mapping.get(reference.name, reference.name)


def ts_type(reference: TypeRef) -> str:
    if reference.kind == "optional":
        return f"{ts_type(reference.inner)} | null"
    if reference.kind == "list":
        return f"ReadonlyArray<{ts_type(reference.inner)}>"
    mapping = {
        "bool": "boolean",
        "bytes": "Uint8Array",
        "bytes32": "Uint8Array",
        "i64": "bigint",
        "string": "string",
        "u32": "number",
        "u64": "bigint",
    }
    return mapping.get(reference.name, reference.name)


#: The licence notice every generated binding carries, one line per entry so
#: each renderer can prefix it with its own comment marker. The wording after
#: the copyright line is Exhibit A of the Mozilla Public License and is not
#: editable (decision 0205). It is spelled out here rather than imported from
#: the tool suite because a generator's inputs are declared to GN, and an
#: import across the tree is an input nothing declares.
NOTICE = (
    "Copyright (c) 2026 Matterward Labs Private Limited.",
    "",
    "This Source Code Form is subject to the terms of the Mozilla Public",
    "License, v. 2.0. If a copy of the MPL was not distributed with this",
    "file, You can obtain one at https://mozilla.org/MPL/2.0/.",
)


def notice(marker: str) -> list[str]:
    """The notice in one comment syntax."""
    return [f"{marker} {line}".rstrip() for line in NOTICE]


def header(schema: dict[str, Any], marker: str) -> list[str]:
    version = schema["version"]
    return [
        *notice(marker),
        marker,
        f"{marker} Generated from schema/contract.json. Do not edit.",
        f"{marker} Contract {schema['contract']} {version['major']}.{version['minor']}.",
        "",
    ]


def render_rust(schema: dict[str, Any]) -> str:
    lines = header(schema, "//")
    lines += [
        "#![allow(",
        "    clippy::module_name_repetitions,",
        "    clippy::needless_question_mark,",
        "    clippy::struct_excessive_bools,",
        "    clippy::too_many_lines,",
        "    clippy::trivially_copy_pass_by_ref",
        ")]",
        "",
    ]
    for name, value in schema["limits"].items():
        lines.append(f"pub const {name}: usize = {value:_};")
    lines.append("")
    for enum in schema["enums"]:
        lines += ["#[derive(Clone, Copy, Debug, Eq, PartialEq)]", "#[repr(u32)]", f"pub enum {enum['name']} {{"]
        lines += [f"    {pascal(member['name'])} = {member['wire']}," for member in enum["members"]]
        lines += ["}", "", f"impl {enum['name']} {{", "    pub const fn from_wire(value: u32) -> Option<Self> {", "        match value {"]
        lines += [f"            {member['wire']} => Some(Self::{pascal(member['name'])})," for member in enum["members"]]
        lines += ["            _ => None,", "        }", "    }", "}", ""]
    for struct in schema["structs"]:
        lines += ["#[derive(Clone, Debug, Eq, PartialEq)]", f"pub struct {struct['name']} {{"]
        for field in struct["fields"]:
            lines.append(f"    pub {field['name']}: {rust_type(parse_type(field['type']))},")
        lines += ["}", ""]
        union = tagged_union_for(schema, struct["name"])
        if union is not None:
            pairing = enum_pairing_for(schema, struct["name"])
            body_fields = [variant["field"] for variant in union["variants"]]
            tag_type = next(
                field["type"]
                for field in struct["fields"]
                if field["name"] == union["tag_field"]
            )
            lines += [f"impl {struct['name']} {{", "    pub fn has_valid_body(&self) -> bool {"]
            lines.append("        let body_count = [")
            lines += [f"            self.{field}.is_some()," for field in body_fields]
            lines += [
                "        ]",
                "        .into_iter()",
                "        .filter(|present| *present)",
                "        .count();",
                "        body_count == 1",
                f"            && match self.{union['tag_field']} {{",
            ]
            lines += [
                f"                {tag_type}::{pascal(variant['tag'])} => self.{variant['field']}.is_some(),"
                for variant in union["variants"]
            ]
            lines.append("            }")
            if pairing is not None:
                fields = {field["name"]: field for field in struct["fields"]}
                left_type = fields[pairing["left_field"]]["type"]
                right_type = fields[pairing["right_field"]]["type"]
                lines += [
                    "            && matches!(",
                    f"                (self.{pairing['left_field']}, "
                    f"self.{pairing['right_field']}),",
                ]
                patterns: list[str] = []
                for pair in pairing["pairs"]:
                    right_patterns = " | ".join(
                        f"{right_type}::{pascal(right)}" for right in pair["right"]
                    )
                    patterns.append(
                        f"({left_type}::{pascal(pair['left'])}, {right_patterns})"
                    )
                lines += [
                    "                " + "\n                    | ".join(patterns),
                    "            )",
                ]
            lines += ["    }", "}", ""]
        presence_rules = presence_rules_for(schema, struct["name"])
        if presence_rules:
            fields = {field["name"]: field for field in struct["fields"]}
            lines += [
                f"impl {struct['name']} {{",
                "    pub fn has_valid_presence(&self) -> bool {",
            ]
            # A `match` is not a valid operand of `&&` without parentheses, so
            # a struct with two or more presence rules needs them. A struct with
            # exactly one rule does not, and emitting them anyway is what clippy
            # rejects as `unnecessary_parens` — a lint the workspace denies, so
            # the generated crate would not compile at all.
            parenthesise = len(presence_rules) > 1
            for index, rule in enumerate(presence_rules):
                tag_type = fields[rule["tag_field"]]["type"]
                patterns = " | ".join(
                    f"{tag_type}::{pascal(member)}" for member in rule["present_for"]
                )
                prefix = "        " if index == 0 else "            && "
                opening = "(" if parenthesise else ""
                closing = "})" if parenthesise else "}"
                lines += [
                    f"{prefix}{opening}match self.{rule['tag_field']} {{",
                    f"            {patterns} => self.{rule['field']}.is_some(),",
                    f"            _ => self.{rule['field']}.is_none(),",
                    f"        {closing}",
                ]
            lines += ["    }", "}", ""]
    lines += render_rust_codecs(schema, parse_type, tagged_union_for)
    return "\n".join(lines).rstrip() + "\n"


def render_kotlin(schema: dict[str, Any]) -> str:
    lines = header(schema, "//")
    lines.append(f"package {schema['namespace']}\n")
    for name, value in schema["limits"].items():
        lines.append(f"const val {name}: Int = {value}")
    lines.append("")
    for enum in schema["enums"]:
        lines.append(f"enum class {enum['name']}(val wire: UInt) {{")
        for index, member in enumerate(enum["members"]):
            suffix = ";" if index == len(enum["members"]) - 1 else ","
            lines.append(f"    {member['name']}({member['wire']}u){suffix}")
        lines += ["", "    companion object {", f"        fun fromWire(value: UInt): {enum['name']}? = entries.firstOrNull {{ it.wire == value }}", "    }", "}", ""]
    for struct in schema["structs"]:
        presence_rules = presence_rules_for(schema, struct["name"])
        lines.append(f"data class {struct['name']}(")
        for index, field in enumerate(struct["fields"]):
            comma = "," if index < len(struct["fields"]) - 1 else ""
            lines.append(f"    val {field['name']}: {kotlin_type(parse_type(field['type']))}{comma}")
        union = tagged_union_for(schema, struct["name"])
        if union is None:
            lines += [")", ""]
            if not presence_rules:
                continue
            conditions_and_fields = []
            for rule in presence_rules:
                fields = {field["name"]: field for field in struct["fields"]}
                tag_type = fields[rule["tag_field"]]["type"]
                conditions = " || ".join(
                    f"{rule['tag_field']} == {tag_type}.{member}"
                    for member in rule["present_for"]
                )
                conditions_and_fields.append(
                    f"(if ({conditions}) {rule['field']} != null else {rule['field']} == null)"
                )
            lines += [
                f"fun {struct['name']}.hasValidPresence(): Boolean =",
                "    " + " &&\n        ".join(conditions_and_fields),
                "",
            ]
            continue
        pairing = enum_pairing_for(schema, struct["name"])
        body_fields = [variant["field"] for variant in union["variants"]]
        tag_type = next(
            field["type"]
            for field in struct["fields"]
            if field["name"] == union["tag_field"]
        )
        lines += [") {", "    fun hasValidBody(): Boolean {"]
        lines.append(
            "        val bodyCount = listOf("
            + ", ".join(body_fields)
            + ").count { it != null }"
        )
        lines += [
            "        if (bodyCount != 1) return false",
            (
                f"        val bodyMatches = when ({union['tag_field']}) {{"
                if pairing is not None
                else f"        return when ({union['tag_field']}) {{"
            ),
        ]
        lines += [
            f"            {tag_type}.{variant['tag']} -> {variant['field']} != null"
            for variant in union["variants"]
        ]
        lines.append("        }")
        if pairing is not None:
            fields = {field["name"]: field for field in struct["fields"]}
            left_type = fields[pairing["left_field"]]["type"]
            right_type = fields[pairing["right_field"]]["type"]
            lines += [
                "        if (!bodyMatches) return false",
                f"        return when ({pairing['left_field']}) {{",
            ]
            for pair in pairing["pairs"]:
                conditions = " || ".join(
                    f"{pairing['right_field']} == {right_type}.{right}"
                    for right in pair["right"]
                )
                lines.append(f"            {left_type}.{pair['left']} -> {conditions}")
            lines.append("        }")
        lines += ["    }", "", "    companion object {"]
        fields = {field["name"]: field for field in struct["fields"]}
        common_fields = [
            field
            for field in struct["fields"]
            if field["name"] != union["tag_field"] and field["name"] not in body_fields
        ]
        for variant in union["variants"]:
            body_reference = parse_type(fields[variant["field"]]["type"])
            body_type = kotlin_type(body_reference.inner)
            parameters = [
                f"{field['name']}: {kotlin_type(parse_type(field['type']))}"
                for field in common_fields
            ]
            parameters.append(f"body: {body_type}")
            lines += [
                f"        fun {camel(variant['field'])}({', '.join(parameters)}): {struct['name']} =",
                f"            {struct['name']}(",
            ]
            for field in struct["fields"]:
                field_name = field["name"]
                if field_name == union["tag_field"]:
                    value = f"{tag_type}.{variant['tag']}"
                elif field_name in body_fields:
                    value = "body" if field_name == variant["field"] else "null"
                else:
                    value = field_name
                lines.append(f"                {field_name} = {value},")
            lines += ["            )", ""]
        lines += ["    }", "}", ""]
        for rule in presence_rules:
            fields = {field["name"]: field for field in struct["fields"]}
            tag_type = fields[rule["tag_field"]]["type"]
            conditions = " || ".join(
                f"{rule['tag_field']} == {tag_type}.{member}"
                for member in rule["present_for"]
            )
            lines += [
                f"fun {struct['name']}.hasValidPresence(): Boolean =",
                f"    if ({conditions}) {rule['field']} != null else {rule['field']} == null",
                "",
            ]
    return "\n".join(lines).rstrip() + "\n"


def render_typescript(schema: dict[str, Any]) -> str:
    lines = header(schema, "//")
    for name, value in schema["limits"].items():
        lines.append(f"export const {name} = {value} as const;")
    lines.append("")
    for enum in schema["enums"]:
        lines.append(f"export enum {enum['name']} {{")
        lines += [f"  {pascal(member['name'])} = {member['wire']}," for member in enum["members"]]
        lines += ["}", ""]
    for struct in schema["structs"]:
        lines.append(f"export interface {struct['name']} {{")
        for field in struct["fields"]:
            lines.append(f"  readonly {field['name']}: {ts_type(parse_type(field['type']))};")
        lines += ["}", ""]
        union = tagged_union_for(schema, struct["name"])
        if union is not None:
            pairing = enum_pairing_for(schema, struct["name"])
            body_fields = [variant["field"] for variant in union["variants"]]
            tag_type = next(
                field["type"]
                for field in struct["fields"]
                if field["name"] == union["tag_field"]
            )
            lines += [
                f"export function isValid{struct['name']}(value: {struct['name']}): boolean {{",
                "  const bodyCount = [",
            ]
            lines += [f"    value.{field} !== null," for field in body_fields]
            lines += [
                "  ].filter(Boolean).length;",
                "  if (bodyCount !== 1) return false;",
                f"  switch (value.{union['tag_field']}) {{",
            ]
            for variant in union["variants"]:
                lines += [
                    f"    case {tag_type}.{pascal(variant['tag'])}:",
                    (
                        f"      if (value.{variant['field']} === null) return false;"
                        if pairing is not None
                        else f"      return value.{variant['field']} !== null;"
                    ),
                ]
                if pairing is not None:
                    lines.append("      break;")
            lines += ["  }", "}", ""]
            if pairing is not None:
                fields = {field["name"]: field for field in struct["fields"]}
                left_type = fields[pairing["left_field"]]["type"]
                right_type = fields[pairing["right_field"]]["type"]
                lines[-2:] = [f"  switch (value.{pairing['left_field']}) {{"]
                for pair in pairing["pairs"]:
                    conditions = " || ".join(
                        f"value.{pairing['right_field']} === {right_type}.{pascal(right)}"
                        for right in pair["right"]
                    )
                    lines += [
                        f"    case {left_type}.{pascal(pair['left'])}:",
                        f"      return {conditions};",
                    ]
                lines += ["  }", "}", ""]
        presence_rules = presence_rules_for(schema, struct["name"])
        if presence_rules:
            fields = {field["name"]: field for field in struct["fields"]}
            expressions = []
            for rule in presence_rules:
                tag_type = fields[rule["tag_field"]]["type"]
                conditions = " || ".join(
                    f"value.{rule['tag_field']} === {tag_type}.{pascal(member)}"
                    for member in rule["present_for"]
                )
                expressions.append(
                    f"(({conditions}) ? value.{rule['field']} !== null : value.{rule['field']} === null)"
                )
            lines += [
                f"export function isValid{struct['name']}Presence(value: {struct['name']}): boolean {{",
                "  return " + " &&\n      ".join(expressions) + ";",
                "}",
                "",
            ]
    return "\n".join(lines).rstrip() + "\n"
