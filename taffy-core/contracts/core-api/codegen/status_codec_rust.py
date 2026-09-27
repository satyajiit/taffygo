# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate the bounded Rust CoreStatus payload codec.

The public Core API records remain the only semantic model.  This module emits
one Rust codec that reads and writes those generated records directly; it does
not create a second payload type or a second hand-maintained wire format.
"""

from __future__ import annotations

from typing import Any

from contract_schema import TypeRef, parse_type
from status_codec_schema import reachable_types, snake, validate_codec_schema
from status_codec_rust_validation import render_validation


def _encode_lines(
    reference: TypeRef,
    expression: str,
    path: str,
    indent: str,
    enum_names: set[str],
    codec: dict[str, Any],
    referenced: bool = False,
) -> list[str]:
    if reference.kind == "optional":
        lines = [f"{indent}match &{expression} {{", f"{indent}    Some(value) => {{"]
        lines.append(f"{indent}        encoder.put_bool(true)?;")
        lines += _encode_lines(
            reference.inner, "value", path, indent + "        ", enum_names, codec, True
        )
        return lines + [
            f"{indent}    }}",
            f"{indent}    None => encoder.put_bool(false)?,",
            f"{indent}}}",
        ]
    if reference.kind == "list":
        limit = codec["list_limits"][path]
        lines = [
            f"{indent}encoder.put_length({expression}.len(), {limit})?;",
            f"{indent}for item in &{expression} {{",
        ]
        lines += _encode_lines(
            reference.inner, "item", path, indent + "    ", enum_names, codec, True
        )
        return lines + [f"{indent}}}"]
    if reference.kind == "named":
        if reference.name in enum_names:
            value = f"*{expression}" if referenced else expression
            return [f"{indent}encoder.put_u32({value} as u32)?;"]
        value = expression if referenced else f"&{expression}"
        return [f"{indent}encode_{snake(reference.name)}(encoder, {value})?;"]
    if reference.name == "string":
        value = expression if referenced else f"&{expression}"
        return [
            f"{indent}encoder.put_string({value}, {codec['string_limits'][path]})?;"
        ]
    value = f"*{expression}" if referenced else expression
    if reference.name == "u32" and path in codec["u32_limits"]:
        return [
            f"{indent}if !usize::try_from({value})",
            f"{indent}    .is_ok_and(|value| value <= {codec['u32_limits'][path]})",
            f"{indent}{{",
            f"{indent}    return Err(CoreStatusPayloadCodecError::ValueLimit);",
            f"{indent}}}",
            f"{indent}encoder.put_u32({value})?;",
        ]
    method = {"bool": "put_bool", "u32": "put_u32", "u64": "put_u64"}[reference.name]
    return [f"{indent}encoder.{method}({value})?;"]


def _decode_result_expression(
    reference: TypeRef,
    path: str,
    enum_names: set[str],
    codec: dict[str, Any],
) -> str:
    if reference.kind == "optional":
        inner = _decode_result_expression(reference.inner, path, enum_names, codec)
        return f"decoder.read_optional(|decoder| {inner})"
    if reference.kind == "list":
        inner = _decode_result_expression(reference.inner, path, enum_names, codec)
        limit = codec["list_limits"][path]
        return f"decoder.read_list({limit}, |decoder| {inner})"
    if reference.kind == "named":
        if reference.name in enum_names:
            error = "CoreStatusPayloadCodecError::InvalidEnum"
            return (
                "decoder.read_u32().and_then(|value| "
                f"{reference.name}::from_wire(value).ok_or({error}))"
            )
        return f"decode_{snake(reference.name)}(decoder)"
    if reference.name == "string":
        return f"decoder.read_string({codec['string_limits'][path]})"
    if reference.name == "u32" and path in codec["u32_limits"]:
        limit = codec["u32_limits"][path]
        return f"decoder.read_bounded_u32({limit})"
    return f"decoder.read_{reference.name}()"


def _decode_expression(
    reference: TypeRef,
    path: str,
    enum_names: set[str],
    codec: dict[str, Any],
) -> str:
    return f"{_decode_result_expression(reference, path, enum_names, codec)}?"


def render_status_codec(schema: dict[str, Any]) -> str:
    codec = validate_codec_schema(schema)
    enums, structs = reachable_types(schema, codec["root"])
    enum_names = {item["name"] for item in enums}
    imports = sorted({item["name"] for item in enums + structs})
    limits = sorted(
        {
            schema["state_payload_codec"]["max_bytes_limit"],
            *codec["string_limits"].values(),
            *codec["list_limits"].values(),
            *codec["u32_limits"].values(),
        }
    )
    magic = ", ".join(str(value) for value in codec["magic"].encode("ascii"))
    lines = [
        "// Copyright (c) 2026 Matterward Labs Private Limited.",
        "//",
        "// This Source Code Form is subject to the terms of the Mozilla Public",
        "// License, v. 2.0. If a copy of the MPL was not distributed with this",
        "// file, You can obtain one at https://mozilla.org/MPL/2.0/.",
        "//",
        "// Generated from schema/contract.json. Do not edit.",
        f"// Contract core_api {schema['version']['major']}.{schema['version']['minor']} state payload.",
        "",
        "use crate::{",
        *[f"    {name}," for name in imports + limits],
        "};",
        "",
        "#[derive(Clone, Copy, Debug, Eq, PartialEq)]",
        "pub enum CoreStatusPayloadCodecError {",
        "    SizeLimit,",
        "    CollectionLimit,",
        "    StringLimit,",
        "    ValueLimit,",
        "    LengthOverflow,",
        "    Truncated,",
        "    InvalidMagic,",
        "    UnsupportedVersion,",
        "    InvalidBoolean,",
        "    InvalidEnum,",
        "    InvalidUtf8,",
        "    Malformed,",
        "    TrailingBytes,",
        "}",
        "",
        f"pub const CORE_STATUS_PAYLOAD_MAGIC: &[u8] = &[{magic}];",
        f"pub const CORE_STATUS_PAYLOAD_SCHEMA_VERSION: u32 = {codec['schema_version']};",
        "",
        "struct CoreStatusPayloadEncoder {",
        "    bytes: Option<Vec<u8>>,",
        "    length: usize,",
        "}",
        "",
        "impl CoreStatusPayloadEncoder {",
        "    fn put_raw(&mut self, value: &[u8]) -> Result<(), CoreStatusPayloadCodecError> {",
        "        let length = self.length.checked_add(value.len())",
        "            .ok_or(CoreStatusPayloadCodecError::LengthOverflow)?;",
        "        if length > MAX_EVENT_PAYLOAD_BYTES {",
        "            return Err(CoreStatusPayloadCodecError::SizeLimit);",
        "        }",
        "        if let Some(bytes) = self.bytes.as_mut() {",
        "            bytes.extend_from_slice(value);",
        "        }",
        "        self.length = length;",
        "        Ok(())",
        "    }",
        "",
        "    fn put_bool(&mut self, value: bool) -> Result<(), CoreStatusPayloadCodecError> {",
        "        self.put_raw(&[u8::from(value)])",
        "    }",
        "",
        "    fn put_u32(&mut self, value: u32) -> Result<(), CoreStatusPayloadCodecError> {",
        "        self.put_raw(&value.to_le_bytes())",
        "    }",
        "",
        "    fn put_u64(&mut self, value: u64) -> Result<(), CoreStatusPayloadCodecError> {",
        "        self.put_raw(&value.to_le_bytes())",
        "    }",
        "",
        "    fn put_length(",
        "        &mut self,",
        "        value: usize,",
        "        limit: usize,",
        "    ) -> Result<(), CoreStatusPayloadCodecError> {",
        "        if value > limit {",
        "            return Err(CoreStatusPayloadCodecError::CollectionLimit);",
        "        }",
        "        let value = u32::try_from(value)",
        "            .map_err(|_| CoreStatusPayloadCodecError::LengthOverflow)?;",
        "        self.put_u32(value)",
        "    }",
        "",
        "    fn put_string(",
        "        &mut self,",
        "        value: &str,",
        "        limit: usize,",
        "    ) -> Result<(), CoreStatusPayloadCodecError> {",
        "        if value.len() > limit {",
        "            return Err(CoreStatusPayloadCodecError::StringLimit);",
        "        }",
        "        let length = u32::try_from(value.len())",
        "            .map_err(|_| CoreStatusPayloadCodecError::LengthOverflow)?;",
        "        self.put_u32(length)?;",
        "        self.put_raw(value.as_bytes())",
        "    }",
        "}",
        "",
        "struct CoreStatusPayloadDecoder<'a> {",
        "    bytes: &'a [u8],",
        "    offset: usize,",
        "}",
        "",
        "impl<'a> CoreStatusPayloadDecoder<'a> {",
        "    fn take(&mut self, length: usize) -> Result<&'a [u8], CoreStatusPayloadCodecError> {",
        "        let end = self.offset.checked_add(length)",
        "            .ok_or(CoreStatusPayloadCodecError::LengthOverflow)?;",
        "        let value = self.bytes.get(self.offset..end)",
        "            .ok_or(CoreStatusPayloadCodecError::Truncated)?;",
        "        self.offset = end;",
        "        Ok(value)",
        "    }",
        "",
        "    fn read_bool(&mut self) -> Result<bool, CoreStatusPayloadCodecError> {",
        "        match self.take(1)?.first().copied() {",
        "            Some(0) => Ok(false),",
        "            Some(1) => Ok(true),",
        "            Some(_) => Err(CoreStatusPayloadCodecError::InvalidBoolean),",
        "            None => Err(CoreStatusPayloadCodecError::Truncated),",
        "        }",
        "    }",
        "",
        "    fn read_u32(&mut self) -> Result<u32, CoreStatusPayloadCodecError> {",
        "        let value: [u8; 4] = self.take(4)?.try_into()",
        "            .map_err(|_| CoreStatusPayloadCodecError::Truncated)?;",
        "        Ok(u32::from_le_bytes(value))",
        "    }",
        "",
        "    fn read_bounded_u32(",
        "        &mut self,",
        "        limit: usize,",
        "    ) -> Result<u32, CoreStatusPayloadCodecError> {",
        "        let value = self.read_u32()?;",
        "        if !usize::try_from(value).is_ok_and(|value| value <= limit) {",
        "            return Err(CoreStatusPayloadCodecError::ValueLimit);",
        "        }",
        "        Ok(value)",
        "    }",
        "",
        "    fn read_u64(&mut self) -> Result<u64, CoreStatusPayloadCodecError> {",
        "        let value: [u8; 8] = self.take(8)?.try_into()",
        "            .map_err(|_| CoreStatusPayloadCodecError::Truncated)?;",
        "        Ok(u64::from_le_bytes(value))",
        "    }",
        "",
        "    fn read_length(&mut self, limit: usize) -> Result<usize, CoreStatusPayloadCodecError> {",
        "        let value = usize::try_from(self.read_u32()?)",
        "            .map_err(|_| CoreStatusPayloadCodecError::LengthOverflow)?;",
        "        if value > limit {",
        "            return Err(CoreStatusPayloadCodecError::CollectionLimit);",
        "        }",
        "        Ok(value)",
        "    }",
        "",
        "    fn read_string(&mut self, limit: usize) -> Result<String, CoreStatusPayloadCodecError> {",
        "        let length = usize::try_from(self.read_u32()?)",
        "            .map_err(|_| CoreStatusPayloadCodecError::LengthOverflow)?;",
        "        if length > limit {",
        "            return Err(CoreStatusPayloadCodecError::StringLimit);",
        "        }",
        "        let value = core::str::from_utf8(self.take(length)?)",
        "            .map_err(|_| CoreStatusPayloadCodecError::InvalidUtf8)?;",
        "        Ok(value.to_owned())",
        "    }",
        "",
        "    fn read_optional<T>(",
        "        &mut self,",
        "        read: impl FnOnce(&mut Self) -> Result<T, CoreStatusPayloadCodecError>,",
        "    ) -> Result<Option<T>, CoreStatusPayloadCodecError> {",
        "        if self.read_bool()? { read(self).map(Some) } else { Ok(None) }",
        "    }",
        "",
        "    fn read_list<T>(",
        "        &mut self,",
        "        limit: usize,",
        "        mut read: impl FnMut(&mut Self) -> Result<T, CoreStatusPayloadCodecError>,",
        "    ) -> Result<Vec<T>, CoreStatusPayloadCodecError> {",
        "        let length = self.read_length(limit)?;",
        "        let mut values = Vec::with_capacity(length);",
        "        for _ in 0..length {",
        "            values.push(read(self)?);",
        "        }",
        "        Ok(values)",
        "    }",
        "}",
        "",
    ]

    for owner in structs:
        function = snake(owner["name"])
        validation = render_validation(owner, schema, codec, "    ")
        if validation:
            lines.append(
                f"fn validate_{function}(value: &{owner['name']}) "
                "-> Result<(), CoreStatusPayloadCodecError> {"
            )
            lines += validation
            lines += ["    Ok(())", "}", ""]
        lines.append(
            f"fn encode_{function}(encoder: &mut CoreStatusPayloadEncoder, "
            f"value: &{owner['name']}) -> Result<(), CoreStatusPayloadCodecError> {{"
        )
        if validation:
            lines.append(f"    validate_{function}(value)?;")
        for field in owner["fields"]:
            path = f"{owner['name']}.{field['name']}"
            lines += _encode_lines(
                parse_type(field["type"]),
                f"value.{field['name']}",
                path,
                "    ",
                enum_names,
                codec,
            )
        lines += ["    Ok(())", "}", ""]
        lines += [
            f"fn decode_{function}(decoder: &mut CoreStatusPayloadDecoder<'_>)",
            f"    -> Result<{owner['name']}, CoreStatusPayloadCodecError>",
            "{",
            f"    let value = {owner['name']} {{",
        ]
        for field in owner["fields"]:
            path = f"{owner['name']}.{field['name']}"
            expression = _decode_expression(
                parse_type(field["type"]), path, enum_names, codec
            )
            lines.append(f"        {field['name']}: {expression},")
        lines.append("    };")
        if validation:
            lines.append(f"    validate_{function}(&value)?;")
        lines += ["    Ok(value)", "}", ""]

    root_function = snake(codec["root"])
    lines += [
        "pub fn encode_core_status_payload(",
        "    value: &CoreStatus,",
        ") -> Result<Vec<u8>, CoreStatusPayloadCodecError> {",
        "    let mut encoder = CoreStatusPayloadEncoder { bytes: Some(Vec::new()), length: 0 };",
        "    encoder.put_raw(CORE_STATUS_PAYLOAD_MAGIC)?;",
        "    encoder.put_u32(CORE_STATUS_PAYLOAD_SCHEMA_VERSION)?;",
        f"    encode_{root_function}(&mut encoder, value)?;",
        "    encoder.bytes.ok_or(CoreStatusPayloadCodecError::LengthOverflow)",
        "}",
        "",
        "/// Measures the exact encoded payload path without allocating its byte buffer.",
        "pub fn measure_core_status_payload(",
        "    value: &CoreStatus,",
        ") -> Result<usize, CoreStatusPayloadCodecError> {",
        "    let mut encoder = CoreStatusPayloadEncoder { bytes: None, length: 0 };",
        "    encoder.put_raw(CORE_STATUS_PAYLOAD_MAGIC)?;",
        "    encoder.put_u32(CORE_STATUS_PAYLOAD_SCHEMA_VERSION)?;",
        f"    encode_{root_function}(&mut encoder, value)?;",
        "    Ok(encoder.length)",
        "}",
        "",
        "pub fn decode_core_status_payload(",
        "    bytes: &[u8],",
        ") -> Result<CoreStatus, CoreStatusPayloadCodecError> {",
        "    if bytes.len() > MAX_EVENT_PAYLOAD_BYTES {",
        "        return Err(CoreStatusPayloadCodecError::SizeLimit);",
        "    }",
        "    let mut decoder = CoreStatusPayloadDecoder { bytes, offset: 0 };",
        "    if decoder.take(CORE_STATUS_PAYLOAD_MAGIC.len())? != CORE_STATUS_PAYLOAD_MAGIC {",
        "        return Err(CoreStatusPayloadCodecError::InvalidMagic);",
        "    }",
        "    if decoder.read_u32()? != CORE_STATUS_PAYLOAD_SCHEMA_VERSION {",
        "        return Err(CoreStatusPayloadCodecError::UnsupportedVersion);",
        "    }",
        f"    let value = decode_{root_function}(&mut decoder)?;",
        "    if decoder.offset != bytes.len() {",
        "        return Err(CoreStatusPayloadCodecError::TrailingBytes);",
        "    }",
        "    Ok(value)",
        "}",
        "",
    ]
    return "\n".join(lines)
