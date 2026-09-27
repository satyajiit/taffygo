#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate the browser adapter constants for the canonical core journal schema.

The document carries three things the browser needs and one it needs in order to
be believed:

  - ``statements`` -- the creation list for the head version.
  - ``migrations`` -- one entry per superseded version, naming the checksum a
    database on disk must have recorded and the statements that carry it to head.
  - ``history`` -- what each of those superseded versions physically *was*.
    Without it there is no way to build a real old database, so the invariant
    that matters -- migrating a version-N database to head produces exactly the
    schema a fresh head creation produces -- cannot be tested at all.

``history`` is not taken on trust. Each entry reconstructs the whole document
that version was, and this generator recomputes its FNV-1a checksum and refuses
to write anything unless it equals the ``from_checksum`` the migration ladder
already recorded. A wrong or invented historical statement list cannot pass.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from core_service_schema_validation import (
    checksum,
    expand_closed_enum_bounds,
    proved_migrations,
    quoted,
    schema_identity,
    validated_history,
    validated_migrations,
    validated_statements,
)


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "core_service_journal.json"
OUTPUT = ROOT.parents[1] / "browser" / "generated" / "core_service_journal_schema.h"
GENERATED_VERSION = re.compile(r"kVersion = (?P<value>[0-9]+)u;")
GENERATED_CHECKSUM = re.compile(r'kChecksum = "(?P<value>[0-9a-f]{16})";')


def render(source_document: dict[str, object]) -> str:
    document = expand_closed_enum_bounds(source_document)
    component = document["component"]
    version = document["version"]
    initial_revision = document["initial_task_revision"]
    ledger_statement = document["ledger_statement"]
    statements = document["statements"]
    if not isinstance(component, str) or not component:
        raise ValueError("component must be a non-empty string")
    if not isinstance(version, int) or version <= 0:
        raise ValueError("version must be positive")
    if not isinstance(initial_revision, int) or initial_revision < 0:
        raise ValueError("initial_task_revision must be non-negative")
    if not isinstance(ledger_statement, str) or not ledger_statement.startswith("CREATE TABLE"):
        raise ValueError("ledger_statement must create the schema ledger")
    validated_statements(statements, "statements")
    migrations = validated_migrations(document.get("migrations", []), version, "migrations")
    history = validated_history(source_document, version, migrations)
    proved_migrations(document, migrations, history)

    migration_statements: list[str] = []
    migration_rows: list[str] = []
    for migration in migrations:
        steps = migration["statements"]
        offset = len(migration_statements)
        migration_statements.extend(steps)
        migration_rows.append(
            f"    {{{migration['from_version']}u, {quoted(migration['from_checksum'])}, "
            f"{offset}u, {len(steps)}u}},"
        )
    historical_statements: list[str] = []
    historical_rows: list[str] = []
    for entry in history:
        steps = entry["statements"]
        offset = len(historical_statements)
        historical_statements.extend(steps)
        historical_rows.append(
            f"    {{{entry['version']}u, {quoted(entry['checksum'])}, "
            f"{offset}u, {len(steps)}u}},"
        )

    rows = "\n".join(f"    {quoted(statement)}," for statement in statements)
    migration_step_rows = "\n".join(
        f"    {quoted(statement)}," for statement in migration_statements
    )
    migrations_rendered = "\n".join(migration_rows)
    historical_step_rows = "\n".join(
        f"    {quoted(statement)}," for statement in historical_statements
    )
    history_rendered = "\n".join(historical_rows)
    return f"""// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from components/storage/core/schema/core_service_journal.json.
// Do not edit.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_GENERATED_CORE_SERVICE_JOURNAL_SCHEMA_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_GENERATED_CORE_SERVICE_JOURNAL_SCHEMA_H_

#include <array>
#include <stddef.h>
#include <stdint.h>

#include "base/strings/cstring_view.h"

namespace taffy::storage_schema {{

struct Migration {{
  uint32_t from_version;
  base::cstring_view from_checksum;
  size_t statement_offset;
  size_t statement_count;
}};

// What a superseded version physically was: the creation list a database
// recording that version and checksum actually ran. The generator proves each
// entry by reconstructing the document it describes and recomputing the
// checksum, so a test may build a genuine old database from these rather than
// approximating one from kStatements -- which is not in version order and would
// hand a fixture tables its version never had.
struct HistoricalSchema {{
  uint32_t version;
  base::cstring_view checksum;
  size_t statement_offset;
  size_t statement_count;
}};

inline constexpr base::cstring_view kComponent = {quoted(component)};
inline constexpr uint32_t kVersion = {version}u;
inline constexpr uint64_t kInitialTaskRevision = {initial_revision}u;
inline constexpr base::cstring_view kChecksum = {quoted(checksum(schema_identity(document)))};
inline constexpr base::cstring_view kLedgerStatement = {quoted(ledger_statement)};
inline constexpr std::array<base::cstring_view, {len(statements)}> kStatements = {{
{rows}
}};
inline constexpr std::array<base::cstring_view, {len(migration_statements)}>
    kMigrationStatements = {{
{migration_step_rows}
}};
inline constexpr std::array<Migration, {len(migrations)}> kMigrations = {{{{
{migrations_rendered}
}}}};
inline constexpr std::array<base::cstring_view, {len(historical_statements)}>
    kHistoricalStatements = {{
{historical_step_rows}
}};
inline constexpr std::array<HistoricalSchema, {len(history)}> kHistoricalSchemas = {{{{
{history_rendered}
}}}};

}}  // namespace taffy::storage_schema

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_GENERATED_CORE_SERVICE_JOURNAL_SCHEMA_H_
"""


def refuse_same_version_identity_change(document: dict[str, object]) -> None:
    """Require a version bump when the already-generated identity changes."""
    if not OUTPUT.is_file():
        return
    existing = OUTPUT.read_text(encoding="utf-8")
    version_match = GENERATED_VERSION.search(existing)
    checksum_match = GENERATED_CHECKSUM.search(existing)
    if version_match is None or checksum_match is None:
        raise ValueError("generated schema has no readable version/checksum identity")
    expanded = expand_closed_enum_bounds(document)
    version = expanded.get("version")
    next_checksum = checksum(schema_identity(expanded))
    if int(version_match["value"]) == version and checksum_match["value"] != next_checksum:
        raise ValueError(
            f"schema identity changed at version {version}: generated "
            f"{checksum_match['value']}, next {next_checksum}. Advance the version and "
            f"add a proved one-hop migration; do not rewrite one version's identity"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true")
    action.add_argument("--check", action="store_true")
    args = parser.parse_args()
    document = json.loads(SOURCE.read_text(encoding="utf-8"))
    generated = render(document)
    refuse_same_version_identity_change(document)
    if args.write:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(generated, encoding="utf-8")
        return 0
    if not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8") != generated:
        raise SystemExit(f"stale generated schema: run {Path(__file__).name} --write")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
