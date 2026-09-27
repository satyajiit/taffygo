#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Count the lines of a source file that the 400-line cap is actually about.

Authority boundary: this module decides *how a file is measured*. It never
decides what the limit is, which files are exempt, or whether a finding is a
failure — those belong to `file_discipline.py` and `tools/check.d/`.

Owning milestone: M0 (WP-M0-09), extending the rule
`docs/architecture/android-app-architecture.md` section 5 makes explicit and
decision 0014 requires the fast lane to enforce.

Three things are excluded from the count, each for a stated reason. The
semantics are the ones
`taffy-core/services/core/tools/check_build_graph.py` already
applies to the product root's Rust, generalised to every language this repository
writes so one file cannot be measured two ways:

**Blank lines.** Formatting is not complexity.

**Comments.** This repository documents its reasoning in the source on
purpose. A cap that counted prose would be a cap on explanation, and the first
thing anyone would do to get under it is delete the paragraph that says why the
code is the way it is.

**Inline test blocks.** A Rust `#[cfg(test)]` module is not compiled into the
thing being sized, and Rust convention keeps unit tests in the file they test.
Counting them would make a well-tested module look like a design smell. A
free-standing test *file* is not excluded here: it is a file with its own
responsibility, and it is sized like any other.

What is left is the measure the cap is about: how much a reader has to hold in
their head to follow one file.

Stdlib only. Read-only.
"""

from __future__ import annotations

import os

__all__ = ["LANGUAGES", "Language", "code_lines", "inline_test_lines", "language_for"]


class Language:
    """How one family of source files marks comments and inline test blocks.

    `line` is the line-comment token, `block` the `(open, close)` pair or
    `None`, and `test_attribute` the token that opens a brace-delimited inline
    test block that is compiled out of the shipping artifact.
    """

    def __init__(
        self,
        name: str,
        line: tuple[str, ...],
        block: tuple[str, str] | None = None,
        test_attribute: str | None = None,
        docstrings: bool = False,
    ) -> None:
        self.name = name
        self.line = line
        self.block = block
        self.test_attribute = test_attribute
        self.docstrings = docstrings


#: One entry per language this repository writes, keyed by file extension.
#: A file whose extension is absent is not a source file for the purposes of
#: the cap — `file_discipline.py` uses this table as the definition of "source",
#: so adding a language here is what puts it under the rule.
LANGUAGES: dict[str, Language] = {
    ".rs": Language("Rust", ("//",), ("/*", "*/"), test_attribute="#[cfg(test)]"),
    ".cc": Language("C++", ("//",), ("/*", "*/")),
    ".cpp": Language("C++", ("//",), ("/*", "*/")),
    ".h": Language("C++", ("//",), ("/*", "*/")),
    ".kt": Language("Kotlin", ("//",), ("/*", "*/")),
    ".kts": Language("Kotlin", ("//",), ("/*", "*/")),
    ".java": Language("Java", ("//",), ("/*", "*/")),
    ".ts": Language("TypeScript", ("//",), ("/*", "*/")),
    ".tsx": Language("TypeScript", ("//",), ("/*", "*/")),
    ".js": Language("JavaScript", ("//",), ("/*", "*/")),
    ".mjs": Language("JavaScript", ("//",), ("/*", "*/")),
    ".py": Language("Python", ("#",), None, docstrings=True),
    ".sh": Language("Shell", ("#",), None),
    ".sql": Language("SQL", ("--",), ("/*", "*/")),
    ".gn": Language("GN", ("#",), None),
    ".gni": Language("GN", ("#",), None),
    ".mojom": Language("Mojom", ("//",), ("/*", "*/")),
}


def language_for(path: str) -> Language | None:
    """The measurement rules for a path, or None when it is not source."""
    return LANGUAGES.get(os.path.splitext(path)[1])


def _strip_blocks(line: str, opener: str, closer: str, inside: bool) -> tuple[str, bool]:
    """Remove block-comment spans from one line; report the state after it."""
    out: list[str] = []
    index = 0
    while index < len(line):
        if inside:
            end = line.find(closer, index)
            if end < 0:
                return "".join(out), True
            index = end + len(closer)
            inside = False
            continue
        start = line.find(opener, index)
        if start < 0:
            out.append(line[index:])
            return "".join(out), False
        out.append(line[index:start])
        index = start + len(opener)
        inside = True
    return "".join(out), inside


class _DocstringScanner:
    """Track Python module/class/function docstrings, which are prose."""

    def __init__(self) -> None:
        self._quote: str | None = None

    def consume(self, stripped: str) -> bool:
        """True when this line is inside (or opens) a standalone docstring."""
        if self._quote is not None:
            if self._quote in stripped:
                self._quote = None
            return True
        for quote in ('"""', "'''"):
            if stripped.startswith(quote) or stripped.startswith(("r" + quote, "f" + quote)):
                body = stripped[stripped.index(quote) + len(quote) :]
                if quote not in body:
                    self._quote = quote
                return True
        return False


