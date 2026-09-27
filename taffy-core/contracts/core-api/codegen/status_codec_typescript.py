# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Render the TypeScript half of the generated CoreStatus payload codec."""

from __future__ import annotations

from typing import Any

from contract_renderers import pascal
from contract_schema import TypeRef, parse_type
from status_codec import reachable_types, validate_codec_schema


def _encode_lines(
    reference: TypeRef,
    expression: str,
    path: str,
    indent: str,
    enum_names: set[str],
    codec: dict[str, Any],
) -> list[str]:
    if reference.kind == "optional":
        lines = [f"{indent}if ({expression} === null) {{", f"{indent}  encoder.putBoolean(false);"]
        lines += [f"{indent}}} else {{", f"{indent}  encoder.putBoolean(true);"]
        lines += _encode_lines(reference.inner, expression, path, indent + "  ", enum_names, codec)
        return lines + [f"{indent}}}"]
    if reference.kind == "list":
        lines = [
            f"{indent}encoder.putLength({expression}.length, {codec['list_limits'][path]});",
            f"{indent}for (const item of {expression}) {{",
        ]
        lines += _encode_lines(reference.inner, "item", path, indent + "  ", enum_names, codec)
        return lines + [f"{indent}}}"]
    if reference.kind == "named":
        return [f"{indent}encode{reference.name}(encoder, {expression});"]
    if reference.name == "string":
        return [f"{indent}encoder.putString({expression}, {codec['string_limits'][path]});"]
    if reference.name == "u32" and path in codec["u32_limits"]:
        return [f"{indent}encoder.putBoundedU32({expression}, {codec['u32_limits'][path]});"]
    method = {"bool": "putBoolean", "u32": "putU32", "u64": "putU64"}[reference.name]
    return [f"{indent}encoder.{method}({expression});"]


def _decode_expression(
    reference: TypeRef,
    path: str,
    enum_names: set[str],
    codec: dict[str, Any],
) -> str:
    if reference.kind == "optional":
        inner = _decode_expression(reference.inner, path, enum_names, codec)
        return f"decoder.readBoolean() ? {inner} : null"
    if reference.kind == "list":
        inner = _decode_expression(reference.inner, path, enum_names, codec)
        return f"decoder.readList({codec['list_limits'][path]}, () => {inner})"
    if reference.kind == "named":
        return f"decode{reference.name}(decoder)"
    if reference.name == "string":
        return f"decoder.readString({codec['string_limits'][path]})"
    if reference.name == "u32" and path in codec["u32_limits"]:
        return f"decoder.readBoundedU32({codec['u32_limits'][path]})"
    method = {"bool": "Boolean", "u32": "U32", "u64": "U64"}[reference.name]
    return f"decoder.read{method}()"


def _validation(
    owner: dict[str, Any], schema: dict[str, Any], codec: dict[str, Any]
) -> list[str]:
    fields = {field["name"]: field for field in owner["fields"]}
    lines: list[str] = []
    for rule in codec["presence_rules"]:
        if rule["struct"] != owner["name"]:
            continue
        tag_type = fields[rule["tag_field"]]["type"]
        expected = " || ".join(
            f"value.{rule['tag_field']} === {tag_type}.{pascal(member)}"
            for member in rule["present_for"]
        )
        lines += [
            f"  if (({expected}) !== (value.{rule['field']} !== null)) {{",
            "    fail(CoreStatusPayloadCodecError.Malformed);",
            "  }",
        ]
    enum_map = {item["name"]: item for item in schema["enums"]}
    for rule in codec["ordered_enum_list_rules"]:
        if rule["struct"] != owner["name"]:
            continue
        tag_type = fields[rule["tag_field"]]["type"]
        exact = " || ".join(
            f"value.{rule['tag_field']} === {tag_type}.{pascal(member)}"
            for member in rule["exact_for"]
        )
        enum_type = rule["enum"]
        expected = ", ".join(
            f"{enum_type}.{pascal(member['name'])}"
            for member in enum_map[enum_type]["members"]
        )
        item_path = ".".join(["item", *rule["enum_path"].split(".")])
        lines += [
            f"  if ({exact}) {{",
            f"    const expected = [{expected}] as const;",
            f"    if (value.{rule['list_field']}.length !== expected.length ||",
            f"        value.{rule['list_field']}.some((item, index) => {item_path} !== expected[index])) {{",
            "      fail(CoreStatusPayloadCodecError.Malformed);",
            "    }",
            "  }",
        ]
        if rule["empty_for"]:
            empty = " || ".join(
                f"value.{rule['tag_field']} === {tag_type}.{pascal(member)}"
                for member in rule["empty_for"]
            )
            lines += [
                f"  if (({empty}) && value.{rule['list_field']}.length !== 0) {{",
                "    fail(CoreStatusPayloadCodecError.Malformed);",
                "  }",
            ]
    return lines


