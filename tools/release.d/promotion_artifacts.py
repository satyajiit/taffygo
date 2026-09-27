#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Required candidate artifacts and cross-section attestation links."""

from __future__ import annotations


REQUIRED_KINDS = (
    "bundle",
    "apk",
    "test-apk",
    "mapping",
    "symbols",
    "sbom",
    "license-inventory",
    "vulnerability-report",
    "test-report",
    "benchmark-report",
    "benchmark-run",
    "evidence-index",
    "rollback-evidence",
    "provenance",
)


def _get(node, *path, default=None):
    for key in path:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node


def matches(manifest, kind: str, path=None, digest=None) -> bool:
    for artifact in manifest.get("artifacts", []):
        if artifact.get("kind") != kind:
            continue
        if path is not None and artifact.get("path") != path:
            continue
        if digest is not None and artifact.get("sha256") != digest:
            continue
        return True
    return False


def require_attested(manifest, section: str, kind: str, path, digest, report) -> None:
    if path and digest and matches(manifest, kind, path, digest):
        return
    report.fail(
        section,
        f"does not identify one attested {kind} artifact by path and digest",
        f"Attach the file as --artifact {kind}:<path> and copy that artifact's path "
        "and SHA-256 into the evidence section. Two unlinked claims are not evidence.",
    )


def check(manifest, report) -> None:
    artifacts = manifest.get("artifacts", [])
    present = {row.get("kind") for row in artifacts}
    for kind in REQUIRED_KINDS:
        if kind not in present:
            report.fail(
                f"artifacts.{kind}",
                f'a candidate carries no "{kind}" artifact',
                f"Produce and attest it with --artifact {kind}:<path>. Candidate "
                "evidence is a fixed set, not whatever files happened to exist.",
            )
    for artifact in artifacts:
        if artifact.get("kind") not in REQUIRED_KINDS:
            continue
        size = artifact.get("size_bytes")
        if not isinstance(size, int) or isinstance(size, bool) or size < 1:
            report.fail(
                f'artifacts.{artifact.get("kind")}',
                f'required artifact "{artifact.get("path")}" is empty',
                "Attach the produced output, not an empty placeholder. Candidate "
                "evidence must contain bytes and pass the manifest digest check.",
            )
    linked = (
        (
            "dependencies.sbom",
            "sbom",
            _get(manifest, "dependencies", "sbom", "path"),
            _get(manifest, "dependencies", "sbom", "sha256"),
        ),
        (
            "dependencies.license_inventory",
            "license-inventory",
            _get(manifest, "dependencies", "license_inventory", "path"),
            _get(manifest, "dependencies", "license_inventory", "sha256"),
        ),
        (
            "provenance",
            "provenance",
            _get(manifest, "provenance", "path"),
            _get(manifest, "provenance", "sha256"),
        ),
    )
    for section, kind, path, digest in linked:
        require_attested(manifest, section, kind, path, digest, report)
