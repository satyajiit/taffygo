#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""What gets written, and whether what is on disk still matches.

Authority boundary: the output plan and the two file operations over it.
`--write` and `--check` are the same plan read two ways, which is what makes
`--check` a real gate rather than a second implementation that can drift.
"""

from __future__ import annotations

import json
import os
import re

import render_cxx
import render_rust
from contract import Contract
from layout import (
    CXX_VERSION_OUT,
    RUST_OUT_DIR,
    VERSION_FILE,
    VERSION_RESTATEMENTS,
    relative,
)


def check_restatements(version: dict) -> list[str]:
    """Findings for hand-written copies of the protocol version that disagree.

    A restatement that has drifted is not a stale generated file: `--write`
    cannot fix it, because a generator does not own the line. So this reports
    the file, both values, and what to edit, rather than the regenerate
    instruction `check` gives for everything else.
    """
    expected = version["protocol_version"]
    findings: list[str] = []
    for path, pattern in sorted(VERSION_RESTATEMENTS.items()):
        if not os.path.exists(path):
            findings.append(f"missing  {relative(path)} (restates the protocol version)")
            continue
        with open(path, encoding="utf-8") as handle:
            found = re.search(pattern, handle.read())
        if not found:
            # The literal moved or was renamed. Silence here would be the
            # failure this check exists to prevent, so an unreadable
            # restatement is a finding rather than a pass.
            findings.append(
                f"unreadable  {relative(path)}: no protocol version matched "
                f"/{pattern}/; update VERSION_RESTATEMENTS in layout.py"
            )
            continue
        if found.group(1) != expected:
            findings.append(
                f"disagrees  {relative(path)} says {found.group(1)!r}, "
                f"{relative(VERSION_FILE)} says {expected!r}; edit the source file"
            )
    return findings

def build(contract: Contract, version: dict) -> dict[str, str]:
    outputs: dict[str, str] = {}
    for document_name in sorted(contract.store.documents):
        module = contract.modules[document_name]
        outputs[os.path.join(RUST_OUT_DIR, f"{module}.rs")] = render_rust.module(
            contract, document_name
        )
    outputs[os.path.join(RUST_OUT_DIR, "version.rs")] = render_rust.version(version)
    outputs[os.path.join(RUST_OUT_DIR, "mod.rs")] = render_rust.mod(contract)
    outputs[CXX_VERSION_OUT] = render_cxx.version(version)
    return outputs


def load_version() -> dict:
    with open(VERSION_FILE, encoding="utf-8") as handle:
        return json.load(handle)


def write(outputs: dict[str, str]) -> int:
    changed = 0
    for path in sorted(outputs):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        existing = None
        if os.path.exists(path):
            with open(path, encoding="utf-8") as handle:
                existing = handle.read()
        if existing == outputs[path]:
            continue
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(outputs[path])
        changed += 1
        print(f"wrote {relative(path)}")
    print(f"{len(outputs)} generated file(s), {changed} changed")
    return 0


def check(outputs: dict[str, str]) -> int:
    stale: list[str] = []
    for path in sorted(outputs):
        expected = outputs[path]
        if not os.path.exists(path):
            stale.append(f"missing  {relative(path)}")
            continue
        with open(path, encoding="utf-8") as handle:
            found = handle.read()
        if found == expected:
            continue
        expected_lines = expected.splitlines()
        found_lines = found.splitlines()
        first = next(
            (
                index + 1
                for index, (a, b) in enumerate(zip(expected_lines, found_lines))
                if a != b
            ),
            min(len(expected_lines), len(found_lines)) + 1,
        )
        stale.append(
            f"differs  {relative(path)} "
            f"(expected {len(expected_lines)} lines, found {len(found_lines)}; "
            f"first difference at line {first})"
        )
    for line in stale:
        print(line)
    if stale:
        print()
        print(f"{len(stale)} of {len(outputs)} generated file(s) are out of date.")
        print("Run: python3 taffy-core/contracts/bip/codegen/generate.py --write")
        return 1
    print(f"{len(outputs)} generated file(s) are up to date")

    # Reported after the generated files and gated separately, because the
    # remediation is the opposite one: nothing here is regenerated.
    restated = check_restatements(load_version())
    for line in restated:
        print(line)
    if restated:
        print()
        print(f"{len(restated)} hand-written copy(ies) of the protocol version disagree.")
        return 1
    print(f"{len(VERSION_RESTATEMENTS)} hand-written copy(ies) agree")
    return 0
