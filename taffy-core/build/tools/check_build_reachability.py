#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Every BUILD.gn in the overlay is reachable, or it is not in the build at all.

WHY THIS EXISTS

GN loads a build file only when an already-loaded file names a label inside it.
A `BUILD.gn` that nothing points at is not an empty target or a skipped target —
it is a file GN never opens. Its targets do not exist, its sources are never
compiled, and nothing anywhere reports a problem.

That is not hypothetical. The former product test root sat unloaded while
`taffy_browsertests` built, ran the suites it *could* see, and reported them all
passing — a green suite that was a fifth of itself, with twenty-two files under
`test/{parity,correctness,adversarial}` never once handed to a compiler. Three
separate defects were hiding in the unloaded file, including two that were
chased as unrelated incidents before anyone noticed they shared a cause.

A build cannot catch this, because from the build's point of view nothing is
wrong: the graph it loaded is consistent. Only counting what exists against what
is reachable catches it, which is what this does.

WHY IT IS STATIC

It reads labels out of the overlay's own build files rather than asking GN, so
it runs on a documentation-only host with no checkout, in the same lane as the
rest of the overlay's hygiene checks. That costs some precision — a label built
by string concatenation would be missed — and buys a check that runs on every
machine instead of only on the builder.

WHAT IT DOES NOT CATCH

It works a build *file* at a time, so an orphaned **target** inside a reachable
file is invisible to it. That is not hypothetical either: on 2026-08-19
`gn refs` reported "Nothing references this." for
`//taffy/app/android:unit_tests` (20 gtest cases) and for
`//taffy/renderer:test_support`, the group carrying
`//taffy/renderer/test:tests` (20 more, including the redaction
canary sweep) — both declared in build files this check calls reachable, both
never compiled into any binary. Both are wired in now — `android:unit_tests` into
`taffy_unittests`, `renderer:test_support` into `taffy_browsertests`, which is
the binary upstream runs its own `content::RenderViewTest` suites from — but
nothing here would have told you. Catching that class needs `gn refs` against a
real checkout, so it lives in `tools/chromium/orphan-targets`, which the builder
runs and this check names because a reader of one should know about the other.

    check_build_reachability.py              report unreachable build files
    check_build_reachability.py --self-test  prove the check can fail

Exit status: 0 all reachable, 1 findings.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile

#: The tree this checks, relative to the repository root.
OVERLAY = "taffy-core"

#: The component's root build file. It is reachable by construction: upstream's
#: own `//BUILD.gn` names the product target in it, through patch 0001.
ROOT_BUILD_FILE = "BUILD.gn"

#: Test-only overlay entry points loaded directly by an upstream Chromium test
#: runner rather than by the product root. This is not orphan debt: each entry
#: names the numbered registration patch that makes GN load it, and the check
#: rejects both missing entries and entries later reachable from the product
#: root so this list cannot become a quiet exemption.
UPSTREAM_TEST_BUILD_ROOTS = {
    "test/android/profile/BUILD.gn": (
        "Chromium patch 0045 composes the Android Profile test APK's shipping-shell peer"
    ),
    "test/recovery/BUILD.gn": (
        "Chromium patch 0030 registers the host and Android Chrome test runners"
    ),
}

#: Imported GN files whose labels are architecture metadata, not dependency
#: values consumed by a target. Treating these strings as edges would make
#: every manifest target look product-reachable even though GN never loads its
#: BUILD.gn. Other imported .gni files remain transitive because their labels
#: are dependency lists consumed by templates and targets.
NON_GRAPH_GNI = {"build/generated/components.gni"}

#: A GN label naming a directory under //taffy, in any of the forms
#: the overlay writes: with a target, without one, and as an import() path,
#: which loads a file just as a dep does.
_LABEL = re.compile(r'"(//taffy(?:/[A-Za-z0-9_./-]+)?)(?::[A-Za-z0-9_-]+)?"')

#: Build files known to be unreachable, with the reason and who owns landing
#: them. An entry here is a debt with a name on it, not a permission: the check
#: stays strict for everything else, so a *new* orphan fails on the machine that
#: created it rather than being discovered a wave later.
#:
#: Each is one line that would make it reachable, deliberately not written yet.
KNOWN_UNREACHABLE: dict[str, str] = {}
"""Empty on 2026-08-19, and that is the point: the two entries that used to be
here (`test/BUILD.gn` and `test/fuzz/BUILD.gn`) were paid off rather than
renewed. The twenty-two files under `test/{parity,correctness,adversarial}`
compile and their suites are in `taffy_browsertests`. Keep it empty if you can;
an entry here is a debt with a name on it, not a permission."""

_ROOT_MARKERS = ("chromium/REVISION", "TOOLCHAIN.md", "Cargo.toml")
_LEVELS_TO_REPO_ROOT = 3


