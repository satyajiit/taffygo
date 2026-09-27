# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""A reference reader and writer for the contract-owned payload codecs.

Authority boundary: the generated Rust, Kotlin, and TypeScript codecs are the
implementations that ship. This module is a checker, not a fourth codec: it
exists so that a committed compatibility payload can be decoded on any host
with nothing but Python, and so the byte corpus can be built from a golden
document rather than typed by hand. Nothing in the product links against it.

It reads the two codec declarations this repository already has — Core API's
``state_payload_codec``, whose bounds are per field, and the shared
``binary_codecs`` shape, whose bounds are per codec — through one normalised
model, so a deviation fixture is decoded by the same rules whichever seam it
came from. The error names it returns are the generated Rust variant names on
purpose: a fixture that names ``InvalidEnum`` must mean the variant the shipped
decoder returns, or the two halves of the gate are measuring different things.
"""

from __future__ import annotations

import struct
from typing import Any

from contract_schema import ContractError, TypeRef, parse_type

#: Every generated decoder maps its error to one of these verdicts. The mapping
#: is here rather than in a fixture table because a verdict a fixture could
#: choose freely would prove nothing about the decoder that returned the error.
VERDICT_FOR_ERROR = {
    "SizeLimit": "REJECT_OVERSIZED",
    "CollectionLimit": "REJECT_OVERSIZED",
    "StringLimit": "REJECT_OVERSIZED",
    "ValueLimit": "REJECT_OVERSIZED",
    "UnsupportedVersion": "REJECT_VERSION",
    "InvalidEnum": "REJECT_CLOSED_ENUM",
    "LengthOverflow": "REJECT_MALFORMED",
    "Truncated": "REJECT_MALFORMED",
    "InvalidMagic": "REJECT_MALFORMED",
    "InvalidBoolean": "REJECT_MALFORMED",
    "InvalidTaggedUnion": "REJECT_MALFORMED",
    "InvalidUtf8": "REJECT_MALFORMED",
    "Malformed": "REJECT_MALFORMED",
    "TrailingBytes": "REJECT_MALFORMED",
}


class CodecRefusal(Exception):
    """One generated decoder error, carried by its Rust variant name."""

    def __init__(self, error: str) -> None:
        super().__init__(error)
        self.error = error


def state_payload_model(schema: dict[str, Any]) -> dict[str, Any]:
    """Normalise Core API's per-field bounded state codec."""
    codec = schema["state_payload_codec"]
    return {
        "name": codec["function_suffix"],
        "magic": codec["magic"].encode("ascii"),
        "version_is_prefix": True,
        "version_field": None,
        "schema_version": codec["schema_version"],
        "max_bytes": schema["limits"][codec["max_bytes_limit"]],
        "root": codec["root"],
        "enums": {
            item["name"]: {member["wire"] for member in item["members"]}
            for item in schema["enums"]
        },
        "enum_members": {
            item["name"]: {member["wire"]: member["name"] for member in item["members"]}
            for item in schema["enums"]
        },
        "structs": {item["name"]: item["fields"] for item in schema["structs"]},
        "unions": {},
        "string_limits": {
            path: schema["limits"][name] for path, name in codec["string_limits"].items()
        },
        "list_limits": {
            path: schema["limits"][name] for path, name in codec["list_limits"].items()
        },
        "u32_limits": {
            path: schema["limits"][name] for path, name in codec["u32_limits"].items()
        },
        "default_string_limit": None,
        "default_list_limit": None,
        "presence": codec["presence_rules"],
    }


def binary_codec_model(schema: dict[str, Any], codec: dict[str, Any]) -> dict[str, Any]:
    """Normalise one shared ``binary_codecs`` declaration."""
    types = codec["types"]
    return {
        "name": codec["function_suffix"],
        "magic": codec["magic"].encode("ascii"),
        "version_is_prefix": False,
        "version_is_first": True,
        "version_field": codec["schema_version_field"],
        "schema_version": codec["schema_version"],
        "max_bytes": schema["limits"][codec["max_bytes_limit"]],
        "root": codec["root"],
        "enums": {
            item["name"]: {member["wire"] for member in item["members"]}
            for item in types["enums"]
        },
        "enum_members": {
            item["name"]: {member["wire"]: member["name"] for member in item["members"]}
            for item in types["enums"]
        },
        "structs": {item["name"]: item["fields"] for item in types["structs"]},
        "unions": {
            item["name"]: {
                variant["wire"]: variant for variant in item["variants"]
            }
            for item in types["unions"]
        },
        "string_limits": {},
        "list_limits": {},
        "u32_limits": {},
        "default_string_limit": codec["max_string_bytes"],
        "default_list_limit": codec["max_collection_items"],
        "presence": [],
    }


