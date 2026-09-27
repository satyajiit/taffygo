#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Reject Chromium licence boilerplate on TaffyGo first-party product files.

Authority boundary: the two header shapes that contradict the repository's
first-party licence, and which files below ``taffy-core/`` are registered
third-party assets or licence texts. The root ``LICENSE`` and decision 0027
own the legal terms; this gate only prevents a first-party file from carrying
a different notice by accident.

Why the exception is computed. A copied or vendored file keeps the notice its
owner supplied. ``vendored_assets.py`` is already the register that says which
concrete files and licence texts have that status, so this checker reads that
register instead of maintaining a second allowlist. Product integration files
such as ``BUILD.gn``, ``OWNERS`` and ``README.chromium`` remain first-party and
are scanned even when they sit below ``third_party/``.

Generated product files are scanned too. A stale generated header is still a
notice carried by the source tree, and finding its generator as well as its
output is what keeps regeneration from restoring the contradiction.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from license_notice import LINE_HASH, STYLES
from license_notice import apply as apply_notice
from license_notice import carries_legacy, carries_notice, style_for
from vendored_asset_sweep import registered_asset_files
from vendored_assets import REGISTER

PRODUCT_ROOT = "taffy-core"

#: Every tree whose files are TaffyGo's own source. The product root is the
#: bulk of it; the rest is the tool suite, the two Workers, the site and the
#: Gradle convention plugins, which are first-party work that is published
#: too. Deliberately absent: `chromium/patches/`, which is a diff of somebody
#: else's source, `test-fixtures/`, whose bytes are the fixture, and the
#: planning documents, which carry no code.
SOURCE_ROOTS = (PRODUCT_ROOT, "tools", "website", "build-logic")

#: Tracked files that a third-party tool rewrites, where a notice cannot
#: survive. This is not "files we chose not to bother with": each entry names
#: the tool that regenerates the file and would delete the header again on its
#: next run, which is a diff nobody made and a gate failure nobody caused. It
#: is checked in both directions like NOTICE_DEBT, so an entry for a file that
#: has stopped being regenerated is itself a finding.
FOREIGN_GENERATED = {
    os.path.normpath("website/next-env.d.ts"): (
        "Next.js rewrites this file on every `next build` and its own "
        "documentation says to commit it and never edit it. A notice added "
        "here survives exactly until the next build -- which is how it was "
        "found: the relicensing sweep added one, the first website build "
        "after it silently removed it, and the files lane then failed on a "
        "file nobody had touched."
    ),
}

#: Files carrying the superseded notice on purpose, with the reason and the
#: work that pays each one off. This is not an exemption list for headers --
#: every entry here is a *published artifact's* own notice rather than a
#: source header, where rewriting the text changes bytes that are already
#: somewhere else. It is checked in both directions, so an entry cannot
#: outlive the debt it records.
NOTICE_DEBT = {
    os.path.normpath("taffy-core/ui/android/tools/scenes/build_scene_pack.py"): (
        "NOTICE_MEMBER_TEXT is the notice inside the start-page scene pack, "
        "published on 2026-09-10 with its digest pinned in the asset catalog. "
        "Rewriting it changes the pack and strands that pin, so it is paid off "
        "by the work that rebuilds the packs, not by the relicensing."
    ),
}

# The two rejected notices, assembled from fragments rather than spelled out.
# The scan is absolute and this file is inside it: written as literals, the
# checker is the one file in the repository that always fails its own rule,
# and the only ways out are an exemption for itself -- which is a rule with a
# hole exactly where the rule lives -- or not scanning the tool suite at all,
# which was the accidental arrangement until this scan grew past taffy-core/.
# The self-test fixtures below are built from the same fragments for the same
# reason.
_BSD_NOTICE = "Use of this source code is governed by a " + "BSD" + "-style license"
_AUTHORS = "The " + "TaffyGo" + " Authors"
_AUTHORS_KIND = f'copyright holder "{_AUTHORS}"'
_BSD_KIND = "Chromium " + "BSD" + " boilerplate"

# Chromium's stock source header. It cannot govern TaffyGo-owned code: this
# repository rejected that licence for its first-party work when it was
# proprietary (decision 0027) and did not adopt it when it opened (0205).
_CHROMIUM_BSD = re.compile(re.escape(_BSD_NOTICE) + r"\b")

