#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Reject semantic drift between contracts that describe the same thing twice.

Core Service and Tool Runtime share the tool protocol outright: the same
types, the same limits, the same pairing rules. Core API does not share the
delivery types — it projects them — so what is held here is that the
projection is a rename of the contract and nothing more.
"""

from __future__ import annotations

import argparse
import copy
import json
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CORE_SERVICE_SCHEMA = ROOT / "core-service" / "schema" / "contract.json"
TOOL_RUNTIME_SCHEMA = ROOT / "tool-runtime" / "schema" / "contract.json"
CORE_API_SCHEMA = ROOT / "core-api" / "schema" / "contract.json"

SHARED_ENUMS = (
    "ToolRuntimeKind",
    "ToolOperation",
    "ToolChunkKind",
    "LocalModelFinishReason",
    "ToolTerminalStatus",
    "ToolModelArtifactKind",
    "EmbeddingElementKind",
)

SHARED_STRUCTS = (
    "OperationEnvelope",
    "AudioExtractArguments",
    "AudioExtractResult",
    "BinaryOutputChunk",
    "BundledPythonArguments",
    "BundledPythonResult",
    "FrameSampleArguments",
    "FrameSampleResult",
    "LocalEmbeddingArguments",
    "LocalEmbeddingResult",
    "LocalModelArguments",
    "LocalModelResult",
    "MediaProbeArguments",
    "MediaProbeResult",
    "SignedWasmArguments",
    "SignedWasmResult",
    "TextOutputChunk",
    "ToolModelArtifact",
    "ToolProgress",
    "ToolSuccess",
    "TranscodeArguments",
    "TranscodeResult",
)

# Delivery vocabulary the Core API only projects.
#
# The browser reports a transfer over Core Service and the isolated core turns
# what it reports into the outward view; the two sets must therefore be the
# same set, member for member and wire for wire. Held here rather than trusted
# to review, because a projection that may rename a verdict on the way out is a
# projection that may invent one — and the reason a person is shown for an
# artifact that did not arrive has to be the reason it did not arrive.
#
# Descriptions deliberately differ: one side names a condition inside the
# product and the other names what a person is told.
PROJECTED_ENUMS = {
    "AssetKind": "AssetKindView",
    "AssetPresence": "AssetPresenceView",
    "AssetRefusalReason": "AssetRefusalView",
    "AssetNetworkCost": "AssetNetworkCostView",
    "ProviderAuthMethod": "ProviderAuthMethodView",
    "ProviderWireApi": "ProviderWireApiView",
    "ProviderCredentialState": "ProviderCredentialStateView",
    "ThinkingLevel": "ThinkingLevelView",
    "ServerKind": "ServerKindView",
}

LIMIT_MAPPINGS = {
    "MAX_TOOL_JOB_INPUT_BYTES": "MAX_JOB_INPUT_BYTES",
    "MAX_TOOL_JOB_OUTPUT_BYTES": "MAX_JOB_OUTPUT_BYTES",
    "MAX_TOOL_OUTPUT_CHUNK_BYTES": "MAX_OUTPUT_CHUNK_BYTES",
    "MAX_TOOL_OUTPUT_CHUNKS": "MAX_OUTPUT_CHUNKS",
    "MAX_TOOL_PROGRESS_EVENTS": "MAX_PROGRESS_EVENTS",
    "MAX_TOOL_JOB_ID_BYTES": "MAX_JOB_ID_BYTES",
    "MAX_TOOL_OPERATION_ID_BYTES": "MAX_OPERATION_ID_BYTES",
    "MAX_TOOL_IDEMPOTENCY_KEY_BYTES": "MAX_IDEMPOTENCY_KEY_BYTES",
    "MAX_TOOL_ID_BYTES": "MAX_TOOL_ID_BYTES",
    "MAX_TOOL_VERSION_BYTES": "MAX_TOOL_VERSION_BYTES",
    "MAX_TOOL_CONVERSATION_ID_BYTES": "MAX_CONVERSATION_ID_BYTES",
    "MAX_TOOL_ENTRYPOINT_ID_BYTES": "MAX_ENTRYPOINT_ID_BYTES",
    "MAX_TOOL_HANDLE_ID_BYTES": "MAX_HANDLE_ID_BYTES",
    "MAX_TOOL_PRESET_ID_BYTES": "MAX_PRESET_ID_BYTES",
    "MAX_TOOL_JOB_MEMORY_BYTES": "MAX_JOB_MEMORY_BYTES",
    "MAX_TOOL_JOB_CPU_MS": "MAX_JOB_CPU_MS",
    "MAX_TOOL_JOB_TEMPORARY_BYTES": "MAX_JOB_TEMPORARY_BYTES",
    "MAX_TOOL_JOB_WALL_TIME_MS": "MAX_JOB_WALL_TIME_MS",
    "MAX_TOOL_INLINE_PAYLOAD_BYTES": "MAX_INLINE_PAYLOAD_BYTES",
    "MAX_TOOL_STREAM_OUTPUT_CHUNKS": "MAX_STREAM_OUTPUT_CHUNKS",
    "MAX_TOOL_STREAM_CHUNK_BYTES": "MAX_STREAM_CHUNK_BYTES",
    "MAX_TOOL_MODEL_ID_BYTES": "MAX_MODEL_ID_BYTES",
    "MAX_TOOL_MODEL_REVISION_BYTES": "MAX_MODEL_REVISION_BYTES",
    "MAX_TOOL_MODEL_ARTIFACT_BYTES": "MAX_MODEL_ARTIFACT_BYTES",
    "MAX_TOOL_EMBEDDING_DIMENSIONS": "MAX_EMBEDDING_DIMENSIONS",
    "MAX_TOOL_EMBEDDING_VALUE_BYTES": "MAX_EMBEDDING_VALUE_BYTES",
}


def _load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot load {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain one JSON object")
    return value


def _named(document: dict[str, Any], collection: str, name: str) -> dict[str, Any]:
    values = document.get(collection)
    if not isinstance(values, list):
        raise ValueError(f"{collection} must be an array")
    matches = [value for value in values if isinstance(value, dict) and value.get("name") == name]
    if len(matches) != 1:
        raise ValueError(f"{collection} must contain exactly one {name}")
    return matches[0]


def _without_descriptions(value: dict[str, Any]) -> dict[str, Any]:
    normalized = copy.deepcopy(value)
    normalized.pop("description", None)
    for collection in ("members", "fields"):
        for member in normalized.get(collection, []):
            if isinstance(member, dict):
                member.pop("description", None)
    return normalized


def _pairing(document: dict[str, Any]) -> list[dict[str, Any]]:
    pairings = document.get("enum_pairings")
    if not isinstance(pairings, list):
        raise ValueError("enum_pairings must be an array")
    matches = [
        pairing
        for pairing in pairings
        if isinstance(pairing, dict)
        and pairing.get("left_field") == "runtime"
        and pairing.get("right_field") == "operation_kind"
    ]
    if len(matches) != 1:
        raise ValueError("each schema must define one runtime/operation pairing")
    pairs = matches[0].get("pairs")
    if not isinstance(pairs, list):
        raise ValueError("runtime/operation pairing must contain pairs")
    return pairs


def _success_statuses(document: dict[str, Any]) -> list[str]:
    rules = document.get("conditional_presence")
    if not isinstance(rules, list):
        raise ValueError("conditional_presence must be an array")
    matches = [
        rule
        for rule in rules
        if isinstance(rule, dict)
        and rule.get("tag_field") == "status"
        and rule.get("field") == "success"
    ]
    if len(matches) != 1 or not isinstance(matches[0].get("present_for"), list):
        raise ValueError("each schema must define one terminal success-presence rule")
    return matches[0]["present_for"]


def findings(core: dict[str, Any], tool: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    for name in SHARED_ENUMS:
        left = _without_descriptions(_named(core, "enums", name))
        right = _without_descriptions(_named(tool, "enums", name))
        if left != right:
            errors.append(f"shared enum {name} differs")

    for name in SHARED_STRUCTS:
        left = _without_descriptions(_named(core, "structs", name))
        right = _without_descriptions(_named(tool, "structs", name))
        if left != right:
            errors.append(f"shared struct {name} differs")

    core_limits = core.get("limits")
    tool_limits = tool.get("limits")
    if not isinstance(core_limits, dict) or not isinstance(tool_limits, dict):
        raise ValueError("limits must be objects")
    for core_name, tool_name in LIMIT_MAPPINGS.items():
        if core_limits.get(core_name) != tool_limits.get(tool_name):
            errors.append(
                f"limit {core_name}={core_limits.get(core_name)!r} differs from "
                f"{tool_name}={tool_limits.get(tool_name)!r}"
            )

    if _pairing(core) != _pairing(tool):
        errors.append("runtime/operation pairing differs")
    if _success_statuses(core) != _success_statuses(tool):
        errors.append("terminal success-presence rule differs")
    return errors


def _members(enum: dict[str, Any]) -> list[tuple[str, Any]]:
    return [(member["name"], member["wire"]) for member in enum["members"]]


def projection_findings(core: dict[str, Any], api: dict[str, Any]) -> list[str]:
    """Whether the outward view carries exactly the vocabulary it projects."""
    errors: list[str] = []
    for service_name, view_name in PROJECTED_ENUMS.items():
        left = _members(_named(core, "enums", service_name))
        right = _members(_named(api, "enums", view_name))
        if left != right:
            errors.append(f"projected enum {service_name} differs from {view_name}")
    return errors


def _self_test() -> None:
    core = _load(CORE_SERVICE_SCHEMA)
    tool = _load(TOOL_RUNTIME_SCHEMA)
    api = _load(CORE_API_SCHEMA)
    if findings(core, tool) or projection_findings(core, api):
        raise ValueError("checked-in schemas must agree before the self-test runs")

    broken = copy.deepcopy(api)
    _named(broken, "enums", "AssetRefusalView")["members"][0]["name"] = "SOMETHING_ELSE"
    if "projected enum AssetRefusalReason differs from AssetRefusalView" not in (
        projection_findings(core, broken)
    ):
        raise ValueError("self-test failed to detect projected enum drift")

    broken = copy.deepcopy(tool)
    terminal = _named(broken, "enums", "ToolTerminalStatus")
    terminal["members"][0]["wire"] = 99
    if "shared enum ToolTerminalStatus differs" not in findings(core, broken):
        raise ValueError("self-test failed to detect enum drift")

    broken = copy.deepcopy(core)
    broken["limits"]["MAX_TOOL_JOB_INPUT_BYTES"] += 1
    if not any(item.startswith("limit MAX_TOOL_JOB_INPUT_BYTES") for item in findings(broken, tool)):
        raise ValueError("self-test failed to detect limit drift")

    with tempfile.TemporaryDirectory() as directory:
        invalid = Path(directory) / "invalid.json"
        invalid.write_text("[]", encoding="utf-8")
        try:
            _load(invalid)
        except ValueError:
            pass
        else:
            raise ValueError("self-test accepted a non-object schema")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            _self_test()
            print("cross-contract self-test passed")
            return 0
        core_service = _load(CORE_SERVICE_SCHEMA)
        errors = findings(core_service, _load(TOOL_RUNTIME_SCHEMA))
        errors += projection_findings(core_service, _load(CORE_API_SCHEMA))
    except ValueError as error:
        print(f"cross-contract error: {error}")
        return 1
    if errors:
        for error in errors:
            print(f"cross-contract drift: {error}")
        return 1
    print(
        "cross-contract: shared tool types and limits agree, and the Core API "
        "projects the delivery vocabulary unchanged"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