def repository_root() -> str:
    root = os.path.dirname(os.path.realpath(__file__))
    for _ in range(_LEVELS_TO_REPO_ROOT):
        root = os.path.dirname(root)
    for marker in _ROOT_MARKERS:
        if not os.path.exists(os.path.join(root, marker)):
            raise SystemExit(f"{root} does not look like the repository root")
    return root


def build_files(overlay_dir: str) -> list[str]:
    """Every BUILD.gn under the overlay, relative to it."""
    found = []
    for current, _dirs, files in os.walk(overlay_dir):
        if "BUILD.gn" in files:
            relative = os.path.relpath(os.path.join(current, "BUILD.gn"), overlay_dir)
            found.append(relative.replace(os.sep, "/"))
    return sorted(found)


def named_files(overlay_dir: str, build_file: str) -> set[str]:
    """The build files this one names, relative to the overlay.

    An `import()` of a dependency `.gni` is followed *through*, not merely
    counted. A `.gni` that supplies `//taffy/x:y` to a target makes
    `x/BUILD.gn` reachable exactly as a directly written `deps` entry would.
    The generated component projection is the one explicit exception: its
    labels are inert architecture metadata, not dependency values.

    This was not hypothetical either. `taffy_dagger_library.gni` is where the
    vendored Dagger jars are named, because the point of the template
    is that no module has to name them; the first run of this check after that
    landed called `third_party/dagger/BUILD.gn` unreachable while
    `gn desc out/dev-x64 //taffy/app/android:taffy_public_apk deps --all` listed
    four of its targets inside the product APK. Same class of mistake as the one
    this file exists for, one level up: reading only part of what the build
    reads.
    """
    return _named_files(overlay_dir, build_file, set())


def _named_files(overlay_dir: str, build_file: str, seen_gni: set[str]) -> set[str]:
    path = os.path.join(overlay_dir, build_file)
    if not os.path.isfile(path):
        return set()
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    # The orphaned test-root case had a commented-out label: the line was there,
    # but GN did not load the file. Strip comments before looking for labels.
    text = re.sub(r"#[^\n]*", "", text)

    named: set[str] = set()
    for label in _LABEL.findall(text):
        relative = label[len("//taffy"):].strip("/")
        if relative.endswith(".gni"):
            if relative in NON_GRAPH_GNI:
                continue
            if relative not in seen_gni:
                seen_gni.add(relative)
                named |= _named_files(overlay_dir, relative, seen_gni)
            continue
        named.add(f"{relative}/BUILD.gn" if relative else ROOT_BUILD_FILE)
    return named


def reachable(
    overlay_dir: str, files: list[str], additional_roots: set[str] | None = None
) -> set[str]:
    """Everything reachable from the product and registered test roots."""
    roots = {ROOT_BUILD_FILE}
    if additional_roots:
        roots.update(additional_roots)
    seen = set(roots)
    frontier = list(roots)
    known = set(files)
    while frontier:
        for named in named_files(overlay_dir, frontier.pop()):
            if named in known and named not in seen:
                seen.add(named)
                frontier.append(named)
    return seen


def findings_for(
    overlay_dir: str,
    known: dict[str, str] | None = None,
    upstream_test_roots: dict[str, str] | None = None,
) -> list[str]:
    known = KNOWN_UNREACHABLE if known is None else known
    upstream_test_roots = (
        UPSTREAM_TEST_BUILD_ROOTS
        if upstream_test_roots is None
        else upstream_test_roots
    )
    files = build_files(overlay_dir)
    if ROOT_BUILD_FILE not in files:
        return [f"{OVERLAY}/{ROOT_BUILD_FILE} is missing; nothing can be reachable"]
    file_set = set(files)
    product_reachable = reachable(overlay_dir, files)
    registered_roots = set(upstream_test_roots) & file_set
    all_reachable = reachable(overlay_dir, files, registered_roots)
    unreachable = file_set - all_reachable
    findings = [
        f"{OVERLAY}/{name}: no registered build root names a label in it, so GN never loads it. "
        "Its targets do not exist and its sources are never compiled — and nothing fails, "
        "which is why this is a check rather than something a build would tell you."
        for name in sorted(unreachable - set(known))
    ]
    findings += [
        f"{OVERLAY}/{name}: registered as an upstream test build root, but there is no such "
        "build file. Remove the stale registration or restore the numbered test patch's target."
        for name in sorted(set(upstream_test_roots) - file_set)
    ]
    findings += [
        f"{OVERLAY}/{name}: registered as an upstream test build root and now reachable from "
        "the product root. Remove the redundant registration so a product dependency on the "
        "test-only component cannot go unnoticed."
        for name in sorted(set(upstream_test_roots) & product_reachable)
    ]
    # The other direction, which is how the list stops being an excuse: a file
    # declared unreachable that has since been wired up is a stale entry, and a
    # stale entry hides the next real one.
    findings += [
        f"{OVERLAY}/{name}: declared unreachable and now reachable. Remove its "
        "KNOWN_UNREACHABLE entry — a debt that has been paid and left on the books is "
        "how the next orphan goes unnoticed."
        for name in sorted(set(known) & all_reachable)
    ]
    findings += [
        f"{OVERLAY}/{name}: declared unreachable, but there is no such build file."
        for name in sorted(set(known) - set(files))
    ]
    return findings


