#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Reject duplicate first-party C++ type definitions with external linkage.

One definition in each translation unit can compile cleanly even when two of
those definitions name the same namespace-level type. If both objects reach an
artifact, the program violates C++'s one-definition rule and the linker is not
required to diagnose it. This gate catches that silent shape before GN runs.

The scanner is deliberately narrower than a C++ parser. It tokenizes comments,
strings and preprocessor bodies away, tracks named and anonymous namespaces,
and records only namespace-level class, struct, union and enum *definitions*.
Forward declarations, local types, nested types, template parameters, explicit
specializations and anonymous-namespace definitions are excluded. That is the
set for which a repeated fully qualified name is unambiguously a repository
ownership defect rather than a same-file language construct.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
import re
import sys

import source_index


_CPP_EXTENSIONS = (".cc", ".cpp", ".cxx", ".h", ".hpp")
_TYPE_KEYWORDS = {"class", "struct", "union", "enum"}
_EXPORT_MACRO = re.compile(r"(?:^|_)(?:EXPORT|API)(?:_|$)")


@dataclass(frozen=True)
class Token:
    value: str
    line: int


@dataclass(frozen=True)
class Definition:
    qualified_name: str
    path: str
    line: int


@dataclass(frozen=True)
class Scope:
    kind: str
    namespace_parts: tuple[str, ...] = ()
    anonymous: bool = False


def _skip_quoted(text: str, start: int, quote: str) -> int:
    index = start + 1
    while index < len(text):
        if text[index] == "\\":
            index += 2
            continue
        if text[index] == quote:
            return index + 1
        index += 1
    return index


def _skip_raw_string(text: str, start: int) -> int | None:
    if not text.startswith('R"', start):
        return None
    opening = text.find("(", start + 2, start + 19)
    if opening < 0:
        return None
    delimiter = text[start + 2 : opening]
    if any(char.isspace() or char in "()\\" for char in delimiter):
        return None
    closing = text.find(")" + delimiter + '"', opening + 1)
    return len(text) if closing < 0 else closing + len(delimiter) + 2


def tokens(text: str) -> list[Token]:
    """Return identifiers and structural punctuation outside inert regions."""
    found: list[Token] = []
    index = 0
    line = 1
    at_line_start = True
    while index < len(text):
        char = text[index]
        if char == "\n":
            line += 1
            at_line_start = True
            index += 1
            continue
        if char in " \t\r\f\v":
            index += 1
            continue
        if at_line_start and char == "#":
            index += 1
            while index < len(text):
                if text[index] == "\n":
                    back = index - 1
                    while back >= 0 and text[back] == "\r":
                        back -= 1
                    if back < 0 or text[back] != "\\":
                        break
                    line += 1
                index += 1
            continue
        at_line_start = False
        if text.startswith("//", index):
            newline = text.find("\n", index + 2)
            index = len(text) if newline < 0 else newline
            continue
        if text.startswith("/*", index):
            closing = text.find("*/", index + 2)
            end = len(text) if closing < 0 else closing + 2
            line += text.count("\n", index, end)
            index = end
            continue
        raw_end = _skip_raw_string(text, index)
        if raw_end is not None:
            line += text.count("\n", index, raw_end)
            index = raw_end
            continue
        if char in "\"'":
            end = _skip_quoted(text, index, char)
            line += text.count("\n", index, end)
            index = end
            continue
        if char.isalpha() or char == "_":
            end = index + 1
            while end < len(text) and (text[end].isalnum() or text[end] == "_"):
                end += 1
            found.append(Token(text[index:end], line))
            index = end
            continue
        if text.startswith("::", index):
            found.append(Token("::", line))
            index += 2
            continue
        if char in "{};:<>()[],=":
            found.append(Token(char, line))
        index += 1
    return found


