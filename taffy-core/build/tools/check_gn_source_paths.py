#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Every file a `//taffy` build file lists as a source is a file that is there.

WHY THIS EXISTS

A GN source list is a list of paths, and a path that no longer resolves is not a
compile error, a link error, or anything a reader of the build file can see. It
is a ninja edge with no rule to produce it, and it fails at the very end of a
configure-and-build cycle with a message about a stamp file rather than about
the list that named it.

That is not hypothetical, and it is why the exclusions below are written out
rather than assumed. On 2026-09-19 `taffy-core/app/android/ui/BUILD.gn` still
carried `android_assets("feature_settings_profile_banner_assets")` over
thirty-one tiles at `ui/android/feature/settings/vendor/profile-banners/`,
seventeen days after the commit that moved those tiles to
`ui/android/core/ui/vendor/` — where `:core:ui` decodes them — had deleted the
originals. Two commits landed on top of it. Every lane of `./tools/check fast`
was green for both, because no lane on a host without a Chromium checkout reads
a GN source list, and the two that come closest do not answer this question:
`generate_kotlin_sources.py --check` compares a *generated* list against the
disk, so it cannot drift, and `check_build_reachability.py` asks whether a build
file is loaded at all, not whether what it names exists.

WHY IT READS ASSIGNMENTS RATHER THAN EVERY STRING

The first version of this check read every `"//taffy/..."` literal it could find
and reported 854 paths. That number was true and the claim around it was not:
this repository keeps most of its source lists as paths *relative* to the build
file, so the absolute-only sweep silently skipped 3,414 literals across 44
files — including `browser/source_lists.gni` and the hand-written list in
`services/core/BUILD.gn`, which are exactly the two a reader would most want
covered. A check that reports a large number while reading a fifth of the
subject is worse than no check, because the number reads as coverage.

So it parses `name = [ ... ]` and `name += [ ... ]` and considers only lists
whose name says they hold sources — `sources`, `inputs`, `public`, and anything
ending `_sources` or `_inputs`. It explicitly declines `outputs`,
`renaming_destinations` and anything ending `_destinations` or `_outputs`:
those are paths inside a *built artifact*, not files on the disk, and checking
them would report every asset destination as missing.

WHY IT IS STATIC

It reads path literals out of the overlay's own build files rather than asking
GN, so it runs on a documentation-only host with no checkout, beside the rest of
the overlay's build-graph checks. A missing source is a fact about the
repository, not about a configured build, and anyone who has the repository
should be able to answer it.

WHAT IT DELIBERATELY DOES NOT READ

Three paths, each with a named owner that answers for it instead, because a
decline with no owner is how a blind spot is born. The third is declined only
on a host where it is absent:

  * `//taffy/app/android/kotlin/` is a gitignored per-module symlink mount that
    `kotlin_mounts.py --mount` creates on each host. On a host where it has not
    been run, every one of the ~1,200 paths beneath it is legitimately absent
    and this check would call the whole tree broken while nothing is wrong.
    `kotlin_mounts.py --verify` proves the mount exists and matches the
    component contract, and `generate_kotlin_sources.py --check` proves the
    generated list matches the disk.
  * `build/generated/components.gni` holds `taffy_components`, a generated
    projection of `build/components.toml`. Its `sources` are manifest data
    relative to the product root rather than GN source paths relative to the
    build file, and `component_graph.py --check` regenerates and compares the
    whole file, so it cannot drift.
  * `third_party/cpython/src/` is the pinned CPython source. It is gitignored
    and only `./tools/chromium/sync` fills it, through
    `tools/chromium/lib/cpython_source.py`, which verifies the archive against
    `third_party/cpython/tools/manifest.json` and refuses a tree without the
    members it needs. On a clone that has never synced, the ~190 CPython
    sources the build files list are all legitimately absent, so the tree is
    declined while it is missing and the run says how many it did not read.
    Once a sync has filled it, every path in it is checked like any other.

WHAT IT DOES NOT CATCH

A path assembled by string concatenation or interpolation, because it never
appears as a literal — any list entry containing `$` is skipped. A path that
exists but is the wrong one. A *label* dependency on a target that does not
exist, which GN reports itself, loudly, at configure time. And a file that is
present but named by no build file at all, which is the opposite question and
belongs to the `files` lane's disk-first sweep.

