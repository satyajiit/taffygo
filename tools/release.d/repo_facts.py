#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Facts read directly off disk: digests, sizes, and the Chromium patch queue.

Authority boundary: this module is the single implementation of every byte-level
fact a manifest records. It exists because a digest computed one way when the
manifest is written and another way when it is verified is not a check — it is
two programs agreeing by luck. repository_fragment.py and verify.py both import
this, so there is exactly one algorithm.

Owning milestone: M1 (WP-M1-07).

It reads files and nothing else: no git, no pins, no environment. Reading the
pin register is tools/lib and tools/release.d/collect.sh.
"""

from __future__ import annotations

import hashlib
import os

BLOCK = 1 << 20

# Every committed file that locks a dependency set. The Gradle wrapper
# properties are here because they carry the distribution checksum: that file
# locks Gradle itself.
LOCKFILES = (
    "Cargo.lock",
    "pnpm-lock.yaml",
    "gradle/libs.versions.toml",
    "gradle/wrapper/gradle-wrapper.properties",
)


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(BLOCK), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_string(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def file_size(path: str) -> int:
    return os.path.getsize(path)


def path_beneath(base: str, relative: str) -> str | None:
    """Resolve an attested relative path without allowing upload escape.

    realpath closes both ``..`` traversal and a symlink beneath the upload that
    points outside it. Absolute paths are rejected even when they happen to sit
    below the base; a portable manifest never records a host-specific path.
    """
    if (
        not isinstance(relative, str)
        or not relative
        or os.path.isabs(relative)
        or os.pardir in relative.split(os.sep)
    ):
        return None
    resolved_base = os.path.realpath(base)
    resolved = os.path.realpath(os.path.join(resolved_base, relative))
    try:
        return resolved if os.path.commonpath((resolved_base, resolved)) == resolved_base else None
    except ValueError:
        return None


def patch_files(root: str) -> list:
    """The queue in application order: chromium/patches/*.patch, then
    chromium/patches/security/*.patch. The same order tools/chromium applies."""
    base = os.path.join(root, "chromium", "patches")
    ordered = []
    for directory in (base, os.path.join(base, "security")):
        if not os.path.isdir(directory):
            continue
        ordered.extend(
            os.path.join(directory, name)
            for name in sorted(os.listdir(directory))
            if name.endswith(".patch") and os.path.isfile(os.path.join(directory, name))
        )
    return ordered


def patch_queue_digest(root: str) -> str:
    """One digest over the whole queue: each patch's repository-relative path
    and its content digest, in application order.

    Two checkouts with the same Chromium commit and the same queue digest hold
    the same source. That is the entire reason the manifest records it, and it
    is why an empty queue still produces a digest rather than nothing."""
    listing = "".join(
        f"{os.path.relpath(path, root)} {sha256_file(path)}\n" for path in patch_files(root)
    )
    return sha256_string(listing)


def patch_counts(root: str) -> dict:
    """count, security_count and modified upstream lines, by the same rule the
    fork-debt gate in ./tools/check uses: a line added or removed by the diff,
    excluding the ---/+++ file headers."""
    files = patch_files(root)
    security = os.path.join(root, "chromium", "patches", "security")
    modified = 0
    for path in files:
        with open(path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if line.startswith(("+++", "---")):
                    continue
                if line.startswith(("+", "-")):
                    modified += 1
    return {
        "count": len(files),
        "security_count": sum(1 for path in files if path.startswith(security + os.sep)),
        "modified_upstream_lines": modified,
    }


def lock_digests(root: str) -> list:
    rows = []
    for relative in LOCKFILES:
        path = os.path.join(root, relative)
        if os.path.exists(path):
            rows.append({"file": relative, "sha256": sha256_file(path)})
    return rows
