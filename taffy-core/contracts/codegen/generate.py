#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate all Taffy host/service contract projections."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Iterable

import frozen_payloads
import payload_codec
import wire_ledger
from contract_renderers import (
    pascal,
    render_kotlin,
    render_rust,
    render_typescript,
    rust_type,
    tagged_union_for,
)
from contract_schema import CONTRACT_ROOT, ContractError, TypeRef, parse_type
from contract_validation import load_schema, validate_schema
from mojom_renderer import mojo_type, render_mojom
from cxx_renderer import render_cxx

CONTRACTS = ("browsing", "core-api", "core-service", "tool-runtime")


def outputs(contract: str, schema: dict[str, Any]) -> dict[Path, str]:
    """Every language projection this contract declares, and no others.

    A contract emits what it has a consumer for. That is not thrift: a
    projection nothing compiles is a file that has never been through a
    compiler, cannot fail, and reads in a review as though it had been
    checked — the shape of defect this repository has already paid for once,
    in three thousand lines of generated Kotlin that no target named. So the
    schema says which projections exist, and adding one is the same edit as
    wiring up the target that consumes it.
    """
    root = CONTRACT_ROOT / contract / "generated"
    stem = contract.replace("-", "_")
    available = {
        "rust": (root / "rust" / f"{stem}.rs", render_rust),
        "kotlin": (root / "kotlin" / f"{pascal(stem)}.kt", render_kotlin),
        "typescript": (root / "typescript" / f"{stem}.ts", render_typescript),
        "mojom": (root / "mojom" / f"{stem}.mojom", render_mojom),
        "cpp": (root / "cpp" / f"{stem}_enums.h", lambda value: render_cxx(contract, value)),
    }
    return {
        available[name][0]: available[name][1](schema)
        for name in schema["projections"]
    }


def selected_contracts(value: str | None) -> Iterable[str]:
    if value is None:
        return CONTRACTS
    if value not in CONTRACTS:
        raise ContractError(f"unknown contract {value}")
    return (value,)


def self_test() -> None:
    """Exercise the deliberately small type grammar and a portability refusal."""
    assert parse_type("optional<string>").inner == TypeRef("scalar", name="string")
    assert rust_type(parse_type("list<u64>")) == "Vec<u64>"
    assert rust_type(parse_type("bytes32")) == "[u8; 32]"
    assert mojo_type(parse_type("bytes32")) == "array<uint8, 32>"
    for invalid in ("", "map<string>", "list<list<u32>>", "optional<>"):
        try:
            parse_type(invalid)
        except ContractError:
            continue
        raise ContractError(f"self-test accepted invalid type {invalid}")

    optional_enum_schema = {
        "contract": "core_api",
        "namespace": "taffy.self_test",
        "version": {"major": 1, "minor": 0},
        "projections": ["mojom"],
        "limits": {"MAX_VALUE": 1},
        "enums": [
            {
                "name": "ClosedValue",
                "description": "Self-test enum.",
                "members": [{"name": "ONE", "wire": 0, "description": "One."}],
            }
        ],
        "structs": [
            {
                "name": "BadOptional",
                "description": "Self-test record.",
                "fields": [
                    {
                        "name": "value",
                        "type": "optional<ClosedValue>",
                        "ordinal": 0,
                        "description": "Invalid optional enum.",
                    }
                ],
            }
        ],
    }
    try:
        validate_schema("core-api", optional_enum_schema)
    except ContractError:
        pass
    else:
        raise ContractError("self-test accepted an optional enum")

    # The C++ decoder is the one projection whose absence is silent: a
    # `static_cast<Enum>(integer)` compiles, and the value it produces for a
    # non-member is undefined rather than refused. So the self-test asserts the
    # two properties that make the emitted function a decoder rather than a
    # cast with extra steps: one case per declared member, and a default that
    # returns nothing.
    decoder = render_cxx(
        "core-api",
        {
            "contract": "core_api",
            "namespace": "taffy.self_test",
            "version": {"major": 1, "minor": 0},
            "limits": {},
            "enums": [
                {
                    "name": "ClosedValue",
                    "description": "Self-test enum.",
                    "members": [
                        {"name": "ONE", "wire": 0, "description": "One."},
                        {"name": "TWO_PART", "wire": 7, "description": "Two."},
                    ],
                }
            ],
            "structs": [],
        },
    )
    for fragment in (
        "constexpr std::optional<mojom::ClosedValue>\nClosedValueFromWire(uint32_t value) {",
        "    case 0:\n      return mojom::ClosedValue::kOne;",
        "    case 7:\n      return mojom::ClosedValue::kTwoPart;",
        "    default:\n      return std::nullopt;",
        "namespace taffy::self_test::wire {",
        "#ifndef TAFFY_CONTRACTS_CORE_API_GENERATED_CPP_CORE_API_ENUMS_H_",
    ):
        if fragment not in decoder:
            raise ContractError(f"self-test: the C++ decoder omitted {fragment!r}")
    if "static_cast" in decoder:
        raise ContractError(
            "self-test: the C++ decoder reached for a cast, which is the "
            "conversion it exists to replace"
        )
    for line in decoder.splitlines():
        if len(line) > 80:
            raise ContractError(f"self-test: generated C++ line exceeds 80 columns: {line}")