One resolution rule is worth knowing before trusting a finding. A relative entry
is resolved against the directory of the file that *defines* the list, which is
correct for every list in this tree today. GN itself resolves against the
directory of the build file that declares the *target*, so a `.gni` that defined
relative sources for a different directory would be reported here as missing. It
would be a false finding rather than a missed one — loud, not silent — which is
the right direction for the mistake to fall.

    check_gn_source_paths.py              report source lists with no file
    check_gn_source_paths.py --self-test  prove the check can fail

Exit status: 0 every listed source exists, 1 findings.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# The overlay is mounted at Chromium `src/taffy`, so `//taffy/x` is `taffy-core/x`.
SOURCE_ROOT_PREFIX = "//taffy/"
PRODUCT_ROOT = Path(__file__).resolve().parents[2]
REPOSITORY_ROOT = PRODUCT_ROOT.parent

# Read "WHAT IT DELIBERATELY DOES NOT READ" before adding to this. Every entry
# needs a named owner that answers the question instead.
DECLINED: dict[str, str] = {
    "app/android/kotlin/": (
        "gitignored symlink mount; owned by kotlin_mounts.py --verify and "
        "generate_kotlin_sources.py --check"
    ),
    "build/generated/components.gni": (
        "generated projection of build/components.toml, product-root-relative; "
        "owned by component_graph.py --check"
    ),
}

# Declined only while the directory is absent; read in full once it exists.
UNSYNCED: dict[str, str] = {
    "third_party/cpython/src/": (
        "the gitignored pinned CPython source, not synced on this host; "
        "./tools/chromium/sync fills it and tools/chromium/lib/cpython_source.py "
        "verifies it against third_party/cpython/tools/manifest.json"
    ),
}


def declines_for(present) -> dict[str, str]:
    """DECLINED, plus each UNSYNCED tree for which `present(prefix)` is False."""
    declines = dict(DECLINED)
    declines.update({prefix: reason for prefix, reason in UNSYNCED.items() if not present(prefix)})
    return declines

# `name = [ ... ]` and `name += [ ... ]`, non-greedy to the first `]`.
_ASSIGNMENT = re.compile(r"(?m)^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*\+?=[ \t]*\[(.*?)\]", re.S)
_LITERAL = re.compile(r'"([^"\n]+)"')
# A list whose name says it holds files on the disk.
_SOURCE_LIST = re.compile(r"^(sources|inputs|public|.*_sources|.*_inputs)$")
# A list whose name says it holds paths inside a built artifact. Checked first.
_ARTIFACT_LIST = re.compile(r"^(outputs|renaming_destinations|.*_destinations|.*_outputs)$")


def is_source_list(name: str) -> bool:
    return not _ARTIFACT_LIST.match(name) and bool(_SOURCE_LIST.match(name))


def declined(relative_path: str, declines: dict[str, str] = DECLINED) -> str | None:
    for prefix, reason in declines.items():
        if relative_path.startswith(prefix):
            return reason
    return None


def resolve(build_file: str, entry: str) -> str | None:
    """The product-root-relative path an entry names, or None if it names no file.

    `build_file` is repository-relative (`taffy-core/browser/BUILD.gn`).
    """
    if "$" in entry or ":" in entry or "*" in entry or entry.startswith("-"):
        return None
    if entry.startswith("//"):
        if not entry.startswith(SOURCE_ROOT_PREFIX):
            return None  # Chromium's own tree, not ours to answer for.
        candidate = entry[len(SOURCE_ROOT_PREFIX) :]
    elif entry.startswith("/"):
        return None
    else:
        directory = Path(build_file).parent.relative_to("taffy-core")
        candidate = str(directory / entry) if str(directory) != "." else entry
    if not Path(candidate).suffix:
        return None
    return candidate


def listed_sources(build_file: str, text: str) -> list[str]:
    """Every product-root-relative source path one build file lists, in order."""
    found: list[str] = []
    for name, body in _ASSIGNMENT.findall(text):
        if not is_source_list(name):
            continue
        for entry in _LITERAL.findall(body):
            candidate = resolve(build_file, entry)
            if candidate is not None:
                found.append(candidate)
    return found


