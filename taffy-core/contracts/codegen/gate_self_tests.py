# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Prove the three new gates refuse the change each of them exists to catch.

A gate that has never been seen to fail is a gate nobody has tested. Each check
here breaks one thing deliberately and requires the refusal by name, using the
live schemas on disk so the tests cannot drift away from what the gates read.
Nothing here writes to the repository: every mutation is made on a copy in
memory, because a self-test that leaves the tree changed is a self-test people
learn to skip.
"""

from __future__ import annotations

import copy
from typing import Any

import frozen_payloads
import payload_codec
import wire_ledger
from contract_schema import ContractError
from contract_validation import load_schema, validate_schema


def _expect(condition: bool, message: str) -> None:
    if not condition:
        raise ContractError(f"gate self-test: {message}")


def _ledger_refuses_a_moved_ordinal() -> None:
    schema = load_schema("core-service")
    live = wire_ledger.live_declarations(schema)
    ledger = dict(wire_ledger.load("core-service"))

    moved = copy.deepcopy(live)
    record = moved["records"]["OperationEnvelope"]
    first, second = list(record)[:2]
    record[first][0], record[second][0] = record[second][0], record[first][0]
    refusals, _additions = wire_ledger.compare(ledger, moved)
    _expect(
        any("never moves" in refusal for refusal in refusals),
        "two swapped ordinals were accepted, which is the mid-record insertion "
        "the 0..n contiguity rule cannot see",
    )

    removed = copy.deepcopy(live)
    del removed["enums"]["TaskKind"]
    refusals, _additions = wire_ledger.compare(ledger, removed)
    _expect(
        any("was removed" in refusal for refusal in refusals),
        "a removed enumeration was accepted",
    )

    appended = copy.deepcopy(live)
    appended["records"]["OperationEnvelope"]["added_later"] = [5, "u64"]
    refusals, additions = wire_ledger.compare(ledger, appended)
    _expect(not refusals, "an appended field was reported as a moved wire value")
    _expect(bool(additions), "an appended field was not reported at all")


def _refreeze_refuses_an_undeclared_change() -> None:
    schema = load_schema("core-service")
    live = wire_ledger.live_declarations(schema)
    ledger = wire_ledger.load("core-service")
    models = payload_codec.models(schema)

    appended = copy.deepcopy(live)
    appended["records"]["TaskTransactionBatch"]["added_later"] = [5, "u64"]
    findings = wire_ledger.refreeze_findings(
        ledger, appended, ledger["contract_version"], models
    )
    _expect(
        any("schema_version" in finding for finding in findings),
        "a field appended to the persisted journal record was accepted without a "
        "codec schema_version bump",
    )
    _expect(
        any("contract version raised" in finding for finding in findings),
        "--refreeze was willing to record a change at an unchanged contract version",
    )

    unchanged = wire_ledger.refreeze_findings(
        ledger, live, wire_ledger.version_text(schema["version"]), models
    )
    _expect(
        any("contract version raised" in finding for finding in unchanged),
        "--refreeze was willing to run at the version the ledger already records",
    )


def _frozen_bytes_refuse_an_unbumped_layout() -> None:
    schema = load_schema("core-api")
    models = payload_codec.models(schema)
    ledger = wire_ledger.load("core-api")
    refusals = frozen_payloads.codec_bump_refusals(
        ledger, models, ["core_status_payload"]
    )
    _expect(
        bool(refusals),
        "--refreeze would replace the frozen payload at an unchanged schema_version, "
        "which is exactly the re-blessing the frozen bytes exist to prevent",
    )

    raised = copy.deepcopy(ledger)
    raised["codec_schema_versions"]["core_status_payload"] -= 1
    _expect(
        not frozen_payloads.codec_bump_refusals(raised, models, ["core_status_payload"]),
        "--refreeze refused a payload whose schema_version really was raised",
    )


def _frozen_bytes_catch_a_changed_wire_layout() -> None:
    """One byte of the encoder's output, changed, must not still be accepted."""
    schema = load_schema("core-api")
    model = payload_codec.models(schema)["core_status_payload"]
    entry = wire_ledger.load("core-api")["frozen_payloads"]["core_status_payload"]
    frozen = frozen_payloads.committed_bytes("core-api", entry)
    _expect(payload_codec.verdict(model, frozen)[0] == "ACCEPT", "the frozen bytes no longer decode")
    version_at = len(model["magic"])
    moved = bytearray(frozen)
    moved[version_at] = (moved[version_at] + 1) % 256
    _expect(
        bytes(moved) != frozen and payload_codec.verdict(model, bytes(moved))[0] != "ACCEPT",
        "a payload whose schema version byte was changed still decoded",
    )


