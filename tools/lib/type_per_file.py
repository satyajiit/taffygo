#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""One public type per file, for every Kotlin and Java source in the tree.

Authority boundary: what a file is allowed to *declare*. It says nothing about
how long a file is (`file_discipline.py`) or what it may depend on
(`module_graph.py`, `include_direction.py`).

Owning milestone: M0 (WP-M0-09). The rule is
`docs/architecture/android-app-architecture.md` section 5.

Why it exists next to the Gradle task. `build-logic`'s `checkFileDiscipline`
already applies this rule, but only to the Compose modules Gradle builds.
The Chromium shell's Java under `taffy-core/app/android` is built
by GN and is never seen by that task, so without this lane the rule stops at
the boundary between the two builds. Same rule, both sides, no toolchain
needed.

What counts as a public type: a top-level `class`, `interface`, `object`, or
`enum class` that is not `internal` or `private`. Three things deliberately do
not count, because none of them is a second thing a reader has to hold in their
head:

  - a nested type, which takes its meaning from the type that encloses it;
  - a `private`/`internal` type, which is implementation of the public one;
  - a Kotlin file with no public type at all — a file of extension functions
    is a legitimate shape and the rule is not "exactly one".

The file name must also match the public type it declares. A `UserRepository`
in `Repo.kt` is findable only by someone who already knows where it is.

ONE NAME, ONE FILE — BETWEEN FILES AS WELL AS INSIDE ONE

The rule above is about a single file. The second rule here is about two, and
it is the one no compiler will find for you: two files, in two directories,
declaring the same `package` and the same type name. Both compile. Both are
packaged. The runtime keeps whichever the classpath reaches first, and the
other declaration is simply not there — no error, no warning, nothing missing
from the build log.

The story that put this rule here is C++ rather than Kotlin, which is why it
took a person and not a build to find it. Seven types under the downstream
`renderer/` and `renderer/adapters/` carried names that were already declared
elsewhere in `//taffy`. Nothing complained, because the DEPS rules
that keep the renderer out of the browser half also mean no translation unit
ever held both declarations: the two copies only ever meet in the linked
artifact. They were found and fixed by hand in one review, and until this
check there was nothing standing between the tree and an eighth.

Kotlin has exactly that shape and one extra way into it. `taffy-core/ui/android`
and `taffy-core/app/android` are two trees that both declare
`com.taffygo.browser.ui.*`, and decision 0024's Kotlin mount compiles files from
the first into the same APK as the second. A binding declared in both places
does not fail that build. It produces a green build with one of the two
implementations silently gone, which is the worst kind of green.

THE LEGITIMATE DUPLICATES, AND WHERE THEIR PERMISSION COMES FROM

Some duplicates are deliberate: the Chromium shell declares its own version of
an Android UI type, and GN compiles that one *instead of* the UI source. Those
two files are never in one APK, so they are not the failure above.

The permission for that is not a list in this file. It is a computed fact owned
by `taffy-core/app/android/tools/kotlin_mounts.py`: which
Android UI files GN actually compiles. This module imports that contract and
asks it, the way the module-layer checkers read the component manifest's
generated `android-modules.tsv` projection rather than restating it — a
hand-copied second allowlist is the same defect class as the collision it would
be guarding.

Asking the real authority matters more here than it looks. The obvious place to
reach for is `REPLACED_FILES`, and for the duplicates that exist today it is the
wrong place: the UI's `@Preview` annotation files in `core/ui` are kept out
of the fork's source list by the preview rule, not by `REPLACED_FILES`, which
does not name them at all. `excluded_files()` is where all four exclusion
mechanisms — replaced, preview-only, annotation-processed, unvendored — and the
closure over them come together, so that is what is consulted. Key the exception
off any one of the four by hand and the gate is either blind or unusable.

A duplicate is therefore allowed only in one exact shape: one shell file, one
UI file, and GN does not compile the UI one. Two shell files, two UI files, or
a UI file GN *does* compile are all the real
failure, and are reported.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

import source_index

#: A top-level type declaration. Anchored, because an indented declaration is a
#: member of the type that encloses it and takes its visibility from it.
_KOTLIN_TYPE = re.compile(
    r"^(?P<modifiers>(?:public\s+)?(?:(?:abstract|final|open|sealed|data|value|inline|"
    r"annotation|enum|fun|expect|actual|external)\s+)*)"
    r"(?P<keyword>class|interface|object)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
)
_JAVA_TYPE = re.compile(
    r"^(?:public\s+)(?:(?:abstract|final|static|sealed|non-sealed)\s+)*"
    r"(?:class|interface|enum|record|@interface)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
)

