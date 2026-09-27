#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Project tools/play.d/store.toml into the metadata directory gplay reads.

gplay syncs a store listing from a Fastlane-shaped directory — one file per
field per locale. That is a good format to *send* and a bad format to *author*:
five files with no schema, no comments and no way to say why the title spends
its characters the way it does. So the listing keeps one authored home,
`store.toml`, and this projects it.

The projection is committed, exactly like the generated component graph under
`taffy-core/build/generated/`, and for the same reason: a generated file that
only exists after somebody remembers to run a generator is a file that is
wrong on every host that did not. `--check` holds the two equal and the `play`
lane runs it, so editing a `.txt` by hand — the thing the Fastlane format
invites — fails the gate that same run rather than being silently overwritten
on the next publish.

Exit codes: 0 = clean, 1 = the projection is stale or a field is invalid,
2 = it could not run.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import play_config  # noqa: E402

REPO_ROOT = play_config.REPO_ROOT
METADATA = os.path.join("tools", "play.d", "metadata", "android")

#: store.toml field -> the file name gplay and Fastlane expect.
FIELD_FILES = {
    "title": "title.txt",
    "short_description": "short_description.txt",
    "full_description": "full_description.txt",
}
CHANGELOG = os.path.join("changelogs", "default.txt")

HEADER = (
    "# Generated from tools/play.d/store.toml by tools/lib/play_listing.py.\n"
    "# Do not edit: the `play` lane fails on any disagreement with the source.\n"
)


def render(config) -> dict:
    """Return {relative path: exact file content} for the whole projection.

    A listing may name `also_publish_as`, and one authored locale is then
    written into several. That exists because Play indexes every locale
    separately — an app with one listing is invisible to search in every market
    it has no listing for — while en-GB and en-US differ by nothing in this
    copy. Two hand-maintained copies of one paragraph is how they come to
    differ later; one source and a list of destinations cannot.
    """
    out = {}
    for locale, listing in sorted(config.get("listing", {}).items()):
        targets = [locale] + list(listing.get("also_publish_as", []))
        for target in targets:
            for field, name in FIELD_FILES.items():
                value = listing.get(field)
                if isinstance(value, str) and value.strip():
                    out[os.path.join(target, name)] = value.strip() + "\n"
            notes = listing.get("release_notes")
            if isinstance(notes, str) and notes.strip():
                out[os.path.join(target, CHANGELOG)] = notes.strip() + "\n"
    return out


def on_disk(root: str) -> dict:
    base = os.path.join(root, METADATA)
    found = {}
    for directory, _dirs, files in os.walk(base):
        for name in files:
            if not name.endswith(".txt"):
                continue
            path = os.path.join(directory, name)
            with open(path, encoding="utf-8") as handle:
                found[os.path.relpath(path, base)] = handle.read()
    return found


def diff(expected: dict, actual: dict) -> list[str]:
    findings = []
    for path in sorted(set(expected) - set(actual)):
        findings.append(f"{path}: missing from the projection")
    for path in sorted(set(actual) - set(expected)):
        findings.append(
            f"{path}: on disk but not in store.toml — a hand-written listing file "
            "is never read by anything, because a publish projects the source"
        )
    for path in sorted(set(expected) & set(actual)):
        if expected[path] != actual[path]:
            findings.append(
                f"{path}: differs from store.toml. Edit the source and re-run "
                "`python3 tools/lib/play_listing.py --write`."
            )
    return findings


