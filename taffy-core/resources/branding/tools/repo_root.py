#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Find the TaffyGo repository root from inside the mounted overlay.

The problem this solves, stated once so neither generator has to restate it:

`taffy-core/` is symlink-mounted into the Chromium
checkout at `src/taffy` (decision 0012). Two inputs the product
target needs live in the repository *outside* that mount:

  * `brand/png/` — the committed marks the launcher icons are derived from;
  * `chromium/REVISION`, `chromium/SECURITY_PATCH_LEVEL` and
    `chromium/patches/` — the upstream provenance PAR-SEC-002 requires the
    build to report.

GN cannot name either: a label like `//taffy/../../../REVISION`
normalizes to `//REVISION`, which does not exist in the checkout. So the two
generators resolve the repository root at run time by following this file's
own real path back out of the mount, and each declares what it read through a
Ninja depfile with absolute paths, which is how the outputs stay correctly
rebuilt when the pin or the artwork changes.

Every caller may override the result with `--repo-root`, because a host that
runs these scripts straight out of the repository (no checkout, no mount) is
the ordinary documentation-host case and must not depend on symlink
behaviour.

Stdlib only. Importable as a module; not executable on its own.
"""

from __future__ import annotations

import os

# Files that, together, identify the TaffyGo repository root rather than some
# other directory that happens to contain a `chromium/` folder. All three must
# be present: any one of them alone is a plausible coincidence.
_ROOT_MARKERS = ("chromium/REVISION", "TOOLCHAIN.md", "brand/README.md")


class RepoRootError(Exception):
    """The repository root could not be identified."""


def looks_like_repo_root(path: str) -> bool:
    return all(os.path.exists(os.path.join(path, marker)) for marker in _ROOT_MARKERS)


def find(start: str | None = None) -> str:
    """Walk up from `start` (default: this file's real location) to the root.

    `os.path.realpath` is what crosses the overlay symlink: inside the
    checkout this file is reached as
    `src/taffy/resources/branding/tools/repo_root.py`, and its real path is
    `<repo>/taffy-core/resources/branding/tools/repo_root.py`.
    """
    current = os.path.realpath(start or __file__)
    if os.path.isfile(current):
        current = os.path.dirname(current)
    while True:
        if looks_like_repo_root(current):
            return current
        parent = os.path.dirname(current)
        if parent == current:
            raise RepoRootError(
                "no TaffyGo repository root above "
                f"{os.path.realpath(start or __file__)} "
                f"(looked for {', '.join(_ROOT_MARKERS)}). "
                "Pass --repo-root explicitly."
            )
        current = parent


def resolve(explicit: str | None) -> str:
    """`--repo-root` when the caller gave one, otherwise the walk above."""
    if not explicit:
        return find()
    root = os.path.abspath(explicit)
    if not looks_like_repo_root(root):
        raise RepoRootError(
            f"{root} is not a TaffyGo repository root "
            f"(looked for {', '.join(_ROOT_MARKERS)})"
        )
    return root


def write_depfile(depfile: str, output: str, inputs: list[str]) -> None:
    """Emit a Ninja depfile so outputs rebuild when out-of-tree inputs change.

    Absolute paths on the right-hand side are exactly the case this exists
    for: the inputs live outside the checkout's source root, so GN cannot
    carry them in `inputs` and Ninja has to learn about them from here.
    """
    os.makedirs(os.path.dirname(os.path.abspath(depfile)) or ".", exist_ok=True)
    escaped = [path.replace(" ", "\\ ") for path in sorted(set(inputs))]
    with open(depfile, "w", encoding="utf-8") as handle:
        handle.write(f"{output.replace(' ', chr(92) + ' ')}: {' '.join(escaped)}\n")
