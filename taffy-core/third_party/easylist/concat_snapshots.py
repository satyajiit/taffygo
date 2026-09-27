#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Concatenate pinned EasyList snapshots into one APK asset payload."""

from __future__ import annotations

import argparse
import os
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args()
    chunks: list[bytes] = []
    for path in args.inputs:
        with open(path, "rb") as handle:
            chunks.append(handle.read().rstrip(b"\n") + b"\n")
    os.makedirs(os.path.dirname(os.path.abspath(args.output)) or ".", exist_ok=True)
    with open(args.output, "wb") as handle:
        handle.write(b"".join(chunks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