def _corpus_refuses_a_mismatched_verdict() -> None:
    """The corpus reads bytes; a fixture cannot simply declare its verdict."""
    schema = load_schema("core-api")
    model = payload_codec.models(schema)["core_status_payload"]
    entry = wire_ledger.load("core-api")["frozen_payloads"]["core_status_payload"]
    frozen = frozen_payloads.committed_bytes("core-api", entry)
    verdict, error = payload_codec.verdict(model, frozen + b"\x00")
    _expect(
        (verdict, error) == ("REJECT_MALFORMED", "TrailingBytes"),
        f"a payload with one trailing byte decoded as {verdict}/{error}",
    )
    verdict, error = payload_codec.verdict(model, frozen[:-1])
    _expect(
        (verdict, error) == ("REJECT_MALFORMED", "Truncated"),
        f"a truncated payload decoded as {verdict}/{error}",
    )


def _binary_codec_refuses_an_old_version_before_its_body() -> None:
    """An incompatible body is never interpreted under today's layout."""
    schema = load_schema("core-service")
    model = payload_codec.models(schema)["transaction_batch"]
    old_prefix = model["magic"] + (model["schema_version"] - 1).to_bytes(4, "little")
    verdict, error = payload_codec.verdict(model, old_prefix)
    _expect(
        (verdict, error) == ("REJECT_VERSION", "UnsupportedVersion"),
        f"an old transaction prefix was parsed as {verdict}/{error} before its version refusal",
    )


#: One interface that is valid on its own terms, used as the base every case
#: below breaks in exactly one way. It is written here rather than read from a
#: contract so that a contract which happens to declare no interface today
#: still exercises the rules that govern one.
_INTERFACE_FIXTURE: dict[str, Any] = {
    "contract": "core_api",
    "namespace": "taffy.core_api",
    "version": {"major": 1, "minor": 0},
    "projections": ["mojom"],
    "limits": {"MAX_ONE": 1},
    "enums": [],
    "structs": [],
    "interfaces": [
        {
            "name": "Surface",
            "description": "A fixture interface.",
            "methods": [
                {
                    "name": "Observe",
                    "ordinal": 0,
                    "description": "Hands over an observer.",
                    "params": [
                        {
                            "name": "observer",
                            "type": "pending_remote<Watcher>",
                            "ordinal": 0,
                            "description": "The observer.",
                        }
                    ],
                    "response": None,
                },
                {
                    "name": "Submit",
                    "ordinal": 1,
                    "description": "Submits one intent.",
                    "params": [
                        {
                            "name": "goal",
                            "type": "string",
                            "ordinal": 0,
                            "description": "The goal.",
                        }
                    ],
                    "response": [
                        {
                            "name": "accepted",
                            "type": "bool",
                            "ordinal": 0,
                            "description": "Whether it was admitted.",
                        }
                    ],
                },
            ],
        },
        {
            "name": "Watcher",
            "description": "A fixture observer.",
            "methods": [
                {
                    "name": "OnChange",
                    "ordinal": 0,
                    "description": "One change.",
                    "params": [],
                    "response": None,
                }
            ],
        },
    ],
}


def _interface_schema(**overrides: Any) -> dict[str, Any]:
    schema = copy.deepcopy(_INTERFACE_FIXTURE)
    schema.update(copy.deepcopy(overrides))
    return schema


def _refuses(schema: dict[str, Any], expected: str, why: str) -> None:
    try:
        validate_schema("core-api", schema)
    except ContractError as error:
        _expect(expected in str(error), f"{why}: refused with {error!r}")
        return
    _expect(False, why)


