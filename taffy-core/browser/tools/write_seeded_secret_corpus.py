#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate the seeded-secret corpus PAR-SEC-009 is measured against.

`test-fixtures/web/manifest.json` is the single authority for the canaries: the
token, its sensitivity class and the fixture page that carries it. This script
turns the `canaries` array into a C++ translation unit defining the accessors
declared in `browser/seeded_secret_corpus.h`, so the browser-process leak suite
runs against the same tokens the fixture pages actually contain.

Why generate instead of copying the tokens into a test file:

  * A corpus change is a version bump with a like-for-like comparison
    (`test-fixtures/web/README.md`). A hand-copied list would drift past that
    rule silently, and the first sign would be a canary nobody was checking.
  * The corpus version travels with the data, so a suite result names what it
    ran against.

The same repository-root problem the provenance generator has applies here:
`taffy-core/` is symlink-mounted into the Chromium
checkout, and `test-fixtures/` lives outside that mount. The resolution and the
depfile mechanics are shared rather than reimplemented — this script imports
`repo_root` from `//taffy/resources/branding/tools`, which is the one place
that logic exists.

Modes:

  --output-cc PATH   write the generated translation unit
  --print            print the parsed corpus as JSON on stdout, write nothing

Exit status: 0 clean, 1 on any finding. Stdlib only.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
# repo_root.py lives with the branding generator, which is the one place the
# overlay-mount walk is implemented. Importing it rather than copying it keeps
# a single definition of "where is the repository".
sys.path.insert(
    0,
    os.path.abspath(
        os.path.join(_THIS_DIR, "..", "..", "resources", "branding", "tools")
    ),
)

import repo_root  # noqa: E402

MANIFEST_RELATIVE_PATH = os.path.join("test-fixtures", "web", "manifest.json")

# The prefix every canary carries. It is asserted rather than assumed: a token
# without it would not be caught by the scrubber's tripwire rule, and a corpus
# that silently added one would leave a hole in the suite.
REQUIRED_TOKEN_PREFIX = "TAFFYGO-CANARY-"


class Finding(Exception):
    """Something the caller must fix; never worked around."""


def load_canaries(manifest_path: str) -> tuple[str, list[dict[str, str]]]:
    if not os.path.exists(manifest_path):
        raise Finding(f"fixture manifest not found: {manifest_path}")
    with open(manifest_path, encoding="utf-8") as handle:
        manifest = json.load(handle)

    version = str(manifest.get("version", "")).strip()
    if not version:
        raise Finding(f"{manifest_path} has no 'version'; a suite result must "
                      "name the corpus version it ran against")

    raw = manifest.get("canaries")
    if not isinstance(raw, list) or not raw:
        raise Finding(f"{manifest_path} has no 'canaries' array")

    seen: set[str] = set()
    canaries: list[dict[str, str]] = []
    for index, entry in enumerate(raw):
        token = str(entry.get("token", "")).strip()
        secret_class = str(entry.get("class", "")).strip()
        carried_by = entry.get("carried_by") or []
        if not token:
            raise Finding(f"canary {index} has no token")
        if not token.startswith(REQUIRED_TOKEN_PREFIX):
            raise Finding(
                f"canary token {token!r} does not start with "
                f"{REQUIRED_TOKEN_PREFIX!r}. The scrubber's tripwire rule "
                "matches that prefix, so a token without it would not be "
                "counted when it leaked."
            )
        if token in seen:
            raise Finding(f"canary token {token!r} appears twice")
        seen.add(token)
        if not secret_class:
            raise Finding(f"canary {token!r} has no class")
        canaries.append(
            {
                "token": token,
                "class": secret_class,
                "carried_by": ", ".join(str(item) for item in carried_by),
            }
        )
    return version, canaries


def cpp_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def render(version: str, canaries: list[dict[str, str]]) -> str:
    rows = "\n".join(
        "    {{{token}, {klass}, {carried_by}}},".format(
            token=cpp_string(entry["token"]),
            klass=cpp_string(entry["class"]),
            carried_by=cpp_string(entry["carried_by"]),
        )
        for entry in canaries
    )
    return f"""// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// GENERATED FILE — DO NOT EDIT.
//
// Written by //taffy/browser/tools/write_seeded_secret_corpus.py
// from {MANIFEST_RELATIVE_PATH}, corpus version {version}. Editing this file
// would create a second source of truth for which canaries exist; edit the
// manifest instead, which is a version bump with a like-for-like comparison
// (test-fixtures/web/README.md).

#include "taffy/browser/seeded_secret_corpus.h"

namespace taffy {{

namespace {{

constexpr SeededSecret kSeededSecrets[] = {{
{rows}
}};

constexpr SeededSecret kOutOfRange = {{"", "", ""}};

}}  // namespace

std::string_view GetSeededSecretCorpusVersion() {{
  return {cpp_string(version)};
}}

size_t GetSeededSecretCount() {{
  return sizeof(kSeededSecrets) / sizeof(kSeededSecrets[0]);
}}

const SeededSecret& GetSeededSecret(size_t index) {{
  if (index >= GetSeededSecretCount()) {{
    return kOutOfRange;
  }}
  return kSeededSecrets[index];
}}

}}  // namespace taffy
"""


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-cc")
    parser.add_argument("--depfile")
    parser.add_argument("--repo-root")
    parser.add_argument("--print", action="store_true", dest="print_json")
    args = parser.parse_args(argv)

    if not args.output_cc and not args.print_json:
        parser.error("one of --output-cc or --print is required")

    try:
        root = repo_root.resolve(args.repo_root)
        manifest_path = os.path.join(root, MANIFEST_RELATIVE_PATH)
        version, canaries = load_canaries(manifest_path)
    except (repo_root.RepoRootError, Finding, json.JSONDecodeError) as error:
        sys.stderr.write(f"write_seeded_secret_corpus: {error}\n")
        return 1

    if args.print_json:
        json.dump({"version": version, "canaries": canaries}, sys.stdout, indent=2)
        sys.stdout.write("\n")

    if args.output_cc:
        os.makedirs(os.path.dirname(os.path.abspath(args.output_cc)) or ".", exist_ok=True)
        with open(args.output_cc, "w", encoding="utf-8") as handle:
            handle.write(render(version, canaries))
        if args.depfile:
            repo_root.write_depfile(args.depfile, args.output_cc, [manifest_path])

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
