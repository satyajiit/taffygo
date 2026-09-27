#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Dependency direction in the //taffy product root, enforced from its DEPS.

Authority boundary: which `#include` lines a file in
`taffy-core` is allowed to have, and which GN targets a
target may depend on. The rules are not written here — they are the
`include_rules` in the product root's own DEPS files, which are the authority a
Chromium reviewer would read.

Owning milestone: M0 (WP-M0-09). The layering itself is
`docs/architecture/system-architecture.md` and the protocol specification
section 4.

Why this exists. Chromium's `checkdeps` enforces these rules, but only inside a
Chromium checkout with the product root mounted — so between mounts, nothing does.
These two rules in particular are the ones a mistake would quietly cross:

  - **the renderer never depends on the browser.** The browser half owns
    capabilities, actor leases, approval, and the postcondition verifier. If
    renderer code could include one of its headers, the authority boundary
    would be one `#include` away from being crossed by accident.
  - **portable //taffy layers never reach into //chrome.** The product
    `app/` and `browser/` roots are embedder composition and may use narrow
    Chromium browser APIs. Contracts, components, services, renderer, common,
    resources, and UI projections remain downward-only.

Semantics, matching `checkdeps`: rules accumulate from the product root down,
the nearest and longest match wins, and a `specific_include_rules` pattern
matching the file's path is consulted before the directory's own rules. An
include no rule mentions is allowed — this lane reports what a DEPS file
forbids, never what it merely did not think of.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
import ast
import os
import re
import sys
import warnings

#: The tree these rules govern.
PRODUCT_ROOT = "taffy-core"
FOREIGN_SOURCE_ROOT = os.path.normpath(
    os.path.join(PRODUCT_ROOT, "third_party", "cpython", "src")
)

#: The embedder directories a component may never reach into, checked even
#: where a DEPS file forgot to say so.
EMBEDDER_ROOTS = ("chrome", "ios", "android_webview", "chromecast")

#: Product/embedder roots. This is a responsibility boundary: profile/window/
#: tab composition belongs in browser/, while app/ assembles platform products.
#: The recovery prefix is a test-only Chrome composition root registered by
#: patch 0030; its child DEPS grants only the Profile/tab/test APIs the truthful
#: utility-process crash test needs. No portable or production layer may name
#: an embedder API.
EMBEDDER_ALLOWED_PREFIXES = (
    f"{PRODUCT_ROOT}/app/",
    f"{PRODUCT_ROOT}/browser/",
    f"{PRODUCT_ROOT}/test/recovery/",
    # The Android Profile test APK composes the shipping shell's file controls.
    f"{PRODUCT_ROOT}/test/android/profile/",
)

_INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"')
_GN_DEP = re.compile(r'"(//[A-Za-z0-9_/:.-]+)"')


class Rules:
    """The include rules in force for one directory."""

    def __init__(self) -> None:
        #: (allowed, prefix, owning DEPS), most recently added last.
        self.general: list[tuple[bool, str, str]] = []
        #: (pattern, rules, the directory the owning DEPS sits in). The
        #: pattern is tried against the file's path relative to that directory
        #: and against its bare name, because `checkdeps` has been written
        #: both ways over the years and a lane that reported a violation
        #: Chromium's own tool would not is worse than no lane.
        self.specific: list[tuple[re.Pattern[str], list[tuple[bool, str, str]], str]] = []

    def extended(self, general, specific) -> "Rules":
        child = Rules()
        child.general = self.general + general
        child.specific = self.specific + specific
        return child

    def verdict(self, include: str, file_path: str) -> tuple[bool, str] | None:
        """(allowed, owning DEPS), or None when no rule mentions the include."""
        for pattern, rules, directory in reversed(self.specific):
            relative = os.path.relpath(file_path, directory).replace(os.sep, "/")
            if pattern.match(relative) or pattern.match(os.path.basename(file_path)):
                answer = _longest(rules, include)
                if answer is not None:
                    return answer
        return _longest(self.general, include)


def _longest(rules, include: str):
    best = None
    for allowed, prefix, source in rules:
        if include == prefix or include.startswith(prefix.rstrip("/") + "/"):
            if best is None or len(prefix) > best[0]:
                best = (len(prefix), allowed, source)
    return None if best is None else (best[1], best[2])


def _parse_rule_list(values, source: str) -> list[tuple[bool, str, str]]:
    rules: list[tuple[bool, str, str]] = []
    for value in values:
        if not isinstance(value, str) or not value:
            continue
        if value[0] == "+":
            rules.append((True, value[1:], source))
        elif value[0] == "-":
            rules.append((False, value[1:], source))
        elif value[0] == "!":
            rules.append((True, value[1:], source))
    return rules


def read_deps(path: str, source: str, directory: str) -> tuple[list[tuple[bool, str, str]], list]:
    """`include_rules` and `specific_include_rules` from one DEPS file."""
    namespace: dict[str, object] = {}
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    with warnings.catch_warnings():
        # A DEPS file's regexes are ordinary strings with backslashes in them.
        warnings.simplefilter("ignore", SyntaxWarning)
        try:
            tree = ast.parse(text)
        except SyntaxError:
            return [], []
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if not isinstance(target, ast.Name):
            continue
        try:
            namespace[target.id] = ast.literal_eval(node.value)
        except ValueError:
            continue
    general = _parse_rule_list(namespace.get("include_rules", []), source)
    specific = [
        (re.compile(pattern), _parse_rule_list(values, source), directory)
        for pattern, values in dict(namespace.get("specific_include_rules", {})).items()
    ]
    return general, specific


