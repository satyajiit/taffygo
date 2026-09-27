#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The kernel's catalog vocabulary is the authority; the baseline generator is
a projection, and this proves it still projects it.

WHAT IT COMPARES

Full vocabularies, read out of source texts: every variant of every closed
enum in `src/catalog/types.rs`, spelled the way serde's SCREAMING_SNAKE_CASE
spells it, against the generator's own tuples.

The Rust source is parsed, never transcribed: the variant lists here are read
out of `types.rs` and respelled by the same rule serde applies, so nothing in
this file can drift from the kernel without the comparison noticing.

WHAT IT USED TO COMPARE, AND WHY THAT IS GONE

There were four projections and a companion tool. This file also read the
managed-route Worker's `export const` lists and the `catalog` schema's SQL
enum types folded across every Supabase migration, and it checked *field
presence* against that schema: every field the kernel's decoder requires had
to exist as a column, through an explicit mapping table. That half was written
for two real divergences -- `catalog.wire_api` spelled `OPENAI_RESPONSES`
where the kernel reads `OPEN_AI_RESPONSES`, and a `catalog.models` with no
`tool_calling` column at all -- and both would have dropped every affected row
on the device with no error naming the cause.

TaffyGo operates no server (decision 0200). The Worker and the account plane
are both deleted, so there is no SQL schema to disagree with and no Worker
parser to disagree with: what is left is the one comparison that was never
about either. The field-presence check goes with the columns it checked
against, and nothing replaces it, because the device now reads the embedded
baseline and the generator that writes it is checked byte-for-byte by
`generate_baseline.py --check`.

Stdlib only, no checkout, no network, no database.

    check_schema_agreement.py             compare and report
    check_schema_agreement.py --self-test prove the comparison can fail

Exit status: 0 agree, 1 disagree.
"""

from __future__ import annotations

import argparse
import os
import sys

_ROOT_MARKERS = ("chromium/REVISION", "TOOLCHAIN.md", "Cargo.toml")

#: Each closed enum the catalog carries: the Rust type and the generator tuple
#: that must spell it identically. The generator's LEVELS ladder is not here
#: because thinking levels are a jsonb map rather than a closed enum.
ENUMS = (
    ("WireApi", "WIRE_APIS"),
    ("AuthMethod", "AUTH_METHODS"),
    ("ModelSource", "MODEL_SOURCES"),
    ("ModelRole", "ROLES"),
    ("InputModality", "MODALITIES"),
    ("PriceBasis", "BASES"),
)


def repository_root() -> str:
    root = os.path.dirname(os.path.realpath(__file__))
    while True:
        if all(os.path.exists(os.path.join(root, marker)) for marker in _ROOT_MARKERS):
            return root
        parent = os.path.dirname(root)
        if parent == root:
            raise SystemExit("catalog schema agreement: could not find the repository root")
        root = parent


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schema_agreement_sources import (  # noqa: E402
    GENERATOR, TYPES_RS, generator_tuple, rust_enum_variants, screaming_snake,
)


# --- the comparison -----------------------------------------------------------


def compare(types_source: str, generator_source: str) -> list[str]:
    findings: list[str] = []
    for rust_name, tuple_name in ENUMS:
        kernel = rust_enum_variants(types_source, rust_name)
        tup = generator_tuple(generator_source, tuple_name)
        if tup != kernel:
            findings.append(
                f"enum {rust_name}: the generator tuple {tuple_name} spells "
                f"{{{', '.join(sorted(tup))}}} where the kernel reads "
                f"{{{', '.join(sorted(kernel))}}}. Both parsers fail closed, so a "
                "row carrying the difference is dropped without an error naming "
                "the cause."
            )
    return findings


# --- the tool's own checks ----------------------------------------------------


_FAKE_TYPES = """
pub enum WireApi {
    AnthropicMessages,
    OpenAiResponses,
}
pub enum AuthMethod {
    ApiKey,
}
pub enum ModelSource {
    StaticCatalog,
}
pub enum ModelRole {
    PrimaryReasoning,
}
pub enum InputModality {
    Text,
}
pub enum PriceBasis {
    Metered,
}
"""

_FAKE_GENERATOR = (
    'WIRE_APIS = (\n    "ANTHROPIC_MESSAGES",\n    "OPEN_AI_RESPONSES",\n)\n'
    'AUTH_METHODS = ("API_KEY",)\n'
    'MODEL_SOURCES = ("STATIC_CATALOG",)\n'
    'ROLES = ("PRIMARY_REASONING",)\n'
    'MODALITIES = ("TEXT",)\n'
    'BASES = ("METERED",)\n'
)


def self_test() -> int:
    failures: list[str] = []

    if screaming_snake("OpenAiResponses") != "OPEN_AI_RESPONSES":
        failures.append("the serde respelling no longer splits OpenAiResponses")

    # Agreement is the quiet case, and a comparison that never clears is as
    # useless as one that never fires.
    agreed = compare(_FAKE_TYPES, _FAKE_GENERATOR)
    if agreed:
        failures.append(f"two agreeing sources produced findings; got {agreed}")

    # A respelling the token boundary hides: OPEN_AI_RESPONSES and
    # openai_responses are not the same string under any case folding, which
    # is the divergence this tool was written for.
    drifted = compare(
        _FAKE_TYPES, _FAKE_GENERATOR.replace('"OPEN_AI_RESPONSES"', '"openai_responses"')
    )
    if not any("WIRE_APIS" in f and "dropped" in f for f in drifted):
        failures.append(f"a drifted generator tuple was not caught; got {drifted}")

    # A value present on one side only, in each direction.
    added = compare(_FAKE_TYPES, _FAKE_GENERATOR.replace(
        'ROLES = ("PRIMARY_REASONING",)', 'ROLES = ("PRIMARY_REASONING", "EMBEDDING")'))
    if not any("ModelRole" in f for f in added):
        failures.append(f"a generator-only value was not caught; got {added}")
    dropped = compare(
        _FAKE_TYPES.replace("pub enum InputModality {\n    Text,\n}",
                            "pub enum InputModality {\n    Text,\n    Image,\n}"),
        _FAKE_GENERATOR)
    if not any("InputModality" in f for f in dropped):
        failures.append(f"a kernel-only variant was not caught; got {dropped}")

    # A source that cannot be read must raise rather than compare nothing: an
    # empty set equals an empty set, so a silent misread passes as agreement.
    for source, generator, what in (
        ("", _FAKE_GENERATOR, "an unreadable types.rs"),
        (_FAKE_TYPES, "", "an unreadable generator"),
    ):
        try:
            compare(source, generator)
        except SystemExit:
            pass
        else:
            failures.append(f"{what} compared instead of raising")

    for failure in failures:
        print(f"  {failure}")
    if failures:
        return 1
    print("schema agreement: the comparison fires on a known break and clears a fix")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--self-test", action="store_true", help="prove the comparison can fail"
    )
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()

    root = repository_root()
    with open(os.path.join(root, TYPES_RS), encoding="utf-8") as handle:
        types_source = handle.read()
    with open(os.path.join(root, GENERATOR), encoding="utf-8") as handle:
        generator_source = handle.read()

    findings = compare(types_source, generator_source)
    if findings:
        for finding in findings:
            print(f"  {finding}")
        return 1
    print(
        "schema agreement: the baseline generator projects the kernel's catalog "
        "vocabulary exactly"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