def code_lines(path: str) -> int:
    """Lines of code in `path`, or 0 when its extension is not a source one."""
    language = language_for(path)
    if language is None:
        return 0
    with open(path, encoding="utf-8", errors="replace") as handle:
        return count_lines(handle, language)


def inline_test_lines(path: str) -> int:
    """Physical lines of `path` inside inline test blocks, or 0 when not source.

    The lines the cap does *not* count, made visible. An inline test block is
    excluded from the measure above for a stated reason, and the same reason
    means nothing else ever looks at how large one has grown — so a module can
    carry a thousand lines of tests the size rule has no opinion on. This is
    the same scan, reporting the other side of the same decision: every line
    from the test attribute to the brace that closes its block, summed over
    the file. It holds no threshold; `file_discipline.py` decides what is worth
    mentioning.
    """
    language = language_for(path)
    if language is None or language.test_attribute is None:
        return 0
    with open(path, encoding="utf-8", errors="replace") as handle:
        return measure(handle, language)[1]


def count_lines(lines, language: Language) -> int:
    """The counting itself, over any iterable of raw lines."""
    return measure(lines, language)[0]


def measure(lines, language: Language) -> tuple[int, int]:
    """`(code lines, inline test lines)` over any iterable of raw lines.

    One scan reports both, because both are decided by the same state: a line
    is either counted as code, or skipped as blank, comment, or test block, and
    the test-block skips are what the second number adds up.
    """
    total = 0
    inline_tests = 0
    in_block = False
    in_test = False
    test_depth = 0
    docstrings = _DocstringScanner() if language.docstrings else None

    for raw in lines:
        if in_test:
            inline_tests += 1
            test_depth += raw.count("{") - raw.count("}")
            if test_depth <= 0:
                in_test = False
            continue

        text = raw
        if language.block is not None:
            text, in_block_next = _strip_blocks(text, language.block[0], language.block[1], in_block)
            was_in_block = in_block
            in_block = in_block_next
            if was_in_block and not text.strip():
                continue

        stripped = text.strip()
        if not stripped:
            continue
        if any(stripped.startswith(token) for token in language.line):
            continue
        if docstrings is not None and docstrings.consume(stripped):
            continue
        if language.test_attribute and stripped.startswith(language.test_attribute):
            in_test = True
            test_depth = 0
            inline_tests += 1
            continue
        total += 1

    return total, inline_tests


def _refuse_to_be_a_gate() -> int:
    """Refuses to run as a command instead of exiting 0 having checked nothing.

    This module measures a file; it does not hold the limit, the exemption
    table, or the verdict. Run directly it therefore has nothing to report,
    and a silent success is exactly the shape the honesty rule forbids — a
    command that appears to have checked something it never looked at.
    """
    import sys

    sys.stderr.write(
        "tools/lib/code_lines.py is a library, not a gate: it measures a file "
        "and holds no limit, exemption table, or verdict.\n"
        "Run the gate that does:  ./tools/check fast --only files\n"
        "Or the checker directly: python3 tools/lib/file_discipline.py .\n"
    )
    return 2


if __name__ == "__main__":
    raise SystemExit(_refuse_to_be_a_gate())