def findings(
    documents: dict[str, str], exists, declines: dict[str, str] = DECLINED
) -> list[tuple[str, str]]:
    """(build file, missing path) for every listed source `exists` answers False for."""
    found: list[tuple[str, str]] = []
    for build_file in sorted(documents):
        if declined(build_file.removeprefix("taffy-core/"), declines) is not None:
            continue
        for candidate in listed_sources(build_file, documents[build_file]):
            if declined(candidate, declines) is not None:
                continue
            if not exists(candidate):
                found.append((build_file, candidate))
    return found


def counted(documents: dict[str, str], declines: dict[str, str] = DECLINED) -> int:
    return sum(
        1
        for build_file, text in documents.items()
        if declined(build_file.removeprefix("taffy-core/"), declines) is None
        for candidate in listed_sources(build_file, text)
        if declined(candidate, declines) is None
    )


def unsynced_notes(documents: dict[str, str], declines: dict[str, str]) -> list[str]:
    """One line per UNSYNCED tree declined on this run, with what it did not read."""
    notes = []
    for prefix in UNSYNCED:
        if prefix not in declines:
            continue
        skipped = sum(
            1
            for build_file, text in documents.items()
            for candidate in listed_sources(build_file, text)
            if candidate.startswith(prefix)
        )
        notes.append(f"  declined {skipped} listed source(s) under {prefix}: {declines[prefix]}")
    return notes


def tracked_build_files() -> dict[str, str]:
    listed = subprocess.run(
        ["git", "ls-files", "-z", "--", "taffy-core/*.gn", "taffy-core/*.gni"],
        capture_output=True,
        text=True,
        check=True,
        cwd=REPOSITORY_ROOT,
    ).stdout
    return {
        name: (REPOSITORY_ROOT / name).read_text(encoding="utf-8")
        for name in listed.split("\0")
        if name
    }


