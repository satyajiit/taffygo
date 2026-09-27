# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Interface declarations, and the two things a handle may not be.

An interface is a declaration kind like any other, and it is validated here
for one reason the record rules do not cover: Mojo assigns a method ordinal by
declaration order unless one is written down, so a tidy-up of a method list is
a silent reinterpretation of every message a peer already sends. Every method
and every argument therefore carries a written ordinal, and this module is
what refuses a schema that omits or reorders one.

The other two rules are about where a live connection may appear. It may be
handed over in a parameter and never returned in a reply, and it may never be
a record field at all — a record is stored, copied and replayed, and a
connection survives none of that.
"""

from __future__ import annotations

import re
from typing import Any

from contract_schema import (
    HANDLE_WRAPPERS,
    IDENTIFIER,
    SCALARS,
    ContractError,
    parse_type,
    require_description,
)


def _validate_method_arguments(
    owner: str,
    arguments: Any,
    declared: set[str],
    interface_names: set[str],
    *,
    handles_allowed: bool,
) -> None:
    """One parameter list, in declaration order, with append-only ordinals."""
    if not isinstance(arguments, list):
        raise ContractError(f"{owner}: arguments must be a list")
    seen: set[str] = set()
    for ordinal, argument in enumerate(arguments):
        if not isinstance(argument, dict):
            raise ContractError(f"{owner}: argument must be an object")
        name = argument.get("name")
        if not isinstance(name, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", name):
            raise ContractError(f"{owner}: invalid argument name {name!r}")
        if name in seen or argument.get("ordinal") != ordinal:
            raise ContractError(
                f"{owner}.{name}: duplicate name or non-append-only ordinal"
            )
        seen.add(name)
        require_description(f"{owner}.{name}", argument)
        reference = parse_type(argument.get("type", ""))
        if reference.kind in HANDLE_WRAPPERS:
            if not handles_allowed:
                raise ContractError(
                    f"{owner}.{name}: a reply cannot carry {reference.kind}; "
                    "an interface is handed over, not returned"
                )
            if reference.inner.name not in interface_names:
                raise ContractError(
                    f"{owner}.{name}: {reference.inner.name} is not an interface "
                    "in this contract"
                )
            continue
        named = reference.inner.name if reference.inner else reference.name
        if reference.kind in {"named", "optional", "list"} and named not in declared | SCALARS:
            raise ContractError(f"{owner}.{name}: unknown type {named}")


def validate_interfaces(
    contract: str, schema: dict[str, Any], declared: set[str]
) -> None:
    """The contract's own interfaces, with frozen method and argument ordinals.

    Mojo assigns a method ordinal by declaration order unless one is written
    down, and a reordered method is a silently reinterpreted message on every
    peer built before the change. So an ordinal is written down here, the
    ledger freezes it, and reordering the list changes nothing on the wire.
    """
    interfaces = schema.get("interfaces", [])
    if not isinstance(interfaces, list):
        raise ContractError(f"{contract}: interfaces must be a list")
    interface_names = {
        interface.get("name")
        for interface in interfaces
        if isinstance(interface, dict)
    }
    seen_interfaces: set[str] = set()
    for interface in interfaces:
        if not isinstance(interface, dict):
            raise ContractError(f"{contract}: interface must be an object")
        name = interface.get("name")
        if not isinstance(name, str) or not IDENTIFIER.fullmatch(name):
            raise ContractError(f"{contract}: invalid interface name {name!r}")
        if name in seen_interfaces or name in declared:
            raise ContractError(f"{contract}: duplicate declaration {name!r}")
        seen_interfaces.add(name)
        require_description(name, interface)
        methods = interface.get("methods")
        if not isinstance(methods, list) or not methods:
            raise ContractError(f"{name}: an interface needs methods")
        method_names: set[str] = set()
        for ordinal, method in enumerate(methods):
            if not isinstance(method, dict):
                raise ContractError(f"{name}: method must be an object")
            method_name = method.get("name")
            if not isinstance(method_name, str) or not IDENTIFIER.fullmatch(method_name):
                raise ContractError(f"{name}: invalid method name {method_name!r}")
            if method_name in method_names or method.get("ordinal") != ordinal:
                raise ContractError(
                    f"{name}.{method_name}: duplicate name or non-append-only ordinal"
                )
            method_names.add(method_name)
            require_description(f"{name}.{method_name}", method)
            _validate_method_arguments(
                f"{name}.{method_name}",
                method.get("params", []),
                declared,
                interface_names,
                handles_allowed=True,
            )
            response = method.get("response")
            if response is None:
                continue
            _validate_method_arguments(
                f"{name}.{method_name} reply",
                response,
                declared,
                interface_names,
                handles_allowed=False,
            )
