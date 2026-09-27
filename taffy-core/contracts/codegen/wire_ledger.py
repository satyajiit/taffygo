# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The frozen wire-value ledger: every ordinal recorded once, and never moved.

The generator already refuses a record whose ordinals are not contiguous from
zero. That rule is necessary and it is not sufficient, because inserting a
field in the middle of a record and renumbering the rest satisfies it perfectly
while changing what every following field means. On a wire that is only ever
read by two builds of the same change that is survivable; on the device journal
it is not, because a phone that already holds transactions written by the old
layout will decode them with the new one and be confidently wrong.

So the ordinals are written down. `schema/wire-ledger.json` is the frozen
record of what each member's wire value was; the schema on disk is what it is
now; and the two are compared on every run. Reuse, reordering, retyping and
removal are refused outright — there is no flag for them, because there is no
change of that kind that is safe. An append is allowed, but only through
`--refreeze`, which will not run unless the contract version was raised in the
same change, and which refuses to rewrite a frozen payload unless the codec's
own `schema_version` was raised too.

This generalises the `x-bip-added-in` ledger BIP already carries. BIP records
*when* a member arrived because its compatibility fixtures derive an older
reader from it; this records *where* a member sits because these contracts are
positional on the wire. Same principle: a claim about compatibility is only
worth what the frozen record beside it can prove.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from contract_schema import CONTRACT_ROOT, ContractError, parse_type

LEDGER_NAME = "wire-ledger.json"

#: Sections in the order they are rendered. Enumerations first because a member
#: value is the smallest thing that can move and the most expensive when it
#: does; unions last because their variants carry records of their own.
#: `interfaces` is last because a method is the largest thing here: moving one
#: reinterprets a whole message rather than one field of it, and the entry
#: carries every argument's own ordinal and type so a retyped argument is a
#: frozen-value change too.
#:
#: A method's parameters are recorded the way a record's fields are — one
#: entry per argument — rather than as one signature string, because Mojo
#: builds a parameter list into a versioned struct and appending an argument
#: to it is the same append-only change as adding a field. Recorded as one
#: string, an append was indistinguishable from a rename, so the ledger
#: refused it outright and no flag could permit it: the only way to give an
#: existing call a new argument was to declare a second call beside it. The
#: reply is still frozen whole, and deliberately — nothing has yet needed to
#: append to one, and until something does, "the reply changed" is the honest
#: severity for every change to it.
SECTIONS = ("enums", "records", "unions", "interfaces")


def ledger_path(contract: str) -> Path:
    return CONTRACT_ROOT / contract / "schema" / LEDGER_NAME


def version_text(version: dict[str, Any]) -> str:
    return f"{version['major']}.{version['minor']}"


def _version_tuple(text: str) -> tuple[int, int]:
    major, _, minor = text.partition(".")
    try:
        return (int(major), int(minor))
    except ValueError as error:
        raise ContractError(f"{text!r} is not a major.minor contract version") from error


def live_declarations(schema: dict[str, Any]) -> dict[str, dict[str, Any]]:
    """Every wire-carrying declaration the schema projects, ordinals included.

    The contract's own records and the records of any binary codec it declares
    share one namespace because they share one generated Rust module: a name
    collision between them is already impossible, and keeping them in one table
    means the ledger covers the persisted layout and the message layout alike.
    """
    enums: dict[str, Any] = {}
    records: dict[str, Any] = {}
    unions: dict[str, Any] = {}
    interfaces: dict[str, Any] = {}

    def add_enum(item: dict[str, Any]) -> None:
        enums[item["name"]] = {
            member["name"]: member["wire"] for member in item["members"]
        }

    def add_record(item: dict[str, Any]) -> None:
        records[item["name"]] = {
            field["name"]: [ordinal, field["type"]]
            for ordinal, field in enumerate(item["fields"])
        }

    for item in schema["enums"]:
        add_enum(item)
    for item in schema["structs"]:
        add_record(item)
    for codec in schema.get("binary_codecs", []):
        types = codec["types"]
        for item in types["enums"]:
            add_enum(item)
        for item in types["structs"]:
            add_record(item)
        for item in types["unions"]:
            unions[item["name"]] = {
                variant["name"]: {
                    "wire": variant["wire"],
                    "fields": {
                        field["name"]: field["type"] for field in variant["fields"]
                    },
                }
                for variant in item["variants"]
            }
    def signature(arguments: list[dict[str, Any]]) -> str:
        return ", ".join(
            f"{argument['name']}: {argument['type']}" for argument in arguments
        )

    for item in schema.get("interfaces", []):
        methods: dict[str, Any] = {}
        for method in item["methods"]:
            response = method.get("response")
            methods[method["name"]] = {
                "ordinal": method["ordinal"],
                "params": {
                    argument["name"]: [argument["ordinal"], argument["type"]]
                    for argument in method.get("params", [])
                },
                "reply": "" if response is None else f"({signature(response)})",
            }
        interfaces[item["name"]] = methods

    for name in sorted(set(enums) & set(records)):
        raise ContractError(f"{name} is declared as both an enum and a record")
    return {
        "enums": enums,
        "records": records,
        "unions": unions,
        "interfaces": interfaces,
    }


