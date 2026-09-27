# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Small, fail-closed reader for TaffyGo's declarative GN surface.

This is not a general GN interpreter. It reads the source/dependency lists and
custom templates used below ``//taffy``. An expression it cannot resolve never
becomes permission for an edge: unresolved first-party labels are findings in
the graph checker, and a configured Chromium graph is checked separately.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


DEPENDENCY_FIELDS = {
    "annotation_processor_deps",
    "data_deps",
    "deps",
    "processor_deps",
    "public_deps",
    "srcjar_deps",
}
SOURCE_FIELDS = {"inputs", "sources"}
TARGET_EXCLUSIONS = {"config", "declare_args", "template"}
TEST_TARGET_KINDS = {"fuzzer_test", "robolectric_binary", "test"}
_BLOCK = re.compile(r'(?m)^[ \t]*([A-Za-z_][A-Za-z0-9_]*)\(\s*"([^"]+)"\s*\)\s*\{')
_ASSIGNMENT = re.compile(r"^[ \t]*([A-Za-z_][A-Za-z0-9_.]*)\s*(\+?=)\s*(.*)$")
_IMPORT = re.compile(r'(?m)^[ \t]*import\(\s*"([^"]+)"\s*\)')
_TOKEN = re.compile(r'"((?:\\.|[^"])*)"|\b([A-Za-z_][A-Za-z0-9_.]*)\b')
_INTERPOLATION = re.compile(
    r"\$(?:\{([A-Za-z_][A-Za-z0-9_.]*)\}|([A-Za-z_][A-Za-z0-9_]*))"
)
_UNRESOLVED = "\0unresolved:"
_BUILTIN_ENVIRONMENT = {
    "current_toolchain": frozenset(("current_toolchain",)),
    "host_toolchain": frozenset(("host_toolchain",)),
    "root_build_dir": frozenset(("//out/root_build_dir",)),
    "root_gen_dir": frozenset(("//out/root_gen_dir",)),
    "root_out_dir": frozenset(("//out/root_out_dir",)),
    "target_gen_dir": frozenset(("//out/target_gen_dir",)),
    "target_out_dir": frozenset(("//out/target_out_dir",)),
}
FOREIGN_SOURCE_ROOTS = (("third_party", "cpython", "src"),)


def _is_foreign_source(path: Path, core_root: Path) -> bool:
    relative_parts = path.relative_to(core_root).parts
    return any(
        relative_parts[: len(root)] == root for root in FOREIGN_SOURCE_ROOTS
    )


@dataclass(frozen=True)
class GnTarget:
    label: str
    kind: str
    deps: frozenset[str]
    sources: frozenset[str]
    declaration: str
    test_only: bool


@dataclass(frozen=True)
class _Template:
    body: str
    environment: dict[str, frozenset[str]]
    test_only: bool


def _declares_test_only(kind: str, body: str) -> bool:
    return kind in TEST_TARGET_KINDS or bool(
        re.search(r"(?m)^[ \t]*testonly[ \t]*=[ \t]*true\b", body)
    )


def _without_comments(text: str) -> str:
    output: list[str] = []
    quoted = False
    escaped = False
    index = 0
    while index < len(text):
        character = text[index]
        if escaped:
            output.append(character)
            escaped = False
        elif character == "\\" and quoted:
            output.append(character)
            escaped = True
        elif character == '"':
            output.append(character)
            quoted = not quoted
        elif character == "#" and not quoted:
            newline = text.find("\n", index)
            if newline < 0:
                break
            output.append("\n")
            index = newline
        else:
            output.append(character)
        index += 1
    return "".join(output)


def _without_named_blocks(text: str) -> str:
    spans: list[tuple[int, int]] = []
    for match in _BLOCK.finditer(text):
        opening = match.end() - 1
        closing = _closing_brace(text, opening)
        if any(start < match.start() < end for start, end in spans):
            continue
        spans.append((match.start(), closing + 1))
    output = list(text)
    for start, end in spans:
        for index in range(start, end):
            if output[index] != "\n":
                output[index] = " "
    return "".join(output)


def _closing_brace(text: str, opening: int) -> int:
    depth = 1
    quoted = False
    escaped = False
    for index in range(opening + 1, len(text)):
        character = text[index]
        if escaped:
            escaped = False
        elif character == "\\" and quoted:
            escaped = True
        elif character == '"':
            quoted = not quoted
        elif not quoted and character == "{":
            depth += 1
        elif not quoted and character == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("unclosed GN block")


