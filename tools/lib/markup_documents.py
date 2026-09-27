#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Every tracked markup document in this repository parses.

The gap this closes is narrow and was expensive. An Android resource under
``res/values-hi/`` stopped being well-formed XML, and the ``files``,
``strings`` and ``kotlin`` lanes were all green over it: ``check_strings.py``
reads the string names a resource declares and never parses the document,
Gradle's lint did not reach it, and nothing else looked. The first parser in
this repository's reach was Chromium's own ``resources_parser.py``, which
lives on the far side of ``./tools/chromium/build`` — so a malformed resource
was a build-time failure with a green host lane in front of it.

Scope is every extension whose content is XML: Android resources and
manifests, GRIT ``.grd``/``.grdp`` bundles, and ``.svg``. Deliberately not
``.jinja2``: a template carrying ``{% block %}`` is not well-formed XML and is
not meant to be, and the rendered output is the build's business.

This checks that a document parses. It says nothing about whether it means
what it should -- that is the string catalogue's job, and it has one.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from xml.etree import ElementTree

#: Extensions whose content is XML and must parse as XML.
SUFFIXES = (".xml", ".grd", ".grdp", ".svg")


def tracked_documents(repo_root: str) -> list[str]:
    """Every tracked markup document, falling back to a disk walk."""
    try:
        result = subprocess.run(
            ["git", "-C", repo_root, "ls-files", "--cached", "--others",
             "--exclude-standard"],
            capture_output=True, text=True, check=True,
        )
        listing = [line for line in result.stdout.splitlines() if line]
    except (OSError, subprocess.CalledProcessError):
        listing = []
        for base, directories, files in os.walk(repo_root):
            directories[:] = [name for name in directories
                              if name not in {".git", "node_modules", "out", "build", "target"}]
            for name in files:
                listing.append(os.path.relpath(os.path.join(base, name), repo_root))
    return sorted(
        path for path in listing
        if path.endswith(SUFFIXES) and os.path.isfile(os.path.join(repo_root, path))
    )


def findings(repo_root: str) -> list[tuple[str, str]]:
    """One (path, reason) per document that does not parse."""
    found: list[tuple[str, str]] = []
    for relative in tracked_documents(repo_root):
        try:
            ElementTree.parse(os.path.join(repo_root, relative))
        except ElementTree.ParseError as error:
            found.append((relative, str(error)))
        except OSError as error:
            found.append((relative, f"cannot be read: {error}"))
    return found


def self_test() -> list[str]:
    """Prove the check fires on the damage that motivated it."""
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="taffy-markup-") as directory:
        good = os.path.join(directory, "good.xml")
        with open(good, "w", encoding="utf-8") as handle:
            handle.write('<?xml version="1.0"?>\n<!-- a notice -->\n<resources />\n')
        # The exact shape that got past three lanes: a one-line comment
        # expanded by repeating its opener instead of closing it once.
        broken = os.path.join(directory, "broken.xml")
        with open(broken, "w", encoding="utf-8") as handle:
            handle.write(
                '<?xml version="1.0"?>\n'
                "<!-- Copyright line.\n"
                "<!--\n"
                "<!-- More of the notice. -->\n"
                "<resources />\n"
            )
        untouched = os.path.join(directory, "template.xml.jinja2")
        with open(untouched, "w", encoding="utf-8") as handle:
            handle.write("{% extends 'base' %}\n<manifest />\n")

        reported = {path for path, _ in findings(directory)}
        if "broken.xml" not in reported:
            failures.append("a document with unclosed comment openers was accepted")
        if "good.xml" in reported:
            failures.append("a well-formed document was reported")
        if "template.xml.jinja2" in reported:
            failures.append("a Jinja template was required to be well-formed XML")
    return failures


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", action="store_true",
                        help="exercise the well-formed, malformed and template fixtures")
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    args = parser.parse_args(argv)

    problems = self_test()
    for problem in problems:
        print(f"markup documents self-test: {problem}", file=sys.stderr)
    if problems:
        return 1
    if args.self_test:
        print("markup documents self-test: all fixtures passed")
        return 0

    root = os.path.abspath(args.root)
    reported = findings(root)
    for relative, reason in reported:
        print(f"markup documents: {relative}: {reason}", file=sys.stderr)
    if reported:
        print(f"markup documents: {len(reported)} document(s) do not parse", file=sys.stderr)
        return 1
    print(f"markup documents: {len(tracked_documents(root))} document(s) parse")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
