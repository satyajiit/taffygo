# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Derive the CoreStatus payload compat fixtures from one base payload.

Every `core-status-*.hex` fixture in `core-api/compat/` is the same accepted
payload plus exactly the one deviation its manifest row describes. Writing them
by hand is how they went stale once already: the corpus sat at codec version 6
while the codec wrote 7, so the fixture whose whole job was to prove a current
payload decodes was proving the opposite, and the contract's own `--verify` was
failing for a reason nobody had read in weeks.

So the corpus is derived rather than typed. Raising the codec's schema version
means running this with `--write`; `--check` proves the committed bytes are
exactly what this file would produce, which is what keeps a hand-edited fixture
from surviving review.

    python3 taffy-core/contracts/core-api/codegen/status_payload_corpus.py --check
    python3 taffy-core/contracts/core-api/codegen/status_payload_corpus.py --write

Host Python 3 only, no installs. It reads the schema for every layout it needs,
so a field added to `CoreStatus` reaches the fixtures without being named here.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path
from typing import Any

CODEGEN = Path(__file__).resolve().parent
CONTRACT = CODEGEN.parent
COMPAT = CONTRACT / "compat"
sys.path.insert(0, str(CONTRACT.parent / "codegen"))

from contract_schema import parse_type  # noqa: E402


def _member(enums: dict[str, Any], enum: str, name: str) -> int:
    for candidate in enums[enum]["members"]:
        if candidate["name"] == name:
            return int(candidate["wire"])
    raise SystemExit(f"{enum} has no member {name}")


def _highest_member(enums: dict[str, Any], enum: str) -> int:
    """The largest wire value the enumeration defines, so a fixture that
    steps one past it stays one past it when a member is added."""
    return max(int(member["wire"]) for member in enums[enum]["members"])


def _builtin_skills(enums: dict[str, Any]) -> list[dict[str, Any]]:
    """The fixed catalogue rows retained by every publishable projection."""
    abilities = (
        "PAGES_LOOKUP",
        "DEPTH",
        "PRODUCTS",
        "PAGES_COMPARE",
        "PAGES_SUMMARIZE",
        "PDF",
        "PAGES_TABLE",
        "FORM",
        "OFFERS",
        "DOWNLOADS",
        "TRIP",
        "VIDEO",
        "PICTURES",
        "KEEP",
        "SHEET",
        "DOCUMENT",
    )
    tool_counts = (12, 14, 13, 8, 7, 7, 7, 12, 12, 9, 14, 9, 6, 6, 6, 8)
    available_tool_counts = (*tool_counts[:9], 5, *tool_counts[10:])
    available = _member(enums, "BuiltinSkillAvailabilityView", "AVAILABLE")
    tool_unavailable = _member(
        enums,
        "BuiltinSkillAvailabilityView",
        "REQUIRED_TOOL_UNAVAILABLE",
    )
    return [
        {
            "reference": {"skill_id": skill_id, "version": 1},
            "required_ability": _member(enums, "AssistantAbilityView", ability),
            "enabled": True,
            "availability": tool_unavailable if skill_id == 9 else available,
            "required_tool_count": required,
            "available_tool_count": available_count,
            "required_part_count": 0,
            "installed_part_count": 0,
        }
        for skill_id, (ability, required, available_count) in enumerate(
            zip(abilities, tool_counts, available_tool_counts, strict=True)
        )
    ]


