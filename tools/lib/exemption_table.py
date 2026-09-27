#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Read a `tools/check.d/*.tsv` policy table.

Authority boundary: parsing and validating the shape of a policy table. It
holds no policy of its own and knows nothing about what any column means.

Owning milestone: M0 (WP-M0-09).

The rule these tables share with `allow-terms.tsv`: **every entry carries a
reason**. An allowlist without reasons becomes a silencer. Two consequences are
enforced here rather than left to review:

  - a row with the wrong number of columns is an error, not a skipped line. A
    table that silently drops malformed rows tells a contributor their
    exemption applied when it did not;
  - a row whose reason is blank, or is a placeholder like `TODO`, is an error.
    "Because the check failed" is not a reason.

Stdlib only. Read-only.
"""

from __future__ import annotations

import os

__all__ = ["TableError", "Row", "read_table"]

#: Reasons that are not reasons. Case-insensitive, matched whole.
_EMPTY_REASONS = {"todo", "tbd", "n/a", "na", "-", "none", "fixme", "wip", "later"}


class TableError(Exception):
    """A policy table is malformed. The message names file, line and fix."""


class Row:
    """One entry: its fields, its reason, and where it came from."""

    def __init__(self, path: str, number: int, fields: list[str], reason: str) -> None:
        self.path = path
        self.number = number
        self.fields = fields
        self.reason = reason

    @property
    def where(self) -> str:
        return f"{self.path}:{self.number}"


def read_table(path: str, columns: int, table_name: str) -> list[Row]:
    """Every entry in `path`, which has `columns` fields plus a trailing reason.

    Raises `TableError` on any malformed row, naming the line and the fix.
    A missing file yields no rows: a policy table that has not been written yet
    exempts nothing, which is the safe direction.
    """
    if not os.path.exists(path):
        return []

    rows: list[Row] = []
    with open(path, encoding="utf-8") as handle:
        for number, raw in enumerate(handle, start=1):
            if not raw.strip() or raw.lstrip().startswith("#"):
                continue
            parts = raw.rstrip("\n").split("\t")
            parts = [part.strip() for part in parts]
            if len(parts) != columns + 1:
                raise TableError(
                    f"{path}:{number}: {table_name} rows have {columns + 1} "
                    f"tab-separated columns, this one has {len(parts)}. "
                    "The last column is always the reason."
                )
            reason = parts[-1]
            if not reason or reason.lower() in _EMPTY_REASONS:
                raise TableError(
                    f"{path}:{number}: every entry needs a reason, and "
                    f"\"{reason}\" is not one. Say why this entry exists, or "
                    "delete it and fix the finding."
                )
            rows.append(Row(path, number, parts[:-1], reason))
    return rows