# There is no legal person of that name. Decision 0027 names Matterward Labs
# Private Limited as the holder and 0205 leaves the holder alone. Limit the
# match to copyright lines so ordinary prose cannot become a false finding.
_TAFFYGO_AUTHORS = re.compile(
    r"^.*Copyright(?: \(c\))?\s+\d{4}(?:-\d{4})?\s+" + re.escape(_AUTHORS) + r"\b.*$",
    re.MULTILINE,
)

_SKIP_DIRECTORIES = {
    ".git",
    ".gradle",
    ".idea",
    ".kotlin",
    "__pycache__",
    "dist",
    "node_modules",
    "out",
    "target",
}


@dataclass(frozen=True)
class Finding:
    """One first-party file and the contradictory notices it carries."""

    path: str
    kinds: tuple[str, ...]


@dataclass(frozen=True)
class Report:
    """The complete result, including auditable scan/exclusion counts."""

    findings: tuple[Finding, ...]
    scanned: int
    excluded: int


def _repository_files(repo_root: str) -> list[str]:
    """Tracked and untracked product files, falling back to a disk walk."""

    def git_files(*arguments: str) -> list[str]:
        result = subprocess.run(
            ["git", "-C", repo_root, "ls-files", *arguments, "--", *SOURCE_ROOTS],
            capture_output=True,
            text=True,
            check=True,
        )
        return [line for line in result.stdout.splitlines() if line]

    def nested_build_output(relative: str) -> bool:
        parts = relative.replace("\\", "/").split("/")
        return "build" in parts[2:]

    try:
        tracked = git_files("--cached")
        untracked = git_files("--others", "--exclude-standard")
    except (OSError, subprocess.CalledProcessError):
        tracked = None
        untracked = None

    if tracked is not None and untracked is not None:
        found: set[str] = set()
        # Every tracked file is repository-owned, whatever its directory is.
        # For untracked files, prune only a nested build output. The root
        # `taffy-core/build/` directory is source and is deliberately retained.
        untracked_sources = [path for path in untracked if not nested_build_output(path)]
        for relative in tracked + untracked_sources:
            normalized = os.path.normpath(relative)
            if os.path.isfile(os.path.join(repo_root, normalized)):
                found.add(normalized)
        return sorted(found)

    found: list[str] = []
    for source_root in SOURCE_ROOTS:
        found.extend(_walk(os.path.join(repo_root, source_root), repo_root))
    return sorted(found)


def _walk(root: str, repo_root: str) -> list[str]:
    """The disk fallback for one source root, used when git cannot answer."""
    found: list[str] = []
    for base, directories, files in os.walk(root):
        directories[:] = [
            name
            for name in directories
            if name not in _SKIP_DIRECTORIES
            and not (
                name == "build"
                and os.path.normpath(os.path.join(base, name))
                != os.path.normpath(os.path.join(root, "build"))
            )
        ]
        for name in files:
            found.append(os.path.normpath(os.path.relpath(os.path.join(base, name), repo_root)))
    return found


def registered_asset_and_notice_files(repo_root: str) -> set[str]:
    """Concrete provenance-owned assets and inbound licence texts in the product."""
    excluded = registered_asset_files(repo_root, REGISTER)
    for entry in REGISTER.values():
        excluded.update(os.path.normpath(path) for path in entry["licences"])
    return {
        path
        for path in excluded
        if path == PRODUCT_ROOT or path.startswith(PRODUCT_ROOT + os.sep)
    }


def _text(path: str) -> str | None:
    """UTF-8 text, or None for a binary/inbound artifact this rule cannot parse."""
    try:
        payload = Path(path).read_bytes()
    except OSError:
        return None
    if b"\0" in payload:
        return None
    try:
        return payload.decode("utf-8")
    except UnicodeDecodeError:
        return None


