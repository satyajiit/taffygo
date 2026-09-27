# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Loading one contract document, and validating it as a whole.

This is the entry point every generator and every gate goes through:
`load_schema` reads a contract's `schema/contract.json` and refuses to hand
back a document that does not validate, so no caller can hold an unchecked
schema. `validate_schema` is the single pass over that document — its header,
the projections it declares, its limits, its enumerations and records — and it
delegates the two parts that are their own subject: interface declarations to
`contract_interfaces`, and the rules that bind two declarations together to
`contract_relations`.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from contract_interfaces import validate_interfaces
from contract_relations import validate_relations
from contract_schema import (
    CONTRACT_ROOT,
    HANDLE_WRAPPERS,
    IDENTIFIER,
    MEMBER,
    PROJECTIONS,
    SCALARS,
    ContractError,
    parse_type,
    require_description,
)
from rust_binary_codec import validate_binary_codecs


def load_schema(contract: str) -> dict[str, Any]:
    path = CONTRACT_ROOT / contract / "schema" / "contract.json"
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        codec_source = value.pop("binary_codec_schema", None)
        if codec_source is not None:
            if (
                not isinstance(codec_source, str)
                or Path(codec_source).name != codec_source
                or not codec_source.endswith(".json")
            ):
                raise ContractError(f"{path}: invalid binary codec schema path")
            codec_path = path.parent / codec_source
            codec = json.loads(codec_path.read_text(encoding="utf-8"))
            value["binary_codecs"] = [codec]
    except (OSError, json.JSONDecodeError) as error:
        raise ContractError(f"{path}: {error}") from error
    validate_schema(contract, value)
    return value

def validate_schema(contract: str, schema: dict[str, Any]) -> None:
    if schema.get("contract") != contract.replace("-", "_"):
        raise ContractError(f"{contract}: contract name does not match directory")
    namespace = schema.get("namespace")
    if not isinstance(namespace, str) or not namespace.startswith("taffy."):
        raise ContractError(f"{contract}: namespace must start with taffy.")
    version = schema.get("version")
    if not isinstance(version, dict) or set(version) != {"major", "minor"}:
        raise ContractError(f"{contract}: version must contain major and minor")
    if not all(isinstance(version[key], int) and version[key] >= 0 for key in version):
        raise ContractError(f"{contract}: version values must be non-negative integers")
    projections = schema.get("projections")
    if (
        not isinstance(projections, list)
        or not projections
        or len(set(projections)) != len(projections)
        or not set(projections) <= set(PROJECTIONS)
    ):
        raise ContractError(
            f"{contract}: projections must name a non-empty, distinct subset of "
            f"{list(PROJECTIONS)}"
        )
    limits = schema.get("limits")
    if not isinstance(limits, dict) or not limits:
        raise ContractError(f"{contract}: limits must be explicit")
    for name, value in limits.items():
        if not MEMBER.fullmatch(name) or not isinstance(value, int) or value <= 0:
            raise ContractError(f"{contract}: invalid limit {name}")

    enums = schema.get("enums")
    structs = schema.get("structs")
    if not isinstance(enums, list) or not isinstance(structs, list):
        raise ContractError(f"{contract}: enums and structs must be lists")
    names: set[str] = set()
    for enum in enums:
        name = enum.get("name")
        if not isinstance(name, str) or not IDENTIFIER.fullmatch(name) or name in names:
            raise ContractError(f"{contract}: invalid or duplicate enum name {name!r}")
        names.add(name)
        require_description(name, enum)
        members = enum.get("members")
        if not isinstance(members, list) or not members:
            raise ContractError(f"{name}: closed enum must have members")
        wires: list[int] = []
        member_names: set[str] = set()
        for member in members:
            member_name = member.get("name")
            wire = member.get("wire")
            if not isinstance(member_name, str) or not MEMBER.fullmatch(member_name):
                raise ContractError(f"{name}: invalid member {member_name!r}")
            if member_name in member_names or not isinstance(wire, int):
                raise ContractError(f"{name}: duplicate member or non-integer wire")
            member_names.add(member_name)
            wires.append(wire)
            require_description(f"{name}.{member_name}", member)
        if wires != list(range(len(wires))):
            raise ContractError(f"{name}: wire values must be append-only 0..n")

    enum_names = {enum["name"] for enum in enums}
    for struct in structs:
        name = struct.get("name")
        if not isinstance(name, str) or not IDENTIFIER.fullmatch(name) or name in names:
            raise ContractError(f"{contract}: invalid or duplicate struct name {name!r}")
        names.add(name)
        require_description(name, struct)

    forbidden = tuple(schema.get("forbidden_field_substrings", []))
    for struct in structs:
        fields = struct.get("fields")
        if not isinstance(fields, list) or not fields:
            raise ContractError(f"{struct['name']}: records cannot be empty")
        field_names: set[str] = set()
        for ordinal, field in enumerate(fields):
            name = field.get("name")
            if not isinstance(name, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", name):
                raise ContractError(f"{struct['name']}: invalid field name {name!r}")
            if name in field_names or field.get("ordinal") != ordinal:
                raise ContractError(
                    f"{struct['name']}.{name}: duplicate name or non-append-only ordinal"
                )
            if any(part.lower() in name.lower() for part in forbidden):
                raise ContractError(f"{struct['name']}.{name}: forbidden authority field")
            field_names.add(name)
            require_description(f"{struct['name']}.{name}", field)
            reference = parse_type(field.get("type", ""))
            if reference.kind in HANDLE_WRAPPERS:
                raise ContractError(
                    f"{struct['name']}.{name}: a record field cannot carry "
                    f"{reference.kind}; a live connection is not a value"
                )
            named = reference.inner.name if reference.inner else reference.name
            if reference.kind in {"named", "optional", "list"} and named not in names | SCALARS:
                raise ContractError(f"{struct['name']}.{name}: unknown type {named}")
            if reference.kind == "optional" and named in enum_names:
                raise ContractError(
                    f"{struct['name']}.{name}: optional enums are not portable to Mojo; "
                    "wrap the enum in an optional record"
                )

    validate_interfaces(contract, schema, names)
    validate_relations(contract, schema, enums, structs)

    required = schema.get("required_envelope_fields", [])
    if not isinstance(required, list) or not all(isinstance(value, str) for value in required):
        raise ContractError(f"{contract}: required_envelope_fields must be strings")
    if required:
        envelope = next((item for item in structs if item["name"] == "OperationEnvelope"), None)
        if envelope is None:
            raise ContractError(f"{contract}: OperationEnvelope is required")
        actual = {field["name"] for field in envelope["fields"]}
        missing = set(required) - actual
        if missing:
            raise ContractError(f"{contract}: envelope misses {sorted(missing)}")

    try:
        validate_binary_codecs(contract, schema, parse_type)
    except ValueError as error:
        raise ContractError(str(error)) from error
