#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Turning a wire name into an identifier, once.

Authority boundary: spelling. The Rust view renders identifiers and the Mojo
comparison predicts them, and both ask this module rather than deciding for
themselves — which is what stops a checker from disagreeing with the renderer
about what a wire name is called and reporting the difference as drift in the
contract.
"""

from __future__ import annotations

import re
import textwrap

from layout import DOC_WIDTH

RUST_KEYWORDS = frozenset(
    """as break const continue crate dyn else enum extern false fn for if impl in let loop
    match mod move mut pub ref return self Self static struct super trait true type unsafe
    use where while async await box do final macro override priv try typeof unsized virtual
    yield""".split()
)


class GeneratorError(Exception):
    """The schema asks for something the generator will not guess at."""


# --------------------------------------------------------------------------
# naming
# --------------------------------------------------------------------------


def pascal_case(wire_value: str) -> str:
    """``STALE_PAGE_EPOCH`` -> ``StalePageEpoch``."""
    parts = [p for p in re.split(r"[_\s-]+", wire_value) if p]
    if not parts:
        raise GeneratorError(f"cannot name an enumeration member from {wire_value!r}")
    return "".join(p[:1].upper() + p[1:].lower() for p in parts)


def rust_field(name: str) -> str:
    return f"r#{name}" if name in RUST_KEYWORDS else name


def escape_doc(text: str) -> str:
    """Keep bracketed status labels literal in Rust doc comments."""
    return text.replace("[", "\\[").replace("]", "\\]")


def doc_block(text: str, prefix: str) -> list[str]:
    if not text:
        return []
    body = textwrap.wrap(escape_doc(" ".join(text.split())), width=DOC_WIDTH)
    return [f"{prefix}{line}".rstrip() for line in body]
