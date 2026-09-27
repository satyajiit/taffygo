#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Render the artifacts section of a release manifest by hashing real files.

Authority boundary: this module turns a list of files into attested artifact
entries. It hashes what is on disk and records what it found; it never accepts
a digest or a size from the caller, because a manifest whose digests were typed
in attests to nothing.

Owning milestone: M1 (WP-M1-07).

  --artifact KIND:PATH[:ABI]   repeat, once per file
  --base DIR                   paths are recorded relative to this directory
  --out FILE                   where to write the fragment

A file that does not exist is an error naming that exact file: an artifact
listed in a manifest and absent from the upload is the failure mode this
section exists to catch.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import repo_facts  # noqa: E402  (after sys.path setup)

KINDS = (
    "apk", "test-apk", "bundle", "mapping", "symbols", "sbom",
    "license-inventory", "vulnerability-report", "test-report",
    "benchmark-report", "benchmark-run", "evidence-index", "rollback-evidence", "trace",
    "provenance", "drill-record",
)


def parse_artifact(argument: str):
    parts = argument.split(":")
    if len(parts) < 2:
        raise ValueError(f"{argument!r} is not KIND:PATH[:ABI]")
    kind, path = parts[0], parts[1]
    abi = parts[2] if len(parts) > 2 else ""
    if kind not in KINDS:
        raise ValueError(
            f"{kind!r} is not an artifact kind. Known kinds: {', '.join(KINDS)}"
        )
    return kind, path, abi


def main() -> int:
    parser = argparse.ArgumentParser(prog="artifact_fragment.py",
                                     description=__doc__.splitlines()[0])
    parser.add_argument("--artifact", action="append", default=[], required=True)
    parser.add_argument("--base", default=".")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    entries = []
    for argument in args.artifact:
        try:
            kind, path, abi = parse_artifact(argument)
        except ValueError as error:
            print(f"--artifact {error}", file=sys.stderr)
            return 2
        if not os.path.isfile(path):
            print(f"{path}: no such file, but it is listed as a {kind} artifact.",
                  file=sys.stderr)
            print("    fix: Produce it before composing the manifest, or drop the "
                  "--artifact entry. A manifest never lists a file it has not hashed.",
                  file=sys.stderr)
            return 1
        relative = os.path.relpath(path, args.base)
        if repo_facts.path_beneath(args.base, relative) is None:
            print(f"{path}: is outside the artifact directory {args.base}.", file=sys.stderr)
            print("    fix: Copy the file beneath --base before attesting it; a "
                  "portable manifest never records ../ or an absolute host path.",
                  file=sys.stderr)
            return 1
        entry = {
            "path": relative,
            "kind": kind,
            "sha256": repo_facts.sha256_file(path),
            "size_bytes": repo_facts.file_size(path),
        }
        if abi:
            entry["abi"] = abi
        entries.append(entry)

    entries.sort(key=lambda entry: (entry["kind"], entry["path"]))
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump({"artifacts": entries}, handle, indent=2)
        handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
