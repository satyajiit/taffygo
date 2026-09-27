#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The Android-resource half of the Kotlin mount contract.

`kotlin_mounts.py` answers one question — *which UI host Kotlin does the fork
compile* — and this file answers the one beside it: *which of those modules'
Android resources does the fork package, and are their names safe to put in an
APK's table*. They were one file until that file passed the repository's
400-line cap; the seam is real enough that a reader can tell from a symbol's
name which of the two owns it.

WHY THE NAMES ARE RESERVED

`custom_package` names the package of the generated `R` class and nothing else.
An APK's *resource table* is a single flat namespace, and
`compile_resources.py` hands every plain `dependency_zips` entry to one
`aapt2 link` as a peer. Two peers defining `string/new_tab_title` with different
text is `error: resource 'string/new_tab_title' has a conflicting value`, and
that is exactly what `feature/browsing` did against `IDS_NEW_TAB_TITLE` in
`chrome/browser/ui/android/strings/android_chrome_strings.grd`. The same fact
has a second face: `-R`, which `resource_overlay = true` selects, does not
conflict, it silently *replaces* — which is the branding substitution mechanism
working as designed, and would be a cross-cutting change to upstream text if a
screen module reached for it.

So every Android resource name a UI host module contributes begins with
`taffy_`. That is not a style rule. It is the same reservation the grit half
already carries — `check_strings.py` enforces `IDS_TAFFY_[A-Z0-9_]+`, grit's
android output strips `IDS_` and lowercases, so a grit-borne resource arrives as
`taffy_*` — and it is the only form of the rule that is checkable on any host,
with no Chromium checkout and no staleness at a rebase: upstream will not begin
using the prefix.

The rule is checked over **every** module with a `res/`, not only the mounted
ones, because a name is only cheap to change before the module that owns it
ships.

Stdlib only. Imported by `kotlin_mounts.py`; it has no command of its own,
because the verdicts it produces are part of that file's one report.
"""

from __future__ import annotations

import os
import re

#: Modules with their own `res/` directory, which is mounted beside the sources
#: and becomes an `android_resources` target carrying the module's package. The
#: island already works this way; nothing here is new machinery.
RESOURCE_MODULES = (
    "core/designsystem",
    "core/ui",
    "feature/browsing",
    "feature/downloads",
    "feature/assistant",
    "feature/workspaces",
    "feature/settings",
    "feature/onboarding",
    "feature/providers",
)

#: The prefix every Android resource name in a UI host module must carry, so
#: that it cannot collide with one of the ~16,000 names Chromium contributes to
#: the same flat APK resource table. See the header.
RESOURCE_NAME_PREFIX = "taffy_"

#: The one module whose `res/` the rule does not reach, and why. `app`'s Kotlin
#: is mounted and its resources are not: everything under `app/src/main/res` is
#: the Android shell's — the launcher icons, the splash theme, the window
#: ground — and every one of those is replaced by the fork's own `branding/`
#: resources or by Chromium's. The launcher layers in particular are written by
#: `taffy-core/ui/android/tools/icons/generate_launcher_icons.py`, which names
#: them, so the reserved-prefix rule could not apply to them anyway.
#:
#: `kotlin_mounts.verify` asserts that no file the fork compiles from a module
#: named here reads that module's own `R`, which is what keeps the exemption
#: honest now that the module is mounted: a mounted module with a `res/`
#: normally has to be in RESOURCE_MODULES, and this is the one exception.
UNMOUNTABLE_RESOURCE_MODULES = ("app",)

#: Elements that *declare* a resource in a `values*/` XML. `<item name="...">`
#: inside a `<style>` names an attribute rather than declaring a resource, so it
#: is deliberately absent.
_RESOURCE_DECLARATION = re.compile(
    r"<(string|plurals|string-array|color|dimen|bool|integer|style|attr)\s+name=\"([^\"]+)\""
)

#: Resource directories whose *file names* are the resource names.
_FILE_RESOURCE_DIRS = frozenset(
    (
        "drawable",
        "layout",
        "font",
        "xml",
        "raw",
        "anim",
        "animator",
        "menu",
        "mipmap",
        "color",
        "interpolator",
        "transition",
    )
)

#: A file reading the generated `R` class of the module it is in. Same-module
#: `R` needs no import, so this matches the use rather than the import: `R.` at
#: a token boundary, which is how it is always spelled.
READS_OWN_R = re.compile(
    r"(?<![\w.])R\.(string|drawable|color|dimen|style|plurals|font|raw|xml)\."
)


def module_resource_dir(root: str, module: str) -> str:
    """A module's `res/`, in the UI host."""
    return os.path.join(root, "taffy-core", "ui", "android", module, "src", "main", "res")


def resource_mount_path(component_dir: str, mount_dir: str, module: str) -> str:
    """Where a module's `res/` is mounted. One directory per module, by name."""
    return os.path.join(component_dir, mount_dir, "res", module.replace("/", "_"))


def resource_names(directory: str) -> list[tuple[str, str, str]]:
    """Every Android resource a `res/` declares: (type, name, file).

    Both shapes, because both land in the same table: a name declared inside a
    `values*/` XML, and a name that *is* a file name under `drawable/`, `font/`
    and their siblings. Locale-qualified directories are read as well — a
    translation carries the same name, so a rename that missed `values-hi/`
    would leave a resource with no Hindi and no error anywhere.
    """
    found: list[tuple[str, str, str]] = []
    if not os.path.isdir(directory):
        return found
    for current, _dirs, files in os.walk(directory):
        relative_dir = os.path.relpath(current, directory)
        if relative_dir == os.curdir:
            continue
        kind = relative_dir.split(os.sep)[0].split("-")[0]
        for name in sorted(files):
            where = os.path.join(relative_dir, name)
            if kind == "values":
                if not name.endswith(".xml"):
                    continue
                with open(os.path.join(current, name), encoding="utf-8") as handle:
                    text = handle.read()
                for resource_type, resource in _RESOURCE_DECLARATION.findall(text):
                    found.append((resource_type, resource, where))
            elif kind in _FILE_RESOURCE_DIRS:
                found.append((kind, name.split(".")[0], where))
    return sorted(found)


def unreserved_names(module: str, directory: str) -> list[str]:
    """Findings for every resource in [directory] whose name is not reserved."""
    findings: list[str] = []
    for resource_type, resource, where in resource_names(directory):
        if resource.startswith(RESOURCE_NAME_PREFIX):
            continue
        findings.append(
            f"{module}: {resource_type}/{resource} ({where}) does not begin with "
            f"{RESOURCE_NAME_PREFIX!r}. An APK's resource table is one flat "
            "namespace, so an unreserved name collides with whichever upstream "
            "target happens to use it and fails the aapt2 link of the whole APK."
        )
    return findings