def _interface_rules_refuse_what_they_exist_for() -> None:
    """The five ways an interface declaration can be wrong on the wire."""
    _expect(
        validate_schema("core-api", _interface_schema()) is None,
        "the valid interface fixture was refused",
    )

    # A method ordinal that is not its position. Mojo assigns one by
    # declaration order when none is written, so a list that reorders without
    # the ordinal moving would silently reinterpret every peer's messages.
    broken = _interface_schema()
    methods = broken["interfaces"][0]["methods"]
    methods[0]["ordinal"], methods[1]["ordinal"] = 1, 0
    _refuses(broken, "non-append-only ordinal", "a swapped method ordinal was accepted")

    # An argument ordinal that is not its position, for the same reason: a
    # method's parameters are a struct, and a struct's ordinals are frozen.
    broken = _interface_schema()
    broken["interfaces"][0]["methods"][1]["params"][0]["ordinal"] = 3
    _refuses(broken, "non-append-only ordinal", "a moved argument ordinal was accepted")

    # A handle in a reply. An interface is handed over, not returned: a reply
    # carrying one would make the receiver the owner of a connection it never
    # asked for, and no lifetime in this tree is written that way.
    broken = _interface_schema()
    broken["interfaces"][0]["methods"][1]["response"][0]["type"] = "pending_remote<Watcher>"
    _refuses(broken, "a reply cannot carry", "a handle in a reply was accepted")

    # A handle naming something that is not an interface in this contract.
    broken = _interface_schema()
    broken["interfaces"][0]["methods"][0]["params"][0]["type"] = "pending_remote<Absent>"
    _refuses(broken, "is not an interface", "a handle to nothing was accepted")

    # A record field carrying a live connection. A record is a value: it can be
    # stored, copied and replayed, and a connection cannot.
    broken = _interface_schema(
        structs=[
            {
                "name": "Holder",
                "description": "A fixture record.",
                "fields": [
                    {
                        "name": "watcher",
                        "type": "pending_remote<Watcher>",
                        "ordinal": 0,
                        "description": "Not a value.",
                    }
                ],
            }
        ]
    )
    _refuses(broken, "a record field cannot carry", "a connection in a record was accepted")


def _the_ledger_freezes_an_interface_method() -> None:
    """A moved method and a retyped argument are frozen-value changes.

    An *appended* argument is not, and the last case here is the one this
    file existed without: Mojo builds a parameter list into a versioned
    struct, so giving a call a new trailing argument is the same append-only
    change as adding a record field. While the ledger recorded a method as one
    signature string it could not tell the two apart, refused the append
    outright, and left declaring a second call beside the first as the only
    way through.
    """
    schema = _interface_schema()
    live = wire_ledger.live_declarations(schema)
    _expect(
        live["interfaces"]["Surface"]["Submit"]["ordinal"] == 1,
        "the ledger did not record the method ordinal",
    )
    _expect(
        live["interfaces"]["Surface"]["Submit"]["params"]["goal"] == [0, "string"],
        "the ledger did not record the argument ordinal and type",
    )
    ledger = {
        "contract": "core_api",
        "description": "fixture",
        "contract_version": "1.0",
        "codec_schema_versions": {},
        "frozen_payloads": {},
        **live,
    }

    moved = copy.deepcopy(live)
    surface = moved["interfaces"]["Surface"]
    surface["Observe"]["ordinal"], surface["Submit"]["ordinal"] = 1, 0
    refusals, _additions = wire_ledger.compare(ledger, moved)
    _expect(
        bool(refusals),
        "two swapped method ordinals were accepted by the ledger",
    )

    retyped = copy.deepcopy(live)
    retyped["interfaces"]["Surface"]["Submit"]["params"]["goal"] = [0, "u64"]
    refusals, _additions = wire_ledger.compare(ledger, retyped)
    _expect(
        bool(refusals),
        "a retyped method argument was accepted by the ledger, so the "
        "argument's type is recorded and never read",
    )

    reordered = copy.deepcopy(live)
    reordered["interfaces"]["Surface"]["Submit"]["params"]["goal"] = [1, "string"]
    refusals, _additions = wire_ledger.compare(ledger, reordered)
    _expect(
        bool(refusals),
        "a renumbered method argument was accepted by the ledger",
    )

    replied = copy.deepcopy(live)
    replied["interfaces"]["Surface"]["Observe"]["reply"] = "(accepted: bool)"
    refusals, _additions = wire_ledger.compare(ledger, replied)
    _expect(
        bool(refusals),
        "a method that grew a reply was accepted by the ledger",
    )

    appended = copy.deepcopy(live)
    appended["interfaces"]["Surface"]["Submit"]["params"]["hint"] = [1, "string"]
    refusals, additions = wire_ledger.compare(ledger, appended)
    _expect(
        not refusals,
        "an appended method argument was refused as a moved wire value, so no "
        "existing call can ever be given one",
    )
    _expect(
        any("hint" in addition for addition in additions),
        "an appended method argument was not recorded as an addition, so "
        "--refreeze would not ask for the version bump that declares it",
    )


def run_gate_self_tests() -> None:
    _interface_rules_refuse_what_they_exist_for()
    _the_ledger_freezes_an_interface_method()
    _ledger_refuses_a_moved_ordinal()
    _refreeze_refuses_an_undeclared_change()
    _frozen_bytes_refuse_an_unbumped_layout()
    _frozen_bytes_catch_a_changed_wire_layout()
    _corpus_refuses_a_mismatched_verdict()
    _binary_codec_refuses_an_old_version_before_its_body()
