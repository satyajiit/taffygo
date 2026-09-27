# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""A very small JSON Schema 2020-12 checker for the BIP contract fixtures.

Scope on purpose. This module implements only the keywords the BIP schemas in
``taffy-core/contracts/bip/schema`` actually use, so the contract can be checked on a
stock host Python 3 with no package installs, no network, and no vendored
third-party code. It is a fixture checker, not a general validator: a schema
using a keyword outside :data:`KNOWN_KEYWORDS` raises rather than passing
silently, which keeps the checker honest as the schemas grow.

Supported: ``$ref`` (same-document and cross-file, ``#/$defs/Name`` pointers),
``type``, ``enum``, ``const``, ``properties``, ``required``,
``additionalProperties``, ``items``, ``contains``, ``minItems``, ``maxItems``,
``minimum``, ``maximum``, ``minLength``, ``maxLength``, ``pattern``, ``allOf``,
``anyOf``, ``oneOf``, ``not``, and ``if``/``then``/``else``.
"""

from __future__ import annotations

import json
import os
import re

# Keywords the checker understands. Annotations carry no assertion.
ANNOTATION_KEYWORDS = frozenset(
    {"$schema", "$id", "$defs", "$comment", "title", "description", "default", "examples"}
)
ASSERTION_KEYWORDS = frozenset(
    {
        "$ref",
        "type",
        "enum",
        "const",
        "properties",
        "required",
        "additionalProperties",
        "items",
        "contains",
        "minItems",
        "maxItems",
        "minimum",
        "maximum",
        "minLength",
        "maxLength",
        "pattern",
        "allOf",
        "anyOf",
        "oneOf",
        "not",
        "if",
        "then",
        "else",
    }
)
KNOWN_KEYWORDS = ANNOTATION_KEYWORDS | ASSERTION_KEYWORDS


# How each keyword carries its subschemas, so the audit walks them correctly and
# never mistakes a property name or a definition name for a keyword.
MAP_OF_SCHEMAS = frozenset({"$defs", "properties"})
LIST_OF_SCHEMAS = frozenset({"allOf", "anyOf", "oneOf"})
SUBSCHEMA_KEYWORDS = frozenset(
    {"items", "contains", "not", "if", "then", "else", "additionalProperties"}
)


class SchemaError(Exception):
    """The schema itself is wrong, as opposed to the instance under test."""


def _is_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _is_number(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


TYPE_CHECKS = {
    "object": lambda v: isinstance(v, dict),
    "array": lambda v: isinstance(v, list),
    "string": lambda v: isinstance(v, str),
    "integer": _is_int,
    "number": _is_number,
    "boolean": lambda v: isinstance(v, bool),
    "null": lambda v: v is None,
}


class SchemaStore:
    """Every BIP schema document, addressable by file name."""

    def __init__(self, documents: dict[str, dict]) -> None:
        self.documents = documents
        for name, document in documents.items():
            self._audit(document, f"{name}#")

    @classmethod
    def from_directory(cls, schema_dir: str) -> "SchemaStore":
        documents: dict[str, dict] = {}
        for name in sorted(os.listdir(schema_dir)):
            if not name.endswith(".schema.json"):
                continue
            with open(os.path.join(schema_dir, name), encoding="utf-8") as handle:
                documents[name] = json.load(handle)
        if not documents:
            raise SchemaError(f"no .schema.json documents under {schema_dir}")
        return cls(documents)

    def _audit(self, node: object, where: str) -> None:
        """Reject a schema keyword the checker would otherwise ignore."""
        if not isinstance(node, dict):
            return
        for key, value in node.items():
            if key.startswith("x-"):
                continue
            if key not in KNOWN_KEYWORDS:
                raise SchemaError(f"{where}: unsupported schema keyword {key!r}")
            if key in MAP_OF_SCHEMAS and isinstance(value, dict):
                for name, child in value.items():
                    self._audit(child, f"{where}/{key}/{name}")
            elif key in LIST_OF_SCHEMAS and isinstance(value, list):
                for index, child in enumerate(value):
                    self._audit(child, f"{where}/{key}/{index}")
            elif key in SUBSCHEMA_KEYWORDS:
                self._audit(value, f"{where}/{key}")

    def resolve(self, ref: str, base: str) -> tuple[dict, str]:
        """Resolve a ``$ref`` to (schema node, the file it lives in)."""
        document_ref, _, pointer = ref.partition("#")
        target = document_ref or base
        if target not in self.documents:
            raise SchemaError(f"$ref {ref!r} names unknown document {target!r}")
        node: object = self.documents[target]
        for raw in pointer.split("/"):
            if raw == "":
                continue
            token = raw.replace("~1", "/").replace("~0", "~")
            if not isinstance(node, dict) or token not in node:
                raise SchemaError(f"$ref {ref!r} does not resolve inside {target}")
            node = node[token]
        if not isinstance(node, dict):
            raise SchemaError(f"$ref {ref!r} does not point at a schema object")
        return node, target


def validate(instance: object, schema: dict, store: SchemaStore, base: str) -> list[str]:
    """Return every reason ``instance`` fails ``schema``. Empty means valid."""
    errors: list[str] = []
    _check(instance, schema, store, base, "$", errors)
    return errors


def _fails(instance: object, schema: dict, store: SchemaStore, base: str) -> bool:
    scratch: list[str] = []
    _check(instance, schema, store, base, "$", scratch)
    return bool(scratch)


def _check(
    instance: object,
    schema: dict,
    store: SchemaStore,
    base: str,
    path: str,
    errors: list[str],
) -> None:
    if "$ref" in schema:
        target, target_base = store.resolve(schema["$ref"], base)
        _check(instance, target, store, target_base, path, errors)

    expected_type = schema.get("type")
    if expected_type is not None:
        options = expected_type if isinstance(expected_type, list) else [expected_type]
        if not any(TYPE_CHECKS[name](instance) for name in options):
            errors.append(f"{path}: expected type {'/'.join(options)}")
            return

    if "enum" in schema and instance not in schema["enum"]:
        errors.append(f"{path}: {instance!r} is outside the closed enumeration")
    if "const" in schema and instance != schema["const"]:
        errors.append(f"{path}: expected the constant {schema['const']!r}")

    if isinstance(instance, str):
        _check_string(instance, schema, path, errors)
    if _is_number(instance):
        _check_number(instance, schema, path, errors)
    if isinstance(instance, list):
        _check_array(instance, schema, store, base, path, errors)
    if isinstance(instance, dict):
        _check_object(instance, schema, store, base, path, errors)

    for keyword in ("allOf", "anyOf", "oneOf"):
        if keyword not in schema:
            continue
        branches = schema[keyword]
        passing = [b for b in branches if not _fails(instance, b, store, base)]
        if keyword == "allOf" and len(passing) != len(branches):
            for branch in branches:
                _check(instance, branch, store, base, path, errors)
        elif keyword == "anyOf" and not passing:
            errors.append(f"{path}: matched none of the anyOf branches")
        elif keyword == "oneOf" and len(passing) != 1:
            errors.append(f"{path}: matched {len(passing)} oneOf branches, expected 1")

    if "not" in schema and not _fails(instance, schema["not"], store, base):
        errors.append(f"{path}: matched a schema it must not match")

    if "if" in schema and ("then" in schema or "else" in schema):
        branch = "then" if not _fails(instance, schema["if"], store, base) else "else"
        if branch in schema:
            _check(instance, schema[branch], store, base, path, errors)


def _check_string(instance: str, schema: dict, path: str, errors: list[str]) -> None:
    if "minLength" in schema and len(instance) < schema["minLength"]:
        errors.append(f"{path}: shorter than minLength {schema['minLength']}")
    if "maxLength" in schema and len(instance) > schema["maxLength"]:
        errors.append(f"{path}: longer than maxLength {schema['maxLength']}")
    if "pattern" in schema and re.search(schema["pattern"], instance) is None:
        errors.append(f"{path}: does not match pattern {schema['pattern']}")


def _check_number(instance: float, schema: dict, path: str, errors: list[str]) -> None:
    if "minimum" in schema and instance < schema["minimum"]:
        errors.append(f"{path}: below minimum {schema['minimum']}")
    if "maximum" in schema and instance > schema["maximum"]:
        errors.append(f"{path}: above maximum {schema['maximum']}")


def _check_array(
    instance: list,
    schema: dict,
    store: SchemaStore,
    base: str,
    path: str,
    errors: list[str],
) -> None:
    if "minItems" in schema and len(instance) < schema["minItems"]:
        errors.append(f"{path}: fewer than minItems {schema['minItems']}")
    if "maxItems" in schema and len(instance) > schema["maxItems"]:
        errors.append(f"{path}: more than maxItems {schema['maxItems']}")
    if "items" in schema:
        for index, item in enumerate(instance):
            _check(item, schema["items"], store, base, f"{path}[{index}]", errors)
    if "contains" in schema:
        if not any(not _fails(item, schema["contains"], store, base) for item in instance):
            errors.append(f"{path}: no element satisfies the contains schema")


def _check_object(
    instance: dict,
    schema: dict,
    store: SchemaStore,
    base: str,
    path: str,
    errors: list[str],
) -> None:
    for name in schema.get("required", []):
        if name not in instance:
            errors.append(f"{path}: missing required property {name!r}")
    properties = schema.get("properties", {})
    for name, value in sorted(instance.items()):
        if name in properties:
            _check(value, properties[name], store, base, f"{path}.{name}", errors)
            continue
        extra = schema.get("additionalProperties")
        if extra is False:
            errors.append(f"{path}: property {name!r} is not allowed")
        elif isinstance(extra, dict):
            _check(value, extra, store, base, f"{path}.{name}", errors)
