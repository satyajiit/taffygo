# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Render bounded deterministic Rust codecs for contract-owned records."""

from __future__ import annotations

from typing import Any, Callable

from binary_codec_helpers import (
    _decode_expression,
    _emit_encode,
    _reachable_types,
    _rust_type,
    _snake,
)
from binary_codec_schema import validate_binary_codecs

def render_rust_codecs(
    schema: dict[str, Any],
    parse_type: Callable[[str], Any],
    tagged_union_for: Callable[[dict[str, Any], str], dict[str, Any] | None],
) -> list[str]:
    rendered: list[str] = []
    for codec in schema.get("binary_codecs", []):
        root = codec["root"]
        suffix = codec["function_suffix"]
        error = "".join(part.capitalize() for part in suffix.split("_")) + "CodecError"
        encoder_name = "".join(part.capitalize() for part in suffix.split("_")) + "Encoder"
        decoder_name = "".join(part.capitalize() for part in suffix.split("_")) + "Decoder"
        enums, structs, unions = _reachable_types(codec, root, parse_type)
        enum_names = {item["name"] for item in enums}
        magic_values = ", ".join(str(value) for value in codec["magic"].encode("ascii"))
        max_limit = codec["max_bytes_limit"]
        collection_limit = codec["max_collection_items"]
        string_limit = codec["max_string_bytes"]
        version = codec["schema_version"]
        version_field = codec["schema_version_field"]

        for enum in enums:
            rendered += [
                "#[derive(Clone, Copy, Debug, Eq, PartialEq)]",
                "#[repr(u32)]",
                f"pub enum {enum['name']} {{",
            ]
            rendered += [
                f"    {member['name']} = {member['wire']}," for member in enum["members"]
            ]
            rendered += [
                "}",
                f"impl {enum['name']} {{",
                "    pub const fn from_wire(value: u32) -> Option<Self> {",
                "        match value {",
            ]
            rendered += [
                f"            {member['wire']} => Some(Self::{member['name']}),"
                for member in enum["members"]
            ]
            rendered += ["            _ => None,", "        }", "    }", "}", ""]
        for struct in structs:
            rendered += [
                "#[derive(Clone, Debug, Eq, PartialEq)]",
                f"pub struct {struct['name']} {{",
            ]
            rendered += [
                f"    pub {field['name']}: {_rust_type(parse_type(field['type']))},"
                for field in struct["fields"]
            ]
            rendered += ["}", ""]
        for union in unions:
            rendered += [
                "#[derive(Clone, Debug, Eq, PartialEq)]",
                f"pub enum {union['name']} {{",
            ]
            for variant in union["variants"]:
                if not variant["fields"]:
                    rendered.append(f"    {variant['name']},")
                    continue
                rendered.append(f"    {variant['name']} {{")
                rendered += [
                    f"        {field['name']}: {_rust_type(parse_type(field['type']))},"
                    for field in variant["fields"]
                ]
                rendered.append("    },")
            rendered += ["}", ""]

        rendered += [
            "#[derive(Clone, Copy, Debug, Eq, PartialEq)]",
            f"pub enum {error} {{",
            "    SizeLimit,",
            "    CollectionLimit,",
            "    StringLimit,",
            "    LengthOverflow,",
            "    Truncated,",
            "    InvalidMagic,",
            "    UnsupportedVersion,",
            "    InvalidBoolean,",
            "    InvalidEnum,",
            "    InvalidTaggedUnion,",
            "    InvalidUtf8,",
            "    TrailingBytes,",
            "}",
            "",
            f"pub const {suffix.upper()}_MAGIC: &[u8] = &[{magic_values}];",
            f"pub const {suffix.upper()}_SCHEMA_VERSION: u32 = {version};",
            f"const {suffix.upper()}_MAX_COLLECTION_ITEMS: usize = {collection_limit};",
            f"const {suffix.upper()}_MAX_STRING_BYTES: usize = {string_limit};",
            "",
            f"struct {encoder_name} {{",
            "    bytes: Vec<u8>,",
            "}",
            "",
            "#[allow(dead_code)]",
            f"impl {encoder_name} {{",
            f"    fn put_raw(&mut self, value: &[u8]) -> Result<(), {error}> {{",
            "        let length = self.bytes.len().checked_add(value.len())",
            f"            .ok_or({error}::LengthOverflow)?;",
            f"        if length > {max_limit} {{",
            f"            return Err({error}::SizeLimit);",
            "        }",
            "        self.bytes.extend_from_slice(value);",
            "        Ok(())",
            "    }",
            f"    fn put_len(&mut self, value: usize) -> Result<(), {error}> {{",
            f"        if value > {suffix.upper()}_MAX_COLLECTION_ITEMS {{",
            f"            return Err({error}::CollectionLimit);",
            "        }",
            f"        let value = u32::try_from(value).map_err(|_| {error}::LengthOverflow)?;",
            "        self.put_u32(value)",
            "    }",
            f"    fn put_bool(&mut self, value: bool) -> Result<(), {error}> {{ self.put_u8(u8::from(value)) }}",
            f"    fn put_u8(&mut self, value: u8) -> Result<(), {error}> {{ self.put_raw(&[value]) }}",
            f"    fn put_u32(&mut self, value: u32) -> Result<(), {error}> {{ self.put_raw(&value.to_le_bytes()) }}",
            f"    fn put_u64(&mut self, value: u64) -> Result<(), {error}> {{ self.put_raw(&value.to_le_bytes()) }}",
            f"    fn put_i64(&mut self, value: i64) -> Result<(), {error}> {{ self.put_raw(&value.to_le_bytes()) }}",
            f"    fn put_bytes32(&mut self, value: &[u8; 32]) -> Result<(), {error}> {{ self.put_raw(value) }}",
            f"    fn put_bytes(&mut self, value: &[u8]) -> Result<(), {error}> {{",
            f"        if value.len() > {max_limit} {{ return Err({error}::SizeLimit); }}",
            f"        let length = u32::try_from(value.len()).map_err(|_| {error}::LengthOverflow)?;",
            "        self.put_u32(length)?;",
            "        self.put_raw(value)",
            "    }",
            f"    fn put_string(&mut self, value: &str) -> Result<(), {error}> {{",
            f"        if value.len() > {suffix.upper()}_MAX_STRING_BYTES {{",
            f"            return Err({error}::StringLimit);",
            "        }",
            f"        let length = u32::try_from(value.len()).map_err(|_| {error}::LengthOverflow)?;",
            "        self.put_u32(length)?;",
            "        self.put_raw(value.as_bytes())",
            "    }",
            "}",
            "",
            f"struct {decoder_name}<'a> {{",
            "    bytes: &'a [u8],",
            "    offset: usize,",
            "}",
            "",
            "#[allow(dead_code)]",
            f"impl<'a> {decoder_name}<'a> {{",
            f"    fn take(&mut self, length: usize) -> Result<&'a [u8], {error}> {{",
            "        let end = self.offset.checked_add(length)",
            f"            .ok_or({error}::LengthOverflow)?;",
            f"        let value = self.bytes.get(self.offset..end).ok_or({error}::Truncated)?;",
            "        self.offset = end;",
            "        Ok(value)",
            "    }",
            f"    fn read_u8(&mut self) -> Result<u8, {error}> {{",
            f"        self.take(1)?.first().copied().ok_or({error}::Truncated)",
            "    }",
            f"    fn read_bool(&mut self) -> Result<bool, {error}> {{",
            f"        match self.read_u8()? {{ 0 => Ok(false), 1 => Ok(true), _ => Err({error}::InvalidBoolean) }}",
            "    }",
            f"    fn read_u32(&mut self) -> Result<u32, {error}> {{",
            f"        let value: [u8; 4] = self.take(4)?.try_into().map_err(|_| {error}::Truncated)?;",
            "        Ok(u32::from_le_bytes(value))",
            "    }",
            f"    fn read_u64(&mut self) -> Result<u64, {error}> {{",
            f"        let value: [u8; 8] = self.take(8)?.try_into().map_err(|_| {error}::Truncated)?;",
            "        Ok(u64::from_le_bytes(value))",
            "    }",
            f"    fn read_i64(&mut self) -> Result<i64, {error}> {{",
            f"        let value: [u8; 8] = self.take(8)?.try_into().map_err(|_| {error}::Truncated)?;",
            "        Ok(i64::from_le_bytes(value))",
            "    }",
            f"    fn read_bytes32(&mut self) -> Result<[u8; 32], {error}> {{",
            f"        self.take(32)?.try_into().map_err(|_| {error}::Truncated)",
            "    }",
            f"    fn read_length(&mut self) -> Result<usize, {error}> {{",
            "        let value = usize::try_from(self.read_u32()?)",
            f"            .map_err(|_| {error}::LengthOverflow)?;",
            f"        if value > {suffix.upper()}_MAX_COLLECTION_ITEMS {{",
            f"            return Err({error}::CollectionLimit);",
            "        }",
            "        Ok(value)",
            "    }",
            f"    fn read_bytes(&mut self) -> Result<Vec<u8>, {error}> {{",
            "        let length = usize::try_from(self.read_u32()?)",
            f"            .map_err(|_| {error}::LengthOverflow)?;",
            f"        if length > {max_limit} {{ return Err({error}::SizeLimit); }}",
            "        Ok(self.take(length)?.to_vec())",
            "    }",
            f"    fn read_string(&mut self) -> Result<String, {error}> {{",
            "        let length = usize::try_from(self.read_u32()?)",
            f"            .map_err(|_| {error}::LengthOverflow)?;",
            f"        if length > {suffix.upper()}_MAX_STRING_BYTES {{",
            f"            return Err({error}::StringLimit);",
            "        }",
            f"        let value = core::str::from_utf8(self.take(length)?).map_err(|_| {error}::InvalidUtf8)?;",
            "        Ok(value.to_owned())",
            "    }",
            f"    fn read_optional<T>(&mut self, read: impl FnOnce(&mut Self) -> Result<T, {error}>) -> Result<Option<T>, {error}> {{",
            f"        match self.read_u8()? {{ 0 => Ok(None), 1 => read(self).map(Some), _ => Err({error}::InvalidBoolean) }}",
            "    }",
            f"    fn read_list<T>(&mut self, mut read: impl FnMut(&mut Self) -> Result<T, {error}>) -> Result<Vec<T>, {error}> {{",
            "        let length = self.read_length()?;",
            "        let mut values = Vec::with_capacity(length);",
            "        for _ in 0..length { values.push(read(self)?); }",
            "        Ok(values)",
            "    }",
            "}",
            "",
        ]

        for enum in enums:
            function = _snake(enum["name"])
            rendered += [
                f"fn encode_{function}(encoder: &mut {encoder_name}, value: &{enum['name']}) -> Result<(), {error}> {{",
                "    encoder.put_u32(*value as u32)",
                "}",
                f"fn decode_{function}(decoder: &mut {decoder_name}<'_>) -> Result<{enum['name']}, {error}> {{",
                f"    {enum['name']}::from_wire(decoder.read_u32()?).ok_or({error}::InvalidEnum)",
                "}",
                "",
            ]

        for struct in structs:
            function = _snake(struct["name"])
            rendered.append(
                f"fn encode_{function}(encoder: &mut {encoder_name}, value: &{struct['name']}) -> Result<(), {error}> {{"
            )
            for field in struct["fields"]:
                rendered += _emit_encode(
                    parse_type(field["type"]),
                    f"value.{field['name']}",
                    "    ",
                    enum_names,
                )
            rendered += ["    Ok(())", "}"]
            rendered += [
                f"fn decode_{function}(decoder: &mut {decoder_name}<'_>) -> Result<{struct['name']}, {error}> {{",
                f"    let value = {struct['name']} {{",
            ]
            for field in struct["fields"]:
                rendered.append(
                    f"        {field['name']}: {_decode_expression(parse_type(field['type']), enum_names)},"
                )
            rendered += ["    };"]
            rendered += ["    Ok(value)", "}", ""]

        for union in unions:
            function = _snake(union["name"])
            rendered += [
                f"fn encode_{function}(encoder: &mut {encoder_name}, value: &{union['name']}) -> Result<(), {error}> {{",
                "    match value {",
            ]
            for variant in union["variants"]:
                fields = variant["fields"]
                if fields:
                    pattern = ", ".join(field["name"] for field in fields)
                    rendered.append(
                        f"        {union['name']}::{variant['name']} {{ {pattern} }} => {{"
                    )
                else:
                    rendered.append(f"        {union['name']}::{variant['name']} => {{")
                rendered.append(f"            encoder.put_u32({variant['wire']})?;")
                for field in fields:
                    rendered += _emit_encode(
                        parse_type(field["type"]),
                        field["name"],
                        "            ",
                        enum_names,
                        True,
                    )
                rendered.append("        }")
            rendered += ["    }", "    Ok(())", "}"]
            rendered += [
                f"fn decode_{function}(decoder: &mut {decoder_name}<'_>) -> Result<{union['name']}, {error}> {{",
                "    match decoder.read_u32()? {",
            ]
            for variant in union["variants"]:
                fields = variant["fields"]
                if not fields:
                    rendered.append(
                        f"        {variant['wire']} => Ok({union['name']}::{variant['name']}),"
                    )
                    continue
                rendered += [
                    f"        {variant['wire']} => Ok({union['name']}::{variant['name']} {{",
                ]
                for field in fields:
                    rendered.append(
                        f"            {field['name']}: {_decode_expression(parse_type(field['type']), enum_names)},"
                    )
                rendered += ["        }),"]
            rendered += [f"        _ => Err({error}::InvalidEnum),", "    }", "}", ""]

        root_function = _snake(root)
        rendered += [
            f"pub fn encode_{suffix}(value: &{root}) -> Result<Vec<u8>, {error}> {{",
            f"    if value.{version_field} != {suffix.upper()}_SCHEMA_VERSION {{",
            f"        return Err({error}::UnsupportedVersion);",
            "    }",
            f"    let mut encoder = {encoder_name} {{ bytes: Vec::new() }};",
            f"    encoder.put_raw({suffix.upper()}_MAGIC)?;",
            f"    encode_{root_function}(&mut encoder, value)?;",
            "    Ok(encoder.bytes)",
            "}",
            "",
            f"pub fn decode_{suffix}(bytes: &[u8]) -> Result<{root}, {error}> {{",
            f"    if bytes.len() > {max_limit} {{",
            f"        return Err({error}::SizeLimit);",
            "    }",
            f"    let mut decoder = {decoder_name} {{ bytes, offset: 0 }};",
            f"    if decoder.take({suffix.upper()}_MAGIC.len())? != {suffix.upper()}_MAGIC {{",
            f"        return Err({error}::InvalidMagic);",
            "    }",
            "    let root_offset = decoder.offset;",
            f"    if decoder.read_u32()? != {suffix.upper()}_SCHEMA_VERSION {{",
            f"        return Err({error}::UnsupportedVersion);",
            "    }",
            "    decoder.offset = root_offset;",
            f"    let value = decode_{root_function}(&mut decoder)?;",
            "    if decoder.offset != bytes.len() {",
            f"        return Err({error}::TrailingBytes);",
            "    }",
            f"    if value.{version_field} != {suffix.upper()}_SCHEMA_VERSION {{",
            f"        return Err({error}::UnsupportedVersion);",
            "    }",
            "    Ok(value)",
            "}",
            "",
        ]
    return rendered