def _inside_template_parameters(values: list[Token], index: int) -> bool:
    depth = 0
    for cursor in range(index - 1, max(-1, index - 128), -1):
        value = values[cursor].value
        if value == ">":
            depth += 1
        elif value == "<":
            if depth == 0:
                return cursor > 0 and values[cursor - 1].value == "template"
            depth -= 1
        elif depth == 0 and value in {";", "{", "}"}:
            return False
    return False


def _skip_balanced(values: list[Token], index: int, opening: str, closing: str) -> int:
    if index >= len(values) or values[index].value != opening:
        return index
    depth = 1
    index += 1
    while index < len(values) and depth:
        if values[index].value == opening:
            depth += 1
        elif values[index].value == closing:
            depth -= 1
        index += 1
    return index


def _namespace_declaration(values: list[Token], index: int) -> tuple[int, Scope] | None:
    cursor = index + 1
    parts: list[str] = []
    while cursor < len(values):
        value = values[cursor].value
        if value == "{":
            return cursor, Scope("namespace", tuple(parts), not parts)
        if value in {";", "="}:
            return None
        if value == "::":
            cursor += 1
            continue
        if value in {"inline", "__attribute__"}:
            cursor += 1
            continue
        if value == "[":
            cursor = _skip_balanced(values, cursor, "[", "]")
            continue
        if value[0].isalpha() or value[0] == "_":
            parts.append(value)
        cursor += 1
    return None


def _namespace_scope(scopes: list[Scope]) -> bool:
    return all(scope.kind == "namespace" for scope in scopes)


def _namespace_name(scopes: list[Scope]) -> tuple[str, ...]:
    parts: list[str] = []
    for scope in scopes:
        parts.extend(scope.namespace_parts)
    return tuple(parts)


def _has_anonymous_namespace(scopes: list[Scope]) -> bool:
    return any(scope.anonymous for scope in scopes)


def _type_definition(values: list[Token], index: int) -> tuple[int, str] | None:
    cursor = index + 1
    if values[index].value == "enum" and cursor < len(values) and values[cursor].value in {
        "class",
        "struct",
    }:
        cursor += 1

    while cursor < len(values):
        value = values[cursor].value
        if value == "[":
            cursor = _skip_balanced(values, cursor, "[", "]")
            continue
        if value == "alignas":
            cursor += 1
            cursor = _skip_balanced(values, cursor, "(", ")")
            continue
        if _EXPORT_MACRO.search(value):
            cursor += 1
            continue
        break
    if cursor >= len(values):
        return None
    name = values[cursor].value
    if not (name[0].isalpha() or name[0] == "_"):
        return None
    name_index = cursor
    cursor += 1
    if cursor < len(values) and values[cursor].value in {"::", "<"}:
        return None

    paren_depth = 0
    bracket_depth = 0
    while cursor < len(values):
        value = values[cursor].value
        if value == "(":
            paren_depth += 1
        elif value == ")":
            paren_depth = max(0, paren_depth - 1)
        elif value == "[":
            bracket_depth += 1
        elif value == "]":
            bracket_depth = max(0, bracket_depth - 1)
        elif paren_depth == 0 and bracket_depth == 0:
            if value == ";":
                return None
            if value == "{":
                return cursor, name
            if value == "=" and values[index].value != "enum":
                return None
            if (
                cursor == name_index + 1
                and (value[0].isalpha() or value[0] == "_")
                and value != "final"
            ):
                return None
        cursor += 1
    return None


