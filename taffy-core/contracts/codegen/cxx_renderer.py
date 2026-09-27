# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The C++ closed-enumeration decoders.

Rust gets ``from_wire`` and Kotlin gets ``fromWire`` from
:mod:`contract_renderers`; this is the same function for the third language
that reads these contracts. It is a separate module because it answers a
different question from the other four renderers. They project a contract into
a language's own types. This one exists because of a property of C++
specifically: it is the only one of the three languages where the unchecked
conversion compiles. ``static_cast<Enum>(integer)`` for a value that names no
enumerator is undefined behaviour, and at a trust seam it is also a fail-open,
because the value travels on and every later switch misses it silently.

The decoder never forms that value. It matches on the integer and returns an
enumerator or ``std::nullopt``, so an unknown wire value can only reach the
seam's own refusal path.
"""

from __future__ import annotations

from typing import Any

from contract_renderers import header, pascal


def cxx_namespace(schema: dict[str, Any]) -> str:
    """``taffy.core_service`` -> ``taffy::core_service``."""
    return "::".join(schema["namespace"].split("."))


def cxx_include(contract: str) -> str:
    """The generated Mojo header the decoders name their enumerations from."""
    stem = contract.replace("-", "_")
    return f"taffy/contracts/{contract}/generated/mojom/{stem}.mojom-shared.h"


def cxx_guard(contract: str) -> str:
    stem = contract.replace("-", "_")
    slug = contract.replace("-", "_").upper()
    return f"TAFFY_CONTRACTS_{slug}_GENERATED_CPP_{stem.upper()}_ENUMS_H_"


def cxx_comment(text: str, indent: str = "") -> list[str]:
    """Wrap one description into ``//`` lines that fit Chromium's 80 columns."""
    words_out: list[str] = []
    line = f"{indent}//"
    for word in " ".join(text.split()).split(" "):
        candidate = f"{line} {word}"
        if len(candidate) > 80 and line != f"{indent}//":
            words_out.append(line)
            line = f"{indent}// {word}"
            continue
        line = candidate
    words_out.append(line)
    return words_out


def render_cxx(contract: str, schema: dict[str, Any]) -> str:
    """One header carrying a decoder for every closed enumeration."""
    guard = cxx_guard(contract)
    lines = header(schema, "//")
    lines += [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stdint.h>",
        "",
        "#include <optional>",
        "",
        f'#include "{cxx_include(contract)}"',
        "",
    ]
    lines += cxx_comment(
        "Closed-enumeration decoders for this contract. Every enumeration here "
        "is closed: a wire integer that names no member is not a member, and "
        "these return std::nullopt for it instead of forming an out-of-range "
        "enumerator, which is undefined behaviour and, at a trust seam, a "
        "fail-open. Any caller holding an integer must come through here."
    )
    lines += ["", f"namespace {cxx_namespace(schema)}::wire {{", ""]
    codec = schema.get("state_payload_codec")
    if codec:
        lines += cxx_comment(
            "The version this contract's state payload is written at. C++ never "
            "encodes or decodes that payload — it carries the bytes — but a "
            "browser test asserts the version a published state arrives with, "
            "and a literal there goes stale silently on every layout change. "
            "Emitted so it cannot."
        )
        lines += [
            "constexpr uint32_t kStatePayloadSchemaVersion = "
            f"{codec['schema_version']};",
            "",
        ]
    for enum in schema["enums"]:
        name = enum["name"]
        lines += cxx_comment(enum["description"])
        signature = (
            f"constexpr std::optional<mojom::{name}> {name}FromWire(uint32_t value) {{"
        )
        if len(signature) <= 80:
            lines.append(signature)
        else:
            lines += [
                f"constexpr std::optional<mojom::{name}>",
                f"{name}FromWire(uint32_t value) {{",
            ]
        lines.append("  switch (value) {")
        for member in enum["members"]:
            lines += [
                f"    case {member['wire']}:",
                f"      return mojom::{name}::k{pascal(member['name'])};",
            ]
        lines += [
            "    default:",
            "      return std::nullopt;",
            "  }",
            "}",
            "",
        ]
    lines += [
        f"}}  // namespace {cxx_namespace(schema)}::wire",
        "",
        f"#endif  // {guard}",
    ]
    return "\n".join(lines).rstrip() + "\n"