def models(schema: dict[str, Any]) -> dict[str, dict[str, Any]]:
    """Every payload codec one contract declares, by function suffix."""
    found: dict[str, dict[str, Any]] = {}
    if "state_payload_codec" in schema:
        model = state_payload_model(schema)
        found[model["name"]] = model
    for codec in schema.get("binary_codecs", []):
        model = binary_codec_model(schema, codec)
        found[model["name"]] = model
    return found


def _string_limit(model: dict[str, Any], path: str) -> int:
    limit = model["string_limits"].get(path, model["default_string_limit"])
    if limit is None:
        raise ContractError(f"{model['name']}: no string bound for {path}")
    return limit


def _list_limit(model: dict[str, Any], path: str) -> int:
    limit = model["list_limits"].get(path, model["default_list_limit"])
    if limit is None:
        raise ContractError(f"{model['name']}: no collection bound for {path}")
    return limit


class _Writer:
    def __init__(self, model: dict[str, Any]) -> None:
        self.model = model
        self.bytes = bytearray()

    def raw(self, value: bytes) -> None:
        if len(self.bytes) + len(value) > self.model["max_bytes"]:
            raise CodecRefusal("SizeLimit")
        self.bytes.extend(value)

    def u32(self, value: int) -> None:
        self.raw(struct.pack("<I", value))

    def value(self, reference: TypeRef, item: Any, path: str) -> None:
        model = self.model
        if reference.kind == "optional":
            self.raw(bytes([0 if item is None else 1]))
            if item is not None:
                self.value(reference.inner, item, path)
            return
        if reference.kind == "list":
            if len(item) > _list_limit(model, path):
                raise CodecRefusal("CollectionLimit")
            self.u32(len(item))
            for member in item:
                self.value(reference.inner, member, path)
            return
        if reference.kind == "named":
            self.named(reference.name, item)
            return
        self.scalar(reference.name, item, path)

    def named(self, name: str, item: Any) -> None:
        model = self.model
        if name in model["enums"]:
            if item not in model["enums"][name]:
                raise CodecRefusal("InvalidEnum")
            self.u32(item)
            return
        if name in model["unions"]:
            for wire, variant in model["unions"][name].items():
                if variant["name"] != item["variant"]:
                    continue
                self.u32(wire)
                for field in variant["fields"]:
                    self.value(
                        parse_type(field["type"]),
                        item["fields"][field["name"]],
                        f"{name}.{field['name']}",
                    )
                return
            raise ContractError(f"{name}: no variant named {item['variant']!r}")
        for field in model["structs"][name]:
            self.value(
                parse_type(field["type"]),
                item[field["name"]],
                f"{name}.{field['name']}",
            )

    def scalar(self, name: str, item: Any, path: str) -> None:
        if name == "string":
            encoded = item.encode("utf-8")
            if len(encoded) > _string_limit(self.model, path):
                raise CodecRefusal("StringLimit")
            self.u32(len(encoded))
            self.raw(encoded)
            return
        if name == "bytes":
            if len(item) > self.model["max_bytes"]:
                raise CodecRefusal("SizeLimit")
            self.u32(len(item))
            self.raw(bytes(item))
            return
        if name == "bytes32":
            self.raw(bytes(item))
            return
        if name == "bool":
            self.raw(bytes([1 if item else 0]))
            return
        if name == "u32":
            limit = self.model["u32_limits"].get(path)
            if limit is not None and item > limit:
                raise CodecRefusal("ValueLimit")
            self.u32(item)
            return
        self.raw(struct.pack("<Q" if name == "u64" else "<q", item))