def load(contract: str) -> dict[str, Any]:
    path = ledger_path(contract)
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractError(f"{path}: {error}") from error
    if not isinstance(value, dict) or value.get("contract") != contract.replace("-", "_"):
        raise ContractError(f"{path}: ledger does not name this contract")
    for section in SECTIONS:
        if not isinstance(value.get(section), dict):
            raise ContractError(f"{path}: ledger section {section} must be an object")
    if not isinstance(value.get("contract_version"), str):
        raise ContractError(f"{path}: ledger must record the contract version")
    return value


def _frozen_findings(frozen: Any, live: Any, owner: str) -> list[str]:
    """What moved, what was retyped, and what disappeared, in that order."""
    findings: list[str] = []
    for name, recorded in frozen.items():
        current = live.get(name)
        if current is None:
            findings.append(f"{owner} {name} was removed; a frozen wire value never leaves")
            continue
        if isinstance(recorded, dict):
            findings += _frozen_findings(recorded, current, f"{owner} {name}")
            continue
        if recorded != current:
            findings.append(
                f"{owner} {name} was frozen as {json.dumps(recorded)} and is now "
                f"{json.dumps(current)}; a wire value never moves"
            )
    return findings


def _additions(frozen: Any, live: Any, owner: str) -> list[str]:
    additions: list[str] = []
    for name, current in live.items():
        recorded = frozen.get(name)
        if recorded is None:
            additions.append(f"{owner} {name}")
            continue
        if isinstance(current, dict) and isinstance(recorded, dict):
            additions += _additions(recorded, current, f"{owner} {name}")
    return additions


def _owning_declaration(entry: str) -> str:
    """The declaration an addition belongs to, for the reachability test."""
    parts = entry.split()
    return parts[1] if len(parts) > 1 else parts[0]


def compare(ledger: dict[str, Any], live: dict[str, Any]) -> tuple[list[str], list[str]]:
    """`(refusals, additions)` between the frozen ledger and the live schema."""
    refusals: list[str] = []
    additions: list[str] = []
    for section in SECTIONS:
        refusals += _frozen_findings(ledger[section], live[section], section[:-1])
        additions += _additions(ledger[section], live[section], section[:-1])
    return refusals, additions


def reachable(live: dict[str, Any], root: str) -> set[str]:
    """Every declaration a codec root actually reads, transitively."""
    seen: set[str] = set()
    pending = [root]
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        members: list[str] = []
        if name in live["records"]:
            members = [value[1] for value in live["records"][name].values()]
        elif name in live["unions"]:
            for variant in live["unions"][name].values():
                members += list(variant["fields"].values())
        for declared in members:
            reference = parse_type(declared)
            inner = reference.inner if reference.inner is not None else reference
            if inner.kind == "named":
                pending.append(inner.name)
    return seen


def codec_bump_findings(
    ledger: dict[str, Any],
    live: dict[str, Any],
    additions: list[str],
    models: dict[str, dict[str, Any]],
) -> list[str]:
    """Refuse an appended field on a persisted layout without a version bump.

    A payload codec reads its record positionally, so appending a field changes
    what a decoder built yesterday makes of the bytes after it. The codec's own
    `schema_version` is the only thing that lets that decoder recognise the
    payload as one it does not speak, so an append inside its reachable types
    is not finished until that number is raised.
    """
    findings: list[str] = []
    recorded = ledger.get("codec_schema_versions", {})
    for name, model in models.items():
        touched = reachable(live, model["root"])
        involved = [
            entry for entry in additions if _owning_declaration(entry) in touched
        ]
        frozen_version = recorded.get(name)
        if frozen_version is None:
            findings.append(f"the ledger does not record a schema version for codec {name}")
            continue
        if involved and model["schema_version"] <= frozen_version:
            findings.append(
                f"codec {name} still declares schema_version {frozen_version} while "
                f"{involved[0]} was appended to a record it reads; raise it, because a "
                "device holding payloads written by the old layout has no other way to "
                "tell them apart"
            )
    return findings


def refreeze_findings(
    ledger: dict[str, Any],
    live: dict[str, Any],
    current_version: str,
    models: dict[str, dict[str, Any]],
) -> list[str]:
    """Everything that stops `--refreeze` from recording this change."""
    refusals, additions = compare(ledger, live)
    findings = [
        f"--refreeze will not re-bless a moved or removed wire value: {refusal}"
        for refusal in refusals
    ]
    findings += codec_bump_findings(ledger, live, additions, models)
    if _version_tuple(current_version) <= _version_tuple(ledger["contract_version"]):
        findings.append(
            f"--refreeze needs the contract version raised above "
            f"{ledger['contract_version']} in the same change; schema/contract.json "
            f"still says {current_version}"
        )
    return findings


