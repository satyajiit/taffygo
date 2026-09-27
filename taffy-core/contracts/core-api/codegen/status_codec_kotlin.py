# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Render the Kotlin half of the generated CoreStatus payload codec."""

from __future__ import annotations

from typing import Any

from contract_renderers import pascal
from contract_schema import TypeRef, parse_type
from status_codec import reachable_types, snake, validate_codec_schema


def _encode_lines(
    reference: TypeRef,
    expression: str,
    path: str,
    indent: str,
    enum_names: set[str],
    codec: dict[str, Any],
) -> list[str]:
    if reference.kind == "optional":
        lines = [f"{indent}run {{", f"{indent}    val optional = {expression}"]
        lines += [f"{indent}    if (optional == null) {{", f"{indent}        encoder.putBoolean(false)"]
        lines += [f"{indent}    }} else {{", f"{indent}        encoder.putBoolean(true)"]
        lines += _encode_lines(reference.inner, "optional", path, indent + "        ", enum_names, codec)
        return lines + [f"{indent}    }}", f"{indent}}}"]
    if reference.kind == "list":
        limit = codec["list_limits"][path]
        lines = [
            f"{indent}encoder.putLength({expression}.size, {limit})",
            f"{indent}for (item in {expression}) {{",
        ]
        lines += _encode_lines(reference.inner, "item", path, indent + "    ", enum_names, codec)
        return lines + [f"{indent}}}"]
    if reference.kind == "named":
        if reference.name in enum_names:
            return [f"{indent}encode{reference.name}(encoder, {expression})"]
        return [f"{indent}encode{reference.name}(encoder, {expression})"]
    if reference.name == "string":
        return [f"{indent}encoder.putString({expression}, {codec['string_limits'][path]})"]
    if reference.name == "u32" and path in codec["u32_limits"]:
        return [f"{indent}encoder.putBoundedUInt({expression}, {codec['u32_limits'][path]}.toUInt())"]
    method = {"bool": "putBoolean", "u32": "putUInt", "u64": "putULong"}[reference.name]
    return [f"{indent}encoder.{method}({expression})"]


def _decode_expression(
    reference: TypeRef,
    path: str,
    enum_names: set[str],
    codec: dict[str, Any],
) -> str:
    if reference.kind == "optional":
        inner = _decode_expression(reference.inner, path, enum_names, codec)
        return f"if (decoder.readBoolean()) {inner} else null"
    if reference.kind == "list":
        inner = _decode_expression(reference.inner, path, enum_names, codec)
        return f"decoder.readList({codec['list_limits'][path]}) {{ {inner} }}"
    if reference.kind == "named":
        if reference.name in enum_names:
            return f"decode{reference.name}(decoder)"
        return f"decode{reference.name}(decoder)"
    if reference.name == "string":
        return f"decoder.readString({codec['string_limits'][path]})"
    if reference.name == "u32" and path in codec["u32_limits"]:
        return f"decoder.readBoundedUInt({codec['u32_limits'][path]}.toUInt())"
    method = {"bool": "Boolean", "u32": "UInt", "u64": "ULong"}[reference.name]
    return f"decoder.read{method}()"


