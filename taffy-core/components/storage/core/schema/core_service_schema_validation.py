# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Validate and prove the canonical core-service journal schema."""

from __future__ import annotations

import copy
import json
import re
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BROWSER_COMMAND_SOURCE = ROOT.parents[3] / "common" / "public" / "bip_action.h"
BROWSER_COMMAND_MAX_TOKEN = "$BROWSER_COMMAND_TYPE_MAX"
BROWSER_DOCUMENT_COMMANDS_TOKEN = "$BROWSER_DOCUMENT_COMMAND_TYPES"
BROWSER_COMMAND_EXPANSION_KEY = "browser_command_expansion"

#: The fields every version of this document shares. A history entry supplies
#: only what changed, and the reconstruction below fills the rest from head.
INVARIANT_KEYS = ("component", "initial_task_revision", "ledger_statement")

#: The one document key that is provenance rather than schema. ``history``
#: records what each superseded version physically was; it does not describe the
#: database this generator creates. It is therefore excluded from the checksum
#: for two reasons that both have to hold at once: every recorded historical
#: checksum was computed over a document that had no ``history`` key, so
#: including it would make every one of them unverifiable; and a database on disk
#: compares the checksum it recorded against the one rendered here, so a
#: provenance edit that moved the head checksum would strand every existing
#: journal at a version no migration entry claims.
PROVENANCE_KEYS = ("history",)