def run(repo_root: str, excluded: set[str] | None = None,
        debt: dict[str, str] | None = None) -> Report:
    """Scan every first-party textual file below ``taffy-core/``."""
    repo_root = os.path.abspath(repo_root)
    if excluded is None:
        excluded = registered_asset_and_notice_files(repo_root)
    excluded = {os.path.normpath(path) for path in excluded}
    if debt is None:
        debt = NOTICE_DEBT

    findings: list[Finding] = []
    scanned = 0
    excluded_count = 0
    for relative in _repository_files(repo_root):
        if relative in excluded:
            excluded_count += 1
            continue
        contents = _text(os.path.join(repo_root, relative))
        if contents is None:
            continue
        scanned += 1
        first_line = contents.split("\n", 1)[0]
        kinds: list[str] = []
        if _TAFFYGO_AUTHORS.search(contents):
            kinds.append(_AUTHORS_KIND)
        if _CHROMIUM_BSD.search(contents):
            kinds.append(_BSD_KIND)
        if carries_legacy(contents) and relative not in debt:
            kinds.append("the superseded all-rights-reserved notice")
        if relative in debt and not carries_legacy(contents):
            kinds.append("a notice-debt entry whose debt is paid; delete it")
        if (style_for(relative, first_line) is not None
                and not carries_notice(contents)
                and relative not in FOREIGN_GENERATED):
            kinds.append("no licence notice")
        if relative in FOREIGN_GENERATED and carries_notice(contents):
            kinds.append(
                "a foreign-generated entry for a file that does carry a notice; "
                "the tool has stopped rewriting it, so delete the entry"
            )
        if kinds:
            findings.append(Finding(relative, tuple(kinds)))

    return Report(tuple(findings), scanned, excluded_count)


def self_test() -> list[str]:
    """Prove first-party rejection, template coverage, and vendor preservation."""
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="taffy-license-headers-") as directory:
        product = Path(directory, PRODUCT_ROOT)
        product.mkdir()
        (product / "owned.cc").write_text(
            f"// Copyright 2026 {_AUTHORS}\n// {_BSD_NOTICE}.\n",
            encoding="utf-8",
        )
        (product / "generator.py").write_text(
            f'HEADER = "{_BSD_NOTICE}"\n',
            encoding="utf-8",
        )
        (product / "superseded.rs").write_text(
            "// Copyright (c) 2026 Matterward Labs Private Limited. "
            "All rights reserved.\n",
            encoding="utf-8",
        )
        (product / "clean.rs").write_text(
            apply_notice("fn main() {}\n", STYLES[".rs"]), encoding="utf-8"
        )
        (product / "corpus.json").write_text('{"ok": true}\n', encoding="utf-8")
        vendor = product / "third_party" / "dagger"
        vendor.mkdir(parents=True)
        (vendor / "LICENSE").write_text(
            f"// Copyright 2026 The Chromium Authors\n// {_BSD_NOTICE}.\n",
            encoding="utf-8",
        )
        integration = product / "third_party" / "library"
        integration.mkdir(parents=True)
        (integration / "BUILD.gn").write_text(
            f"# Copyright 2026 {_AUTHORS}\n",
            encoding="utf-8",
        )
        product_build = product / "build"
        product_build.mkdir()
        (product_build / "components.toml").write_text(
            f"// Copyright 2026 {_AUTHORS}\n",
            encoding="utf-8",
        )
        gradle_build = product / "ui" / "android" / "module" / "build"
        gradle_build.mkdir(parents=True)
        (gradle_build / "ignored.cc").write_text(
            f"// Copyright 2026 {_AUTHORS}\n",
            encoding="utf-8",
        )

        report = run(directory)
        by_path = {finding.path: finding.kinds for finding in report.findings}
        owned = by_path.get(os.path.normpath("taffy-core/owned.cc"), ())
        if not {_AUTHORS_KIND, _BSD_KIND} <= set(owned):
            failures.append("a first-party file carrying both contradictions did not fail twice")
        generated = by_path.get(os.path.normpath("taffy-core/generator.py"), ())
        if _BSD_KIND not in generated:
            failures.append("a generator template carrying the rejected boilerplate did not fail")
        if os.path.normpath("taffy-core/third_party/dagger/LICENSE") in by_path:
            failures.append("a registered upstream notice was rewritten as first-party")
        integration_path = os.path.normpath("taffy-core/third_party/library/BUILD.gn")
        if _AUTHORS_KIND not in by_path.get(integration_path, ()):
            failures.append("a first-party integration file below third_party was exempted")
        superseded = by_path.get(os.path.normpath("taffy-core/superseded.rs"), ())
        if "the superseded all-rights-reserved notice" not in superseded:
            failures.append("the notice the MPL replaced was accepted")
        if os.path.normpath("taffy-core/clean.rs") in by_path:
            failures.append("a file carrying the licence notice was reported anyway")
        if os.path.normpath("taffy-core/corpus.json") in by_path:
            failures.append("a document with no comment syntax was required to carry a notice")
        if "no licence notice" not in by_path.get(os.path.normpath("taffy-core/owned.cc"), ()):
            failures.append("a source file with no licence notice was not reported")
        source_build = os.path.normpath("taffy-core/build/components.toml")
        if _AUTHORS_KIND not in by_path.get(source_build, ()):
            failures.append("the authoritative taffy-core/build source tree was excluded")
        gradle_output = os.path.normpath(
            "taffy-core/ui/android/module/build/ignored.cc"
        )
        if gradle_output in by_path:
            failures.append("a nested Gradle build output entered the source audit")

        # The register, both ways round. An artifact's own notice is excused
        # while the debt stands; the moment it is paid, the entry is itself
        # the finding, so a paid debt cannot be left on the books.
        owed = os.path.normpath("taffy-core/superseded.rs")
        excused = run(directory, debt={owed: "a reason"})
        excused_kinds = {finding.path: finding.kinds for finding in excused.findings}
        if "the superseded all-rights-reserved notice" in excused_kinds.get(owed, ()):
            failures.append("a registered notice debt was reported as a finding anyway")
        clean = os.path.normpath("taffy-core/clean.rs")
        stale = run(directory, debt={clean: "a debt that is already paid"})
        stale_kinds = {finding.path: finding.kinds for finding in stale.findings}
        if "a notice-debt entry whose debt is paid; delete it" not in stale_kinds.get(clean, ()):
            failures.append("a notice-debt entry outlived its debt without a finding")

        # The repair has to settle what the report said, or the two halves of
        # this file disagree about the same tree.
        repaired = fix(directory, {finding.path for finding in report.findings})
        after = run(directory)
        remaining = {
            path
            for path, kinds in ((f.path, f.kinds) for f in after.findings)
            if "no licence notice" in kinds
        }
        if remaining:
            failures.append("--fix left a source file with no licence notice")
        if not repaired:
            failures.append("--fix repaired nothing on a tree that had findings")
        if fix(directory, {finding.path for finding in after.findings}):
            failures.append("--fix rewrote a file on a second pass")

    return failures


