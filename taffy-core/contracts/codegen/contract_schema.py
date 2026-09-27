# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The vocabulary a Taffy contract schema is written in.

This module is the grammar and nothing else: what a type reference may
say, what a name may look like, which wrappers exist and what each one is
allowed to hold. It knows nothing about whole documents, so every other
module in this directory can depend on it and it depends on none of them.
Validation of a document lives in `contract_validation`, the rules that
hold between two declarations live in `contract_relations`, and interface
declarations live in `contract_interfaces`.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

CONTRACT_ROOT = Path(__file__).resolve().parents[1]
SCALARS = {"bool", "bytes", "bytes32", "i64", "string", "u32", "u64"}
IDENTIFIER = re.compile(r"^[A-Za-z][A-Za-z0-9]*$")
MEMBER = re.compile(r"^[A-Z][A-Z0-9_]*$")


class ContractError(ValueError):
    """A contract is ambiguous, unsafe, or cannot be generated."""


@dataclass(frozen=True)
class TypeRef:
    kind: str
    name: str | None = None
    inner: "TypeRef | None" = None


#: Wrappers whose body is another type. `pending_remote` and `pending_receiver`
#: carry a Mojo interface rather than a value, so they are legal only in an
#: interface method's parameters — a record field holding one would put a live
#: connection inside a value that can be stored, copied and replayed, which is
#: not a thing this contract layer is willing to describe.
HANDLE_WRAPPERS = ("pending_remote", "pending_receiver")
VALUE_WRAPPERS = ("optional", "list")

#: The languages a contract may be projected into. A contract names the subset
#: it emits, because a projection nothing compiles is worse than an absent one:
#: it looks checked and is not.
PROJECTIONS = ("rust", "kotlin", "typescript", "mojom", "cpp")


def parse_type(value: str) -> TypeRef:
    value = value.strip()
    for wrapper in VALUE_WRAPPERS + HANDLE_WRAPPERS:
        prefix = f"{wrapper}<"
        if value.startswith(prefix) and value.endswith(">"):
            inner = value[len(prefix) : -1]
            if not inner or "<" in inner or ">" in inner:
                # Nested containers are intentionally unsupported. They make
                # bounds and ownership unclear at an IPC boundary.
                raise ContractError(f"unsupported nested or empty type: {value}")
            if wrapper in HANDLE_WRAPPERS and not IDENTIFIER.fullmatch(inner):
                raise ContractError(f"{wrapper} must name an interface: {value}")
            return TypeRef(wrapper, inner=parse_type(inner))
    if value in SCALARS:
        return TypeRef("scalar", name=value)
    if IDENTIFIER.fullmatch(value):
        return TypeRef("named", name=value)
    raise ContractError(f"unsupported type: {value}")


def require_description(owner: str, value: dict[str, Any]) -> None:
    description = value.get("description")
    if not isinstance(description, str) or not description.strip():
        raise ContractError(f"{owner} needs a non-empty description")