def self_test() -> int:
    """Prove the check can fail, and that each exclusion is the one intended."""
    present = {
        "ui/android/core/ui/vendor/profile-banners/a1.webp",
        "browser/application_preferences.cc",
        "services/core/rust_core.cc",
    }
    documents = {
        # Absolute entries, one moved out from under its target.
        "taffy-core/app/android/ui/BUILD.gn": """
            android_assets("tiles") {
              sources = [
                "//taffy/ui/android/core/ui/vendor/profile-banners/a1.webp",
                "//taffy/ui/android/feature/settings/vendor/profile-banners/a1.webp",
              ]
              renaming_destinations = [ "profile-banners/a1.webp" ]
              deps = [ "//taffy/app/android/ui:core_ui_java" ]
            }
        """,
        # Relative entries: the shape this repository actually uses most.
        "taffy-core/browser/source_lists.gni": """
            taffy_browser_shared_sources = [
              "application_preferences.cc",
              "gone_with_the_account_plane.cc",
            ]
        """,
        # A hand-written list beside its own BUILD.gn.
        "taffy-core/services/core/BUILD.gn": """
            rust_static_library("service_bridge") {
              sources = [ "rust_core.cc" ]
              outputs = [ "libservice_bridge.rlib" ]
            }
        """,
        # Declined: absent until `kotlin_mounts.py --mount` has run on a host.
        "taffy-core/app/android/kotlin_sources.gni": """
            taffy_core_ui_sources =
                [ "//taffy/app/android/kotlin/com/taffygo/browser/ui/core/ui/Gone.kt" ]
        """,
        # Declined whole: a generated, product-root-relative manifest projection.
        "taffy-core/build/generated/components.gni": """
            taffy_components = [ { sources = [ "test/taffy_browser_test_main_delegate.cc" ] } ]
        """,
        # Not file-shaped: a directory, a pattern, an interpolation, a Chromium path.
        "taffy-core/build/BUILD.gn": """
            group("shapes") {
              inputs = [
                "//taffy/resources/catalog",
                "//taffy/resources/catalog/*.grd",
                "//taffy/$target_name/out.h",
                "//base/check.cc",
              ]
            }
        """,
    }

    result = findings(documents, lambda path: path in present)
    # A clone that never ran `./tools/chromium/sync` has no src/ under CPython;
    # the missing file beside it must still be reported.
    cpython_gni = "taffy-core/third_party/cpython/python_sources.gni"
    cpython = {cpython_gni: 'taffy_python_sources = [ "src/Parser/token.c", "Gone.c" ]'}
    unsynced = declines_for(lambda _prefix: False)
    synced = declines_for(lambda _prefix: True)
    cases: list[tuple[str, bool]] = [
        (
            "the moved tile and the deleted source are the two findings",
            result
            == [
                (
                    "taffy-core/app/android/ui/BUILD.gn",
                    "ui/android/feature/settings/vendor/profile-banners/a1.webp",
                ),
                ("taffy-core/browser/source_lists.gni", "browser/gone_with_the_account_plane.cc"),
            ],
        ),
        (
            "a relative entry resolves against its own directory",
            resolve("taffy-core/browser/source_lists.gni", "application_preferences.cc")
            == "browser/application_preferences.cc",
        ),
        (
            "an absolute //taffy entry resolves to the product root",
            resolve("taffy-core/x/BUILD.gn", "//taffy/browser/a.cc") == "browser/a.cc",
        ),
        ("a Chromium path is not ours to answer for", resolve("taffy-core/x/BUILD.gn", "//base/a.cc") is None),
        ("a label dependency is not a path", resolve("taffy-core/x/BUILD.gn", "//taffy/a:b") is None),
        ("a directory is not a path", resolve("taffy-core/x/BUILD.gn", "//taffy/resources/catalog") is None),
        ("a pattern is not a path", resolve("taffy-core/x/BUILD.gn", "//taffy/a/*.grd") is None),
        ("an interpolated entry is never resolved", resolve("taffy-core/x/BUILD.gn", "//taffy/$n/o.h") is None),
        ("a sources list is read", is_source_list("sources") and is_source_list("taffy_browser_shared_sources")),
        ("inputs and public are read", is_source_list("inputs") and is_source_list("public")),
        (
            "an artifact list is not read",
            not is_source_list("outputs")
            and not is_source_list("renaming_destinations")
            and not is_source_list("taffy_core_ui_profile_banner_asset_destinations"),
        ),
        ("deps is not a source list", not is_source_list("deps")),
        ("the mount is declined, not reported", declined("app/android/kotlin/com/taffygo/Gone.kt") is not None),
        ("the generated projection is declined whole", declined("build/generated/components.gni") is not None),
        ("a real source is not declined", declined("ui/android/core/ui/X.kt") is None),
        ("nothing is a finding when every path resolves", findings(documents, lambda _p: True) == []),
        (
            "an unsynced CPython tree is declined, and only it",
            findings(cpython, lambda path: path in present, unsynced) == [(cpython_gni, "third_party/cpython/Gone.c")],
        ),
        (
            "a synced CPython tree is read like any other",
            findings(cpython, lambda path: path in present, synced)
            == [(cpython_gni, "third_party/cpython/src/Parser/token.c"), (cpython_gni, "third_party/cpython/Gone.c")],
        ),
        (
            "an unsynced run says how many sources it did not read",
            unsynced_notes(cpython, unsynced) != [] and "declined 1 listed" in unsynced_notes(cpython, unsynced)[0]
            and unsynced_notes(cpython, synced) == [],
        ),
    ]

    failed = [name for name, held in cases if not held]
    for name, held in cases:
        print(f"  {'ok  ' if held else 'FAIL'} {name}")
    if failed:
        print(f"self-test: {len(failed)} of {len(cases)} case(s) failed")
        return 1
    print(f"self-test: {len(cases)} cases, the check fails when it should")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="prove the check can fail")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()

    documents = tracked_build_files()
    declines = declines_for(lambda prefix: (PRODUCT_ROOT / prefix).is_dir())
    missing = findings(documents, lambda path: (PRODUCT_ROOT / path).exists(), declines)
    notes = unsynced_notes(documents, declines)
    if missing:
        for build_file, path in missing:
            print(f"  fail {build_file} lists {path}, which is not there")
        for note in notes:
            print(note)
        print(
            f"{len(missing)} missing source(s) in {len(documents)} build file(s). "
            "A GN source list naming a file that is gone fails only at the end of a "
            "build, as a stamp with no rule to make it."
        )
        return 1
    for note in notes:
        print(note)
    print(
        f"GN source paths: {counted(documents, declines)} listed source(s) across "
        f"{len(documents)} build file(s), all present"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
