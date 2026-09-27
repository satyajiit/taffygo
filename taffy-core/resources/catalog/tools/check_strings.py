#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The PAR-L10N-001 gate: every TaffyGo string externalized and pseudo-tested.

PAR-L10N-001 is an M1 **Required** row in the browser parity matrix, and its
acceptance statement makes two claims: all strings externalized, and
pseudo-localization tested. This script is what makes both claims checkable on
any host — no Chromium checkout, no grit, no Android SDK.

It reads three things, through `string_catalogue`, and applies one set of
rules to all of them:

  1. the grit catalogues under `resources/` — the `.grd` roots and the
     `.grdp` parts they carry;
  2. every Android `values*/**.xml` string resource anywhere in the TaffyGo
     overlay, so a string that lives beside the code that shows it is held to
     the same standard as one in the catalogue;
  3. every Kotlin and Java source in the overlay, scanned for a string literal
     handed to a surface a person reads.

The rules, by the identifier each finding is reported under:

  E1  no user-visible string literal in Kotlin or Java source
  C1  every `.grdp` part is carried by exactly one `.grd`, and every part a
      `.grd` references exists
  C2  every message has a well-formed name, a description where a translator
      needs one, and non-empty text
  C3  no message name is defined twice inside one resource namespace
  C4  every Android resource root that carries a translation carries a
      complete one: the localized `values-<locale>` declares each translatable
      name the default `values` declares, and declares nothing else
  P1  the accented pseudo-locale round-trips back to the source exactly
  P2  the bidi pseudo-locale round-trips back to the source exactly
  P3  every placeholder survives both transforms untouched
  P4  no Latin letter in a message falls outside the accent map

Run:
  python3 taffy-core/resources/catalog/tools/check_strings.py

Options:
  --self-test          check the pseudo-localization transforms and the
                       translation-parity rule, then stop
  --overlay-root DIR   check a different overlay tree
  --parity-root DIR    also hold DIR's Android resources to C4, without
                       scanning its sources; repeatable. The Compose product
                       resource roots live here
  --verbose            print the pseudo-localized form of every message

Read-only, stdlib only. Exit status: 0 clean, 1 on any finding.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pseudolocale  # noqa: E402
import string_catalogue as catalogue  # noqa: E402
import translation_parity as parity  # noqa: E402

# Message ids in the grit catalogues. The prefix keeps a TaffyGo resource
# distinguishable from an upstream one in a shared generated header.
GRIT_NAME = re.compile(r"^IDS_TAFFY_[A-Z0-9_]+$")

# Android resource names are lower_snake_case by platform convention.
ANDROID_NAME = re.compile(r"^[a-z][a-z0-9_]*$")

# Surfaces a person reads. A literal reaching any of these is a string that was
# never externalized, which is precisely what PAR-L10N-001 forbids.
LITERAL_SINKS = [
    ("compose-text", re.compile(r"\bText\s*\(\s*\"")),
    ("content-description", re.compile(r"\bcontentDescription\s*=\s*\"")),
    ("state-description", re.compile(r"\bstateDescription\s*=\s*\"")),
    ("view-set-text", re.compile(r"\.setText\s*\(\s*\"")),
    ("view-set-content-description", re.compile(r"\.setContentDescription\s*\(\s*\"")),
    ("view-set-title", re.compile(r"\.setTitle\s*\(\s*\"")),
]

# Inputs chosen because each one breaks a naive implementation of the
# transforms: a bare placeholder, positional arguments, a message that is only
# a placeholder, a message with no letters at all, and one short enough that
# the expansion padding dominates it.
SELF_TEST_STRINGS = [
    "Default browser",
    "Ok",
    "Taffy cannot start downloads. Download the file yourself if you want it.",
    '<ph name="COUNT">$1<ex>3</ex></ph> downloads stopped',
    "Saved %1$s of %2$s",
    "$1",
    "100%",
    "—",
    "Android did not answer. Try again.",
]