def browser_command_values() -> list[int]:
    """Read the closed browser-command table that native validation uses.

    The SQL constraint used to carry a literal ``5`` while the native enum had
    already grown through ``8``. Parsing the reviewed closed table makes that
    drift impossible: a command becomes persistable in the same change that it
    becomes a member of ``kAllBrowserCommandTypes``.
    """
    source = BROWSER_COMMAND_SOURCE.read_text(encoding="utf-8")
    enum_match = re.search(
        r"enum class BrowserCommandType[^\{]*\{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    table_match = re.search(
        r"kAllBrowserCommandTypes\s*=\s*\{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    if enum_match is None or table_match is None:
        raise ValueError("cannot find the closed BrowserCommandType declarations")
    enum_values = {
        name: int(value)
        for name, value in re.findall(r"\b(k[A-Za-z0-9_]+)\s*=\s*([0-9]+)", enum_match["body"])
    }
    names = re.findall(r"BrowserCommandType::(k[A-Za-z0-9_]+)", table_match["body"])
    if not names or len(names) != len(set(names)) or set(names) != set(enum_values):
        raise ValueError(
            "kAllBrowserCommandTypes must contain every BrowserCommandType exactly once"
        )
    values = [enum_values[name] for name in names]
    if values != list(range(len(values))):
        raise ValueError("BrowserCommandType wire values must be contiguous from zero")
    return values


def current_browser_command_expansion() -> dict[str, object]:
    """Return the exact enum-derived values embedded in today's SQL.

    ``document_types`` is read from ``kDocumentBoundBrowserCommandTypes``, the
    closed table the native side declares, and not from the wire values.

    It used to be "every command from OPEN_OBSERVED_LINK onward", which is the
    numeric upper bound the enum's own comment says not to write. ``kReload``
    and ``kStopLoading`` were appended after the node-bearing members and so
    inherited a requirement neither can meet: no reload names a document, so
    the SQL CHECK refused every reload intent, the dispatch came back
    ``kDispatchFailed``, and the errand that proposed it ended as "Taffy
    stopped unexpectedly". A property is a table, not a suffix.
    """
    values = browser_command_values()
    source = BROWSER_COMMAND_SOURCE.read_text(encoding="utf-8")
    enum_match = re.search(
        r"enum class BrowserCommandType[^\{]*\{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    bound_match = re.search(
        r"kDocumentBoundBrowserCommandTypes\s*=\s*\{(?P<body>.*?)\n\};",
        source,
        re.DOTALL,
    )
    if enum_match is None or bound_match is None:
        raise ValueError("cannot find the closed document-bound command table")
    enum_values = {
        name: int(value)
        for name, value in re.findall(
            r"\b(k[A-Za-z0-9_]+)\s*=\s*([0-9]+)", enum_match["body"]
        )
    }
    names = re.findall(r"BrowserCommandType::(k[A-Za-z0-9_]+)", bound_match["body"])
    if not names or len(names) != len(set(names)) or not set(names) <= set(enum_values):
        raise ValueError(
            "kDocumentBoundBrowserCommandTypes must name distinct BrowserCommandType members"
        )
    return {
        "maximum": values[-1],
        "document_types": sorted(enum_values[name] for name in names),
    }


def validated_browser_command_expansion(
    expansion: object, label: str
) -> tuple[str, str]:
    """Validate one frozen/current expansion and return its SQL literals."""
    if not isinstance(expansion, dict) or set(expansion) != {
        "maximum",
        "document_types",
    }:
        raise ValueError(
            f"{label} must name exactly maximum and document_types"
        )
    maximum = expansion.get("maximum")
    document_types = expansion.get("document_types")
    # Ascending, unique and inside the closed range -- deliberately not a
    # contiguous suffix. Requiring a suffix was the same numeric rule as the
    # one above, restated as a validator, so it agreed with the defect instead
    # of catching it.
    if (
        not isinstance(maximum, int)
        or maximum < 0
        or not isinstance(document_types, list)
        or not document_types
        or not all(isinstance(value, int) for value in document_types)
        or document_types != sorted(set(document_types))
        or document_types[0] < 0
        or document_types[-1] > maximum
    ):
        raise ValueError(
            f"{label} must carry an ascending set of document-command wire values "
            f"inside the closed range"
        )
    return str(maximum), ",".join(str(value) for value in document_types)


def expand_closed_enum_bounds(
    document: dict[str, object], expansion: object | None = None
) -> dict[str, object]:
    """Expand the enum-derived SQL tokens with current or frozen values."""
    expanded = copy.deepcopy(document)
    maximum, document_commands = validated_browser_command_expansion(
        current_browser_command_expansion() if expansion is None else expansion,
        "browser command expansion",
    )

    def replace(statement: str) -> str:
        return statement.replace(BROWSER_COMMAND_MAX_TOKEN, maximum).replace(
            BROWSER_DOCUMENT_COMMANDS_TOKEN, document_commands
        )

    expanded["statements"] = [replace(value) for value in expanded["statements"]]
    for migration in expanded.get("migrations", []):
        migration["statements"] = [replace(value) for value in migration["statements"]]
    return expanded


def carries_closed_enum_tokens(document: dict[str, object]) -> bool:
    """Whether a schema fragment still needs the external enum snapshot."""
    statement_lists = [document.get("statements", [])]
    statement_lists.extend(
        migration.get("statements", [])
        for migration in document.get("migrations", [])
        if isinstance(migration, dict)
    )
    return any(
        BROWSER_COMMAND_MAX_TOKEN in statement
        or BROWSER_DOCUMENT_COMMANDS_TOKEN in statement
        for statements in statement_lists
        if isinstance(statements, list)
        for statement in statements
        if isinstance(statement, str)
    )


def schema_identity(document: dict[str, object]) -> dict[str, object]:
    """Return the checksum-bearing half of a schema document."""
    return {key: value for key, value in document.items() if key not in PROVENANCE_KEYS}


def checksum(document: dict[str, object]) -> str:
    canonical = json.dumps(document, ensure_ascii=True, separators=(",", ":"), sort_keys=True)
    value = 0xCBF29CE484222325
    for byte in canonical.encode("utf-8"):
        value ^= byte
        value = (value * 0x00000100000001B3) & 0xFFFF_FFFF_FFFF_FFFF
    return f"{value:016x}"


def quoted(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def is_checksum(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 16
        and all(character in "0123456789abcdef" for character in value)
    )


def validated_statements(statements: object, label: str) -> list[str]:
    if not isinstance(statements, list) or not statements or not all(
        isinstance(statement, str) and statement.startswith("CREATE ")
        for statement in statements
    ):
        raise ValueError(f"{label} must be non-empty CREATE statements")
    return statements


def validated_migrations(migrations: object, version: int, label: str) -> list[dict]:
    """Validate one document's migration ladder and return it in file order."""
    if not isinstance(migrations, list):
        raise ValueError(f"{label} must be a list")
    seen_versions: set[int] = set()
    for migration in migrations:
        if not isinstance(migration, dict):
            raise ValueError(f"each {label} entry must be an object")
        from_version = migration.get("from_version")
        steps = migration.get("statements")
        if (
            not isinstance(from_version, int)
            or from_version <= 0
            or from_version >= version
            or from_version in seen_versions
        ):
            raise ValueError(f"{label} source versions must be unique and precede {version}")
        if not is_checksum(migration.get("from_checksum")):
            raise ValueError(f"{label} checksum must be 16 lowercase hex characters")
        if not isinstance(steps, list) or not all(
            isinstance(statement, str)
            and statement.startswith(
                ("CREATE ", "DROP ", "ALTER ", "UPDATE ", "INSERT ")
            )
            for statement in steps
        ):
            raise ValueError(f"{label} statements must use a closed SQL operation set")
        # An empty list is allowed and is not a placeholder. A version bump can
        # change what this document says without changing a byte on disk -- a
        # corrected migration ladder is inside the checksummed identity, so
        # fixing one moves the head checksum and strands every database that
        # recorded the old one, and the entry that rescues them has genuinely
        # nothing to execute. It is not taken on trust: ``proved_migrations``
        # runs the list against a real database built from that version's
        # recorded schema, so an empty entry passes only where the two schemas
        # are already identical, and a non-empty one only where it lands exactly
        # on head.
        seen_versions.add(from_version)
    return migrations


def validated_history(
    document: dict[str, object], version: int, migrations: list[dict]
) -> list[dict]:
    """Return the recorded history, having proved every entry against its checksum."""
    history = document.get("history")
    if not isinstance(history, list):
        raise ValueError("history must be a list, one entry per migration source version")
    recorded = {
        migration["from_version"]: migration["from_checksum"] for migration in migrations
    }
    normalized: list[dict] = []
    seen_versions: set[int] = set()
    previous_version = 0
    for entry in history:
        if not isinstance(entry, dict):
            raise ValueError("each history entry must be an object")
        entry_version = entry.get("version")
        entry_checksum = entry.get("checksum")
        if not isinstance(entry_version, int) or entry_version not in recorded:
            raise ValueError("each history entry must name one migration source version")
        if entry_version in seen_versions or entry_version <= previous_version:
            raise ValueError("history must list ascending unrepeated versions")
        if entry_checksum != recorded[entry_version]:
            raise ValueError(
                f"history version {entry_version} records checksum {entry_checksum!r}, "
                f"but its migration entry records {recorded[entry_version]!r}"
            )
        label = f"history version {entry_version}"
        expanded_entry = expand_history_prefix(document, entry, entry_version, label)
        validated_statements(expanded_entry.get("statements"), f"{label} statements")
        if "migrations" in expanded_entry:
            validated_migrations(
                expanded_entry["migrations"], entry_version, f"{label} migrations"
            )
        # The proof. Rebuild the document that version was, from the fields every
        # version shares plus the ones this entry records, and recompute the
        # checksum the ladder already committed to.
        identity: dict[str, object] = {key: document[key] for key in INVARIANT_KEYS}
        identity["version"] = entry_version
        identity["statements"] = expanded_entry["statements"]
        if "migrations" in expanded_entry:
            identity["migrations"] = expanded_entry["migrations"]
        computed = checksum(identity)
        if computed != entry_checksum:
            raise ValueError(
                f"{label} does not reconstruct the document that checksum names: "
                f"computed {computed}, recorded {entry_checksum}. Either a statement "
                f"list is wrong or the version carried a field this reconstruction "
                f"does not model; do not adjust the checksum to agree with it"
            )
        seen_versions.add(entry_version)
        previous_version = entry_version
        normalized.append(expanded_entry)
    unrecorded = sorted(set(recorded) - seen_versions)
    if unrecorded:
        raise ValueError(
            f"migration source versions with no recorded history: {unrecorded}. "
            f"A migration that cannot be given a real old database cannot be tested"
        )
    return normalized


def expand_history_prefix(
    document: dict[str, object], entry: dict, entry_version: int, label: str
) -> dict:
    """Expand a checksum-proved prefix snapshot of the immediately prior head.

    A version bump normally appends head statements and appends steps to every
    one-hop migration. Repeating the entire former head in ``history`` makes a
    small schema addition duplicate hundreds of lines. A prefix snapshot names
    the exact old lengths instead. It is not trusted: the existing checksum
    proof below still reconstructs and hashes the complete historical document,
    so changing any byte inside a claimed prefix fails generation.
    """
    has_full = "statements" in entry
    has_prefix = "statement_prefix_count" in entry
    if has_full == has_prefix:
        raise ValueError(
            f"{label} must carry either full statements or one statement prefix"
        )
    if has_full:
        if (
            "migration_statement_prefix_counts" in entry
            or BROWSER_COMMAND_EXPANSION_KEY in entry
        ):
            raise ValueError(f"{label} cannot mix full and prefix history")
        return entry

    statement_count = entry.get("statement_prefix_count")
    prefix_counts = entry.get("migration_statement_prefix_counts")
    statements = document.get("statements")
    if (
        not isinstance(statement_count, int)
        or statement_count <= 0
        or not isinstance(statements, list)
        or statement_count > len(statements)
        or not isinstance(prefix_counts, dict)
    ):
        raise ValueError(f"{label} has an invalid statement prefix")

    current_migrations = [
        migration
        for migration in document.get("migrations", [])
        if migration.get("from_version", entry_version) < entry_version
    ]
    expected_keys = {str(migration["from_version"]) for migration in current_migrations}
    if set(prefix_counts) != expected_keys:
        raise ValueError(f"{label} must name every historical migration prefix exactly once")

    expanded_migrations: list[dict] = []
    for migration in current_migrations:
        count = prefix_counts[str(migration["from_version"])]
        steps = migration.get("statements")
        if (
            not isinstance(count, int)
            or count < 0
            or not isinstance(steps, list)
            or count > len(steps)
        ):
            raise ValueError(f"{label} has an invalid migration statement prefix")
        expanded_migrations.append(
            {
                "from_version": migration["from_version"],
                "from_checksum": migration["from_checksum"],
                "statements": steps[:count],
            }
        )

    reconstructed = {
        "version": entry_version,
        "checksum": entry["checksum"],
        "statements": statements[:statement_count],
        "migrations": expanded_migrations,
    }
    if carries_closed_enum_tokens(reconstructed):
        expansion = entry.get(BROWSER_COMMAND_EXPANSION_KEY)
        if expansion is None:
            raise ValueError(
                f"{label} uses enum-derived SQL and must freeze "
                f"{BROWSER_COMMAND_EXPANSION_KEY}"
            )
        reconstructed = expand_closed_enum_bounds(reconstructed, expansion)
    elif BROWSER_COMMAND_EXPANSION_KEY in entry:
        raise ValueError(f"{label} freezes a browser command expansion it does not use")
    return reconstructed


def objects_in(statements: list[str], label: str) -> list[tuple[str, str, str]]:
    """Return the schema a real SQLite database has after running ``statements``."""
    connection = sqlite3.connect(":memory:")
    try:
        for statement in statements:
            try:
                connection.execute(statement)
            except sqlite3.Error as error:
                raise ValueError(f"{label}: {statement!r} failed: {error}") from error
        return connection.execute(
            "SELECT type, name, sql FROM sqlite_master "
            "WHERE name NOT LIKE 'sqlite_%' ORDER BY type, name"
        ).fetchall()
    finally:
        connection.close()


def describe_difference(
    got: list[tuple[str, str, str]], head: list[tuple[str, str, str]]
) -> str:
    """Name every way two schemas differ, in the terms a reader can act on."""
    got_by_name = {name: sql for _, name, sql in got}
    head_by_name = {name: sql for _, name, sql in head}
    lines: list[str] = []
    for name in sorted(set(head_by_name) - set(got_by_name)):
        lines.append(f"  missing: {name}")
    for name in sorted(set(got_by_name) - set(head_by_name)):
        lines.append(f"  unexpected: {name}")
    for name in sorted(set(got_by_name) & set(head_by_name)):
        if got_by_name[name] != head_by_name[name]:
            lines.append(f"  differs: {name}")
            lines.append(f"    migrated: {got_by_name[name]}")
            lines.append(f"    head:     {head_by_name[name]}")
    return "\n".join(lines)


def proved_migrations(
    document: dict[str, object], migrations: list[dict], history: list[dict]
) -> None:
    """Refuse to generate unless every migration actually lands on head.

    This is the invariant the module docstring names and the one nothing used to
    check. Each entry claims to carry a database recording ``from_version`` all
    the way to head in one hop, and each was written by hand against whatever
    head was on the day it was written. Nothing re-examined them afterwards, so
    when version 9 added two columns to ``core_account_session`` only the
    version-8 entry gained the ``ALTER`` statements. Four paths went on
    producing a version-8 table and then stamping it as version 9. The result
    was not a failed migration, which would have been visible: it was a database
    the product believed was current, whose first ``SELECT`` naming one of the
    new columns aborted the process, on every start, with no way back.

    A device test caught it, which is the right test but the wrong moment. The
    statements are here, the head schema is here, and SQLite is in the standard
    library -- so the claim can be settled where it is made, before a byte is
    written, against a real database rather than by reading.
    """
    head = objects_in(document["statements"], "head statements")
    recorded = {entry["version"]: entry["statements"] for entry in history}
    for migration in migrations:
        from_version = migration["from_version"]
        label = f"migration from version {from_version}"
        migrated = objects_in(
            list(recorded[from_version]) + list(migration["statements"]), label
        )
        if migrated == head:
            continue
        raise ValueError(
            f"{label} does not produce the head schema:\n"
            f"{describe_difference(migrated, head)}\n"
            f"Every migration entry carries its source version to head in one "
            f"hop, so raising the head version means revisiting all of them. Add "
            f"what this path is missing; do not weaken the comparison"
        )

