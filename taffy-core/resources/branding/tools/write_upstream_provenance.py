#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate the upstream-provenance report PAR-SEC-002 requires.

One generator run over three inputs — `chromium/REVISION`,
`chromium/SECURITY_PATCH_LEVEL` and `chromium/patches/` — produces two
outputs:

  * a C++ translation unit defining `taffy::GetUpstreamProvenance()`, declared
    in `branding/taffy_upstream_provenance.h`, so the running product can say
    which upstream revision it is and how far it has been changed;
  * a JSON sidecar for the release artifact manifest, so a release engineer
    reads the same numbers from the same run.

Two properties matter more than anything else here and are worth naming:

1. **Deterministic.** Nothing in the output depends on the clock, the host,
   the user or the environment. The pin date comes from the repository's own
   history, not from `now`, so an identical source tree produces an identical
   binary. Lag in days is deliberately not computed: the surface that shows it
   subtracts at display time.

2. **Counted the same way the gate counts.** `./tools/check fast` measures
   fork debt as the number of `.patch` files and the number of added or
   removed lines in them. This script uses the identical rule, so the number
   in the product can never drift from the number that blocks the build.

Modes:

  --output-cc PATH     write the generated translation unit
  --output-json PATH   write the artifact-manifest sidecar
  --print              print the values as JSON on stdout, write nothing

At least one of the three is required. Exit status: 0 clean, 1 on any finding.
Stdlib only.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import repo_root  # noqa: E402

# `./tools/check fast`'s rule, as a regular expression rather than a pipeline:
# a changed line starts with + or -, and the +++ / --- file headers do not
# count as changes.
CHANGED_LINE = re.compile(rb"^[+-]([^+-]|$)")

# chromium/REVISION and chromium/SECURITY_PATCH_LEVEL are `key=value` files
# with `#` comments (their own headers say so).
KEY_VALUE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")

REQUIRED_PIN_KEYS = ("milestone", "tag", "commit")

# The unpinned placeholder chromium/REVISION carries before SP-01 selects a
# revision. Reporting it as if it were a revision would be a false claim in
# the product's own version surface.
UNPINNED = {"", "unpinned", "none", "TBD"}


class Finding(Exception):
    """Something the caller must fix; never worked around."""


def read_key_values(path: str) -> dict[str, str]:
    values: dict[str, str] = {}
    if not os.path.exists(path):
        raise Finding(f"missing input: {path}")
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0]
            match = KEY_VALUE.match(line)
            if match:
                values[match.group(1)] = match.group(2)
    return values


def read_pin(root: str) -> dict[str, str]:
    path = os.path.join(root, "chromium", "REVISION")
    values = read_key_values(path)
    for key in REQUIRED_PIN_KEYS:
        if key not in values:
            raise Finding(f"{path}: missing required key {key!r}")
        if values[key] in UNPINNED:
            raise Finding(
                f"{path}: {key} is unpinned. A build cannot report a revision it "
                "does not have. SP-01 selects the Chromium stable milestone; see "
                "docs/development/chromium-fork-and-build.md section 1.3."
            )
    return values


def read_security_level(root: str) -> tuple[int, str]:
    path = os.path.join(root, "chromium", "SECURITY_PATCH_LEVEL")
    values = read_key_values(path)
    raw_level = values.get("level", "")
    if not raw_level.isdigit():
        raise Finding(f"{path}: level must be a non-negative integer, got {raw_level!r}")
    level = int(raw_level)
    advisories = values.get("advisories", "none") or "none"
    if level == 0 and advisories != "none":
        raise Finding(
            f"{path}: level is 0 but advisories is {advisories!r}. "
            "The level resets to 0 at a milestone rebase and the advisory list "
            "resets with it."
        )
    if level > 0 and advisories == "none":
        raise Finding(
            f"{path}: level is {level} but no advisories are recorded. "
            "Each urgent cherry-pick names its upstream advisory "
            "(chromium/patches/security/README.md)."
        )
    return level, advisories


def measure_patch_queue(root: str) -> tuple[int, int]:
    """Patch count and modified upstream lines, in the queue's apply order."""
    queue = os.path.join(root, "chromium", "patches")
    patches: list[str] = []
    for directory in (queue, os.path.join(queue, "security")):
        if not os.path.isdir(directory):
            continue
        patches.extend(
            os.path.join(directory, name)
            for name in sorted(os.listdir(directory))
            if name.endswith(".patch")
        )
    lines = 0
    for patch in patches:
        with open(patch, "rb") as handle:
            lines += sum(1 for line in handle if CHANGED_LINE.match(line))
    return len(patches), lines


