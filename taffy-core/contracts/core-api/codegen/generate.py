#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate Core API projections and its one bounded state payload codec.

The frozen payload under `golden/` is an input here, never an output. It used
to be written by `--write` and then compared against the same expression on
`--check`, so a change to the wire layout moved both sides together and the
gate that existed to catch exactly that change stayed green. It is now read
and compared only; `--refreeze`, in the shared generator, is the one way to
replace it, and it refuses without a version bump.
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "codegen"))

from contract_schema import ContractError  # noqa: E402
from contract_validation import load_schema  # noqa: E402
from generate import main as generate_contract  # noqa: E402
from status_codec import (  # noqa: E402
    encode_fixture,
    load_fixture,
    render_status_codec,
    self_test,
    verify_language_parity,
)
from status_codec_kotlin import render_kotlin_status_codec  # noqa: E402
from status_codec_typescript import render_typescript_status_codec  # noqa: E402


CONTRACT_ROOT = Path(__file__).resolve().parents[1]
CODEC_OUTPUT = CONTRACT_ROOT / "generated" / "rust" / "core_status_codec.rs"
KOTLIN_CODEC_OUTPUT = CONTRACT_ROOT / "generated" / "kotlin" / "CoreStatusCodec.kt"
TYPESCRIPT_CODEC_OUTPUT = (
    CONTRACT_ROOT / "generated" / "typescript" / "core_status_codec.ts"
)
STATUS_FIXTURE = CONTRACT_ROOT / "golden" / "full-status.json"
PAYLOAD_FIXTURE = CONTRACT_ROOT / "golden" / "full-status-v1.hex"
REQUIRED_COMPAT = {
    "retired-auth-redirect-command": "REJECT_VERSION",
    "core-status-unknown-availability": "REJECT_CLOSED_ENUM",
    "core-status-too-many-tasks": "REJECT_OVERSIZED",
    "core-status-oversized-payload": "REJECT_OVERSIZED",
    "core-status-unsupported-schema-version": "REJECT_VERSION",
    "core-status-trailing-bytes": "REJECT_MALFORMED",
    "page-inspector-unknown-availability": "REJECT_CLOSED_ENUM",
    "page-inspector-available-without-snapshot": "REJECT_MALFORMED",
    "page-inspector-refusal-with-snapshot": "REJECT_MALFORMED",
    "page-inspector-oversized-node-list": "REJECT_OVERSIZED",
    "page-inspector-oversized-name": "REJECT_OVERSIZED",
    "page-inspector-raw-location-field": "REJECT_MALFORMED",
}


def _fixture_hex(schema: dict, fixture: dict) -> str:
    """The bytes this build encodes, for comparison against the frozen file.

    Two independent writers answer this question — the one here, written
    against the Core API codec declaration, and the shared reference writer
    the frozen-payload check uses. Both must agree with the committed bytes,
    which is a stronger statement than either could make alone.
    """
    return encode_fixture(schema, fixture).hex() + "\n"


def _check_file(path: Path, expected: str) -> None:
    try:
        current = path.read_text(encoding="utf-8")
    except OSError as error:
        raise ContractError(f"missing generated output {path}: {error}") from error
    if current != expected:
        raise ContractError(f"stale generated output: {path}")


def _verify_compatibility_manifest() -> None:
    """The payload cases this contract may never stop carrying.

    The corpus itself is executed by the shared checker; this list is the
    separate promise that these particular deviations are still in it, so a
    case cannot be quietly dropped the day it starts failing.
    """
    path = CONTRACT_ROOT / "compat" / "manifest.json"
    values = json.loads(path.read_text(encoding="utf-8"))
    actual = {entry.get("case"): entry.get("verdict") for entry in values["fixtures"]}
    for case, verdict in REQUIRED_COMPAT.items():
        if actual.get(case) != verdict:
            raise ContractError(f"core-api: missing compatibility case {case}={verdict}")


def main() -> int:
    result = generate_contract("core-api")
    if result != 0:
        return result
    try:
        schema = load_schema("core-api")
        fixture = load_fixture(STATUS_FIXTURE)
        rendered = render_status_codec(schema)
        kotlin_codec = render_kotlin_status_codec(schema)
        typescript_codec = render_typescript_status_codec(schema)
        payload = _fixture_hex(schema, fixture)
        arguments = set(sys.argv[1:])
        if "--refreeze" in arguments:
            return 0
        if "--write" in arguments:
            CODEC_OUTPUT.parent.mkdir(parents=True, exist_ok=True)
            CODEC_OUTPUT.write_text(rendered, encoding="utf-8")
            KOTLIN_CODEC_OUTPUT.write_text(kotlin_codec, encoding="utf-8")
            TYPESCRIPT_CODEC_OUTPUT.write_text(typescript_codec, encoding="utf-8")
        elif "--check" in arguments:
            _check_file(CODEC_OUTPUT, rendered)
            _check_file(KOTLIN_CODEC_OUTPUT, kotlin_codec)
            _check_file(TYPESCRIPT_CODEC_OUTPUT, typescript_codec)
            _check_file(PAYLOAD_FIXTURE, payload)
        elif "--verify" in arguments:
            _check_file(PAYLOAD_FIXTURE, payload)
            verify_language_parity(schema, CONTRACT_ROOT)
            for path, markers in (
                (KOTLIN_CODEC_OUTPUT, ("encodeCoreStatusPayload", "decodeCoreStatusPayload")),
                (TYPESCRIPT_CODEC_OUTPUT, ("encodeCoreStatusPayload", "decodeCoreStatusPayload")),
            ):
                text = path.read_text(encoding="utf-8")
                if any(marker not in text for marker in markers):
                    raise ContractError(f"core-api: incomplete generated codec {path}")
            _verify_compatibility_manifest()
        elif "--self-test" in arguments:
            self_test(schema, fixture)
        return 0
    except (ContractError, json.JSONDecodeError, OSError) as error:
        print(f"core-api state codec: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