class Encoder:
    """Encodes one CoreStatus and records where every field landed.

    The span of an optional covers its presence byte as well as the value
    behind it. Recording only the value is the mistake that produces a payload
    claiming a record is present and then ending where the record should have
    started.
    """

    def __init__(self, schema: dict[str, Any]) -> None:
        self.schema = schema
        self.codec = schema["state_payload_codec"]
        self.enums = {enum["name"]: enum for enum in schema["enums"]}
        self.structs = {struct_["name"]: struct_ for struct_ in schema["structs"]}

    def encode(self, value: dict[str, Any]) -> tuple[bytes, dict[str, tuple[int, int]]]:
        out = bytearray(self.codec["magic"].encode("ascii"))
        out += struct.pack("<I", self.codec["schema_version"])
        spans: dict[str, tuple[int, int]] = {}

        def put(reference, item: Any, path: str) -> None:
            start = len(out)
            if reference.kind == "optional":
                out.append(0 if item is None else 1)
                if item is not None:
                    put(reference.inner, item, path)
                spans[path] = (start, len(out))
                return
            if reference.kind == "list":
                out.extend(struct.pack("<I", len(item)))
                for element in item:
                    put(reference.inner, element, path)
            elif reference.kind == "named" and reference.name in self.enums:
                out.extend(struct.pack("<I", item))
            elif reference.kind == "named":
                owner = self.structs[reference.name]
                for field in owner["fields"]:
                    put(
                        parse_type(field["type"]),
                        item[field["name"]],
                        f"{owner['name']}.{field['name']}",
                    )
            elif reference.name == "string":
                raw = item.encode("utf-8")
                out.extend(struct.pack("<I", len(raw)))
                out.extend(raw)
            elif reference.name == "bool":
                out.append(1 if item else 0)
            elif reference.name == "u32":
                out.extend(struct.pack("<I", item))
            elif reference.name == "u64":
                out.extend(struct.pack("<Q", item))
            else:
                raise SystemExit(f"{path}: unsupported scalar {reference.name}")
            spans.setdefault(path, (start, len(out)))

        root = self.structs[self.codec["root"]]
        for field in root["fields"]:
            put(
                parse_type(field["type"]),
                value[field["name"]],
                f"{root['name']}.{field['name']}",
            )
        return bytes(out), spans