def read_pin_date(root: str) -> str:
    """ISO 8601 date of the last change to chromium/REVISION, or ""."""
    try:
        completed = subprocess.run(
            ["git", "-C", root, "log", "-1", "--format=%cs", "--", "chromium/REVISION"],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return ""
    if completed.returncode != 0:
        return ""
    date = completed.stdout.strip()
    return date if re.fullmatch(r"\d{4}-\d{2}-\d{2}", date) else ""


def collect(root: str) -> dict:
    pin = read_pin(root)
    level, advisories = read_security_level(root)
    patch_count, patch_lines = measure_patch_queue(root)
    return {
        "chromium_milestone": pin["milestone"],
        "chromium_tag": pin["tag"],
        "chromium_commit": pin["commit"],
        "pin_recorded_date": read_pin_date(root),
        "downstream_patch_count": patch_count,
        "downstream_modified_upstream_lines": patch_lines,
        "security_patch_level": level,
        "security_advisories": advisories,
    }


def cpp_string(value: str) -> str:
    """A C++ string literal. Refuses anything that would need escaping.

    Every field here is a milestone number, a version tag, a hex commit, an
    ISO date or an advisory reference list. If a value ever needs an escape,
    that is a sign the input file grew a shape this generator was not designed
    for, and stopping is the right answer.
    """
    if not re.fullmatch(r"[-A-Za-z0-9_.,:+/ ]*", value):
        raise Finding(
            f"value {value!r} contains characters this generator will not emit "
            "into a C++ literal"
        )
    return f'"{value}"'


def render_cc(values: dict) -> str:
    return f"""// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// GENERATED FILE — DO NOT EDIT.
// Written by //taffy/resources/branding/tools/write_upstream_provenance.py
// from chromium/REVISION, chromium/SECURITY_PATCH_LEVEL and
// chromium/patches/. Edit those, not this.

#include "taffy/resources/branding/taffy_upstream_provenance.h"

namespace taffy {{

const UpstreamProvenance& GetUpstreamProvenance() {{
  static constexpr UpstreamProvenance kProvenance = {{
      /*chromium_milestone=*/{cpp_string(values["chromium_milestone"])},
      /*chromium_tag=*/{cpp_string(values["chromium_tag"])},
      /*chromium_commit=*/{cpp_string(values["chromium_commit"])},
      /*pin_recorded_date=*/{cpp_string(values["pin_recorded_date"])},
      /*downstream_patch_count=*/{values["downstream_patch_count"]},
      /*downstream_modified_upstream_lines=*/{values["downstream_modified_upstream_lines"]},
      /*security_patch_level=*/{values["security_patch_level"]},
      /*security_advisories=*/{cpp_string(values["security_advisories"])},
  }};
  return kProvenance;
}}

}}  // namespace taffy
"""


def inputs_of(root: str) -> list[str]:
    """Every file the values were derived from, for the depfile."""
    paths = [
        os.path.join(root, "chromium", "REVISION"),
        os.path.join(root, "chromium", "SECURITY_PATCH_LEVEL"),
    ]
    queue = os.path.join(root, "chromium", "patches")
    for directory in (queue, os.path.join(queue, "security")):
        if os.path.isdir(directory):
            paths.extend(
                os.path.join(directory, name)
                for name in sorted(os.listdir(directory))
                if name.endswith(".patch")
            )
    return paths


def write(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output-cc", help="path of the generated translation unit")
    parser.add_argument("--output-json", help="path of the artifact-manifest sidecar")
    parser.add_argument("--print", action="store_true", dest="print_only")
    parser.add_argument("--repo-root", help="override the repository root")
    parser.add_argument("--depfile", help="Ninja depfile to write (GN action use)")
    args = parser.parse_args(argv)

    if not (args.output_cc or args.output_json or args.print_only):
        parser.error("one of --output-cc, --output-json or --print is required")

    try:
        root = repo_root.resolve(args.repo_root)
        values = collect(root)
        if args.output_cc:
            write(args.output_cc, render_cc(values))
        if args.output_json:
            write(args.output_json, json.dumps(values, indent=2, sort_keys=True) + "\n")
        if args.print_only:
            print(json.dumps(values, indent=2, sort_keys=True))
        if args.depfile:
            first = args.output_cc or args.output_json
            repo_root.write_depfile(args.depfile, first, inputs_of(root))
    except (Finding, repo_root.RepoRootError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
