#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Project the `bundled` block into the two files that consume it.

The block in `catalog/source/assets.json` says which artifacts decision 0202
carries in the package. Two other build systems need the same facts and
neither can read JSON at the moment it needs them:

  * GN packages the files into the APK and needs a source list and the
    destinations they take inside it.
  * The browser seeds them into the delivery store before the first scan and
    needs, per artifact, the identity and revision to install it under, the
    path to ask the package for, and the length and digest to refuse it by.

Writing either by hand would be a third and fourth copy of facts that already
have one owner, and the failure would be quiet in both directions: a file left
out of the GN list is committed, checked, and in no APK, and a row left out of
the header is an artifact the browser never seeds and the core then plans a
download for against an origin that is empty.

`bundled_rows.py` holds the rules those facts are checked by. This file holds
only the projection, and refuses to write one from a source that does not
pass those rules.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import bundled_rows

HERE = os.path.dirname(os.path.abspath(__file__))
CATALOG = os.path.dirname(HERE)
SOURCE = os.path.join(CATALOG, "source", "assets.json")
#: Six levels above the crate, the same walk `generate_catalog.py` makes and
#: for the same reason: asset-plane, rust, core, delivery, components,
#: taffy-core. Every path this file writes is named from the repository root,
#: because those are the paths a person reads them at.
REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(CATALOG), *([os.pardir] * 6))
)

#: Where the package keeps them, in the form `OpenApkAsset` takes. The
#: `assets/` prefix is part of that form — see `ReadBundledFilterListsBlocking`
#: in taffy-core/browser/filtering_list_reader.cc, which is the other caller.
APK_ASSET_DIRECTORY = "taffy-delivery"

HEADER = "taffy-core/browser/assets/generated/bundled_asset_rows.h"
#: Deliberately beside the artifact directory rather than inside it. That
#: directory holds artifacts and the record describing them, and both
#: gates over it are totality rules — every file there that is not the
#: record must be a bundled artifact. A build file living among them
#: would have to be excused by name in two checkers, and each excuse is a
#: hole somebody has to keep narrow.
GNI = "taffy-core/components/delivery/bundled_assets.gni"

NOTICE = """// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""

GN_NOTICE = """# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""


def apk_path(entry: dict) -> str:
    """The path the package holds one artifact at."""
    return f"assets/{APK_ASSET_DIRECTORY}/{entry['file']}"


def transfer_bytes(source: dict, entry: dict) -> int:
    """The length the catalogue pins for this artifact.

    Read from the variant rather than from the entry's own `bytes`, because
    the browser refuses the artifact against what the *catalogue* says and
    those two are only equal because `bundled_rows` insists on it. Taking it
    from the row the browser will compare against keeps that true by
    construction rather than by memory.
    """
    for asset in source["assets"]:
        if asset["id"] != entry["asset"] or asset["revision"] != entry["revision"]:
            continue
        for variant in asset["variants"]:
            if variant["platform"] == entry["satisfies"][0]:
                return variant["transfer_bytes"]
    raise AssertionError(f"{entry['file']}: no variant, which the rules forbid")


def render_header(source: dict) -> str:
    """The table the browser's seeder reads."""
    guard = "TAFFY_BROWSER_ASSETS_GENERATED_BUNDLED_ASSET_ROWS_H_"
    lines = [
        NOTICE,
        "// GENERATED FILE — DO NOT EDIT.",
        "//",
        "// Written by taffy-core/components/delivery/core/rust/asset-plane/",
        "// catalog/tools/generate_bundled_assets.py from that catalog's source",
        "// `bundled` block. Change the block and regenerate; an edit here is",
        "// overwritten and its `--check` fails first.",
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stdint.h>",
        "",
        "#include <array>",
        "#include <string_view>",
        "",
        "namespace taffy {",
        "",
        "// One artifact the package carries, and everything needed to install",
        "// it without asking anything outside this binary.",
        "struct BundledAssetRow {",
        "  // The identity and revision the delivery store installs it under.",
        "  std::string_view asset_id;",
        "  std::string_view asset_revision;",
        "  // What to ask the package for, in `base::android::OpenApkAsset` form.",
        "  std::string_view apk_asset_path;",
        "  // What the catalogue pins. The seeder refuses anything else rather",
        "  // than installing bytes the core would later plan a repair for.",
        "  uint64_t transfer_bytes;",
        "  std::string_view digest;",
        "};",
        "",
    ]
    rows = source["bundled"]
    lines.append(
        f"inline constexpr std::array<BundledAssetRow, {len(rows)}> kBundledAssetRows = {{{{"
    )
    for entry in sorted(rows, key=lambda row: row["file"]):
        lines.append("    {")
        lines.append(f'        "{entry["asset"]}",')
        lines.append(f'        "{entry["revision"]}",')
        lines.append(f'        "{apk_path(entry)}",')
        lines.append(f"        {transfer_bytes(source, entry)}u,")
        lines.append(f'        "{entry["digest"]}",')
        lines.append("    },")
    lines.append("}};")
    lines.extend(["", "}  // namespace taffy", "", f"#endif  // {guard}", ""])
    return "\n".join(lines)