def _values(schema: dict[str, Any], enums: dict[str, Any]) -> dict[str, Any]:
    """The five payloads every fixture is cut from.

    A field added to a record has to be given a value here, and the encoder
    fails loudly on a missing one rather than writing a short payload.
    """
    base = {
        "availability": _member(enums, "CoreAvailability", "READY"),
        "generation": 7,
        "active_tasks": [],
        "auth_state": None,
        "workspaces": [],
        "workspace_export": None,
        "asset_delivery": None,
        "provider_roster": [],
        "provider_probes": [],
        "provider_models": [],
        "assistant_configuration": {
            "revision": 0,
            "disabled_abilities": [],
            "preset": _member(enums, "PersonalityPresetView", "CAREFUL_RESEARCHER"),
            "pace": 0,
            "length": 1,
            "check_in": 0,
        },
        "library": {
            "availability": _member(enums, "LibraryAvailability", "AVAILABLE"),
            "revision": 0,
            "entries": [],
            "search": None,
            "refresh_previews": [],
            "refresh_results": [],
        },
        "library_export": None,
        "memory": {
            "availability": _member(enums, "MemoryAvailability", "AVAILABLE"),
            "revision": 0,
            "records": [],
            "search": None,
        },
        "saved_sign_ins": {
            "availability": _member(enums, "SavedDataAvailability", "LOADING"),
            "revision": 0,
            "records": [],
        },
        "saved_details": {
            "availability": _member(enums, "SavedDataAvailability", "LOADING"),
            "revision": 0,
            "people": [],
        },
        "site_skills": [],
        "builtin_skills": _builtin_skills(enums),
        "projection_mode": _member(enums, "CoreStatusProjectionMode", "COMPLETE"),
        "projection_omissions": [],
    }
    task = {
        "task_id": "task-fixture-17",
        "revision": 4,
        "phase": _member(enums, "TaskPhase", "WAITING_FOR_USER"),
        "progress_basis_points": 3_750,
        "status_message_key": "task.waiting_for_approval",
        "failure": None,
        "goal": "Compare the verified evidence",
        "template_id": 0,
        "pending_action": None,
        "workspace_id": "workspace-fixture-2",
        "pending_ask_prompt": None,
        "pending_field_value_request": None,
        "allowed_controls": [
            _member(enums, "TaskControlKind", "PAUSE"),
            _member(enums, "TaskControlKind", "TAKE_OVER"),
            _member(enums, "TaskControlKind", "STOP"),
        ],
        "artifacts": [],
        # Two steps, and the pair is the point: one that names a host and one
        # that cannot, so the optional string is exercised present and absent
        # in the same list (decision 0148).
        "activity": [
            {
                "sequence": 1,
                "kind": _member(enums, "TaskActivityKind", "OPENED_PAGE"),
                "host": "shop.example.invalid",
                "count": 0,
                "at_epoch_ms": 1_788_911_100_000,
            },
            {
                "sequence": 2,
                "kind": _member(enums, "TaskActivityKind", "ASKED_YOU"),
                "host": None,
                "count": 0,
                "at_epoch_ms": 1_788_911_160_000,
            },
        ],
    }
    signed_in = {
        **base,
        "auth_state": {
            "phase": _member(enums, "AuthPhase", "SIGNED_IN"),
            "account": {
                "account_id": "account-fixture-1",
                "display_name": None,
                "email": None,
                "method": enums["AuthProvider"]["members"][0]["wire"],
            },
            "pending_email": None,
            "failure": None,
            "methods": [],
            "entitlement": None,
        },
    }
    # One probe row, so a fixture can deviate inside a list element. The
    # provider is one that serves its own model list and so is filed without a
    # flight (Core API 3.40): the row exists to be patched, and this member is
    # the newest one, which is where an unknown-value fixture is most worth
    # having.
    probe = {
        "provider_id": "openrouter",
        "verdict": _member(enums, "ProviderProbeVerdictView", "NO_MODEL_LISTED"),
        "at_monotonic_ms": 484_000,
        "endpoint": None,
    }
    # One roster row whose endpoint move the guard refused, carrying the host
    # it refused (Core API 3.40). Every field is named because the encoder
    # refuses a missing one rather than writing a short record.
    roster_row = {
        "provider_id": "xai",
        "display_name": "xAI",
        "origin": _member(enums, "ProviderOriginView", "CATALOG"),
        "auth_methods": [_member(enums, "ProviderAuthMethodView", "API_KEY")],
        "stored": None,
        "signing_in": False,
        "enabled": True,
        "endpoint_host": None,
        "configurable": True,
        "endpoint_changed": True,
        "catalog_layer": _member(enums, "CatalogLayerView", "REMOTE_OVERLAY"),
        "selected_model_id": None,
        "thinking": None,
        "presentation": None,
        "endpoint_base": None,
        "last_refusal": None,
        "model_count": 1,
        "subscription": False,
        "refused_endpoint_host": "api.grok.example.invalid",
    }
    return {
        "base": base,
        "with_task": {**base, "active_tasks": [task]},
        "signed_in": signed_in,
        "with_probe": {**base, "provider_probes": [probe]},
        "with_roster": {**base, "provider_roster": [roster_row]},
    }