class Report:
    def __init__(self) -> None:
        self.items: list[tuple[str, str, str]] = []

    def add(self, rule: str, where: str, message: str) -> None:
        self.items.append((rule, where, message))

    def __len__(self) -> int:
        return len(self.items)


# --- self-test --------------------------------------------------------------


def self_test(report: Report) -> None:
    """Prove the transforms are invertible before trusting them on a catalogue.

    A round-trip check over the catalogue only proves the catalogue is clean if
    the transform is doing something in the first place. These cases assert
    both halves: the output differs from the input, and stripping recovers the
    input exactly.
    """
    for source in SELF_TEST_STRINGS:
        expected = pseudolocale.placeholders(source)

        accented = pseudolocale.accented(source)
        if accented == source:
            report.add("SELF", repr(source), "the accented transform changed nothing")
        restored = pseudolocale.strip_accented(accented)
        if restored != source:
            report.add("SELF", repr(source), f"accented round trip returned {restored!r}")
        if pseudolocale.placeholders(accented) != expected:
            report.add("SELF", repr(source), "accented transform altered a placeholder")

        bidi = pseudolocale.bidi(source)
        if pseudolocale.strip_bidi(bidi) != source:
            report.add("SELF", repr(source), "bidi round trip did not restore the source")
        if pseudolocale.placeholders(bidi) != expected:
            report.add("SELF", repr(source), "bidi transform altered a placeholder")

    # The accent map must be a bijection: two letters sharing an accented form
    # would make `strip_accented` guess, and a round trip that guesses proves
    # nothing.
    if len(set(pseudolocale.ACCENT_MAP.values())) != len(pseudolocale.ACCENT_MAP):
        report.add("SELF", "ACCENT_MAP", "two letters share an accented form")
    overlap = set(pseudolocale.ACCENT_MAP.values()) & set(pseudolocale.PAD_ALPHABET)
    if overlap:
        report.add(
            "SELF",
            "PAD_ALPHABET",
            f"shares characters with the accent map ({''.join(sorted(overlap))}), so "
            "padding cannot be told apart from transformed text",
        )

    for name, index, expected in parity.SELF_TEST_CASES:
        found = len(parity.findings(index))
        if found != expected:
            report.add(
                "SELF",
                f"C4/{name}",
                f"the parity rule reported {found} finding(s), expected {expected}",
            )

    for name, index, expected in parity.TREE_SELF_TEST_CASES:
        found = len(parity.tree_findings(index))
        if found != expected:
            report.add(
                "SELF",
                f"C4-tree/{name}",
                f"the deleted-locale rule reported {found} finding(s), expected "
                f"{expected}",
            )


# --- rules ------------------------------------------------------------------


def check_grit_structure(report: Report) -> list[catalogue.Message]:
    """C1: parts and roots agree. Returns every grit message found."""
    roots = catalogue.grit_roots()
    parts = catalogue.grit_parts()

    carried: dict[str, list[str]] = {}
    for root_path in roots:
        references, error = catalogue.parts_referenced_by(root_path)
        if error:
            report.add("C1", catalogue.relative(root_path), f"is not well-formed XML: {error}")
            continue
        for reference, resolved in references:
            if not os.path.exists(resolved):
                report.add(
                    "C1",
                    catalogue.relative(root_path),
                    f'part file "{reference}" does not exist',
                )
                continue
            carried.setdefault(resolved, []).append(catalogue.relative(root_path))

    for part_path in parts:
        holders = carried.get(part_path, [])
        if not holders:
            report.add(
                "C1",
                catalogue.relative(part_path),
                "is carried by no .grd — an uncarried part is never built and never "
                "translated",
            )
        elif len(holders) > 1:
            report.add(
                "C1",
                catalogue.relative(part_path),
                f"is carried by more than one .grd ({', '.join(holders)}); every "
                "message in it would be defined twice",
            )

    messages: list[catalogue.Message] = []
    for path in roots + parts:
        found, error = catalogue.read_grit_messages(path)
        if error:
            report.add("C1", catalogue.relative(path), f"is not well-formed XML: {error}")
        messages.extend(found)
    return messages


