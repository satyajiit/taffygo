# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Reading a committed fixture document, with one rule about what it may be."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from contract_schema import ContractError


def load_document(path: Path) -> dict[str, Any]:
    """One JSON object from disk, or a contract error naming the file."""
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ContractError(f"{path}: {error}") from error
    if not isinstance(value, dict):
        raise ContractError(f"{path}: fixture must be a JSON object")
    return value


def load_hex(path: Path) -> bytes:
    """One committed payload, as bytes, or a contract error naming the file."""
    try:
        text = path.read_text(encoding="utf-8").strip()
    except OSError as error:
        raise ContractError(f"{path}: {error}") from error
    try:
        return bytes.fromhex(text)
    except ValueError as error:
        raise ContractError(f"{path}: payload is not hexadecimal") from error