#: Visibility keywords that make a declaration implementation detail.
_HIDDEN = ("private ", "internal ")

#: The same two declarations again, with visibility allowed rather than
#: required. The collision rule needs these because **visibility is not the
#: question a class name answers**: `internal` is a Kotlin compile-time
#: visibility and a package-private Java class is a compile-time visibility, but
#: both become an ordinary entry in the package at run time. Two of them with
#: one name meet in the artifact exactly as two public ones would.
_KOTLIN_ANY_TYPE = re.compile(
    r"^(?:(?:public|internal|private)\s+)?"
    r"(?:(?:abstract|final|open|sealed|data|value|inline|"
    r"annotation|enum|fun|expect|actual|external)\s+)*"
    r"(?:class|interface|object)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
)
_JAVA_ANY_TYPE = re.compile(
    r"^(?:(?:public|abstract|final|static|sealed|non-sealed)\s+)*"
    r"(?:class|interface|enum|record|@interface)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
)

#: The package a file declares. Anchored at the start of a line and taken from
#: the first match only, so a `package` word later in the file cannot move it.
_PACKAGE = re.compile(r"^package\s+(?P<name>[A-Za-z_][A-Za-z0-9_.]*)")

#: Where the Kotlin mount contract lives. This module imports it rather than
#: restating any part of it; see the header for why that is not optional.
_MOUNT_TOOLS = "taffy-core/app/android/tools"

#: The two trees that both declare `com.taffygo.browser.ui.*`, and so the two
#: halves a deliberate replacement is made of.
_ANDROID_UI_ROOT = "taffy-core/ui/android/"
_PRODUCT_ANDROID_ROOT = "taffy-core/app/android/"


def public_types(path: str) -> list[str]:
    """Every top-level public type name declared in `path`."""
    extension = os.path.splitext(path)[1]
    names: list[str] = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            if extension in (".kt", ".kts"):
                if raw.startswith(_HIDDEN):
                    continue
                match = _KOTLIN_TYPE.match(raw)
                if match:
                    # `companion object` and `object : Foo` are not top level
                    # declarations of a named type in the sense this rule means.
                    names.append(match.group("name"))
            elif extension == ".java":
                match = _JAVA_TYPE.match(raw)
                if match:
                    names.append(match.group("name"))
    return names