def _top_level_blocks(text: str) -> list[tuple[str, str, str]]:
    # A target may sit inside `if (is_android)`, whose condition has no quoted
    # name and therefore is not a named block. Keep such targets, while rejecting
    # the helper targets nested inside a named custom `template` definition.
    candidates: list[tuple[int, int, str, str, str]] = []
    for match in _BLOCK.finditer(text):
        opening = match.end() - 1
        closing = _closing_brace(text, opening)
        candidates.append(
            (
                match.start(),
                closing,
                match.group(1),
                match.group(2),
                text[opening + 1 : closing],
            )
        )
    blocks: list[tuple[str, str, str]] = []
    for start, closing, kind, name, body in candidates:
        if any(
            outer_start < start < outer_closing
            for outer_start, outer_closing, _kind, _name, _body in candidates
        ):
            continue
        blocks.append((kind, name, body))
    return blocks


def _expression_lines(text: str) -> list[tuple[str, str, str]]:
    lines = text.splitlines()
    found: list[tuple[str, str, str]] = []
    index = 0
    while index < len(lines):
        match = _ASSIGNMENT.match(lines[index])
        if not match:
            index += 1
            continue
        field, operation, first = match.groups()
        pieces = [first]
        square = first.count("[") - first.count("]")
        paren = first.count("(") - first.count(")")
        while index + 1 < len(lines) and (
            not "".join(pieces).strip()
            or square > 0
            or paren > 0
            or pieces[-1].rstrip().endswith("+")
        ):
            index += 1
            piece = lines[index]
            pieces.append(piece)
            square += piece.count("[") - piece.count("]")
            paren += piece.count("(") - piece.count(")")
        found.append((field, operation, "\n".join(pieces)))
        index += 1
    return found


def _literal_values(literal: str, environment: dict[str, frozenset[str]]) -> set[str]:
    values = {bytes(literal, "utf-8").decode("unicode_escape")}
    while True:
        expanded: set[str] = set()
        changed = False
        for value in values:
            match = _INTERPOLATION.search(value)
            if not match:
                expanded.add(value)
                continue
            changed = True
            variable = match.group(1) or match.group(2)
            replacements = environment.get(variable)
            if replacements is None:
                if value.startswith("//out/"):
                    expanded.add(value[: match.start()] + "__dynamic__" + value[match.end() :])
                else:
                    expanded.add(f"{_UNRESOLVED}{variable}")
                continue
            for replacement in replacements:
                expanded.add(value[: match.start()] + replacement + value[match.end() :])
        values = expanded
        if not changed or any(value.startswith(_UNRESOLVED) for value in values):
            return values


def _values(expression: str, environment: dict[str, frozenset[str]]) -> set[str]:
    if re.match(r"^[ \t]*get_label_info[ \t]*\(", expression):
        # This GN builtin returns an output-directory or target-name string,
        # never a source label. Treat its result as generated output rather
        # than harvesting its label argument as a source file.
        return {"//out/get_label_info"}
    values: set[str] = set()
    for match in _TOKEN.finditer(expression):
        literal, variable = match.groups()
        if literal is not None:
            values.update(_literal_values(literal, environment))
        elif variable in environment:
            values.update(environment[variable])
        elif variable not in {"false", "true"}:
            values.add(f"{_UNRESOLVED}{variable}")
    return values


def _apply_assignments(
    text: str, environment: dict[str, frozenset[str]]
) -> dict[str, frozenset[str]]:
    result = dict(environment)
    for field, operation, expression in _expression_lines(text):
        values = _values(expression, result)
        if operation == "+=":
            values.update(result.get(field, ()))
        result[field] = frozenset(values)
    return result


def _taffy_import(path: str, core_root: Path) -> Path | None:
    if not path.startswith("//taffy/"):
        return None
    candidate = (core_root / path[len("//taffy/") :]).resolve()
    try:
        candidate.relative_to(core_root.resolve())
    except ValueError:
        return None
    return candidate


