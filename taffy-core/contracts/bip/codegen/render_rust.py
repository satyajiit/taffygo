#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The Rust view of the contract.

Authority boundary: how a definition is spelled in Rust, and nothing else. It
decides no types, resolves no references, and reads no file — it renders what
`contract.Contract` already decided.
"""

from __future__ import annotations

from contract import Contract, field_type, property_doc, scalar_type
from layout import GENERATED_MARK
from naming import doc_block, pascal_case, rust_field

#: The licence notice every generated projection carries. The wording after
#: the copyright line is Exhibit A of the Mozilla Public License and is not
#: editable (decision 0205); it is spelled out here rather than imported
#: because a generator's inputs are declared to GN and an import across the
#: tree is an input nothing declares.
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


def rust_header(source: str) -> list[str]:
    return [
        *notice("//"),
        "//",
        f"// {GENERATED_MARK}",
        "//",
        f"// Source of truth: taffy-core/contracts/bip/schema/{source}",
        "// Regenerate:      python3 taffy-core/contracts/bip/codegen/generate.py --write",
        "// Verify:          python3 taffy-core/contracts/bip/codegen/generate.py --check",
        "",
    ]


def module(contract: Contract, document_name: str) -> str:
    document = contract.store.documents[document_name]
    module = contract.modules[document_name]
    names = contract.names_in(document_name)

    body: list[str] = []
    uses: set[str] = set()
    for name in names:
        body.extend(definition(contract, name, uses))
        body.append("")

    imports: dict[str, list[str]] = {}
    for used in sorted(uses):
        other = contract.module_of(used)
        if other != module:
            imports.setdefault(other, []).append(used)

    lines = rust_header(document_name)
    lines.extend(doc_block(document.get("title", ""), "//! "))
    if document.get("description"):
        lines.append("//!")
        lines.extend(doc_block(document["description"], "//! "))
    lines.append("")
    lines.append("#![allow(clippy::doc_markdown)]")
    lines.append("#![allow(clippy::struct_excessive_bools)]")
    lines.append("")
    lines.append("use serde::{Deserialize, Serialize};")
    if imports:
        lines.append("")
        for other in sorted(imports):
            members = ", ".join(sorted(imports[other]))
            lines.append(f"use super::{other}::{{{members}}};")
    lines.append("")
    lines.extend(body)
    return "\n".join(lines).rstrip() + "\n"


def definition(contract: Contract, name: str, uses: set[str]) -> list[str]:
    node = contract.defs[name]
    kind = contract.kind_of(name)
    if kind == "enum":
        return enum(name, node)
    if kind == "newtype":
        return newtype(name, node)
    return struct(contract, name, node, uses)


def newtype(name: str, node: dict) -> list[str]:
    rust = scalar_type(node)
    derives = ["Clone", "Debug", "PartialEq", "Serialize", "Deserialize"]
    if rust in ("bool", "u64", "i64"):
        derives = ["Clone", "Copy", "Debug", "PartialEq", "Eq", "Hash", "Serialize", "Deserialize"]
    elif rust == "String":
        derives = ["Clone", "Debug", "PartialEq", "Eq", "Hash", "Serialize", "Deserialize"]
    elif rust == "f64":
        derives = ["Clone", "Copy", "Debug", "PartialEq", "PartialOrd", "Serialize", "Deserialize"]
    lines = doc_block(node.get("description", ""), "/// ")
    lines.append(f"#[derive({', '.join(derives)})]")
    lines.append("#[serde(transparent)]")
    lines.append(f"pub struct {name}(pub {rust});")
    return lines


def struct(contract: Contract, name: str, node: dict, uses: set[str]) -> list[str]:
    required = set(node.get("required", []))
    lines = doc_block(node.get("description", ""), "/// ")
    lines.append("#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]")
    lines.append(f"pub struct {name} {{")
    for field, property_node in node.get("properties", {}).items():
        rust = field_type(property_node, contract, uses)
        lines.extend(doc_block(property_doc(property_node, contract), "    /// "))
        if contract.absent_as_empty(node, field):
            lines.append('    #[serde(default, skip_serializing_if = "Vec::is_empty")]')
        elif field not in required:
            lines.append('    #[serde(default, skip_serializing_if = "Option::is_none")]')
            rust = f"Option<{rust}>"
        lines.append(f"    pub {rust_field(field)}: {rust},")
    lines.append("}")
    return lines


def enum(name: str, node: dict) -> list[str]:
    members = list(node["enum"])
    reserved = list(node.get("x-bip-reserved", []))
    milestone = node.get("x-bip-reserved-milestone", "")

    lines = doc_block(node.get("description", ""), "/// ")
    lines.append(
        "#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]"
    )
    lines.append(f"pub enum {name} {{")
    for value in members:
        lines.append(f'    #[serde(rename = "{value}")]')
        lines.append(f"    {pascal_case(value)},")
    lines.append("}")
    lines.append("")
    lines.append(f"impl {name} {{")
    lines.append("    /// Every member, in the order the schema declares them.")
    joined = ", ".join(f"Self::{pascal_case(v)}" for v in members)
    lines.append(f"    pub const ALL: &[Self] = &[{joined}];")
    lines.append("")
    lines.append("    /// The wire value of this member.")
    lines.append("    pub fn wire(self) -> &'static str {")
    lines.append("        match self {")
    for value in members:
        lines.append(f'            Self::{pascal_case(value)} => "{value}",')
    lines.append("        }")
    lines.append("    }")
    lines.append("")
    lines.extend(
        doc_block(
            "Parses a wire value. `None` means the value is outside this closed "
            "enumeration: the caller must treat the message as unsupported and fail "
            "closed, and must never substitute a known member for it.",
            "    /// ",
        )
    )
    lines.append("    pub fn from_wire(value: &str) -> Option<Self> {")
    lines.append("        match value {")
    for value in members:
        lines.append(f'            "{value}" => Some(Self::{pascal_case(value)}),')
    lines.append("            _ => None,")
    lines.append("        }")
    lines.append("    }")
    if reserved:
        lines.append("")
        lines.extend(
            doc_block(
                f"Members reserved for milestone {milestone}. They exist so that bindings "
                "carry them and exhaustive deny behaviour can be compile-tested. A "
                "production endpoint never advertises them and the production dispatcher "
                "denies them.",
                "    /// ",
            )
        )
        joined = ", ".join(f"Self::{pascal_case(v)}" for v in reserved)
        lines.append(f"    pub const RESERVED: &[Self] = &[{joined}];")
        lines.append("")
        lines.append(f"    /// Whether this member is reserved for milestone {milestone}.")
        lines.append("    pub fn is_reserved(self) -> bool {")
        pattern = " | ".join(f"Self::{pascal_case(v)}" for v in reserved)
        lines.append(f"        matches!(self, {pattern})")
        lines.append("    }")
    lines.append("}")
    return lines


def mod(contract: Contract) -> str:
    lines = [
        *notice("//"),
        "//",
        f"// {GENERATED_MARK}",
        "//",
        "// Source of truth: taffy-core/contracts/bip/schema/",
        "// Regenerate:      python3 taffy-core/contracts/bip/codegen/generate.py --write",
        "// Verify:          python3 taffy-core/contracts/bip/codegen/generate.py --check",
        "",
        "//! Generated Browser Intelligence Protocol types.",
        "//!",
    ]
    lines.extend(
        doc_block(
            "One module per schema document. Nothing here is hand-written: edit the "
            "schema and regenerate. The protocol version these types speak is "
            "`version::PROTOCOL_VERSION`.",
            "//! ",
        )
    )
    lines.append("")
    for module in sorted(set(contract.modules.values()) | {"version"}):
        lines.append(f"pub mod {module};")
    lines.append("")
    lines.extend(
        doc_block(
            "Every closed enumeration this contract generates, by name. It exists "
            "so `version::CLOSED_ENUMS` — which is hand-maintained, because Rust "
            "cannot enumerate its own types — can be checked against the schema "
            "rather than against a number somebody remembered to raise. A schema "
            "enumeration added without a registry entry fails a test here instead "
            "of silently narrowing the set the fail-closed property is proved "
            "over.",
            "/// ",
        )
    )
    enumerations = sorted(
        name for name in contract.defs if contract.kind_of(name) == "enum"
    )
    lines.append("pub const GENERATED_ENUMERATIONS: &[&str] = &[")
    for name in enumerations:
        lines.append(f'    "{name}",')
    lines.append("];")
    return "\n".join(lines) + "\n"


def version(version: dict) -> str:
    lines = [
        *notice("//"),
        "//",
        f"// {GENERATED_MARK}",
        "//",
        "// Source of truth: taffy-core/contracts/bip/schema/bip.version.json",
        "// Regenerate:      python3 taffy-core/contracts/bip/codegen/generate.py --write",
        "// Verify:          python3 taffy-core/contracts/bip/codegen/generate.py --check",
        "",
        "//! The protocol version these bindings speak.",
        "//!",
    ]
    lines.extend(doc_block(version.get("description", ""), "//! "))
    lines.append("")
    lines.extend(
        doc_block(
            "The major.minor wire version of the Browser Intelligence Protocol. This "
            "constant is generated from the one file that owns the number; no other "
            "Rust source may restate it.",
            "/// ",
        )
    )
    lines.append(f'pub const PROTOCOL_VERSION: &str = "{version["protocol_version"]}";')
    lines.append("")
    lines.extend(
        doc_block(
            "Status of the protocol contract, using the repository status labels.",
            "/// ",
        )
    )
    lines.append(f'pub const PROTOCOL_STATUS: &str = "{version["status"]}";')
    return "\n".join(lines) + "\n"
