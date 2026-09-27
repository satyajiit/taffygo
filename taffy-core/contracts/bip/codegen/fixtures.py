#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The golden and compatibility fixtures, checked against the schema.

Authority boundary: what a fixture verdict claims and whether the contract
agrees. A fixture that disagrees with the schema is a finding here; which of
the two is wrong is a decision for a person, and this module never edits
either.
"""

from __future__ import annotations

import json
import os
from typing import Any

import jsonschema_mini
from contract import Contract, version_tuple
from layout import COMPAT_DIR, GOLDEN_DIR
from outputs import load_version

# Verdicts a compatibility fixture may claim, and what each one asserts.
COMPAT_VERDICTS = {
    "ACCEPT": "decodes and validates against the named definition",
    "REJECT_UNSUPPORTED": "decodes as JSON but carries a value outside a closed enumeration",
    "REJECT_UNSUPPORTED_VERSION": "declares a major version this build does not speak",
    "REJECT_INVALID": "decodes as JSON but breaks a required-field or shape rule",
    "REJECT_OVERSIZED": "is otherwise valid and exceeds the byte bound the fixture declares",
    "REJECT_MALFORMED": "is not well-formed JSON at all",
}

#: The error :mod:`jsonschema_mini` reports for a value outside a closed
#: enumeration. A fixture that must fail on the value itself has to fail with
#: this and with nothing else, or it is proving a different rule than it claims.
CLOSED_ENUM_ERROR = "outside the closed enumeration"


def major_of(version: str) -> str:
    """The major component of a major.minor protocol version."""
    return version.partition(".")[0]


def at_previous_minor(
    entry: dict,
    instance: object,
    contract: Contract,
    spoken_version: str,
    errors_today: list[str],
) -> list[str]:
    """Check a fixture that names a member added after the reader's version.

    This is the question an additive minor step has to answer, and the one a
    single-version checker cannot ask: a consumer built at the previous minor
    version receives a message naming a member that version does not define.
    It must treat the message as unsupported and fail closed. It must not
    round the value to the nearest member it does know, because an adapter
    report labelled with the wrong adapter is worse than no report at all —
    the caller believes it received evidence it never received.

    Both halves are asserted, and both matter. The message must be valid at
    the current version, or the step is not additive but broken; and it must
    be refused at the reader's version *for the value alone*, or the fixture
    is proving some unrelated shape rule.
    """
    name = entry["file"]
    reader = entry["reader_protocol_version"]
    findings: list[str] = []

    if entry["expected_verdict"] != "REJECT_UNSUPPORTED":
        return [
            f"{name}: a reader_protocol_version fixture asserts the closed-enumeration "
            f"rule, so its verdict must be REJECT_UNSUPPORTED, not "
            f"{entry['expected_verdict']}"
        ]
    if major_of(reader) != major_of(spoken_version):
        return [
            f"{name}: reader_protocol_version {reader} differs in major version from "
            f"{spoken_version}, which is a different rule with its own fixture"
        ]
    if version_tuple(reader) >= version_tuple(spoken_version):
        return [
            f"{name}: reader_protocol_version {reader} is not older than {spoken_version}, "
            "so there is no additive step for it to be on the far side of"
        ]
    if errors_today:
        findings.append(
            f"{name}: a message that proves a step additive must be valid at the "
            f"current version, got {errors_today}"
        )

    store, pruned = contract.reader_view(reader)
    if not pruned:
        return findings + [
            f"{name}: a reader at {reader} knows every member this version does, so "
            "nothing here distinguishes an additive step from no step at all"
        ]
    schema, base = store.resolve(
        f"{entry['schema']}#/$defs/{entry['definition']}", entry["schema"]
    )
    reader_errors = jsonschema_mini.validate(instance, schema, store, base)
    if not reader_errors:
        return findings + [
            f"{name}: a reader at {reader} accepted it. A member it does not define "
            "must be unsupported there, never understood"
        ]

    unrelated = [error for error in reader_errors if CLOSED_ENUM_ERROR not in error]
    if unrelated:
        findings.append(
            f"{name}: the reader at {reader} refused it for a reason other than the "
            f"added member, so the fixture proves a different rule: {unrelated}"
        )
    added = [member for members in pruned.values() for member in members]
    if not any(repr(member) in error for member in added for error in reader_errors):
        findings.append(
            f"{name}: the reader at {reader} refused it, but not on any of the members "
            f"added since that version ({', '.join(sorted(added))})"
        )
    return findings


def unlisted(directory: str, listed: set[str], catalogue: str) -> list[str]:
    """Files present in a fixture directory that its catalogue does not name.

    Walking the catalogue proves every entry has a file. It cannot prove the
    reverse, and the reverse is the failure that hurts: a fixture added to the
    directory and never listed is checked by nothing, and every consumer that
    reads the catalogue — the generator here and fixture-replay tests —
    silently does not see it. A note is listed by its sibling message, because
    a note is documentation of a message rather than a fixture of its own.
    """
    findings = []
    for name in sorted(os.listdir(directory)):
        if name == catalogue or name.startswith("."):
            continue
        stem, extension = os.path.splitext(name)
        owner = stem + ".json" if extension == ".md" else name
        if owner in listed:
            continue
        findings.append(
            f"{name}: present in {os.path.basename(directory)}/ and named by no "
            f"entry in {catalogue}, so nothing checks it"
        )
    return findings


def at_next_minor(
    entry: dict[str, Any],
    instance: dict[str, Any],
    spoken_version: str,
) -> list[str]:
    """Check a fixture whose whole purpose is a message from a later minor.

    The comparison this makes is the one a decoder deliberately does not: a
    reader accepts any minor of its own major, so a fixture declaring a *past*
    minor validates exactly as a future one does. The document keeps passing
    and stops testing anything — it has quietly become a message from behind
    the reader rather than ahead of it, and the additive-growth rule it exists
    for is no longer under test. Nothing else in the corpus notices, which is
    why the assertion lives here.
    """
    name = entry["file"]
    declared = instance.get("schema_version")
    if not isinstance(declared, str):
        return [f"{name}: no schema_version to read a minor from"]
    if major_of(declared) != major_of(spoken_version):
        return [
            f"{name}: declares major version {major_of(declared)}, which is a "
            "different rule with a fixture of its own"
        ]
    expected = f"{major_of(spoken_version)}.{version_tuple(spoken_version)[1] + 1}"
    if declared != expected:
        return [
            f"{name}: declares {declared} while the protocol speaks "
            f"{spoken_version}, so it is a message from a past minor and the "
            f"rule it exists for is not under test. Raise it to {expected} in "
            "the change that raises the schema."
        ]
    return []


def verify(contract: Contract) -> int:
    store = contract.store
    failures: list[str] = []
    checked = 0
    spoken_version = load_version()["protocol_version"]
    spoken_major = major_of(spoken_version)

    with open(os.path.join(GOLDEN_DIR, "index.json"), encoding="utf-8") as handle:
        index = json.load(handle)
    failures += unlisted(
        GOLDEN_DIR, {entry["file"] for entry in index["messages"]}, "index.json"
    )
    for entry in index["messages"]:
        path = os.path.join(GOLDEN_DIR, entry["file"])
        note = os.path.splitext(path)[0] + ".md"
        checked += 1
        if not os.path.exists(note):
            failures.append(f"{entry['file']}: no sibling note naming what it proves")
        with open(path, encoding="utf-8") as handle:
            instance = json.load(handle)
        schema, base = store.resolve(
            f"{entry['schema']}#/$defs/{entry['definition']}", entry["schema"]
        )
        for error in jsonschema_mini.validate(instance, schema, store, base):
            failures.append(f"{entry['file']}: {error}")

    with open(os.path.join(COMPAT_DIR, "manifest.json"), encoding="utf-8") as handle:
        manifest = json.load(handle)
    failures += unlisted(
        COMPAT_DIR, {entry["file"] for entry in manifest["fixtures"]}, "manifest.json"
    )
    for entry in manifest["fixtures"]:
        path = os.path.join(COMPAT_DIR, entry["file"])
        checked += 1
        verdict = entry["expected_verdict"]
        if verdict not in COMPAT_VERDICTS:
            failures.append(f"{entry['file']}: unknown verdict {verdict!r}")
            continue
        raw = open(path, "rb").read()
        try:
            instance = json.loads(raw.decode("utf-8"))
            malformed = False
        except (ValueError, UnicodeDecodeError):
            instance, malformed = None, True

        if verdict == "REJECT_MALFORMED":
            if not malformed:
                failures.append(f"{entry['file']}: expected malformed JSON, but it parsed")
            continue
        if malformed:
            failures.append(f"{entry['file']}: could not be parsed as JSON")
            continue
        schema, base = store.resolve(
            f"{entry['schema']}#/$defs/{entry['definition']}", entry["schema"]
        )
        errors = jsonschema_mini.validate(instance, schema, store, base)

        if "reader_protocol_version" in entry:
            failures += at_previous_minor(
                entry, instance, contract, spoken_version, errors
            )
            continue

        # Not a `continue`: the fixture is an ACCEPT document and must still
        # validate at the version this build speaks. The minor it declares is
        # one more thing about it, not the whole of what it proves.
        if entry.get("declares_next_minor"):
            failures += at_next_minor(entry, instance, spoken_version)

        if verdict == "REJECT_OVERSIZED":
            # The fixture must be valid in every way but its size, so that the
            # size check is provably the only thing rejecting it.
            limit = entry["fixture_limit_bytes"]
            if errors:
                failures.append(
                    f"{entry['file']}: an oversized fixture must otherwise validate, "
                    f"got {errors}"
                )
            if len(raw) <= limit:
                failures.append(
                    f"{entry['file']}: {len(raw)} bytes does not exceed the "
                    f"fixture bound of {limit}"
                )
            continue

        if verdict == "REJECT_UNSUPPORTED_VERSION":
            # The version gate runs before the body is trusted, so the body of
            # this fixture is deliberately valid.
            declared = instance.get("schema_version")
            if not isinstance(declared, str):
                failures.append(f"{entry['file']}: no schema_version to reject on")
            elif major_of(declared) == spoken_major:
                failures.append(
                    f"{entry['file']}: declares major version {major_of(declared)}, "
                    f"which this build does speak"
                )
            if errors:
                failures.append(
                    f"{entry['file']}: the body must be valid so the version alone "
                    f"is the reason to reject, got {errors}"
                )
            continue

        if verdict == "ACCEPT" and errors:
            failures.append(f"{entry['file']}: expected acceptance, got {errors}")
        if verdict.startswith("REJECT") and not errors:
            failures.append(f"{entry['file']}: expected rejection, but it validated")

    for failure in failures:
        print(f"fixture: {failure}")
    if failures:
        print()
        print(f"{len(failures)} fixture finding(s) across {checked} document(s)")
        return 1
    print(f"{checked} fixture document(s) match their declared verdict")
    return 0