class GnSourceReader:
    def __init__(self, core_root: Path):
        self.core_root = core_root.resolve()
        self._environment_cache: dict[Path, dict[str, frozenset[str]]] = {}
        self._templates: dict[str, _Template] = {}

    def _environment(self, path: Path, stack: frozenset[Path] = frozenset()) -> dict[str, frozenset[str]]:
        path = path.resolve()
        if path in self._environment_cache:
            return dict(self._environment_cache[path])
        if path in stack or not path.is_file():
            return {}
        text = _without_comments(path.read_text(encoding="utf-8"))
        environment: dict[str, frozenset[str]] = dict(_BUILTIN_ENVIRONMENT)
        for imported in _IMPORT.findall(text):
            imported_path = _taffy_import(imported, self.core_root)
            if imported_path:
                environment.update(self._environment(imported_path, stack | {path}))
        # Conditions, foreach loops and declare_args are part of the global GN
        # environment. Only named target/template bodies are local scopes.
        globals_only = _without_named_blocks(text)
        environment = _apply_assignments(globals_only, environment)
        self._environment_cache[path] = dict(environment)
        for kind, name, body in _top_level_blocks(text):
            if kind == "template":
                self._templates[name] = _Template(
                    body,
                    dict(environment),
                    bool(
                        re.search(
                            r"(?m)^[ \t]*(?:fuzzer_test|robolectric_binary|test)[ \t]*\(",
                            body,
                        )
                    )
                    or any(
                        bool(self._templates.get(called))
                        and self._templates[called].test_only
                        for called in re.findall(
                            r"(?m)^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(",
                            body,
                        )
                    )
                    or bool(
                        re.search(r"(?m)^[ \t]*testonly[ \t]*=[ \t]*true\b", body)
                    ),
                )
        return environment

    def _template_values(
        self,
        template_name: str,
        invocation: dict[str, frozenset[str]],
        target_name: str,
    ) -> dict[str, frozenset[str]]:
        template = self._templates.get(template_name)
        if not template:
            return {}
        environment = dict(template.environment)
        environment["target_name"] = frozenset((target_name,))
        for match in _TOKEN.finditer(template.body):
            variable = match.group(2)
            if variable and variable.startswith("invoker."):
                environment.setdefault(variable, frozenset())
        for field, values in invocation.items():
            environment[f"invoker.{field}"] = values
        return _apply_assignments(template.body, environment)

    def read(self) -> tuple[dict[str, GnTarget], list[str]]:
        findings: list[str] = []
        targets: dict[str, GnTarget] = {}
        build_files = sorted(
            path
            for path in self.core_root.rglob("BUILD.gn")
            if not _is_foreign_source(path, self.core_root)
        )
        for build_file in build_files:
            environment = self._environment(build_file)
            text = _without_comments(build_file.read_text(encoding="utf-8"))
            directory = build_file.parent.relative_to(self.core_root).as_posix()
            label_prefix = "//taffy" + (f"/{directory}" if directory != "." else "")
            for kind, name, body in _top_level_blocks(text):
                if kind in TARGET_EXCLUSIONS:
                    continue
                invocation = _apply_assignments(body, environment)
                template_values = self._template_values(kind, invocation, name)
                dependencies: set[str] = set()
                for field in DEPENDENCY_FIELDS:
                    dependencies.update(invocation.get(field, ()))
                    dependencies.update(template_values.get(field, ()))
                sources: set[str] = set()
                for field in SOURCE_FIELDS:
                    sources.update(invocation.get(field, ()))
                    sources.update(template_values.get(field, ()))
                label = f"{label_prefix}:{name}"
                if label in targets:
                    findings.append(f"{label}: target is declared more than once")
                    continue
                for field, values in (("deps", dependencies), ("sources", sources)):
                    unresolved = sorted(
                        value.removeprefix(_UNRESOLVED)
                        for value in values
                        if value.startswith(_UNRESOLVED)
                    )
                    if unresolved:
                        findings.append(
                            f"{label}: unresolved {field} expression variables: "
                            f"{', '.join(unresolved)}"
                        )
                dependencies = {
                    value for value in dependencies if not value.startswith(_UNRESOLVED)
                }
                sources = {value for value in sources if not value.startswith(_UNRESOLVED)}
                targets[label] = GnTarget(
                    label=label,
                    kind=kind,
                    deps=frozenset(dependencies),
                    sources=frozenset(sources),
                    declaration=build_file.relative_to(self.core_root).as_posix(),
                    test_only=(
                        _declares_test_only(kind, body)
                        or bool(self._templates.get(kind) and self._templates[kind].test_only)
                    ),
                )
        return targets, findings


def normalize_dependency(value: str, declaration: str) -> str | None:
    value = value.split("(", 1)[0]
    if "$" in value:
        return None
    if value.startswith(":"):
        directory = Path(declaration).parent.as_posix()
        prefix = "//taffy" + (f"/{directory}" if directory != "." else "")
        return prefix + value
    if not value.startswith("//taffy"):
        return None
    if value.endswith((".gni", ".gn")):
        return None
    if ":" not in value[len("//taffy") :]:
        path = value.rstrip("/")
        name = path.rsplit("/", 1)[-1] if "/" in path else "taffy"
        return f"{path}:{name}"
    return value


def resolve_source(value: str, declaration: str, core_root: Path) -> str | None:
    if "$" in value:
        return None
    if value.startswith("//taffy/"):
        candidate = core_root / value[len("//taffy/") :]
    elif value.startswith("//"):
        return None
    else:
        candidate = core_root / Path(declaration).parent / value
    try:
        return candidate.resolve().relative_to(core_root.resolve()).as_posix()
    except ValueError:
        return None
