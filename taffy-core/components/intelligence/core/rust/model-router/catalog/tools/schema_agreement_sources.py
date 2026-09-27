#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Readers for the catalog projections `check_schema_agreement.py` compares.

Each function turns one source text — the kernel's `types.rs` or the baseline
generator — into the plain set of names the comparison consumes. No reader
judges anything; a projection that cannot be read raises, because a
comparison over a misread source proves nothing, and an unread source reads
as an empty set that equals any other empty set.

There were two more: the managed-route Worker's hand-written parser and the
account plane's SQL migrations, folded in order because migrations are
append-only and no single file holds the truth. TaffyGo operates no server
(decision 0200) and both are deleted, so the catalog now has two fewer places
to disagree with itself and this module has two fewer readers.
"""

from __future__ import annotations

import re

TYPES_RS = (
    "taffy-core/components/intelligence/core/rust/model-router/src/catalog/types.rs"
)
# The vocabulary lives beside the generator in its audit half.
GENERATOR = (
    "taffy-core/components/intelligence/core/rust/model-router/catalog/tools/"
    "baseline_audit.py"
)

def screaming_snake(identifier: str) -> str:
    """serde's SCREAMING_SNAKE_CASE, for the identifiers the kernel uses.

    A word boundary sits before every uppercase letter that follows a lowercase
    letter or a digit, so `OpenAiResponses` becomes `OPEN_AI_RESPONSES` — the
    exact respelling this whole tool exists to keep everyone agreeing on.
    """
    return re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", identifier).upper()


def rust_enum_variants(source: str, enum_name: str) -> set[str]:
    """The variants of one `pub enum`, respelled as serde serializes them."""
    match = re.search(
        rf"pub enum {re.escape(enum_name)}\s*\{{(.*?)\n\}}", source, re.S
    )
    if match is None:
        raise SystemExit(f"{TYPES_RS}: no `pub enum {enum_name}` to read")
    variants = re.findall(r"^\s{4}([A-Z][A-Za-z0-9]*),", match.group(1), re.M)
    if not variants:
        raise SystemExit(f"{TYPES_RS}: `pub enum {enum_name}` has no readable variants")
    return {screaming_snake(name) for name in variants}


def generator_tuple(source: str, name: str) -> set[str]:
    match = re.search(
        rf"^{re.escape(name)} = (?:[A-Z_]+ \+ )?\(([^)]*)\)", source, re.M
    )
    if match is None:
        raise SystemExit(f"{GENERATOR}: no tuple `{name}` to compare against")
    values = set(re.findall(r'"([^"]+)"', match.group(1)))
    # TASK_ROLES + ("EMBEDDING",) style concatenation: pull the named prefix in.
    prefix = re.match(rf"^{re.escape(name)} = ([A-Z_]+) \+", source[match.start():])
    if prefix is not None:
        values |= generator_tuple(source, prefix.group(1))
    return values