def rules_for(repo_root: str, directory: str, cache: dict[str, Rules]) -> Rules:
    """Rules in force in `directory`, accumulated from the product root down."""
    if directory in cache:
        return cache[directory]
    product_root = os.path.join(repo_root, PRODUCT_ROOT)
    if os.path.normpath(directory) == os.path.normpath(os.path.dirname(product_root)):
        cache[directory] = Rules()
        return cache[directory]
    parent = rules_for(repo_root, os.path.dirname(directory), cache)
    deps = os.path.join(directory, "DEPS")
    if os.path.exists(deps):
        source = os.path.relpath(deps, repo_root).replace(os.sep, "/")
        rules = parent.extended(*read_deps(deps, source, directory))
    else:
        rules = parent
    cache[directory] = rules
    return rules


def check_includes(repo_root: str) -> list[str]:
    findings: list[str] = []
    cache: dict[str, Rules] = {}
    product_root = os.path.join(repo_root, PRODUCT_ROOT)
    foreign_source = os.path.join(repo_root, FOREIGN_SOURCE_ROOT)
    for dirpath, dirnames, filenames in os.walk(product_root):
        dirnames[:] = [
            name
            for name in dirnames
            if name != "__pycache__"
            and os.path.normpath(os.path.join(dirpath, name)) != foreign_source
        ]
        rules = rules_for(repo_root, dirpath, cache)
        for name in sorted(filenames):
            if not name.endswith((".cc", ".h", ".mm")):
                continue
            path = os.path.join(dirpath, name)
            relative = os.path.relpath(path, repo_root).replace(os.sep, "/")
            product_relative = os.path.relpath(path, product_root).replace(os.sep, "/")
            own_directory = os.path.dirname(f"taffy/{product_relative}")
            with open(path, encoding="utf-8", errors="replace") as handle:
                for number, line in enumerate(handle, start=1):
                    match = _INCLUDE.match(line)
                    if not match:
                        continue
                    include = match.group(1)
                    if include.startswith(own_directory + "/"):
                        continue
                    root = include.split("/", 1)[0]
                    embedder_allowed = relative.startswith(EMBEDDER_ALLOWED_PREFIXES)
                    if root in EMBEDDER_ROOTS and not embedder_allowed:
                        findings.append(
                            f"{relative}:{number}: includes {include}. "
                            "Portable //taffy layers never reach into the embedder."
                        )
                        continue
                    verdict = rules.verdict(include, path)
                    if verdict is not None and verdict[0] is False:
                        findings.append(
                            f"{relative}:{number}: includes {include}, which "
                            f"{verdict[1]} forbids."
                        )
    return findings


def check_gn(repo_root: str) -> list[str]:
    findings: list[str] = []
    product_root = os.path.join(repo_root, PRODUCT_ROOT)
    foreign_source = os.path.join(repo_root, FOREIGN_SOURCE_ROOT)
    for dirpath, dirnames, filenames in os.walk(product_root):
        dirnames[:] = [
            name
            for name in dirnames
            if name != "__pycache__"
            and os.path.normpath(os.path.join(dirpath, name)) != foreign_source
        ]
        for name in sorted(filenames):
            if not name.endswith((".gn", ".gni")):
                continue
            path = os.path.join(dirpath, name)
            relative = os.path.relpath(path, repo_root).replace(os.sep, "/")
            embedder_allowed = relative.startswith(EMBEDDER_ALLOWED_PREFIXES)
            with open(path, encoding="utf-8", errors="replace") as handle:
                for number, line in enumerate(handle, start=1):
                    if line.lstrip().startswith("#"):
                        continue
                    for label in _GN_DEP.findall(line):
                        root = label[2:].split("/", 1)[0].split(":", 1)[0]
                        if root in EMBEDDER_ROOTS and not embedder_allowed:
                            findings.append(
                                f"{relative}:{number}: names {label}. A library "
                                "in a portable //taffy layer may not depend on the embedder."
                            )
                        if (
                            "/renderer/" in relative
                            and label.startswith("//taffy/browser")
                        ):
                            findings.append(
                                f"{relative}:{number}: names {label}. The renderer "
                                "never depends on the browser."
                            )
    return findings


def run(repo_root: str) -> list[str]:
    return check_includes(repo_root) + check_gn(repo_root)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    args = parser.parse_args(argv)
    repo_root = os.path.abspath(args.root)

    if not os.path.isdir(os.path.join(repo_root, PRODUCT_ROOT)):
        print(f"include direction: {PRODUCT_ROOT} is not present; nothing to check")
        return 0

    findings = run(repo_root)
    for finding in findings:
        print(f"include direction: {finding}", file=sys.stderr)
    if findings:
        return 1
    print("include direction: the renderer reaches no browser header, and no library names the embedder")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