def write(root: str, expected: dict) -> int:
    base = os.path.join(root, METADATA)
    # Remove only the text files this projection owns, never the tree. The
    # listing images sit in the same Fastlane layout, because that is the layout
    # `gplay images sync` reads, and they are produced by a different tool from a
    # different source; an rmtree here would delete a rendered deck every time
    # somebody corrected a comma in store.toml, and nothing would say so.
    for existing in sorted(on_disk(root)):
        if existing not in expected:
            os.remove(os.path.join(base, existing))
    for path, content in expected.items():
        full = os.path.join(base, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as handle:
            handle.write(content)
    readme = os.path.join(base, "README.md")
    with open(readme, "w", encoding="utf-8") as handle:
        handle.write(
            "# Generated store-listing metadata\n\n"
            "Every file here is projected from `tools/play.d/store.toml` by\n"
            "`tools/lib/play_listing.py`, in the Fastlane-shaped layout `gplay sync`\n"
            "reads. Do not edit them: `./tools/check fast --only play` compares the\n"
            "two in both directions, so a hand edit here fails the gate rather than\n"
            "being quietly overwritten by the next publish.\n\n"
            "Change the listing in `store.toml`, then run\n"
            "`python3 tools/lib/play_listing.py --write`.\n\n"
            "The listing IMAGES live beside these files, under each locale's\n"
            "`images/` directory, because that is the Fastlane layout\n"
            "`gplay images sync` reads. They are produced by a different tool from a\n"
            "different source, so `--write` deletes only the text files it owns and\n"
            "never the tree.\n"
        )
    return len(expected)


def self_test() -> int:
    """The projection must round-trip, and every disagreement must be a finding."""
    import tempfile

    config = {
        "listing": {
            "en-US": {
                "title": " TaffyGo ",
                "short_description": "A browser.",
                "full_description": "Line one.\n\nLine two.",
                "release_notes": "First build.",
            },
            "de-DE": {"title": "TaffyGo", "short_description": "Ein Browser.",
                      "full_description": "Text."},
            "en-GB": {"title": "TaffyGo", "short_description": "A browser.",
                      "full_description": "Text.", "also_publish_as": ["en-AU"]},
        }
    }
    expected = render(config)
    failures = 0

    if expected.get("en-US/title.txt") != "TaffyGo\n":
        failures += 1
        print("  fail the title is not trimmed and newline-terminated", file=sys.stderr)
    if os.path.join("en-US", CHANGELOG) not in expected:
        failures += 1
        print("  fail release notes do not reach changelogs/default.txt", file=sys.stderr)
    if os.path.join("de-DE", CHANGELOG) in expected:
        failures += 1
        print("  fail a locale with no notes gained a changelog", file=sys.stderr)
    # en-US contributes four files (three fields plus a changelog); de-DE has no
    # release notes and contributes three.
    # en-US contributes four files (three fields plus a changelog); de-DE has no
    # release notes and contributes three; en-GB contributes three and projects
    # the same three again into en-AU.
    if len(expected) != 13:
        failures += 1
        print(f"  fail expected 13 files, rendered {len(expected)}", file=sys.stderr)
    if expected.get("en-AU/title.txt") != expected.get("en-GB/title.txt"):
        failures += 1
        print("  fail also_publish_as did not mirror the source locale", file=sys.stderr)

    with tempfile.TemporaryDirectory() as root:
        write(root, expected)
        if diff(expected, on_disk(root)):
            failures += 1
            print("  fail a freshly written projection does not verify", file=sys.stderr)

        base = os.path.join(root, METADATA)
        # A hand edit is a finding.
        with open(os.path.join(base, "en-US", "title.txt"), "w") as handle:
            handle.write("Something Else\n")
        if not any("differs from store.toml" in f for f in diff(expected, on_disk(root))):
            failures += 1
            print("  fail a hand-edited field is not reported", file=sys.stderr)

        write(root, expected)
        # A stray file is a finding.
        with open(os.path.join(base, "en-US", "video.txt"), "w") as handle:
            handle.write("http://example.invalid\n")
        if not any("not in store.toml" in f for f in diff(expected, on_disk(root))):
            failures += 1
            print("  fail a stray listing file is not reported", file=sys.stderr)

        write(root, expected)
        os.remove(os.path.join(base, "en-US", "short_description.txt"))
        if not any("missing" in f for f in diff(expected, on_disk(root))):
            failures += 1
            print("  fail a deleted field is not reported", file=sys.stderr)

    if failures:
        print(f"play listing self-test: {failures} case(s) failed", file=sys.stderr)
        return 1
    print("play listing self-test: 9 cases passed")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=REPO_ROOT)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--check", action="store_true", default=True)
    group.add_argument("--write", action="store_true")
    group.add_argument("--self-test", action="store_true")
    parser.add_argument("--print-dir", action="store_true",
                        help="print the metadata directory and exit")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if args.print_dir:
        print(os.path.join(args.root, METADATA))
        return 0

    try:
        config, _raw = play_config.load(args.root)
    except (OSError, ValueError) as error:
        print(f"play listing: {error}", file=sys.stderr)
        return 2

    expected = render(config)
    if args.write:
        count = write(args.root, expected)
        print(f"play listing: wrote {count} file(s) under {METADATA}")
        return 0

    findings = diff(expected, on_disk(args.root))
    for finding in findings:
        print(f"  {finding}", file=sys.stderr)
    if findings:
        return 1
    print(
        f"play listing: {METADATA} matches store.toml "
        f"({len(expected)} file(s), {len(set(p.split(os.sep)[0] for p in expected))} locale(s))"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