def check_message_shape(
    messages: list[catalogue.Message], report: Report, grit: bool
) -> None:
    """C2: names, descriptions and text."""
    for message in messages:
        where = f"{message.source}:{message.name or '<unnamed>'}"
        if not message.name:
            report.add("C2", message.source, "a message has no name attribute")
        elif grit and not GRIT_NAME.match(message.name):
            report.add(
                "C2",
                where,
                "name must match IDS_TAFFY_[A-Z0-9_]+ so a TaffyGo resource stays "
                "distinguishable from an upstream one",
            )
        elif not grit and not ANDROID_NAME.match(message.name):
            report.add("C2", where, "Android resource names are lower_snake_case")
        if not message.text:
            report.add("C2", where, "has no text")
        if grit and not message.description:
            report.add(
                "C2",
                where,
                "has no desc — a translator sees the description and nothing else",
            )


def check_uniqueness(messages: list[catalogue.Message], report: Report) -> list[str]:
    """C3: one definition per name per namespace. Returns the intended overrides."""
    seen: dict[tuple[str, str, str], str] = {}
    by_name: dict[str, list[str]] = {}
    roots_by_name: dict[str, set[str]] = {}
    for message in messages:
        if not message.name:
            continue
        root = catalogue.resource_set(message.source)
        key = (root, catalogue.configuration(message.source), message.name)
        if key in seen:
            report.add(
                "C3",
                f"{message.source}:{message.name}",
                f"is already defined in {seen[key]}",
            )
        else:
            seen[key] = message.source
        by_name.setdefault(message.name, []).append(message.source)
        roots_by_name.setdefault(message.name, set()).add(root)

    # Only distinct resource *roots* are an override. Two definitions in one
    # file are the C3 finding above, and listing them here as well would read
    # as approval; two configurations of one root are a translation, and
    # reporting `values-hi` as overriding `values` would be plainly wrong.
    return [
        f"{name}: {' overridden by '.join(sorted(set(sources)))}"
        for name, sources in sorted(by_name.items())
        if len(set(sources)) > 1 and len(roots_by_name[name]) > 1
    ]


def check_translation_parity(
    paths: list[str], trees: list[list[str]], report: Report
) -> tuple[int, int]:
    """C4 over the real catalogue. Returns (roots translated, locale files).

    `paths` is every resource file, compared file-against-file. `trees` is one
    file list per `--parity-root`, each judged on its own for a locale that
    disappeared from a root outright — an expectation only a whole tree can
    supply. See `translation_parity.tree_findings`.
    """
    resources, unreadable = parity.index(paths)
    for where, message in unreadable:
        report.add("C4", where, message)
    for where, message in parity.findings(resources):
        report.add("C4", where, message)
    for tree in trees:
        tree_resources, _ = parity.index(tree)
        for where, message in parity.tree_findings(tree_resources):
            report.add("C4", where, message)
    return parity.translated_scale(resources)


def check_pseudolocalization(
    messages: list[catalogue.Message], report: Report, verbose: bool
) -> None:
    """P1-P4: both transforms round-trip and leave placeholders alone."""
    for message in messages:
        if not message.text or not message.translatable:
            continue
        where = f"{message.source}:{message.name}"
        expected = pseudolocale.placeholders(message.text)

        try:
            accented = pseudolocale.accented(message.text)
            restored = pseudolocale.strip_accented(accented)
        except pseudolocale.PseudolocaleError as error:
            report.add("P1", where, f"accented transform failed: {error}")
            continue
        if restored != message.text:
            report.add(
                "P1",
                where,
                "does not survive the accented pseudo-locale round trip\n"
                f"      source:   {message.text!r}\n"
                f"      restored: {restored!r}",
            )
        if pseudolocale.placeholders(accented) != expected:
            report.add("P3", where, "the accented pseudo-locale changed a placeholder")

        bidi = pseudolocale.bidi(message.text)
        if pseudolocale.strip_bidi(bidi) != message.text:
            report.add("P2", where, "does not survive the bidi round trip")
        if pseudolocale.placeholders(bidi) != expected:
            report.add("P3", where, "the bidi pseudo-locale changed a placeholder")

        missing = pseudolocale.unmapped_letters(message.text)
        if missing:
            report.add(
                "P4",
                where,
                "contains letters the accent map does not cover: "
                f"{''.join(sorted(missing))} — an unaccented letter on screen reads "
                "as a string that was never externalized",
            )

        if verbose:
            print(f"  {where}\n    {accented}\n    {bidi}")