def render_gni(source: dict) -> str:
    """The source list and destinations GN packages them from."""
    rows = sorted(source["bundled"], key=lambda row: row["file"])
    lines = [
        GN_NOTICE,
        "# GENERATED FILE — DO NOT EDIT.",
        "#",
        "# Written by taffy-core/components/delivery/core/rust/asset-plane/",
        "# catalog/tools/generate_bundled_assets.py from that catalog's source",
        "# `bundled` block. The two lists are positional: entry N of one names",
        "# the same artifact as entry N of the other, which is what",
        "# `android_assets` requires of renaming_sources and",
        "# renaming_destinations.",
        "",
        "taffy_bundled_asset_sources = [",
    ]
    lines += [f'  "{bundled_rows.BUNDLED_DIRECTORY.rsplit("/", 1)[-1]}/{entry["file"]}",' for entry in rows]
    lines += ["]", "", "taffy_bundled_asset_destinations = ["]
    lines += [f'  "{APK_ASSET_DIRECTORY}/{entry["file"]}",' for entry in rows]
    lines += ["]", ""]
    return "\n".join(lines)


def outputs(source: dict) -> dict:
    return {HEADER: render_header(source), GNI: render_gni(source)}


def self_test() -> int:
    """The projection says the same thing as the block it came from."""
    failures: list[str] = []
    with open(SOURCE, "r", encoding="utf-8") as handle:
        source = json.load(handle)

    header = render_header(source)
    gni = render_gni(source)
    for entry in source["bundled"]:
        if f'"{apk_path(entry)}"' not in header:
            failures.append(f"{entry['file']} has no row in the header")
        if f'"{entry["digest"]}"' not in header:
            failures.append(f"{entry['file']}'s digest is not in the header")
        if f'"{APK_ASSET_DIRECTORY}/{entry["file"]}"' not in gni:
            failures.append(f"{entry['file']} has no destination in the GN list")

    # The two GN lists are read positionally by `android_assets`, so a pair
    # that disagrees in order is a file packaged under another file's name —
    # which nothing downstream can see, because both names exist.
    sources = [line for line in gni.splitlines() if line.startswith('  "')]
    half = len(sources) // 2
    for left, right in zip(sources[:half], sources[half:]):
        if left.strip().strip('",').rsplit("/", 1)[-1] not in right:
            failures.append(f"the GN lists disagree in order: {left} against {right}")

    if len(source["bundled"]) != half:
        failures.append(f"the GN list holds {half} of {len(source['bundled'])} rows")

    rules = bundled_rows.bundled_finding(
        source, *bundled_rows.repo_readers(REPO_ROOT)
    )
    if rules:
        failures.append(f"the committed block does not pass its own rules: {rules}")

    for failure in failures:
        print(f"self-test: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(
        f"self-test: {len(source['bundled'])} bundled row(s) project into a header "
        "and a positional GN pair that agree with the block"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true", help="rewrite both files")
    group.add_argument("--check", action="store_true", help="fail if either is stale")
    group.add_argument("--self-test", action="store_true", help="check the projection")
    arguments = parser.parse_args()

    if arguments.self_test:
        return self_test()

    with open(SOURCE, "r", encoding="utf-8") as handle:
        source = json.load(handle)

    # A projection of a block that does not pass its own rules would be a
    # header stating a digest nothing measured, so the rules run first.
    finding = bundled_rows.bundled_finding(
        source, *bundled_rows.repo_readers(REPO_ROOT)
    )
    if finding:
        print(f"bundled assets: {finding}", file=sys.stderr)
        return 1

    stale = []
    for relative, rendered in outputs(source).items():
        path = os.path.join(REPO_ROOT, *relative.split("/"))
        if arguments.write:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(rendered)
            print(f"wrote {relative}")
            continue
        try:
            with open(path, "r", encoding="utf-8") as handle:
                existing = handle.read()
        except OSError:
            existing = ""
        if existing != rendered:
            stale.append(relative)

    if stale:
        print(
            "bundled assets: " + ", ".join(stale) + " no longer match the catalog "
            "source. Regenerate with `python3 taffy-core/components/delivery/core/"
            "rust/asset-plane/catalog/tools/generate_bundled_assets.py --write` and "
            "commit the result.",
            file=sys.stderr,
        )
        return 1
    if arguments.check:
        print(f"bundled assets: {len(source['bundled'])} row(s), up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