def fix(repo_root: str, paths: set[str]) -> list[str]:
    """Write the notice into every named file that may carry one.

    Only the missing notice is repaired. An upstream header or a holder
    this repository does not have is a question about where a file came from,
    and answering it by prepending a second notice would bury the evidence.
    """
    repaired: list[str] = []
    for relative in sorted(paths):
        absolute = os.path.join(repo_root, relative)
        contents = _text(absolute)
        if contents is None or carries_notice(contents):
            continue
        style = style_for(relative, contents.split("\n", 1)[0])
        if style is None and (relative in NOTICE_DEBT or not carries_legacy(contents)):
            # No notice is required here and none is wrong here.
            continue
        # A file that already carries a notice is corrected wherever it sits,
        # whatever its type: `DEPS` and `OWNERS` are not required to say
        # anything about the licence, but one that does may not say the
        # opposite of LICENSE.
        # A registered debt keeps its old wording, because that wording is not
        # this file's header. Its own header is still owed and still written.
        updated = apply_notice(
            contents, style or LINE_HASH, replace=relative not in NOTICE_DEBT
        )
        if updated != contents:
            Path(absolute).write_text(updated, encoding="utf-8")
            repaired.append(relative)
    return repaired


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="exercise first-party, generated-template and registered-vendor fixtures",
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="write the licence notice into every source file missing one",
    )
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    args = parser.parse_args(argv)

    failures = self_test()
    for failure in failures:
        print(f"license headers self-test: {failure}", file=sys.stderr)
    if failures:
        return 1
    if args.self_test:
        print("license headers self-test: all fixtures passed")
        return 0

    report = run(args.root)
    if args.fix:
        repaired = fix(args.root, {finding.path for finding in report.findings})
        print(f"license headers: {len(repaired)} file(s) given the licence notice")
        report = run(args.root)
    for finding in report.findings:
        print(
            f"license headers: {finding.path}: " + ", ".join(finding.kinds),
            file=sys.stderr,
        )
    if report.findings:
        print(
            f"license headers: {len(report.findings)} first-party file(s) carry a "
            "notice that contradicts LICENSE",
            file=sys.stderr,
        )
        return 1
    print(
        f"license headers: {report.scanned} first-party text file(s) checked; "
        f"{report.excluded} registered asset/inbound-notice file(s) preserved"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