def refreeze(contract: str, schema: dict[str, Any], models: dict[str, Any]) -> None:
    """Replace the frozen wire record, under the two refusals that guard it."""
    wire_ledger.assert_nothing_moved(contract, schema)
    wire_ledger.assert_codec_bumps(contract, schema, models)
    ledger = wire_ledger.load(contract)
    stale = frozen_payloads.stale_payloads(contract, ledger, schema, models)
    if stale:
        frozen_payloads.require_codec_bump(contract, ledger, models, stale)
    recorded = wire_ledger.refreeze_ledger(contract, schema, models, bool(stale))
    written = frozen_payloads.write(contract, ledger, stale) if stale else []
    if not recorded and not written:
        print(f"{contract}: nothing to refreeze")
        return
    for path in written:
        print(f"{contract}: froze {path.relative_to(CONTRACT_ROOT)}")
    if recorded:
        version = wire_ledger.version_text(schema["version"])
        print(f"{contract}: recorded the wire ledger at contract version {version}")


def main(default_contract: str | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", choices=CONTRACTS, default=default_contract)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true")
    action.add_argument("--check", action="store_true")
    action.add_argument("--verify", action="store_true")
    action.add_argument("--self-test", action="store_true")
    action.add_argument(
        "--refreeze",
        action="store_true",
        help=(
            "Replace the frozen wire ledger and payload bytes. Refused unless the "
            "contract version was raised in the same change, and refused for the "
            "payload bytes unless the codec's schema_version was raised too."
        ),
    )
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            from gate_self_tests import run_gate_self_tests

            run_gate_self_tests()
        for contract in selected_contracts(args.contract):
            schema = load_schema(contract)
            models = payload_codec.models(schema)
            if args.self_test:
                continue
            if args.refreeze:
                refreeze(contract, schema, models)
                continue
            wire_ledger.audit(contract, schema, models)
            frozen_payloads.audit(contract, wire_ledger.load(contract), schema, models)
            if args.verify:
                from compat_corpus import verify
                from fixtures import verify_fixtures

                verify_fixtures(contract, schema)
                print(verify(contract, schema, models))
                continue
            for path, rendered in outputs(contract, schema).items():
                if args.write:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(rendered, encoding="utf-8")
                    continue
                try:
                    current = path.read_text(encoding="utf-8")
                except OSError as error:
                    raise ContractError(f"missing generated output {path}: {error}") from error
                if current != rendered:
                    raise ContractError(f"stale generated output: {path}")
        return 0
    except (AssertionError, ContractError, json.JSONDecodeError, OSError) as error:
        print(f"contract generator: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