def declared_types(path: str) -> tuple[str, list[str]]:
    """The package `path` declares, and every top-level type name in it.

    Unlike `public_types` this counts `internal` and `private` declarations,
    because the question it serves is which names the artifact ends up holding
    rather than which names a reader has to keep in their head.
    """
    extension = os.path.splitext(path)[1]
    package = ""
    names: list[str] = []
    with open(path, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            if not package:
                found = _PACKAGE.match(raw)
                if found:
                    package = found.group("name")
                    continue
            if extension in (".kt", ".kts"):
                match = _KOTLIN_ANY_TYPE.match(raw)
            elif extension == ".java":
                match = _JAVA_ANY_TYPE.match(raw)
            else:
                continue
            if match:
                names.append(match.group("name"))
    return package, names


def fork_compiled_android_ui_sources(repo_root: str) -> set[str]:
    """Android UI files GN compiles, repository-relative, from the contract.

    This is `kotlin_mounts.py`'s answer, not a second copy of it: the mounted
    modules, minus whatever `excluded_files()` computes from the tree for each
    of them. Raising rather than returning an empty set on failure is
    deliberate — an unreadable contract must not read as "nothing is excluded",
    which would quietly turn every deliberate replacement into a finding, nor as
    "everything is excluded", which would silence real ones.
    """
    tools_dir = os.path.join(repo_root, *_MOUNT_TOOLS.split("/"))
    sys.path.insert(0, tools_dir)
    try:
        import kotlin_mounts
    finally:
        if tools_dir in sys.path:
            sys.path.remove(tools_dir)

    compiled: set[str] = set()
    for module in kotlin_mounts.MOUNTED_MODULES:
        package = kotlin_mounts.MODULES[module]
        directory = kotlin_mounts.module_source_dir(repo_root, module, package)
        excluded = set(
            kotlin_mounts.excluded_files(
                directory,
                dagger=module in kotlin_mounts.DAGGER_MODULES,
                replaced=kotlin_mounts.REPLACED_FILES.get(module, {}),
            )
        )
        for relative in kotlin_mounts.kotlin_files(directory):
            if relative in excluded:
                continue
            full = os.path.join(directory, relative)
            compiled.add(os.path.relpath(full, repo_root).replace(os.sep, "/"))
    return compiled


def _is_mount_replacement(paths: list[str], fork_compiled: set[str]) -> bool:
    """True for the one duplicate shape that is a replacement, not a collision.

    One shell file, one Android UI file, and GN does not compile the
    UI one — so the UI copy is in the Gradle APK, the shell copy is
    in the browser APK, and no artifact ever holds both.
    """
    if len(paths) != 2:
        return False
    product = [path for path in paths if path.startswith(_PRODUCT_ANDROID_ROOT)]
    android_ui = [path for path in paths if path.startswith(_ANDROID_UI_ROOT)]
    if len(product) != 1 or len(android_ui) != 1:
        return False
    return android_ui[0] not in fork_compiled


def duplicate_names(repo_root: str) -> list[str]:
    """Every fully qualified name declared by more than one file, in name order.

    Generated sources are included rather than skipped, which is the opposite of
    what the per-file rules do and is right for this question: a generated type
    occupies its name in the artifact like any other, so a hand-written file
    colliding with one is a real collision. The fix is to rename the
    hand-written side, which is a thing a person can do.
    """
    declared: dict[str, list[str]] = {}
    for relative in source_index.source_files(repo_root):
        if not relative.endswith((".kt", ".java")):
            continue
        package, names = declared_types(os.path.join(repo_root, relative))
        for name in dict.fromkeys(names):
            qualified = f"{package}.{name}" if package else name
            declared.setdefault(qualified, []).append(relative)

    collisions = {
        qualified: paths for qualified, paths in declared.items() if len(paths) > 1
    }
    if not collisions:
        return []

    findings: list[str] = []
    try:
        fork_compiled = fork_compiled_android_ui_sources(repo_root)
    except Exception as error:  # noqa: BLE001 - the contract owns its own error type
        # Broad on purpose. The mount contract raises its own `ContractError`,
        # which this module cannot name without importing the very thing that
        # failed, and an unreadable contract is reported rather than assumed
        # either way: the honesty rule says a gate never reports a verdict it
        # did not reach.
        findings.append(
            f"the Kotlin mount contract could not be read, so a deliberate "
            f"replacement cannot be told from a collision: {error}. The "
            f"contract is {_MOUNT_TOOLS}/kotlin_mounts.py; every duplicate "
            "below is reported unclassified."
        )
        for qualified in sorted(collisions):
            findings.append(
                f"{qualified}: declared in {len(collisions[qualified])} files "
                f"({', '.join(collisions[qualified])}); unclassified."
            )
        return findings

    for qualified in sorted(collisions):
        paths = collisions[qualified]
        if _is_mount_replacement(paths, fork_compiled):
            continue
        findings.append(
            f"{qualified}: declared in {len(paths)} files ({', '.join(paths)}); "
            "one fully qualified name, one file. Both reach the same artifact, "
            "where load order decides which declaration wins and nothing "
            "reports the other one missing. Rename one, or — if the fork is "
            "meant to replace the Android UI declaration — keep the UI file out of the "
            f"fork's source list in {_MOUNT_TOOLS}/kotlin_mounts.py."
        )
    return findings


def run(repo_root: str) -> list[str]:
    """Every finding: the per-file rules in path order, then the duplicate
    fully qualified names in name order."""
    findings: list[str] = []
    for relative in source_index.source_files(repo_root):
        if not relative.endswith((".kt", ".java")):
            continue
        absolute = os.path.join(repo_root, relative)
        if source_index.is_generated(absolute, repo_root):
            continue
        names = public_types(absolute)
        if len(names) > 1:
            findings.append(
                f"{relative}: declares {len(names)} public types "
                f"({', '.join(names)}); one public type per file."
            )
            continue
        if len(names) == 1:
            stem = os.path.splitext(os.path.basename(relative))[0]
            if names[0] != stem:
                findings.append(
                    f"{relative}: declares `{names[0]}`; a file is named for "
                    "the type it declares."
                )
    findings += duplicate_names(repo_root)
    return findings


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    args = parser.parse_args(argv)
    repo_root = os.path.abspath(args.root)

    findings = run(repo_root)
    for finding in findings:
        print(f"type discipline: {finding}", file=sys.stderr)
    if findings:
        return 1
    counted = sum(
        1
        for relative in source_index.source_files(repo_root)
        if relative.endswith((".kt", ".java"))
    )
    print(
        f"type discipline: {counted} Kotlin and Java files, one public type each, "
        "no fully qualified name declared twice"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
