#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Hold a facade method and the body record it carries to the same shape.

A contract with an interface has two independent lists that describe one
call: the method a surface invokes, and the record that call is turned into
before it crosses the next seam. Nothing made them agree. Adding a field to
the body alone validates perfectly — every ordinal is contiguous, every type
is declared, every fixture still round-trips — and the field is simply
unreachable, because no argument on the facade supplies it. It has happened
twice: `SaveCustomProviderBody` carried `models` and `detected_server` from
Core API 3.13 and `SaveCustomProvider` took the five arguments it always had,
so a probe's models could not cross the Mojo seam at all; and
`ProbeCustomEndpointBody` grew no identity while `ProviderProbeView` required
one, so a verdict had nowhere to land. Two for two is not a slip, it is the
default failure of a schema with two lists and no rule between them.

So the rule is written down. `facade_bodies` in a contract's schema binds one
interface method to one body record, and this file refuses:

  * a body field no argument supplies and no entry declares browser-minted;
  * an argument that maps to no body field, so a surface can state something
    the next seam cannot carry;
  * a type that differs between the two;
  * a tagged body no method claims, which is how a new command kind reaches
    the wire with no way for a surface to send it;
  * a `browser_supplied` or `parameter_names` entry that names nothing, so a
    exemption cannot outlive the field it was written for.

Run it over every contract, or one:

    python3 taffy-core/contracts/codegen/facade_bodies.py
    python3 taffy-core/contracts/codegen/facade_bodies.py --contract core-api
    python3 taffy-core/contracts/codegen/facade_bodies.py --self-test