def definitions_in_text(text: str, path: str) -> list[Definition]:
    values = tokens(text)
    scopes: list[Scope] = []
    definitions: list[Definition] = []
    index = 0
    while index < len(values):
        value = values[index].value
        if value == "namespace":
            parsed_namespace = _namespace_declaration(values, index)
            if parsed_namespace is not None:
                opening, scope = parsed_namespace
                scopes.append(scope)
                index = opening + 1
                continue
        if (
            value in _TYPE_KEYWORDS
            and _namespace_scope(scopes)
            and not _has_anonymous_namespace(scopes)
            and not _inside_template_parameters(values, index)
        ):
            parsed_type = _type_definition(values, index)
            if parsed_type is not None:
                opening, name = parsed_type
                namespace = _namespace_name(scopes)
                qualified = "::".join((*namespace, name)) if namespace else name
                definitions.append(Definition(qualified, path, values[index].line))
                scopes.append(Scope("type"))
                index = opening + 1
                continue
        if value == "{":
            scopes.append(Scope("other"))
        elif value == "}" and scopes:
            scopes.pop()
        index += 1
    return definitions


def _source_paths(repo_root: str) -> list[str]:
    return [
        relative
        for relative in source_index.source_files(repo_root)
        if relative.endswith(_CPP_EXTENSIONS)
        and relative.startswith("taffy-core/")
        and not relative.startswith("taffy-core/third_party/")
    ]


def duplicate_definitions(repo_root: str) -> tuple[list[str], int]:
    by_name: dict[str, list[Definition]] = {}
    paths = _source_paths(repo_root)
    for relative in paths:
        absolute = os.path.join(repo_root, relative)
        with open(absolute, encoding="utf-8", errors="replace") as handle:
            for definition in definitions_in_text(handle.read(), relative):
                by_name.setdefault(definition.qualified_name, []).append(definition)

    findings: list[str] = []
    for qualified in sorted(by_name):
        definitions = by_name[qualified]
        distinct_paths = {definition.path for definition in definitions}
        if len(distinct_paths) < 2:
            continue
        locations = ", ".join(
            f"{definition.path}:{definition.line}" for definition in definitions
        )
        findings.append(
            f"{qualified}: defined in {len(distinct_paths)} files ({locations}); "
            "one namespace-level C++ type has one owning definition. Rename or "
            "centralize it; anonymous-namespace and forward declarations are already excluded."
        )
    return findings, len(paths)


def self_test() -> list[str]:
    failures: list[str] = []
    fixture = """
        #define FALSE_TYPE class HiddenByMacro {
        namespace taffy::browser {
        class Forward;
        class TAFFY_EXPORT Shared final { int value; };
        template <class T, class U> struct Box { T value; };
        template <> struct Box<int, int> { int value; };
        namespace { struct LocalToUnit {}; }
        void Function() { struct LocalToFunction {}; }
        const char* ignored = "class TextOnly {";
        }
    """
    names = [value.qualified_name for value in definitions_in_text(fixture, "fixture.cc")]
    if names != ["taffy::browser::Shared", "taffy::browser::Box"]:
        failures.append(f"definition selection was {names!r}")

    first = definitions_in_text("namespace a { struct Same {}; }", "first.cc")
    second = definitions_in_text("namespace a { struct Same {}; }", "second.cc")
    other = definitions_in_text("namespace b { struct Same {}; }", "other.cc")
    grouped: dict[str, set[str]] = {}
    for definition in first + second + other:
        grouped.setdefault(definition.qualified_name, set()).add(definition.path)
    collisions = sorted(name for name, paths in grouped.items() if len(paths) > 1)
    if collisions != ["a::Same"]:
        failures.append(f"collision grouping was {collisions!r}")
    return failures


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        failures = self_test()
        for failure in failures:
            print(f"C++ type collision self-test: {failure}", file=sys.stderr)
        if failures:
            return 1
        print("C++ type collision self-test: adversarial fixtures passed")
        return 0

    failures = self_test()
    if failures:
        for failure in failures:
            print(f"C++ type collision self-test: {failure}", file=sys.stderr)
        return 1
    repo_root = os.path.abspath(args.root)
    findings, counted = duplicate_definitions(repo_root)
    for finding in findings:
        print(f"C++ type collision: {finding}", file=sys.stderr)
    if findings:
        return 1
    print(
        f"C++ type collision: {counted} first-party files, no namespace-level type "
        "defined by two files"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