def build(contract: str, schema: dict[str, Any], previous: dict[str, Any]) -> dict[str, Any]:
    """The ledger this schema implies, keeping the previous frozen metadata."""
    live = live_declarations(schema)
    return {
        "contract": contract.replace("-", "_"),
        "description": previous["description"],
        "contract_version": version_text(schema["version"]),
        "codec_schema_versions": previous.get("codec_schema_versions", {}),
        "frozen_payloads": previous.get("frozen_payloads", {}),
        **live,
    }


def render(ledger: dict[str, Any]) -> str:
    """One declaration per line, so a moved ordinal is one reviewable line."""
    lines = ['{']
    lines.append(f'  "contract": {json.dumps(ledger["contract"])},')
    lines.append(f'  "description": {json.dumps(ledger["description"])},')
    lines.append(f'  "contract_version": {json.dumps(ledger["contract_version"])},')
    for key in ("codec_schema_versions", "frozen_payloads"):
        lines.append(f'  "{key}": {json.dumps(ledger[key], sort_keys=True)},')
    for index, section in enumerate(SECTIONS):
        lines.append(f'  "{section}": {{')
        entries = ledger[section]
        for position, name in enumerate(entries):
            comma = "" if position == len(entries) - 1 else ","
            lines.append(f'    {json.dumps(name)}: {json.dumps(entries[name])}{comma}')
        lines.append("  }" + ("," if index < len(SECTIONS) - 1 else ""))
    lines.append("}")
    return "\n".join(lines) + "\n"


def audit(contract: str, schema: dict[str, Any], models: dict[str, Any]) -> None:
    """Fail unless the schema on disk still matches its frozen ledger."""
    ledger = load(contract)
    live = live_declarations(schema)
    refusals, additions = compare(ledger, live)
    if refusals:
        raise ContractError(
            f"{contract}: frozen wire values changed, which no flag permits:\n  "
            + "\n  ".join(refusals)
        )
    findings = codec_bump_findings(ledger, live, additions, models)
    if findings:
        raise ContractError(f"{contract}: " + "\n  ".join(findings))
    if additions:
        raise ContractError(
            f"{contract}: the wire ledger does not record {len(additions)} new "
            f"declaration(s), starting with {additions[0]}. Raise the contract version "
            f"and run the generator with --refreeze."
        )
    if version_text(schema["version"]) != ledger["contract_version"]:
        raise ContractError(
            f"{contract}: the ledger was frozen at contract version "
            f"{ledger['contract_version']} and the schema now declares "
            f"{version_text(schema['version'])}; run --refreeze to record it"
        )


def assert_nothing_moved(contract: str, schema: dict[str, Any]) -> None:
    """The refusal that comes before every other question `--refreeze` asks.

    A moved ordinal is not a re-blessing decision, so it is not weighed against
    a version bump or reported after a fixture mismatch. It is answered first
    and on its own terms.
    """
    refusals, _additions = compare(load(contract), live_declarations(schema))
    if refusals:
        raise ContractError(
            f"{contract}: --refreeze will not re-bless a moved or removed wire value:\n  "
            + "\n  ".join(refusals)
        )


def assert_codec_bumps(
    contract: str, schema: dict[str, Any], models: dict[str, Any]
) -> None:
    """The second refusal, asked before any fixture is looked at.

    An append inside a codec's reachable types is a persisted-layout change.
    Saying so before the golden document is checked keeps the first message a
    person sees the one that names what actually has to happen.
    """
    ledger = load(contract)
    live = live_declarations(schema)
    _refusals, additions = compare(ledger, live)
    findings = codec_bump_findings(ledger, live, additions, models)
    if findings:
        raise ContractError(f"{contract}: " + "\n  ".join(findings))


def refreeze_ledger(
    contract: str,
    schema: dict[str, Any],
    models: dict[str, Any],
    changed_payloads: bool,
) -> bool:
    """Rewrite the ledger, if and only if the change is an append and declared.

    Returns whether anything was written. The two refusals here are the whole
    point of the flag: a moved ordinal is never re-blessed, and an append is
    only re-blessed once the version a reader would check has been raised.
    """
    ledger = load(contract)
    live = live_declarations(schema)
    _refusals, additions = compare(ledger, live)
    current = version_text(schema["version"])
    if not additions and not changed_payloads and current == ledger["contract_version"]:
        return False
    findings = refreeze_findings(ledger, live, current, models)
    if findings:
        raise ContractError(f"{contract}:\n  " + "\n  ".join(findings))
    updated = build(contract, schema, ledger)
    for name, model in models.items():
        updated["codec_schema_versions"][name] = model["schema_version"]
    ledger_path(contract).write_text(render(updated), encoding="utf-8")
    return True