Host Python 3 only, no installs.
"""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from contract_schema import ContractError  # noqa: E402
from contract_validation import load_schema, validate_schema  # noqa: E402

CONTRACTS = ("browsing", "core-api", "core-service", "tool-runtime")

ENTRY_KEYS = {"interface", "method", "body"}
OPTIONAL_ENTRY_KEYS = {"browser_supplied", "parameter_names"}


def _bodies_by_union(schema: dict[str, Any]) -> dict[str, dict[str, str]]:
    """`{union struct: {body record: field name}}` for every tagged union."""
    structs = {item["name"]: item for item in schema["structs"]}
    out: dict[str, dict[str, str]] = {}
    for union in schema.get("tagged_unions", []):
        owner = structs[union["struct"]]
        fields = {field["name"]: field["type"] for field in owner["fields"]}
        bodies: dict[str, str] = {}
        for variant in union["variants"]:
            declared = fields[variant["field"]]
            bodies[declared[len("optional<") : -1]] = variant["field"]
        out[union["struct"]] = bodies
    return out


def _methods(schema: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    return {
        (interface["name"], method["name"]): method
        for interface in schema.get("interfaces", [])
        for method in interface["methods"]
    }


def _entry_findings(
    entry: Any, index: int, schema: dict[str, Any], claimed: dict[str, str]
) -> list[str]:
    """Everything one `facade_bodies` row gets wrong, in reading order."""
    where = f"facade_bodies[{index}]"
    if not isinstance(entry, dict) or not ENTRY_KEYS <= set(entry):
        return [f"{where}: a binding names an interface, a method and a body"]
    if not set(entry) <= ENTRY_KEYS | OPTIONAL_ENTRY_KEYS:
        return [f"{where}: unknown key {sorted(set(entry) - ENTRY_KEYS - OPTIONAL_ENTRY_KEYS)}"]

    structs = {item["name"]: item for item in schema["structs"]}
    methods = _methods(schema)
    identity = (entry["interface"], entry["method"])
    name = f"{entry['interface']}.{entry['method']}"
    findings: list[str] = []
    if identity not in methods:
        findings.append(f"{where}: {name} is not a method of this contract")
    if entry["body"] not in structs:
        findings.append(f"{where}: {entry['body']} is not a record of this contract")
    if findings:
        return findings

    bodies = {body for union in _bodies_by_union(schema).values() for body in union}
    if entry["body"] not in bodies:
        findings.append(
            f"{where}: {entry['body']} is not a tagged body, so binding a method to "
            "it says nothing about what crosses the seam"
        )
    held = claimed.get(entry["body"])
    if held is not None:
        findings.append(f"{where}: {entry['body']} is already carried by {held}")
    claimed[entry["body"]] = name

    minted = entry.get("browser_supplied", [])
    renames = entry.get("parameter_names", {})
    if not isinstance(minted, list) or not all(isinstance(item, str) for item in minted):
        return findings + [f"{where}: browser_supplied must be a list of field names"]
    if not isinstance(renames, dict) or not all(
        isinstance(key, str) and isinstance(value, str) for key, value in renames.items()
    ):
        return findings + [f"{where}: parameter_names must map field names to argument names"]

    body_fields = {field["name"]: field["type"] for field in structs[entry["body"]]["fields"]}
    params = {
        argument["name"]: argument["type"]
        for argument in methods[identity].get("params", [])
    }
    return findings + _shape_findings(name, body_fields, params, minted, renames)


def _shape_findings(
    name: str,
    body_fields: dict[str, str],
    params: dict[str, str],
    minted: list[str],
    renames: dict[str, str],
) -> list[str]:
    """The parameter list against the body, field by field."""
    findings: list[str] = []
    for field in minted:
        if field not in body_fields:
            findings.append(f"{name}: browser_supplied names {field}, which the body has not")
    for field, argument in renames.items():
        if field not in body_fields:
            findings.append(f"{name}: parameter_names names {field}, which the body has not")
        elif field in minted:
            findings.append(f"{name}: {field} is both browser-supplied and renamed")
    if len(set(renames.values())) != len(renames):
        findings.append(f"{name}: parameter_names points two fields at one argument")

    carried: dict[str, str] = {}
    for field, declared in body_fields.items():
        if field in minted:
            if renames.get(field, field) in params:
                findings.append(
                    f"{name}: {field} is declared browser-supplied and the call supplies it"
                )
            continue
        argument = renames.get(field, field)
        carried[argument] = field
        if argument not in params:
            findings.append(
                f"{name}: the body carries {field} and no argument supplies it, so nothing "
                "a caller states can ever reach it"
            )
        elif params[argument] != declared:
            findings.append(
                f"{name}: {field} is {declared} on the body and {argument} is "
                f"{params[argument]} on the call"
            )
    for argument in params:
        if argument not in carried:
            findings.append(
                f"{name}: the call takes {argument} and the body has nowhere to put it"
            )
    return findings


def findings(contract: str, schema: dict[str, Any]) -> list[str]:
    """Every disagreement between this contract's facade and its bodies."""
    entries = schema.get("facade_bodies", [])
    if not isinstance(entries, list):
        return [f"{contract}: facade_bodies must be a list"]
    if not schema.get("interfaces") or not schema.get("tagged_unions"):
        if entries:
            return [
                f"{contract}: facade_bodies is declared and this contract has no facade "
                "with a tagged command record"
            ]
        return []
    if not entries:
        return [
            f"{contract}: declares an interface and a tagged command record and binds "
            "neither to the other, so a body field no argument supplies is invisible"
        ]

    claimed: dict[str, str] = {}
    out: list[str] = []
    for index, entry in enumerate(entries):
        out += _entry_findings(entry, index, schema, claimed)
    for union, bodies in _bodies_by_union(schema).items():
        if not set(bodies) & set(claimed):
            continue
        for body in bodies:
            if body not in claimed:
                out.append(
                    f"{contract}: {union} carries {body} and no facade method does, so the "
                    "kind exists on the wire and no surface can send it"
                )
    return out


