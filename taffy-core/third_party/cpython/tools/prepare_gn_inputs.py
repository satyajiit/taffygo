#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Stages configure-time CPython outputs for GN; GN never runs configure.

This is a roll-time tool.  Its two build directories must have been produced
by ``build_interpreter.py`` from the pinned source and Chromium toolchain.  It
copies only the ABI configuration and ABI-neutral frozen bytecode headers;
the reviewed built-in module table remains the smaller committed
``generated/Modules/config.c``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys

FROZEN = "Python/frozen_modules"
REQUIRED_MACROS = (
    "#define ANDROID_API_LEVEL 29",
    "/* #undef WITH_MIMALLOC */",
    "/* #undef Py_REMOTE_DEBUG */",
)


def digest(path: str) -> str:
    value = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def validate_build(path: str) -> list[str]:
    findings = []
    config = os.path.join(path, "pyconfig.h")
    if not os.path.isfile(config):
        return [config + ": missing"]
    with open(config, encoding="utf-8") as handle:
        content = handle.read()
    findings += [config + ": missing " + macro for macro in REQUIRED_MACROS if macro not in content]
    frozen = os.path.join(path, FROZEN)
    if not os.path.isdir(frozen):
        findings.append(frozen + ": missing")
    return findings


def copy_file(source: str, destination: str) -> None:
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    shutil.copyfile(source, destination)


def stage(arm64: str, x64: str, extra_frozen: str, output: str) -> dict[str, object]:
    findings = validate_build(arm64) + validate_build(x64)
    if findings:
        raise ValueError("\n".join(findings))
    copied = []
    for abi, build in (("android-arm64", arm64), ("android-x64", x64)):
        destination = os.path.join(output, abi, "pyconfig.h")
        copy_file(os.path.join(build, "pyconfig.h"), destination)
        copied.append(destination)

    arm_frozen = os.path.join(arm64, FROZEN)
    x64_frozen = os.path.join(x64, FROZEN)
    for name in sorted(os.listdir(arm_frozen)):
        first, second = os.path.join(arm_frozen, name), os.path.join(x64_frozen, name)
        if not os.path.isfile(first) or digest(first) != digest(second):
            raise ValueError(name + ": frozen output differs by ABI")
        destination = os.path.join(output, FROZEN, name)
        copy_file(first, destination)
        copied.append(destination)
    destination = os.path.join(output, "taffy_frozen.h")
    copy_file(extra_frozen, destination)
    copied.append(destination)
    # The module table is deliberately reviewed rather than copied from the
    # configure build, whose table contains every module the host happened to
    # find.  Preserve it and bind its digest into the same provenance record.
    module_table = os.path.join(output, "Modules", "config.c")
    if not os.path.isfile(module_table):
        raise ValueError(module_table + ": reviewed built-in table is missing")
    copied.append(module_table)

    record = {
        "format": 1,
        "generated_by": "taffy-core/third_party/cpython/tools/prepare_gn_inputs.py",
        "source_version": "3.14.7",
        "configure_runs_during_gn": False,
        "files": {
            os.path.relpath(path, output): digest(path)
            for path in sorted(copied)
        },
    }
    with open(os.path.join(output, "provenance.json"), "w", encoding="utf-8") as handle:
        json.dump(record, handle, indent=2, sort_keys=True)
        handle.write("\n")
    return record


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--arm64-build", required=True)
    parser.add_argument("--x64-build", required=True)
    parser.add_argument("--extra-frozen", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    try:
        result = stage(args.arm64_build, args.x64_build, args.extra_frozen, args.output)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    print(json.dumps({"files": len(result["files"])}, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