def _validation(
    owner: dict[str, Any], schema: dict[str, Any], codec: dict[str, Any]
) -> list[str]:
    lines: list[str] = []
    for rule in codec["presence_rules"]:
        if rule["struct"] != owner["name"]:
            continue
        expected = " || ".join(
            f"value.{rule['tag_field']} == {owner_field_type(owner, rule['tag_field'])}.{member}"
            for member in rule["present_for"]
        )
        lines += [
            f"    if (({expected}) != (value.{rule['field']} != null)) {{",
            "        fail(CoreStatusPayloadCodecError.MALFORMED)",
            "    }",
        ]
    enum_map = {item["name"]: item for item in schema["enums"]}
    for rule in codec["ordered_enum_list_rules"]:
        if rule["struct"] != owner["name"]:
            continue
        tag_type = owner_field_type(owner, rule["tag_field"])
        exact = " || ".join(
            f"value.{rule['tag_field']} == {tag_type}.{member}"
            for member in rule["exact_for"]
        )
        enum_type = rule["enum"]
        expected = ", ".join(
            f"{enum_type}.{member['name']}" for member in enum_map[enum_type]["members"]
        )
        item_path = ".".join(["item", *rule["enum_path"].split(".")])
        lines += [
            f"    if ({exact}) {{",
            f"        val expected = listOf({expected})",
            f"        if (value.{rule['list_field']}.size != expected.size ||",
            f"            value.{rule['list_field']}.zip(expected).any {{ (item, member) ->",
            f"                {item_path} != member",
            "            }",
            "        ) {",
            "            fail(CoreStatusPayloadCodecError.MALFORMED)",
            "        }",
            "    }",
        ]
        if rule["empty_for"]:
            empty = " || ".join(
                f"value.{rule['tag_field']} == {tag_type}.{member}"
                for member in rule["empty_for"]
            )
            lines += [
                f"    if (({empty}) && value.{rule['list_field']}.isNotEmpty()) {{",
                "        fail(CoreStatusPayloadCodecError.MALFORMED)",
                "    }",
            ]
    return lines


def owner_field_type(owner: dict[str, Any], name: str) -> str:
    return next(field["type"] for field in owner["fields"] if field["name"] == name)