def check(contract: str) -> int:
    schema = load_schema(contract)
    problems = findings(contract, schema)
    if problems:
        print(f"facade bodies: {contract}:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    bound = len(schema.get("facade_bodies", []))
    if bound:
        print(f"facade bodies: {contract}: {bound} method(s) agree with the body they carry")
    else:
        print(f"facade bodies: {contract}: no facade over a tagged command record")
    return 0


# --------------------------------------------------------------- self-test --

def _field(name: str, declared: str, ordinal: int) -> dict[str, Any]:
    return {
        "name": name,
        "type": declared,
        "ordinal": ordinal,
        "description": f"Self-test {name}.",
    }


def _record(name: str, *fields: dict[str, Any]) -> dict[str, Any]:
    return {"name": name, "description": "Self-test record.", "fields": list(fields)}


_FIXTURE: dict[str, Any] = {
    "contract": "core_api",
    "namespace": "taffy.self_test",
    "version": {"major": 1, "minor": 0},
    "projections": ["mojom"],
    "limits": {"MAX_ONE": 1},
    "enums": [
        {
            "name": "Kind",
            "description": "Self-test discriminator.",
            "members": [{"name": "SAVE", "wire": 0, "description": "Save."}],
        }
    ],
    "structs": [
        _record("SaveBody", _field("request_id", "string", 0), _field("endpoint", "string", 1)),
        _record("Command", _field("kind", "Kind", 0), _field("save", "optional<SaveBody>", 1)),
    ],
    "tagged_unions": [
        {
            "struct": "Command",
            "tag_field": "kind",
            "variants": [{"tag": "SAVE", "field": "save"}],
        }
    ],
    "facade_bodies": [
        {
            "interface": "Surface",
            "method": "Save",
            "body": "SaveBody",
            "browser_supplied": ["request_id"],
        }
    ],
    "interfaces": [
        {
            "name": "Surface",
            "description": "Self-test facade.",
            "methods": [
                {
                    "name": "Save",
                    "ordinal": 0,
                    "description": "Saves one endpoint.",
                    "params": [_field("endpoint", "string", 0)],
                }
            ],
        }
    ],
}


def _expect_refusal(schema: dict[str, Any], fragment: str, why: str) -> None:
    problems = findings("core-api", schema)
    if not any(fragment in problem for problem in problems):
        raise ContractError(f"facade bodies self-test: {why}; got {problems}")


def self_test() -> None:
    """The fixture passes, and each single break is refused by name."""
    validate_schema("core-api", copy.deepcopy(_FIXTURE))
    if findings("core-api", copy.deepcopy(_FIXTURE)):
        raise ContractError(
            "facade bodies self-test: the fixture that must pass did not: "
            f"{findings('core-api', copy.deepcopy(_FIXTURE))}"
        )

    # The defect this file exists for: a field appended to the body alone.
    unreachable = copy.deepcopy(_FIXTURE)
    unreachable["structs"][0]["fields"].append(_field("models", "string", 2))
    _expect_refusal(
        unreachable,
        "no argument supplies it",
        "a body field the facade cannot supply was accepted",
    )

    retyped = copy.deepcopy(_FIXTURE)
    retyped["interfaces"][0]["methods"][0]["params"][0]["type"] = "u64"
    _expect_refusal(retyped, "on the call", "a retyped argument was accepted")

    stray = copy.deepcopy(_FIXTURE)
    stray["interfaces"][0]["methods"][0]["params"].append(_field("hint", "string", 1))
    _expect_refusal(
        stray, "nowhere to put it", "an argument no body field carries was accepted"
    )

    unclaimed = copy.deepcopy(_FIXTURE)
    unclaimed["enums"][0]["members"].append(
        {"name": "FORGET", "wire": 1, "description": "Forget."}
    )
    unclaimed["structs"].insert(1, _record("ForgetBody", _field("provider_id", "string", 0)))
    unclaimed["structs"][2]["fields"].append(_field("forget", "optional<ForgetBody>", 2))
    unclaimed["tagged_unions"][0]["variants"].append({"tag": "FORGET", "field": "forget"})
    _expect_refusal(
        unclaimed,
        "no facade method does",
        "a command kind with no way to send it was accepted",
    )

    dead = copy.deepcopy(_FIXTURE)
    dead["facade_bodies"][0]["browser_supplied"] = ["never_a_field"]
    _expect_refusal(
        dead, "which the body has not", "an exemption naming no field was accepted"
    )

    supplied = copy.deepcopy(_FIXTURE)
    supplied["interfaces"][0]["methods"][0]["params"].append(_field("request_id", "string", 1))
    _expect_refusal(
        supplied,
        "browser-supplied and the call supplies it",
        "an exemption the call had outgrown was accepted",
    )

    unbound = copy.deepcopy(_FIXTURE)
    unbound["facade_bodies"] = []
    _expect_refusal(
        unbound, "binds neither to the other", "a facade bound to no body was accepted"
    )
    print("facade bodies self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description="Facade methods against their bodies.")
    parser.add_argument("--contract", choices=CONTRACTS)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    try:
        if arguments.self_test:
            self_test()
            return 0
        for contract in (arguments.contract,) if arguments.contract else CONTRACTS:
            if check(contract) != 0:
                return 1
        return 0
    except (ContractError, json.JSONDecodeError, OSError) as error:
        print(f"facade bodies: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