def build(schema: dict[str, Any]) -> dict[str, bytes]:
    encoder = Encoder(schema)
    limits = schema["limits"]
    codec = schema["state_payload_codec"]
    values = _values(schema, encoder.enums)

    base, base_spans = encoder.encode(values["base"])
    with_task, task_spans = encoder.encode(values["with_task"])
    signed_in, signed_in_spans = encoder.encode(values["signed_in"])
    with_probe, probe_spans = encoder.encode(values["with_probe"])
    with_roster, roster_spans = encoder.encode(values["with_roster"])

    def patch(buffer: bytes, span: tuple[int, int], raw: bytes) -> bytes:
        return buffer[: span[0]] + raw + buffer[span[1] :]

    def prefix(span: tuple[int, int], width: int = 4) -> tuple[int, int]:
        return (span[0], span[0] + width)

    def u32(value: int) -> bytes:
        return struct.pack("<I", value)

    version = (len(codec["magic"]), len(codec["magic"]) + 4)
    goal = task_spans["TaskViewState.goal"]
    goal_bytes = len(values["with_task"]["active_tasks"][0]["goal"].encode("utf-8"))
    # The span of an optional starts at its presence byte; the length prefix
    # of a present string is the four bytes after it.
    refused_host = roster_spans["ProviderRosterEntry.refused_endpoint_host"]

    return {
        "core-status-current-payload": base,
        "core-status-unknown-availability": patch(
            base, base_spans["CoreStatus.availability"], b"\xff\xff\xff\xff"
        ),
        "core-status-too-many-tasks": patch(
            base,
            prefix(base_spans["CoreStatus.active_tasks"]),
            u32(limits["MAX_ACTIVE_TASKS"] + 1),
        ),
        "core-status-unsupported-schema-version": patch(
            base, version, u32(codec["schema_version"] + 1)
        ),
        "core-status-trailing-bytes": base + b"\x00",
        "core-status-truncated-payload": base[:-1],
        "core-status-foreign-magic": b"TAFFYBIP" + base[len(codec["magic"]) :],
        "core-status-invalid-optional-flag": patch(
            base, prefix(base_spans["CoreStatus.auth_state"], 1), b"\x02"
        ),
        "core-status-account-missing-while-signed-in": patch(
            signed_in, signed_in_spans["AuthViewState.account"], b"\x00"
        ),
        # The declared length is the deviation, so the bytes it claims stop here.
        "core-status-oversized-goal": with_task[: goal[0]]
        + u32(limits["MAX_TASK_GOAL_BYTES"] + 1),
        "core-status-invalid-utf8-goal": patch(
            with_task, goal, u32(goal_bytes) + b"\xff" * goal_bytes
        ),
        "core-status-progress-over-limit": patch(
            with_task,
            task_spans["TaskViewState.progress_basis_points"],
            u32(limits["MAX_PROGRESS_BASIS_POINTS"] + 1),
        ),
        # One past the highest member, inside a list element: the closed-enum
        # rule has to hold at depth, not only at the root.
        "core-status-unknown-probe-verdict": patch(
            with_probe,
            probe_spans["ProviderProbeView.verdict"],
            u32(_highest_member(encoder.enums, "ProviderProbeVerdictView") + 1),
        ),
        # Present, and declaring one byte more than the host bound; the bytes
        # it claims stop here, as the goal fixture's do.
        "core-status-oversized-refused-host": with_roster[: refused_host[0]]
        + b"\x01"
        + u32(limits["MAX_SOURCE_HOST_BYTES"] + 1),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true")
    group.add_argument("--check", action="store_true")
    arguments = parser.parse_args()

    schema = json.loads((CONTRACT / "schema/contract.json").read_text(encoding="utf-8"))
    fixtures = build(schema)
    version = schema["state_payload_codec"]["schema_version"]

    if arguments.write:
        for name, raw in fixtures.items():
            (COMPAT / f"{name}.hex").write_text(raw.hex() + "\n", encoding="utf-8")
        print(f"core-api: wrote {len(fixtures)} payload fixtures at codec v{version}")
        return 0

    stale = []
    for name, raw in fixtures.items():
        path = COMPAT / f"{name}.hex"
        if not path.exists() or path.read_text(encoding="utf-8").strip() != raw.hex():
            stale.append(name)
    if stale:
        print(
            "core-api: these payload fixtures are not what the schema derives, so one of "
            f"them was edited by hand or the codec moved without them: {', '.join(sorted(stale))}. "
            "Run this file with --write.",
            file=sys.stderr,
        )
        return 1
    print(f"core-api: {len(fixtures)} payload fixtures match the schema at codec v{version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
