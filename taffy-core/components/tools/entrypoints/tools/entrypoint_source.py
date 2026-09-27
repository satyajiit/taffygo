# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""What a frozen entrypoint registry source may say, and what it may not.

This module is the rules and nothing else: it reads no artifact, writes no
file, and decides only whether a loaded document is a registry. `generate.py`
drives it and `entrypoint_projections.py` renders what it accepts.

The split is a responsibility seam rather than a size one. A rule lives in
exactly one place here, so a reviewer asking "what may a row carry" reads this
file and no other, and a renderer cannot quietly accept something the rules
refuse.
"""

from __future__ import annotations

import hashlib
import json
import re

#: Mirrors `MAX_TOOL_ENTRYPOINT_ID_BYTES` in the Core Service contract, which is
#: what the browser and the core already bound this field to. A row longer than
#: the wire allows would be a registry entry no job could ever name.
MAX_ENTRYPOINT_ID_BYTES = 128
MAX_ENTRYPOINTS = 32
MAX_PORTS = 8
MAX_VALUE_KINDS = 32

#: A namespace and a name. Both halves are required, because a bare name is
#: what a registry looks like just before somebody adds a second one that
#: collides with it.
ENTRYPOINT_ID = re.compile(r"^[a-z0-9]+(\.[a-z0-9]+)+$")
KIND_NAME = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
PORT_NAME = re.compile(r"^[a-z][a-z0-9_]*$")

DOCUMENT_KEYS = {"registry", "registry_version", "value_kinds", "native_paths", "entrypoints"}
VALUE_KIND_KEYS = {"name", "description"}
NATIVE_PATH_KEYS = {"id", "state", "description"}
ENTRYPOINT_KEYS = {"id", "summary", "native_alternative", "inputs", "outputs"}
INPUT_KEYS = {"name", "kind", "required", "description"}
OUTPUT_KEYS = {"name", "kind", "description"}

#: A key whose name contains one of these describes a location or a body of
#: code, and neither belongs in a frozen registry. The check runs over every
#: key in the document rather than over the allowed sets above, so widening an
#: allowed set is not enough to introduce one.
FORBIDDEN_KEY_SUBSTRINGS = (
    "argv",
    "command",
    "directory",
    "file",
    "filesystem",
    "module",
    "source_code",
    "uri",
    "url",
)

#: Refused as whole key names rather than as substrings, because this registry
#: uses "native path" for the layer that owns a capability and `native_paths`
#: must stay legal - and because "description" contains "script". A key that
#: *is* `path` is a location; a key that merely contains the word may not be,
#: and guessing wrong in either direction is worse than naming these exactly.
FORBIDDEN_KEY_NAMES = ("code", "dir", "location", "path", "paths", "root", "script", "scripts")

NATIVE_STATES = ("active", "planned")


class RegistryError(Exception):
    """A source file that cannot become a registry."""


def fail(message: str) -> None:
    raise RegistryError(message)


def load(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


# --- validation -------------------------------------------------------------


def check_keys(row: dict, allowed: set[str], what: str) -> None:
    for key in row:
        if not isinstance(key, str):
            fail(f"{what} has a non-string key")
        lowered = key.lower()
        if lowered in FORBIDDEN_KEY_NAMES:
            fail(f"{what} names {key!r}; a registry row may not carry a location")
        for forbidden in FORBIDDEN_KEY_SUBSTRINGS:
            if forbidden in lowered:
                fail(f"{what} names {key!r}; a registry row may not carry a {forbidden}")
        if key not in allowed:
            fail(f"{what} carries the unknown key {key!r}")
    for key in allowed:
        if key not in row:
            fail(f"{what} is missing {key!r}")


def check_text(row: dict, key: str, what: str) -> None:
    value = row[key]
    if not isinstance(value, str) or not value.strip():
        fail(f"{what} has an empty {key}")


def check_sorted(ids: list[str], what: str) -> None:
    """Rows are held in ascending order so that two of them cannot be swapped.

    Order is not cosmetic here. A lookup is a scan over a compiled-in table and
    a reviewer reads the table as a list, so a row that can move is a row whose
    position carries no information; requiring the order makes a diff show one
    added line rather than a rewrite nobody reads.
    """
    if ids != sorted(ids):
        fail(f"{what} are not in ascending order")
    if len(set(ids)) != len(ids):
        fail(f"{what} contain a duplicate")


def validate_value_kinds(document: dict) -> set[str]:
    kinds = document["value_kinds"]
    if not isinstance(kinds, list) or not kinds or len(kinds) > MAX_VALUE_KINDS:
        fail(f"value_kinds must hold between 1 and {MAX_VALUE_KINDS} rows")
    for kind in kinds:
        check_keys(kind, VALUE_KIND_KEYS, "a value kind")
        check_text(kind, "description", "a value kind")
        if not isinstance(kind["name"], str) or not KIND_NAME.match(kind["name"]):
            fail(f"the value kind {kind['name']!r} is not a lowercase dashed name")
    check_sorted([kind["name"] for kind in kinds], "value kinds")
    return {kind["name"] for kind in kinds}


def validate_native_paths(document: dict) -> set[str]:
    paths = document["native_paths"]
    if not isinstance(paths, list):
        fail("native_paths must be a list")
    for path in paths:
        check_keys(path, NATIVE_PATH_KEYS, "a native path")
        check_text(path, "description", "a native path")
        if not isinstance(path["id"], str) or not ENTRYPOINT_ID.match(path["id"]):
            fail(f"the native path {path['id']!r} is not a namespaced lowercase identity")
        if path["state"] not in NATIVE_STATES:
            fail(f"the native path {path['id']} declares the unknown state {path['state']!r}")
    check_sorted([path["id"] for path in paths], "native paths")
    return {path["id"] for path in paths}


def validate_ports(row: dict, key: str, allowed: set[str], kinds: set[str]) -> None:
    ports = row[key]
    identity = row["id"]
    if not isinstance(ports, list) or not ports or len(ports) > MAX_PORTS:
        fail(f"{identity} must declare between 1 and {MAX_PORTS} {key}")
    for port in ports:
        check_keys(port, allowed, f"a {key[:-1]} of {identity}")
        check_text(port, "description", f"a {key[:-1]} of {identity}")
        if not isinstance(port["name"], str) or not PORT_NAME.match(port["name"]):
            fail(f"{identity} declares the malformed {key[:-1]} name {port['name']!r}")
        if port["kind"] not in kinds:
            fail(f"{identity} declares the undeclared value kind {port['kind']!r}")
        if "required" in port and not isinstance(port["required"], bool):
            fail(f"{identity} declares a non-boolean 'required' on {port['name']}")
    names = [port["name"] for port in ports]
    if len(set(names)) != len(names):
        fail(f"{identity} declares two {key} with one name")


def validate(document: dict) -> None:
    """Every rule this registry has, in one place, raising on the first break."""
    if not isinstance(document, dict):
        fail("the source is not a JSON object")
    check_keys(document, DOCUMENT_KEYS, "the registry")
    if document["registry"] != "tool-entrypoints":
        fail("the source does not name this registry")
    version = document["registry_version"]
    if not isinstance(version, int) or isinstance(version, bool) or version < 1:
        fail("registry_version must be a positive integer")

    kinds = validate_value_kinds(document)
    natives = validate_native_paths(document)

    entrypoints = document["entrypoints"]
    if not isinstance(entrypoints, list) or not entrypoints:
        fail("the registry declares no entrypoint")
    if len(entrypoints) > MAX_ENTRYPOINTS:
        fail(f"the registry declares more than {MAX_ENTRYPOINTS} entrypoints")

    referenced: set[str] = set()
    for row in entrypoints:
        check_keys(row, ENTRYPOINT_KEYS, "an entrypoint")
        check_text(row, "summary", "an entrypoint")
        identity = row["id"]
        if not isinstance(identity, str) or not ENTRYPOINT_ID.match(identity):
            fail(f"the entrypoint {identity!r} is not a namespaced lowercase identity")
        if len(identity.encode("utf-8")) > MAX_ENTRYPOINT_ID_BYTES:
            fail(f"the entrypoint {identity} is longer than the wire allows")
        alternative = row["native_alternative"]
        if not isinstance(alternative, str):
            fail(f"{identity} declares a non-string native_alternative")
        if alternative:
            if alternative not in natives:
                fail(f"{identity} names the undeclared native path {alternative!r}")
            referenced.add(alternative)
        validate_ports(row, "inputs", INPUT_KEYS, kinds)
        validate_ports(row, "outputs", OUTPUT_KEYS, kinds)
    check_sorted([row["id"] for row in entrypoints], "entrypoints")

    # A declared native path nothing refuses in favour of is a claim with no
    # consequence, and it would read to a later reviewer as a capability the
    # product owns rather than as an unused row.
    for orphan in sorted(natives - referenced):
        fail(f"the native path {orphan} is declared and named by no entrypoint")


def fingerprint(document: dict) -> int:
    """A digest over what the rows *decide*, not over how they are written."""
    material = []
    for row in document["entrypoints"]:
        ports = []
        for key in ("inputs", "outputs"):
            for port in row[key]:
                ports.append(f"{key}:{port['name']}:{port['kind']}:{port.get('required', False)}")
        material.append(f"{row['id']}|{row['native_alternative']}|" + ",".join(ports))
    digest = hashlib.sha256("\n".join(material).encode("utf-8")).digest()
    return int.from_bytes(digest[:4], "big")