def check_hardcoded_literals(overlay_root: str, report: Report) -> int:
    """E1: no user-visible string literal in Kotlin or Java source."""
    sources = catalogue.source_files(overlay_root)
    for path in sources:
        with open(path, encoding="utf-8") as handle:
            for number, line in enumerate(handle, start=1):
                stripped = line.lstrip()
                if stripped.startswith("//") or stripped.startswith("*"):
                    continue
                for rule, pattern in LITERAL_SINKS:
                    if pattern.search(line):
                        report.add(
                            "E1",
                            f"{catalogue.relative(path)}:{number}",
                            f"{rule}: a string literal reaches a surface a person "
                            "reads. Add it to a catalogue under "
                            "//taffy/resources/catalog and reference it.",
                        )
    return len(sources)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--overlay-root", default=catalogue.OVERLAY_ROOT)
    parser.add_argument(
        "--parity-root",
        action="append",
        default=[],
        metavar="DIR",
        help="also hold this tree's Android resources to C4, without scanning "
        "its sources; repeatable. For modules decision 0024 has not mounted yet",
    )
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="check the pseudo-localization transforms and nothing else",
    )
    args = parser.parse_args(argv)

    report = Report()
    self_test(report)

    if args.self_test:
        for rule, where, message in report.items:
            print(f"{where}: {rule}: {message}")
        print()
        print(
            f"self-tested {len(SELF_TEST_STRINGS)} transform case(s) and "
            f"{len(parity.SELF_TEST_CASES) + len(parity.TREE_SELF_TEST_CASES)} "
            f"translation-parity case(s): "
            f"{len(report)} finding(s)"
        )
        return 1 if len(report) else 0

    grit_messages = check_grit_structure(report)
    check_message_shape(grit_messages, report, grit=True)

    android_paths = catalogue.android_resource_files(args.overlay_root)
    android_messages: list[catalogue.Message] = []
    for path in android_paths:
        found, error = catalogue.read_android_strings(path)
        if error:
            report.add("C1", catalogue.relative(path), f"is not well-formed XML: {error}")
        android_messages.extend(found)
    check_message_shape(android_messages, report, grit=False)

    parity_paths = list(android_paths)
    parity_trees: list[list[str]] = []
    for root in args.parity_root:
        if not os.path.isdir(root):
            report.add("C4", root, "--parity-root names a directory that does not exist")
            continue
        tree = parity.parity_root_files(root)
        parity_paths.extend(tree)
        parity_trees.append(tree)
    translated_roots, locale_files = check_translation_parity(
        parity_paths, parity_trees, report
    )

    every_message = grit_messages + android_messages
    overrides = check_uniqueness(every_message, report)
    if overrides:
        print("resource overrides (a later resource root wins, by design):")
        for line in overrides:
            print(f"  {line}")
        print()
    if args.verbose:
        print("pseudo-localized forms:")
    check_pseudolocalization(every_message, report, args.verbose)

    scanned_sources = check_hardcoded_literals(args.overlay_root, report)

    for rule, where, message in sorted(report.items):
        print(f"{where}: {rule}: {message}")

    print()
    print(
        f"checked {len(grit_messages)} catalogue message(s) in "
        f"{len({m.source for m in grit_messages})} grit file(s), "
        f"{len(android_messages)} Android string resource(s) in "
        f"{len(android_paths)} file(s) "
        f"({locale_files} localized file(s) across {translated_roots} "
        f"translated resource root(s), checked for parity), and "
        f"{scanned_sources} Kotlin/Java source(s): {len(report)} finding(s)"
    )
    return 1 if len(report) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