class _Reader:
    def __init__(self, model: dict[str, Any], data: bytes) -> None:
        self.model = model
        self.data = data
        self.offset = 0

    def take(self, length: int) -> bytes:
        end = self.offset + length
        if end > len(self.data):
            raise CodecRefusal("Truncated")
        value = self.data[self.offset : end]
        self.offset = end
        return value

    def u32(self) -> int:
        return int(struct.unpack("<I", self.take(4))[0])

    def flag(self) -> int:
        value = self.take(1)[0]
        if value > 1:
            raise CodecRefusal("InvalidBoolean")
        return value

    def value(self, reference: TypeRef, path: str) -> Any:
        if reference.kind == "optional":
            return self.value(reference.inner, path) if self.flag() else None
        if reference.kind == "list":
            length = self.u32()
            if length > _list_limit(self.model, path):
                raise CodecRefusal("CollectionLimit")
            return [self.value(reference.inner, path) for _ in range(length)]
        if reference.kind == "named":
            return self.named(reference.name)
        return self.scalar(reference.name, path)

    def named(self, name: str) -> Any:
        model = self.model
        if name in model["enums"]:
            wire = self.u32()
            if wire not in model["enums"][name]:
                raise CodecRefusal("InvalidEnum")
            return wire
        if name in model["unions"]:
            wire = self.u32()
            variant = model["unions"][name].get(wire)
            if variant is None:
                raise CodecRefusal("InvalidEnum")
            return {
                "variant": variant["name"],
                "fields": {
                    field["name"]: self.value(
                        parse_type(field["type"]), f"{name}.{field['name']}"
                    )
                    for field in variant["fields"]
                },
            }
        value = {
            field["name"]: self.value(
                parse_type(field["type"]), f"{name}.{field['name']}"
            )
            for field in model["structs"][name]
        }
        _check_presence(model, name, value)
        return value

    def scalar(self, name: str, path: str) -> Any:
        if name == "string":
            length = self.u32()
            if length > _string_limit(self.model, path):
                raise CodecRefusal("StringLimit")
            raw = self.take(length)
            try:
                return raw.decode("utf-8")
            except UnicodeDecodeError as error:
                raise CodecRefusal("InvalidUtf8") from error
        if name == "bytes":
            length = self.u32()
            if length > self.model["max_bytes"]:
                raise CodecRefusal("SizeLimit")
            return list(self.take(length))
        if name == "bytes32":
            return list(self.take(32))
        if name == "bool":
            return bool(self.flag())
        if name == "u32":
            value = self.u32()
            limit = self.model["u32_limits"].get(path)
            if limit is not None and value > limit:
                raise CodecRefusal("ValueLimit")
            return value
        return int(struct.unpack("<Q" if name == "u64" else "<q", self.take(8))[0])


def _check_presence(model: dict[str, Any], name: str, value: dict[str, Any]) -> None:
    """Apply the codec's conditional-presence rules to one decoded record."""
    fields = {field["name"]: field for field in model["structs"][name]}
    for rule in model["presence"]:
        if rule["struct"] != name:
            continue
        names = model["enum_members"][fields[rule["tag_field"]]["type"]]
        expected = names[value[rule["tag_field"]]] in rule["present_for"]
        if (value[rule["field"]] is not None) != expected:
            raise CodecRefusal("Malformed")


def encode(model: dict[str, Any], value: dict[str, Any]) -> bytes:
    """The bytes a conforming encoder writes, or a `CodecRefusal`."""
    if model["version_field"] and value[model["version_field"]] != model["schema_version"]:
        raise CodecRefusal("UnsupportedVersion")
    writer = _Writer(model)
    writer.raw(model["magic"])
    if model["version_is_prefix"]:
        writer.u32(model["schema_version"])
    writer.named(model["root"], value)
    return bytes(writer.bytes)


def decode(model: dict[str, Any], data: bytes) -> dict[str, Any]:
    """The record a conforming decoder reads, or a `CodecRefusal`."""
    if len(data) > model["max_bytes"]:
        raise CodecRefusal("SizeLimit")
    reader = _Reader(model, data)
    if reader.take(len(model["magic"])) != model["magic"]:
        raise CodecRefusal("InvalidMagic")
    if model["version_is_prefix"] and reader.u32() != model["schema_version"]:
        raise CodecRefusal("UnsupportedVersion")
    if model.get("version_is_first", False):
        root_offset = reader.offset
        if reader.u32() != model["schema_version"]:
            raise CodecRefusal("UnsupportedVersion")
        reader.offset = root_offset
    value = reader.named(model["root"])
    if reader.offset != len(data):
        raise CodecRefusal("TrailingBytes")
    if model["version_field"] and value[model["version_field"]] != model["schema_version"]:
        raise CodecRefusal("UnsupportedVersion")
    return value


def verdict(model: dict[str, Any], data: bytes) -> tuple[str, str | None]:
    """Decode one payload and report ``(verdict, generated error name)``."""
    try:
        decode(model, data)
    except CodecRefusal as refusal:
        return VERDICT_FOR_ERROR[refusal.error], refusal.error
    return "ACCEPT", None