def render_typescript_status_codec(schema: dict[str, Any]) -> str:
    codec = validate_codec_schema(schema)
    enums, structs = reachable_types(schema, codec["root"])
    enum_names = {item["name"] for item in enums}
    # Enums and limits are values at run time; a struct is an interface and
    # exists only in the type system. `verbatimModuleSyntax` — which both
    # TypeScript projects in this repository enable — refuses a value import
    # of something that is erased, so the two go in separate statements.
    imported_values = ",\n  ".join(
        [item["name"] for item in enums]
        + sorted(
            {
                codec["max_bytes_limit"],
                *codec["string_limits"].values(),
                *codec["list_limits"].values(),
                *codec["u32_limits"].values(),
            }
        )
    )
    imported_type_names = ",\n  ".join(item["name"] for item in structs)
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
        "import {",
        f"  {imported_values},",
        '} from "./core_api";',
        "import type {",
        f"  {imported_type_names},",
        '} from "./core_api";',
        "",
        "export enum CoreStatusPayloadCodecError {",
        "  SizeLimit, CollectionLimit, StringLimit, ValueLimit, LengthOverflow,",
        "  Truncated, InvalidMagic, UnsupportedVersion, InvalidBoolean, InvalidEnum,",
        "  InvalidUtf8, Malformed, TrailingBytes,",
        "}",
        "",
        "export type CoreStatusPayloadEncodeResult =",
        "  | { readonly ok: true; readonly bytes: Uint8Array }",
        "  | { readonly ok: false; readonly error: CoreStatusPayloadCodecError };",
        "",
        "export type CoreStatusPayloadDecodeResult =",
        "  | { readonly ok: true; readonly value: CoreStatus }",
        "  | { readonly ok: false; readonly error: CoreStatusPayloadCodecError };",
        "",
        f"const CORE_STATUS_PAYLOAD_MAGIC = new Uint8Array([{magic}]);",
        f"export const CORE_STATUS_PAYLOAD_SCHEMA_VERSION = {codec['schema_version']} as const;",
        "",
        "class CoreStatusCodecFailure extends Error {",
        "  constructor(readonly reason: CoreStatusPayloadCodecError) { super(); }",
        "}",
        "",
        "function fail(reason: CoreStatusPayloadCodecError): never {",
        "  throw new CoreStatusCodecFailure(reason);",
        "}",
        "",
        "class CoreStatusPayloadEncoder {",
        "  private readonly bytes = new Uint8Array(MAX_EVENT_PAYLOAD_BYTES);",
        "  private offset = 0;",
        "",
        "  finish(): Uint8Array { return this.bytes.slice(0, this.offset); }",
        "",
        "  putRaw(value: Uint8Array): void {",
        "    if (value.length > this.bytes.length - this.offset) fail(CoreStatusPayloadCodecError.SizeLimit);",
        "    this.bytes.set(value, this.offset);",
        "    this.offset += value.length;",
        "  }",
        "",
        "  private putByte(value: number): void {",
        "    if (this.offset >= this.bytes.length) fail(CoreStatusPayloadCodecError.SizeLimit);",
        "    this.bytes[this.offset++] = value;",
        "  }",
        "",
        "  putBoolean(value: boolean): void { this.putByte(value ? 1 : 0); }",
        "",
        "  putU32(value: number): void {",
        "    if (!Number.isInteger(value) || value < 0 || value > 0xffff_ffff) fail(CoreStatusPayloadCodecError.ValueLimit);",
        "    for (let shift = 0; shift < 4; shift++) {",
        "      this.putByte(Math.floor(value / 2 ** (shift * 8)) & 0xff);",
        "    }",
        "  }",
        "",
        "  putBoundedU32(value: number, limit: number): void {",
        "    if (value > limit) fail(CoreStatusPayloadCodecError.ValueLimit);",
        "    this.putU32(value);",
        "  }",
        "",
        "  putU64(value: bigint): void {",
        "    if (value < 0n || value > 0xffff_ffff_ffff_ffffn) fail(CoreStatusPayloadCodecError.ValueLimit);",
        "    for (let shift = 0n; shift < 64n; shift += 8n) this.putByte(Number((value >> shift) & 0xffn));",
        "  }",
        "",
        "  putLength(value: number, limit: number): void {",
        "    if (!Number.isInteger(value) || value < 0 || value > limit) fail(CoreStatusPayloadCodecError.CollectionLimit);",
        "    this.putU32(value);",
        "  }",
        "",
        "  putString(value: string, limit: number): void {",
        "    const encoded = new TextEncoder().encode(value);",
        "    if (encoded.length > limit) fail(CoreStatusPayloadCodecError.StringLimit);",
        "    this.putU32(encoded.length);",
        "    this.putRaw(encoded);",
        "  }",
        "}",
        "",
        "class CoreStatusPayloadDecoder {",
        "  offset = 0;",
        "  constructor(private readonly bytes: Uint8Array) {}",
        "",
        "  take(length: number): Uint8Array {",
        "    if (!Number.isInteger(length) || length < 0 || length > this.bytes.length - this.offset) {",
        "      fail(CoreStatusPayloadCodecError.Truncated);",
        "    }",
        "    const value = this.bytes.slice(this.offset, this.offset + length);",
        "    this.offset += length;",
        "    return value;",
        "  }",
        "",
        "  private readByte(): number {",
        "    if (this.offset >= this.bytes.length) fail(CoreStatusPayloadCodecError.Truncated);",
        "    // The bound above already proves this index is in range, but under",
        "    // noUncheckedIndexedAccess the compiler types it as possibly",
        "    // undefined and cannot see that. Answer it with the same refusal",
        "    // rather than a non-null assertion: a decoder whose truncation",
        "    // check is a claim rather than a branch is exactly what fails open.",
        "    const byte = this.bytes[this.offset++];",
        "    if (byte === undefined) fail(CoreStatusPayloadCodecError.Truncated);",
        "    return byte;",
        "  }",
        "",
        "  readBoolean(): boolean {",
        "    const value = this.readByte();",
        "    if (value === 0) return false;",
        "    if (value === 1) return true;",
        "    return fail(CoreStatusPayloadCodecError.InvalidBoolean);",
        "  }",
        "",
        "  readU32(): number {",
        "    let value = 0;",
        "    for (let shift = 0; shift < 4; shift++) value += this.readByte() * 2 ** (shift * 8);",
        "    return value;",
        "  }",
        "",
        "  readBoundedU32(limit: number): number {",
        "    const value = this.readU32();",
        "    if (value > limit) fail(CoreStatusPayloadCodecError.ValueLimit);",
        "    return value;",
        "  }",
        "",
        "  readU64(): bigint {",
        "    let value = 0n;",
        "    for (let shift = 0n; shift < 64n; shift += 8n) value |= BigInt(this.readByte()) << shift;",
        "    return value;",
        "  }",
        "",
        "  private readLength(limit: number): number {",
        "    const value = this.readU32();",
        "    if (value > limit) fail(CoreStatusPayloadCodecError.CollectionLimit);",
        "    return value;",
        "  }",
        "",
        "  readString(limit: number): string {",
        "    const length = this.readU32();",
        "    if (length > limit) fail(CoreStatusPayloadCodecError.StringLimit);",
        "    const value = this.take(length);",
        "    try { return new TextDecoder(\"utf-8\", { fatal: true }).decode(value); }",
        "    catch { return fail(CoreStatusPayloadCodecError.InvalidUtf8); }",
        "  }",
        "",
        "  readList<T>(limit: number, read: () => T): ReadonlyArray<T> {",
        "    const length = this.readLength(limit);",
        "    const values: T[] = [];",
        "    for (let index = 0; index < length; index++) values.push(read());",
        "    return values;",
        "  }",
        "}",
        "",
    ]
    for enum in enums:
        cases = "\n".join(
            f"    case {enum['name']}.{pascal(member['name'])}:" for member in enum["members"]
        )
        lines += [
            f"function encode{enum['name']}(encoder: CoreStatusPayloadEncoder, value: {enum['name']}): void {{",
            "  switch (value) {",
            cases,
            "      encoder.putU32(value); return;",
            "    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);",
            "  }",
            "}",
            "",
            f"function decode{enum['name']}(decoder: CoreStatusPayloadDecoder): {enum['name']} {{",
            "  const value = decoder.readU32();",
            "  switch (value) {",
            cases,
            f"      return value as {enum['name']};",
            "    default: return fail(CoreStatusPayloadCodecError.InvalidEnum);",
            "  }",
            "}",
            "",
        ]
    for owner in structs:
        name = owner["name"]
        lines += [f"function validate{name}(value: {name}): void {{"]
        lines += _validation(owner, schema, codec) or ["  void value;"]
        lines += ["}", ""]
        lines += [
            f"function encode{name}(encoder: CoreStatusPayloadEncoder, value: {name}): void {{",
            f"  validate{name}(value);",
        ]
        for field in owner["fields"]:
            path = f"{name}.{field['name']}"
            lines += _encode_lines(
                parse_type(field["type"]), f"value.{field['name']}", path, "  ", enum_names, codec
            )
        lines += ["}", ""]
        lines += [f"function decode{name}(decoder: CoreStatusPayloadDecoder): {name} {{", "  const value = {"]
        for field in owner["fields"]:
            path = f"{name}.{field['name']}"
            expression = _decode_expression(parse_type(field["type"]), path, enum_names, codec)
            lines.append(f"    {field['name']}: {expression},")
        lines += [f"  }} satisfies {name};", f"  validate{name}(value);", "  return value;", "}", ""]
    lines += [
        "export function encodeCoreStatusPayload(value: CoreStatus): CoreStatusPayloadEncodeResult {",
        "  try {",
        "    const encoder = new CoreStatusPayloadEncoder();",
        "    encoder.putRaw(CORE_STATUS_PAYLOAD_MAGIC);",
        "    encoder.putU32(CORE_STATUS_PAYLOAD_SCHEMA_VERSION);",
        "    encodeCoreStatus(encoder, value);",
        "    return { ok: true, bytes: encoder.finish() };",
        "  } catch (failure) {",
        "    if (failure instanceof CoreStatusCodecFailure) return { ok: false, error: failure.reason };",
        "    throw failure;",
        "  }",
        "}",
        "",
        "export function decodeCoreStatusPayload(bytes: Uint8Array): CoreStatusPayloadDecodeResult {",
        "  if (bytes.length > MAX_EVENT_PAYLOAD_BYTES) return { ok: false, error: CoreStatusPayloadCodecError.SizeLimit };",
        "  try {",
        "    const decoder = new CoreStatusPayloadDecoder(bytes);",
        "    const magic = decoder.take(CORE_STATUS_PAYLOAD_MAGIC.length);",
        "    if (!magic.every((byte, index) => byte === CORE_STATUS_PAYLOAD_MAGIC[index])) {",
        "      fail(CoreStatusPayloadCodecError.InvalidMagic);",
        "    }",
        "    if (decoder.readU32() !== CORE_STATUS_PAYLOAD_SCHEMA_VERSION) {",
        "      fail(CoreStatusPayloadCodecError.UnsupportedVersion);",
        "    }",
        "    const value = decodeCoreStatus(decoder);",
        "    if (decoder.offset !== bytes.length) fail(CoreStatusPayloadCodecError.TrailingBytes);",
        "    return { ok: true, value };",
        "  } catch (failure) {",
        "    if (failure instanceof CoreStatusCodecFailure) return { ok: false, error: failure.reason };",
        "    throw failure;",
        "  }",
        "}",
        "",
    ]
    return "\n".join(lines)
