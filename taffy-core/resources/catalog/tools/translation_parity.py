#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""C4: a resource root that carries a translation carries a complete one.

Split out of `check_strings.py` because it is a separate question from the
rest of that gate. Every other rule there asks whether a *string* is
well-formed — externalized, named correctly, surviving a pseudo-locale round
trip — and reads one file at a time. This one asks whether a *set of files*
still agrees with itself, which needs the resource tree rather than the
message list and is the only rule with an opinion about directory qualifiers.

The hazard it exists for: an Android resource change that satisfies aapt2 by
losing translations. aapt2 is happy with a `values-hi` that declares fewer
names than `values` — the string simply falls back to English on a Hindi
device — and every count `check_strings.py` prints stays internally consistent,
because before this rule there was no baseline to compare a count against.

Read-only, stdlib only.
"""

from __future__ import annotations

import os
import re

import string_catalogue as catalogue

# A locale-qualified resource directory, and only a locale. `values-<lang>`
# with an optional region (`values-en-rUS`) or a BCP-47 tag (`values-b+sr+Latn`)
# is a translation; `values-night`, `values-land`, `values-v31`, `values-xhdpi`
# are configurations of the *same* language and owe nothing to C4. Getting this
# wrong is not a missed finding but a false one — C4 would demand that
# `values-night/colors.xml` carry every translatable string in the module.
LOCALE_CONFIGURATION = re.compile(
    r"^values-(?:b\+[A-Za-z0-9]+(?:\+[A-Za-z0-9]+)*|[a-z]{2,3}(?:-r[A-Z]{2})?)$"
)

# Qualifiers short enough that the pattern above would read them as a language
# code. `car` is a real Android UI-mode qualifier and a real collision; `tv` is
# not a current one but is the spelling people reach for, and a directory named
# for something that is not a language should never be asked for translations.
# Android's qualifier table is the authority here, not this list — extend it if
# a new short qualifier appears.
NOT_A_LOCALE = frozenset({"car", "tv"})


def is_locale(configuration: str) -> bool:
    """True for `values-hi`, false for `values` and for every non-locale qualifier."""
    if not LOCALE_CONFIGURATION.match(configuration):
        return False
    return configuration.split("-", 1)[1] not in NOT_A_LOCALE


# C4 fault injections, each the shape of a real regression, with the number of
# findings the rule must produce. A rule that cannot fail is not a gate, and
# the count matters as much as the verdict: `dropped-and-dead` must report the
# loss and the orphan separately rather than netting them out to one.
SELF_TEST_CASES: list[tuple[str, dict[str, dict[str, dict[str, bool]]], int]] = [
    (
        "complete",
        {"/root": {"values": {"a": True, "b": True}, "values-hi": {"a": True, "b": True}}},
        0,
    ),
    (
        "untranslated-root",
        {"/root": {"values": {"a": True, "b": True}}},
        0,
    ),
    (
        "untranslatable-is-not-owed-a-translation",
        {"/root": {"values": {"a": True, "b": False}, "values-hi": {"a": True}}},
        0,
    ),
    (
        "dropped-translation",
        {"/root": {"values": {"a": True, "b": True}, "values-hi": {"a": True}}},
        1,
    ),
    (
        "orphaned-translation",
        {"/root": {"values": {"a": True}, "values-hi": {"a": True, "gone": True}}},
        1,
    ),
    (
        "dropped-and-dead",
        {"/root": {"values": {"a": True, "b": True}, "values-hi": {"a": True, "x": True}}},
        2,
    ),
    (
        "translated-untranslatable",
        {"/root": {"values": {"a": False}, "values-hi": {"a": True}}},
        1,
    ),
    (
        "locale-without-default",
        {"/root": {"values-hi": {"a": True}}},
        1,
    ),
    (
        "night-is-not-a-locale",
        {"/root": {"values": {"a": True}, "values-night": {}}},
        0,
    ),
    (
        "region-qualified-locale-is-a-locale",
        {"/root": {"values": {"a": True}, "values-en-rUS": {}}},
        1,
    ),
    (
        "bcp47-locale-is-a-locale",
        {"/root": {"values": {"a": True}, "values-b+sr+Latn": {}}},
        1,
    ),
    (
        "emptied-locale-file-is-still-a-locale",
        {"/root": {"values": {"a": True, "b": True}, "values-hi": {}}},
        1,
    ),
    (
        "each-locale-judged-separately",
        {
            "/root": {
                "values": {"a": True, "b": True},
                "values-hi": {"a": True},
                "values-fr": {"a": True},
            }
        },
        2,
    ),
]


def index(
    paths: list[str],
) -> tuple[dict[str, dict[str, dict[str, bool]]], list[tuple[str, str]]]:
    """Android resource files as root -> configuration -> name -> translatable.

    C4 reads the files rather than reusing the `Message` list every other rule
    works from, and the reason is that `Message.source` is a *display* path —
    `string_catalogue.relative` renders it for a human and falls back to an
    absolute path for anything outside the overlay. Keying an index on it
    silently splits one resource root into two whenever a tree is reached by
    two names, which is the normal case here: the GN projection mounts an
    Android UI module by symlink, so every mounted module has both a generated
    mount path and its source path. Resolving each file to its real path is
    what makes the mounted module and its original the same root, counted once.

    A file that parses to no messages still seeds its configuration. Building
    the index from messages alone makes the emptiest possible regression — a
    `values-hi` whose every string was deleted — invisible, because a file with
    no messages contributes no key and C4 then has no configuration to find
    anything missing from. The file list is the difference between "this locale
    is missing one string" and "this locale silently stopped existing".

    Grit catalogues carry no configuration and never reach here: a `.grd` is
    translated by the translation console against `.xtb` files this repository
    does not hold, so parity there is not a fact on disk.

    Returns the index and the files that would not parse. Unreadable files are
    returned rather than reported so this module stays free of the caller's
    reporting type, which is what keeps it importable — and self-testable —
    on its own.
    """
    result: dict[str, dict[str, dict[str, bool]]] = {}
    unreadable: list[tuple[str, str]] = []
    for given in paths:
        path = os.path.realpath(given)
        configuration = catalogue.configuration(path)
        if configuration != "values" and not is_locale(configuration):
            continue
        declared = result.setdefault(catalogue.resource_set(path), {}).setdefault(
            configuration, {}
        )
        messages, error = catalogue.read_android_strings(path)
        if error:
            unreadable.append(
                (catalogue.relative(given), f"is not well-formed XML: {error}")
            )
            continue
        for message in messages:
            if message.name:
                declared[message.name] = message.translatable
    return result, unreadable


def findings(
    index: dict[str, dict[str, dict[str, bool]]],
) -> list[tuple[str, str]]:
    """C4, as a function over the index so it can be self-tested.

    A dropped translation is the exact hazard an aapt2 resource fix can cause
    and no other gate sees: `values-hi` simply declares fewer names, the string
    silently falls back to English on a Hindi device, and every count this
    script prints is still internally consistent because it has no baseline to
    compare against. This rule is that baseline.

    Directional, both ways:

    - a translatable name in `values` and not in `values-<locale>` is a
      translation that was lost or never written;
    - a name in `values-<locale>` and not in `values` is a translation of a
      resource that no longer exists — dead weight aapt2 will not flag;
    - a name marked `translatable="false"` in `values` and translated anyway
      contradicts its own declaration.
    """
    def listed(items: list[str]) -> str:
        """A readable list. A whole emptied locale is 85 names; 10 is enough
        to identify it and the count carries the rest."""
        head = ", ".join(items[:10])
        return head if len(items) <= 10 else f"{head}, and {len(items) - 10} more"

    found: list[tuple[str, str]] = []
    for root in sorted(index):
        configurations = index[root]
        default = configurations.get("values")
        localized = sorted(c for c in configurations if is_locale(c))
        if not localized:
            continue
        where_root = catalogue.relative(root)
        if default is None:
            for configuration in localized:
                found.append(
                    (
                        f"{where_root}/{configuration}",
                        "translates a resource root that has no default values/ — "
                        "every name here resolves to nothing on any other locale",
                    )
                )
            continue
        for configuration in localized:
            names = configurations[configuration]
            where = f"{where_root}/{configuration}"
            missing = sorted(n for n, t in default.items() if t and n not in names)
            if missing:
                found.append(
                    (
                        where,
                        f"is missing {len(missing)} translation(s) declared in "
                        f"values/: {listed(missing)}",
                    )
                )
            extra = sorted(n for n in names if n not in default)
            if extra:
                found.append(
                    (
                        where,
                        f"declares {len(extra)} name(s) that values/ does not: "
                        f"{listed(extra)}",
                    )
                )
            untranslatable = sorted(
                n for n in names if n in default and not default[n]
            )
            if untranslatable:
                found.append(
                    (
                        where,
                        "translates name(s) values/ marks translatable=\"false\": "
                        f"{listed(untranslatable)}",
                    )
                )
    return found


# Gradle writes merged and packaged copies of every resource under `build/`.
# They are outputs, they duplicate their own inputs, and a parity finding in
# one is a finding about the last build rather than about the tree.
GENERATED_TREE = os.sep + "build" + os.sep


def parity_root_files(root: str) -> list[str]:
    """Android resource files under an additional source tree.

    The Android UI source tree is authoritative. C4 reads it directly so
    translation coverage never depends on whether a GN projection currently
    mounts a module. A mounted module costs nothing to name twice: both names
    resolve to one real path and one root.
    """
    return [p for p in catalogue.android_resource_files(root) if GENERATED_TREE not in p]


def tree_findings(
    resources: dict[str, dict[str, dict[str, bool]]],
) -> list[tuple[str, str]]:
    """C4, whole-file half: a locale that vanished from a root entirely.

    `findings` compares two files and cannot see a deletion, because a root
    whose `values-hi` was deleted looks exactly like a root that was never
    translated. Something has to supply the expectation, and the cheapest
    honest source is the tree itself: if any root here carries a locale, the
    tree ships that locale, and every root here that declares a translatable
    string owes it one. No policy file, nothing to keep in sync, and the
    expectation strengthens on its own as translations are added.

    This is why it runs per `--parity-root` rather than over everything at
    once. The Android UI tree is uniformly translated, so the rule holds there;
    the overlay also contains roots whose strings are bound for grit and have
    no `values-<locale>` by design, and pooling the two trees would demand a
    translation of those. A tree is the unit that has one answer.
    """
    shipped = sorted(
        {c for configurations in resources.values() for c in configurations if is_locale(c)}
    )
    if not shipped:
        return []
    found: list[tuple[str, str]] = []
    for root in sorted(resources):
        configurations = resources[root]
        if not any(t for t in configurations.get("values", {}).values()):
            continue
        absent = [c for c in shipped if c not in configurations]
        if absent:
            found.append(
                (
                    catalogue.relative(root),
                    f"declares translatable strings but has no {', '.join(absent)} — "
                    "every other translated root in this tree carries it, so this is "
                    "a locale that was deleted or never written, not one nobody ships",
                )
            )
    return found


def translated_scale(
    resources: dict[str, dict[str, dict[str, bool]]],
) -> tuple[int, int]:
    """(roots carrying a translation, localized files among them).

    Printed by the gate so the reader can see what C4 actually covered. A run
    that says "0 localized file(s)" is the shape of a broken scan, not of a
    clean tree, and that is only visible if the number is printed.
    """
    translated = {
        root: [c for c in configurations if is_locale(c)]
        for root, configurations in resources.items()
    }
    translated = {root: c for root, c in translated.items() if c}
    return len(translated), sum(len(c) for c in translated.values())


#: Fault injections for `tree_findings`, in the same shape as SELF_TEST_CASES.
TREE_SELF_TEST_CASES: list[tuple[str, dict[str, dict[str, dict[str, bool]]], int]] = [
    (
        "uniformly-translated-tree",
        {
            "/a": {"values": {"x": True}, "values-hi": {"x": True}},
            "/b": {"values": {"y": True}, "values-hi": {"y": True}},
        },
        0,
    ),
    (
        "untranslated-tree-owes-nothing",
        {"/a": {"values": {"x": True}}, "/b": {"values": {"y": True}}},
        0,
    ),
    (
        "deleted-locale-file",
        {
            "/a": {"values": {"x": True}, "values-hi": {"x": True}},
            "/b": {"values": {"y": True}},
        },
        1,
    ),
    (
        "root-with-nothing-translatable-owes-nothing",
        {
            "/a": {"values": {"x": True}, "values-hi": {"x": True}},
            "/b": {"values": {"name": False}},
        },
        0,
    ),
    (
        "two-locales-one-missing",
        {
            "/a": {"values": {"x": True}, "values-hi": {}, "values-fr": {}},
            "/b": {"values": {"y": True}, "values-hi": {}},
        },
        1,
    ),
]