def self_test() -> int:
    """Prove the check can fail, on a tree written to fail."""
    with tempfile.TemporaryDirectory() as directory:
        os.makedirs(os.path.join(directory, "orphan"))
        os.makedirs(os.path.join(directory, "child"))
        os.makedirs(os.path.join(directory, "build", "generated"))
        root = os.path.join(directory, "BUILD.gn")
        with open(root, "w", encoding="utf-8") as handle:
            handle.write('import("//taffy/build/generated/components.gni")\n')
            handle.write('group("a") { deps = [ "//taffy/child:b" ] }\n')
        with open(
            os.path.join(directory, "build", "generated", "components.gni"),
            "w",
            encoding="utf-8",
        ) as handle:
            handle.write('manifest_targets = [ "//taffy/orphan:c" ]\n')
        with open(os.path.join(directory, "child", "BUILD.gn"), "w", encoding="utf-8") as handle:
            handle.write('group("b") {}\n')
        with open(os.path.join(directory, "orphan", "BUILD.gn"), "w", encoding="utf-8") as handle:
            handle.write('group("c") {}\n')

        found = findings_for(directory, known={}, upstream_test_roots={})
        if len(found) != 1 or "orphan/BUILD.gn" not in found[0]:
            print("self-test: an unreachable build file was not reported", file=sys.stderr)
            return 1

        if findings_for(
            directory,
            known={},
            upstream_test_roots={"orphan/BUILD.gn": "self-test registration"},
        ):
            print(
                "self-test: a registered upstream test root was reported as orphaned",
                file=sys.stderr,
            )
            return 1
        missing_root = findings_for(
            directory,
            known={},
            upstream_test_roots={"missing/BUILD.gn": "self-test registration"},
        )
        if not any(
            "registered as an upstream test build root" in item
            for item in missing_root
        ):
            print("self-test: a missing upstream test root was not reported", file=sys.stderr)
            return 1

        with open(root, "a", encoding="utf-8") as handle:
            handle.write('# deps = [ "//taffy/orphan:c" ]\n')
        if len(findings_for(directory, known={}, upstream_test_roots={})) != 1:
            print("self-test: a commented-out label was treated as a live one", file=sys.stderr)
            return 1

        with open(root, "a", encoding="utf-8") as handle:
            handle.write('group("d") { deps = [ "//taffy/orphan:c" ] }\n')
        if findings_for(directory, known={}, upstream_test_roots={}):
            print("self-test: a reachable build file was reported anyway", file=sys.stderr)
            return 1

        # Reached only through an imported .gni, which is how the vendored Dagger
        # jars are reached in the real tree.
        os.makedirs(os.path.join(directory, "vendored"))
        with open(os.path.join(directory, "vendored", "BUILD.gn"), "w", encoding="utf-8") as handle:
            handle.write('group("e") {}\n')
        if len(findings_for(directory, known={}, upstream_test_roots={})) != 1:
            print("self-test: a second orphan was not reported", file=sys.stderr)
            return 1
        with open(os.path.join(directory, "shared.gni"), "w", encoding="utf-8") as handle:
            handle.write('shared_deps = [ "//taffy/vendored:e" ]\n')
        with open(root, "a", encoding="utf-8") as handle:
            handle.write('import("//taffy/shared.gni")\n')
        if findings_for(directory, known={}, upstream_test_roots={}):
            print("self-test: a label inside an imported .gni was not followed", file=sys.stderr)
            return 1

    print(
        "build reachability: an orphan is caught, a commented-out label does not save it, "
        "manifest metadata creates no edge, a registered upstream test root is checked, "
        "and a dependency .gni is followed"
    )
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="prove the check can fail")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()

    found = findings_for(os.path.join(repository_root(), OVERLAY))
    if found:
        for finding in found:
            print(f"  {finding}")
        return 1
    known = len(KNOWN_UNREACHABLE)
    if known:
        print(
            f"build reachability: every BUILD.gn under {OVERLAY} is reachable from its "
            f"root, except {known} carrying a named KNOWN_UNREACHABLE reason"
        )
        for name in sorted(KNOWN_UNREACHABLE):
            print(f"    still unreachable: {OVERLAY}/{name}")
        return 0
    external = len(UPSTREAM_TEST_BUILD_ROOTS)
    print(
        f"build reachability: every BUILD.gn under {OVERLAY} is reachable from the product "
        f"root or {external} registered upstream test root"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
