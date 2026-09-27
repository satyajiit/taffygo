# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Rules that hold between two declarations rather than inside one.

A record's own rules — names, ordinals, types, forbidden fields — are about
one declaration at a time and belong with the document validation. The three
rules here each bind two declarations together, and each exists because a
generated decoder in some language would otherwise have to guess:

  * a tagged union, where a closed enumeration decides which optional record
    body is the live one, and the variants must cover the enumeration exactly;
  * an enumeration pairing, where two closed enumerations in one record
    partition each other;
  * conditional presence, where a closed-enumeration tag says when an optional
    field must be there.

None of them can be checked while reading a single declaration, which is why
they run after every name in the contract is known.
"""

from __future__ import annotations

from typing import Any

from contract_schema import ContractError, parse_type


def validate_relations(
    contract: str, schema: dict[str, Any], enums: list, structs: list
) -> None:
    """Every cross-declaration rule, against a fully named contract."""
    struct_by_name = {struct["name"]: struct for struct in structs}
    enum_by_name = {enum["name"]: enum for enum in enums}

    tagged_unions = schema.get("tagged_unions", [])
    if not isinstance(tagged_unions, list):
        raise ContractError(f"{contract}: tagged_unions must be a list")
    seen_unions: set[str] = set()
    enum_by_name = {enum["name"]: enum for enum in enums}
    for union in tagged_unions:
        if not isinstance(union, dict):
            raise ContractError(f"{contract}: tagged union must be an object")
        struct_name = union.get("struct")
        tag_field_name = union.get("tag_field")
        variants = union.get("variants")
        if struct_name not in struct_by_name or struct_name in seen_unions:
            raise ContractError(f"{contract}: invalid or duplicate tagged union {struct_name!r}")
        seen_unions.add(struct_name)
        struct = struct_by_name[struct_name]
        fields = {field["name"]: field for field in struct["fields"]}
        tag_field = fields.get(tag_field_name)
        if tag_field is None or tag_field["type"] not in enum_by_name:
            raise ContractError(f"{struct_name}: tag_field must name a closed-enum field")
        if not isinstance(variants, list) or not variants:
            raise ContractError(f"{struct_name}: tagged union needs variants")
        enum_members = {member["name"] for member in enum_by_name[tag_field["type"]]["members"]}
        variant_tags: set[str] = set()
        variant_fields: set[str] = set()
        for variant in variants:
            if not isinstance(variant, dict) or set(variant) != {"tag", "field"}:
                raise ContractError(f"{struct_name}: invalid tagged-union variant")
            tag = variant["tag"]
            field_name = variant["field"]
            field = fields.get(field_name)
            if tag not in enum_members or tag in variant_tags:
                raise ContractError(f"{struct_name}: invalid or duplicate variant tag {tag!r}")
            if field is None or field_name in variant_fields:
                raise ContractError(f"{struct_name}: invalid or duplicate variant field {field_name!r}")
            reference = parse_type(field["type"])
            if reference.kind != "optional" or reference.inner.kind != "named":
                raise ContractError(
                    f"{struct_name}.{field_name}: variant body must be an optional record"
                )
            variant_tags.add(tag)
            variant_fields.add(field_name)
        if variant_tags != enum_members:
            raise ContractError(
                f"{struct_name}: variants must cover the closed enum exactly; "
                f"missing {sorted(enum_members - variant_tags)}"
            )
        optional_record_fields = {
            name
            for name, field in fields.items()
            if (reference := parse_type(field["type"])).kind == "optional"
            and reference.inner.kind == "named"
        }
        if optional_record_fields != variant_fields:
            raise ContractError(
                f"{struct_name}: every optional record field must be a tagged body"
            )

    enum_pairings = schema.get("enum_pairings", [])
    if not isinstance(enum_pairings, list):
        raise ContractError(f"{contract}: enum_pairings must be a list")
    seen_pairings: set[str] = set()
    for pairing in enum_pairings:
        if not isinstance(pairing, dict) or set(pairing) != {
            "struct",
            "left_field",
            "right_field",
            "pairs",
        }:
            raise ContractError(f"{contract}: invalid enum pairing")
        struct_name = pairing["struct"]
        if struct_name not in struct_by_name or struct_name in seen_pairings:
            raise ContractError(f"{contract}: invalid or duplicate pairing {struct_name!r}")
        seen_pairings.add(struct_name)
        fields = {field["name"]: field for field in struct_by_name[struct_name]["fields"]}
        left_field = fields.get(pairing["left_field"])
        right_field = fields.get(pairing["right_field"])
        if (
            left_field is None
            or right_field is None
            or left_field["type"] not in enum_by_name
            or right_field["type"] not in enum_by_name
        ):
            raise ContractError(f"{struct_name}: pairing fields must be closed enums")
        left_members = {
            member["name"] for member in enum_by_name[left_field["type"]]["members"]
        }
        right_members = {
            member["name"] for member in enum_by_name[right_field["type"]]["members"]
        }
        seen_left: set[str] = set()
        seen_right: set[str] = set()
        pairs = pairing["pairs"]
        if not isinstance(pairs, list) or not pairs:
            raise ContractError(f"{struct_name}: enum pairing needs pairs")
        for pair in pairs:
            if not isinstance(pair, dict) or set(pair) != {"left", "right"}:
                raise ContractError(f"{struct_name}: invalid enum pair")
            left = pair["left"]
            right = pair["right"]
            if left not in left_members or left in seen_left:
                raise ContractError(f"{struct_name}: invalid or duplicate left pair {left!r}")
            if not isinstance(right, list) or not right:
                raise ContractError(f"{struct_name}: enum pair must have right members")
            for member in right:
                if member not in right_members or member in seen_right:
                    raise ContractError(
                        f"{struct_name}: invalid or duplicate right pair {member!r}"
                    )
                seen_right.add(member)
            seen_left.add(left)
        if seen_left != left_members or seen_right != right_members:
            raise ContractError(f"{struct_name}: enum pairing must cover both enums exactly")

    presence_rules = schema.get("conditional_presence", [])
    if not isinstance(presence_rules, list):
        raise ContractError(f"{contract}: conditional_presence must be a list")
    seen_presence: set[tuple[str, str]] = set()
    for rule in presence_rules:
        if not isinstance(rule, dict) or set(rule) != {
            "struct",
            "tag_field",
            "field",
            "present_for",
        }:
            raise ContractError(f"{contract}: invalid conditional-presence rule")
        struct_name = rule["struct"]
        identity = (struct_name, rule["field"])
        if struct_name not in struct_by_name or identity in seen_presence:
            raise ContractError(f"{contract}: invalid or duplicate presence rule {identity!r}")
        seen_presence.add(identity)
        fields = {field["name"]: field for field in struct_by_name[struct_name]["fields"]}
        tag_field = fields.get(rule["tag_field"])
        guarded_field = fields.get(rule["field"])
        if tag_field is None or tag_field["type"] not in enum_by_name:
            raise ContractError(f"{struct_name}: presence tag must be a closed enum")
        if guarded_field is None or parse_type(guarded_field["type"]).kind != "optional":
            raise ContractError(f"{struct_name}: guarded field must be optional")
        members = {member["name"] for member in enum_by_name[tag_field["type"]]["members"]}
        present_for = rule["present_for"]
        if (
            not isinstance(present_for, list)
            or not present_for
            or len(set(present_for)) != len(present_for)
            or not set(present_for) <= members
        ):
            raise ContractError(f"{struct_name}: invalid present_for members")