def render_kotlin_status_codec(schema: dict[str, Any]) -> str:
    codec = validate_codec_schema(schema)
    enums, structs = reachable_types(schema, codec["root"])
    enum_names = {item["name"] for item in enums}
    magic = ", ".join(f"{value}.toByte()" for value in codec["magic"].encode("ascii"))
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
        "package taffy.core_api",
        "",
        "enum class CoreStatusPayloadCodecError {",
        "    SIZE_LIMIT, COLLECTION_LIMIT, STRING_LIMIT, VALUE_LIMIT, LENGTH_OVERFLOW,",
        "    TRUNCATED, INVALID_MAGIC, UNSUPPORTED_VERSION, INVALID_BOOLEAN,",
        "    INVALID_ENUM, INVALID_UTF8, MALFORMED, TRAILING_BYTES,",
        "}",
        "",
        "sealed interface CoreStatusPayloadEncodeResult {",
        "    data class Success(val bytes: ByteArray) : CoreStatusPayloadEncodeResult",
        "    data class Failure(val error: CoreStatusPayloadCodecError) : CoreStatusPayloadEncodeResult",
        "}",
        "",
        "sealed interface CoreStatusPayloadDecodeResult {",
        "    data class Success(val value: CoreStatus) : CoreStatusPayloadDecodeResult",
        "    data class Failure(val error: CoreStatusPayloadCodecError) : CoreStatusPayloadDecodeResult",
        "}",
        "",
        f"private val CORE_STATUS_PAYLOAD_MAGIC: ByteArray = byteArrayOf({magic})",
        f"const val CORE_STATUS_PAYLOAD_SCHEMA_VERSION: UInt = {codec['schema_version']}u",
        "",
        "private class CoreStatusCodecFailure(val reason: CoreStatusPayloadCodecError) : Exception()",
        "",
        "private fun fail(reason: CoreStatusPayloadCodecError): Nothing = throw CoreStatusCodecFailure(reason)",
        "",
        "private class CoreStatusPayloadEncoder {",
        "    private val bytes = ByteArray(MAX_EVENT_PAYLOAD_BYTES)",
        "    private var offset = 0",
        "",
        "    fun finish(): ByteArray = bytes.copyOf(offset)",
        "",
        "    fun putRaw(value: ByteArray) {",
        "        if (value.size > bytes.size - offset) fail(CoreStatusPayloadCodecError.SIZE_LIMIT)",
        "        value.copyInto(bytes, offset)",
        "        offset += value.size",
        "    }",
        "",
        "    fun putBoolean(value: Boolean) = putByte(if (value) 1 else 0)",
        "",
        "    private fun putByte(value: Int) {",
        "        if (offset >= bytes.size) fail(CoreStatusPayloadCodecError.SIZE_LIMIT)",
        "        bytes[offset++] = value.toByte()",
        "    }",
        "",
        "    fun putUInt(value: UInt) {",
        "        repeat(4) { shift -> putByte((value shr (shift * 8)).toInt()) }",
        "    }",
        "",
        "    fun putBoundedUInt(value: UInt, limit: UInt) {",
        "        if (value > limit) fail(CoreStatusPayloadCodecError.VALUE_LIMIT)",
        "        putUInt(value)",
        "    }",
        "",
        "    fun putULong(value: ULong) {",
        "        repeat(8) { shift -> putByte((value shr (shift * 8)).toInt()) }",
        "    }",
        "",
        "    fun putLength(value: Int, limit: Int) {",
        "        if (value < 0 || value > limit) fail(CoreStatusPayloadCodecError.COLLECTION_LIMIT)",
        "        putUInt(value.toUInt())",
        "    }",
        "",
        "    fun putString(value: String, limit: Int) {",
        "        val encoded = value.encodeToByteArray()",
        "        if (encoded.size > limit) fail(CoreStatusPayloadCodecError.STRING_LIMIT)",
        "        putUInt(encoded.size.toUInt())",
        "        putRaw(encoded)",
        "    }",
        "}",
        "",
        "private class CoreStatusPayloadDecoder(private val bytes: ByteArray) {",
        "    var offset: Int = 0",
        "        private set",
        "",
        "    fun take(length: Int): ByteArray {",
        "        if (length < 0 || length > bytes.size - offset) fail(CoreStatusPayloadCodecError.TRUNCATED)",
        "        val value = bytes.copyOfRange(offset, offset + length)",
        "        offset += length",
        "        return value",
        "    }",
        "",
        "    fun readBoolean(): Boolean = when (val value = readByte()) {",
        "        0 -> false",
        "        1 -> true",
        "        else -> fail(CoreStatusPayloadCodecError.INVALID_BOOLEAN)",
        "    }",
        "",
        "    private fun readByte(): Int {",
        "        if (offset >= bytes.size) fail(CoreStatusPayloadCodecError.TRUNCATED)",
        "        return bytes[offset++].toUByte().toInt()",
        "    }",
        "",
        "    fun readUInt(): UInt {",
        "        var value = 0u",
        "        repeat(4) { shift -> value = value or (readByte().toUInt() shl (shift * 8)) }",
        "        return value",
        "    }",
        "",
        "    fun readBoundedUInt(limit: UInt): UInt {",
        "        val value = readUInt()",
        "        if (value > limit) fail(CoreStatusPayloadCodecError.VALUE_LIMIT)",
        "        return value",
        "    }",
        "",
        "    fun readULong(): ULong {",
        "        var value = 0uL",
        "        repeat(8) { shift -> value = value or (readByte().toULong() shl (shift * 8)) }",
        "        return value",
        "    }",
        "",
        "    private fun readLength(limit: Int): Int {",
        "        val value = readUInt()",
        "        if (value > limit.toUInt()) fail(CoreStatusPayloadCodecError.COLLECTION_LIMIT)",
        "        return value.toInt()",
        "    }",
        "",
        "    fun readString(limit: Int): String {",
        "        val length = readUInt()",
        "        if (length > limit.toUInt()) fail(CoreStatusPayloadCodecError.STRING_LIMIT)",
        "        val byteLength = length.toInt()",
        "        if (byteLength > bytes.size - offset) fail(CoreStatusPayloadCodecError.TRUNCATED)",
        "        val start = offset",
        "        val end = start + byteLength",
        "        return try {",
        "            val value = bytes.decodeToString(",
        "                startIndex = start,",
        "                endIndex = end,",
        "                throwOnInvalidSequence = true,",
        "            )",
        "            offset = end",
        "            value",
        "        } catch (_: java.nio.charset.CharacterCodingException) {",
        "            fail(CoreStatusPayloadCodecError.INVALID_UTF8)",
        "        }",
        "    }",
        "",
        "    fun <T> readList(limit: Int, read: () -> T): List<T> {",
        "        val length = readLength(limit)",
        "        return buildList(length) { repeat(length) { add(read()) } }",
        "    }",
        "}",
        "",
    ]
    for enum in enums:
        lines += [
            f"private fun encode{enum['name']}(encoder: CoreStatusPayloadEncoder, value: {enum['name']}) =",
            "    encoder.putUInt(value.wire)",
            "",
            f"private fun decode{enum['name']}(decoder: CoreStatusPayloadDecoder): {enum['name']} =",
            f"    {enum['name']}.fromWire(decoder.readUInt()) ?: fail(CoreStatusPayloadCodecError.INVALID_ENUM)",
            "",
        ]
    for owner in structs:
        name = owner["name"]
        validation = _validation(owner, schema, codec)
        lines += [f"private fun validate{name}(value: {name}) {{"]
        lines += validation or ["    @Suppress(\"UNUSED_VARIABLE\") val checked = value"]
        lines += ["}", ""]
        lines += [
            f"private fun encode{name}(encoder: CoreStatusPayloadEncoder, value: {name}) {{",
            f"    validate{name}(value)",
        ]
        for field in owner["fields"]:
            path = f"{name}.{field['name']}"
            lines += _encode_lines(
                parse_type(field["type"]), f"value.{field['name']}", path, "    ", enum_names, codec
            )
        lines += ["}", ""]
        lines += [
            f"private fun decode{name}(decoder: CoreStatusPayloadDecoder): {name} {{",
            f"    val value = {name}(",
        ]
        for field in owner["fields"]:
            path = f"{name}.{field['name']}"
            expression = _decode_expression(parse_type(field["type"]), path, enum_names, codec)
            lines.append(f"        {field['name']} = {expression},")
        lines += [f"    )", f"    validate{name}(value)", "    return value", "}", ""]
    lines += [
        "fun encodeCoreStatusPayload(value: CoreStatus): CoreStatusPayloadEncodeResult = try {",
        "    val encoder = CoreStatusPayloadEncoder()",
        "    encoder.putRaw(CORE_STATUS_PAYLOAD_MAGIC)",
        "    encoder.putUInt(CORE_STATUS_PAYLOAD_SCHEMA_VERSION)",
        "    encodeCoreStatus(encoder, value)",
        "    CoreStatusPayloadEncodeResult.Success(encoder.finish())",
        "} catch (failure: CoreStatusCodecFailure) {",
        "    CoreStatusPayloadEncodeResult.Failure(failure.reason)",
        "}",
        "",
        "fun decodeCoreStatusPayload(bytes: ByteArray): CoreStatusPayloadDecodeResult {",
        "    if (bytes.size > MAX_EVENT_PAYLOAD_BYTES) {",
        "        return CoreStatusPayloadDecodeResult.Failure(CoreStatusPayloadCodecError.SIZE_LIMIT)",
        "    }",
        "    return try {",
        "        val decoder = CoreStatusPayloadDecoder(bytes)",
        "        if (!decoder.take(CORE_STATUS_PAYLOAD_MAGIC.size).contentEquals(CORE_STATUS_PAYLOAD_MAGIC)) {",
        "            fail(CoreStatusPayloadCodecError.INVALID_MAGIC)",
        "        }",
        "        if (decoder.readUInt() != CORE_STATUS_PAYLOAD_SCHEMA_VERSION) {",
        "            fail(CoreStatusPayloadCodecError.UNSUPPORTED_VERSION)",
        "        }",
        "        val value = decodeCoreStatus(decoder)",
        "        if (decoder.offset != bytes.size) fail(CoreStatusPayloadCodecError.TRAILING_BYTES)",
        "        CoreStatusPayloadDecodeResult.Success(value)",
        "    } catch (failure: CoreStatusCodecFailure) {",
        "        CoreStatusPayloadDecodeResult.Failure(failure.reason)",
        "    }",
        "}",
        "",
    ]
    return "\n".join(lines)
