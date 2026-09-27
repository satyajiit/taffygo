#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""A validator for the subset of JSON Schema the release manifest uses.

Authority boundary: this module knows how to check an instance against a
schema and how to turn a violation into a sentence naming the exact item and
what to do about it. It knows nothing about releases, artifacts or Chromium —
that is manifest.py and the schema document itself.

Why a validator lives here at all. The repository installs nothing unpinned at
run time and the docs host has no package manager in the loop, so a
third-party validator would be a new supply-chain dependency on the one gate
that exists to police supply chains. The schema uses eleven keywords; those
eleven are implemented here, exactly, and an unknown keyword is an error
rather than something silently ignored.

Supported keywords: type, const, enum, pattern, minLength, minimum, maximum,
minItems, required, properties, additionalProperties (false only), items,
$ref (local "#/$defs/<name>" only).

Two extensions the schema relies on, both prefixed x- so a conventional
validator ignores them:

  x-remediation  a sentence naming the next step, inherited by any failure
                 inside that subschema that does not carry its own
  x-profiles     {profile: [json-pointer, ...]} — pointers that must resolve
                 to a present, non-empty value for that profile, on top of
                 whatever `required` already demands

Stdlib only. Python 3 is already a hard prerequisite of the docs gate and of
depot_tools, and is unrelated to decision 0007's sandboxed utility service.
"""

from __future__ import annotations

import re

KEYWORDS = {
    "$ref", "type", "const", "enum", "pattern", "minLength", "minimum",
    "maximum", "minItems", "required", "properties", "additionalProperties",
    "items", "$schema", "$id", "title", "description", "$defs",
    "x-remediation", "x-profiles",
}

TYPES = {
    "object": dict,
    "array": list,
    "string": str,
    "integer": int,
    "number": (int, float),
    "boolean": bool,
}


class Finding:
    """One violation: where it is, what is wrong, what to do about it."""

    def __init__(self, pointer: str, message: str, remediation: str) -> None:
        self.pointer = pointer or "/"
        self.message = message
        self.remediation = remediation

    def __str__(self) -> str:
        text = f"{self.pointer}: {self.message}"
        if self.remediation:
            text += f"\n    fix: {self.remediation}"
        return text


class Validator:
    """Validates an instance against one schema document."""

    def __init__(self, schema: dict) -> None:
        self.schema = schema
        self.defs = schema.get("$defs", {})
        unknown = self._unknown_keywords(schema, set())
        if unknown:
            raise ValueError(
                "the schema uses keywords this validator does not implement: "
                + ", ".join(sorted(unknown))
                + ". Implement them here or stop using them in the schema; "
                "silently ignoring a keyword would make the gate lie."
            )

    # --- schema self-inspection ---------------------------------------------

    def _unknown_keywords(self, node, seen: set) -> set:
        found: set = set()
        if isinstance(node, dict):
            if id(node) in seen:
                return found
            seen.add(id(node))
            looks_like_schema = any(k in KEYWORDS for k in node)
            for key, value in node.items():
                if looks_like_schema and key not in KEYWORDS:
                    found.add(key)
                if key in ("properties", "$defs"):
                    for child in value.values():
                        found |= self._unknown_keywords(child, seen)
                elif key in ("items", "additionalProperties"):
                    found |= self._unknown_keywords(value, seen)
        return found

    def resolve(self, schema: dict) -> dict:
        ref = schema.get("$ref")
        if not ref:
            return schema
        if not ref.startswith("#/$defs/"):
            raise ValueError(f"only local #/$defs/ references are supported, got {ref!r}")
        name = ref[len("#/$defs/"):]
        if name not in self.defs:
            raise ValueError(f"unresolved reference: {ref}")
        merged = dict(self.defs[name])
        for key, value in schema.items():
            if key != "$ref":
                merged.setdefault(key, value)
        return merged

    def schema_at(self, pointer: str) -> dict:
        """The subschema governing a JSON pointer into the instance."""
        node = self.schema
        for token in [t for t in pointer.split("/") if t]:
            node = self.resolve(node)
            if token.isdigit() and "items" in node:
                node = node["items"]
            else:
                node = node.get("properties", {}).get(token, {})
                if not node:
                    return {}
        return self.resolve(node)

    # --- validation ----------------------------------------------------------

    def validate(self, instance, profile: str | None = None) -> list[Finding]:
        findings: list[Finding] = []
        self._check(instance, self.schema, "", "", findings)
        if profile is not None:
            findings.extend(self.check_profile(instance, profile))
        return findings

    def check_profile(self, instance, profile: str) -> list[Finding]:
        profiles = self.schema.get("x-profiles", {})
        if profile not in profiles:
            known = ", ".join(sorted(profiles)) or "none declared"
            return [Finding(
                "", f'unknown profile "{profile}"',
                f"Known profiles: {known}. The profile comes from release.kind.",
            )]
        findings: list[Finding] = []
        for pointer in profiles[profile]:
            value, present = _pointer_get(instance, pointer)
            if not present or _is_empty(value):
                sub = self.schema_at(pointer)
                findings.append(Finding(
                    pointer,
                    f'required for a "{profile}" artifact and it is '
                    + ("absent" if not present else "empty"),
                    sub.get("x-remediation", "")
                    or f"Populate {pointer} before promoting a {profile} artifact.",
                ))
        return findings

    def _check(self, value, schema: dict, pointer: str, remedy: str, out: list) -> None:
        schema = self.resolve(schema)
        remedy = schema.get("x-remediation", remedy)

        def fail(message: str, own: str = "") -> None:
            out.append(Finding(pointer, message, own or remedy))

        if "const" in schema and value != schema["const"]:
            fail(f"must be {schema['const']!r}, found {value!r}")
            return
        if "enum" in schema and value not in schema["enum"]:
            allowed = ", ".join(repr(v) for v in schema["enum"])
            fail(f"{value!r} is not one of: {allowed}")
            return

        expected = schema.get("type")
        if expected:
            python_type = TYPES[expected]
            # JSON has no separate boolean/integer split; Python does not agree.
            if expected in ("integer", "number") and isinstance(value, bool):
                fail(f"must be {expected}, found boolean")
                return
            if not isinstance(value, python_type):
                fail(f"must be {expected}, found {type(value).__name__}")
                return

        if isinstance(value, str):
            if "pattern" in schema and not re.search(schema["pattern"], value):
                fail(f'"{value}" does not match {schema["pattern"]}')
            if "minLength" in schema and len(value) < schema["minLength"]:
                fail(f"must be at least {schema['minLength']} character(s) long")
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if "minimum" in schema and value < schema["minimum"]:
                fail(f"must be >= {schema['minimum']}, found {value}")
            if "maximum" in schema and value > schema["maximum"]:
                fail(f"must be <= {schema['maximum']}, found {value}")

        if isinstance(value, dict):
            self._check_object(value, schema, pointer, remedy, out)
        elif isinstance(value, list):
            if "minItems" in schema and len(value) < schema["minItems"]:
                fail(f"needs at least {schema['minItems']} entry(ies), found {len(value)}")
            item_schema = schema.get("items")
            if item_schema:
                for index, item in enumerate(value):
                    self._check(item, item_schema, f"{pointer}/{index}", remedy, out)

    def _check_object(self, value: dict, schema: dict, pointer: str, remedy: str, out: list) -> None:
        properties = schema.get("properties", {})
        for name in schema.get("required", []):
            if name in value:
                continue
            child = self.resolve(properties.get(name, {}))
            out.append(Finding(
                f"{pointer}/{name}",
                "is required and absent",
                child.get("x-remediation", "") or remedy,
            ))
        if schema.get("additionalProperties") is False:
            for name in value:
                if name not in properties:
                    out.append(Finding(
                        f"{pointer}/{name}",
                        "is not a field of this schema",
                        "Remove it or add it to tools/release.d/manifest-schema.json "
                        "with its own remediation sentence. An unknown field is "
                        "usually a typo in a field that then reads as absent.",
                    ))
        for name, child_schema in properties.items():
            if name in value:
                self._check(value[name], child_schema, f"{pointer}/{name}", remedy, out)


def _pointer_get(instance, pointer: str):
    """(value, present) for an RFC 6901 pointer, without raising."""
    node = instance
    for token in [t for t in pointer.split("/") if t]:
        token = token.replace("~1", "/").replace("~0", "~")
        if isinstance(node, dict):
            if token not in node:
                return None, False
            node = node[token]
        elif isinstance(node, list) and token.isdigit() and int(token) < len(node):
            node = node[int(token)]
        else:
            return None, False
    return node, True


def _is_empty(value) -> bool:
    if value is None:
        return True
    if isinstance(value, (str, list, dict)):
        return len(value) == 0
    return False
