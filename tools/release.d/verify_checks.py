#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The checks behind `./tools/release verify`.

Authority boundary: the schema says what a manifest must contain; these
functions say whether what it contains is true. Each one re-reads the
repository or the disk and compares. Nothing here restates a rule the schema
already enforces, and nothing here decides policy that a document owns — the
rules come from docs/development/testing-and-delivery.md sections 9.5, 12, 13
and 16 and from PAR-SEC-002, PAR-SEC-004 and PAR-SEC-010 in the browser parity
matrix. Running them in order and rendering the result is verify.py.

Owning milestone: M1 (WP-M1-07).
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import repo_facts  # noqa: E402  (after sys.path setup)
from schema_validate import Validator  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA_PATH = os.path.join(HERE, "manifest-schema.json")
UNPINNED = "TO-VERIFY"

class Report:
    def __init__(self) -> None:
        self.findings: list[dict] = []
        self.checks = 0

    def check(self, name: str) -> None:
        self.checks += 1

    def fail(self, item: str, message: str, remediation: str) -> None:
        self.findings.append({"severity": "fail", "item": item,
                              "message": message, "remediation": remediation})

    def warn(self, item: str, message: str, remediation: str) -> None:
        self.findings.append({"severity": "warn", "item": item,
                              "message": message, "remediation": remediation})

    @property
    def failures(self) -> int:
        return sum(1 for f in self.findings if f["severity"] == "fail")


def get(node, *path, default=None):
    for key in path:
        if not isinstance(node, dict) or key not in node:
            return default
        node = node[key]
    return node


# --- checks ------------------------------------------------------------------

def check_schema(manifest, profile, report: Report) -> None:
    report.check("schema")
    with open(SCHEMA_PATH, encoding="utf-8") as handle:
        validator = Validator(json.load(handle))
    for finding in validator.validate(manifest, profile):
        report.fail(finding.pointer, finding.message, finding.remediation)


def check_repository_agreement(manifest, root, report: Report) -> None:
    """The manifest claims a source state; the repository is right here."""
    report.check("repository")
    pins = os.path.join(root, "chromium", "REVISION")
    if os.path.exists(pins):
        recorded = _kv(pins)
        for field, key in (("commit", "commit"), ("tag", "tag"), ("milestone", "milestone")):
            claimed = get(manifest, "chromium", field)
            actual = recorded.get(key)
            if actual and claimed and claimed != actual:
                report.fail(
                    f"chromium.{field}",
                    f'the manifest says "{claimed}", chromium/REVISION says "{actual}"',
                    "Rebuild from the pinned revision. A manifest that disagrees with "
                    "the pin describes a checkout nobody can reproduce.",
                )
    level_file = os.path.join(root, "chromium", "SECURITY_PATCH_LEVEL")
    if os.path.exists(level_file):
        actual = _kv(level_file).get("level")
        claimed = get(manifest, "chromium", "security_patch_level")
        if actual is not None and claimed is not None and str(claimed) != actual:
            report.fail(
                "chromium.security_patch_level",
                f'the manifest says {claimed}, chromium/SECURITY_PATCH_LEVEL says {actual}',
                "The urgent-security fast path bumps the file and the build reads it; "
                "one of the two did not happen. See "
                "docs/development/chromium-fork-and-build.md section 1.4.",
            )
    actual_digest = repo_facts.patch_queue_digest(root)
    claimed_digest = get(manifest, "chromium", "patch_queue", "digest")
    if claimed_digest and claimed_digest != actual_digest:
        report.fail(
            "chromium.patch_queue.digest",
            "the recorded patch-queue digest does not match chromium/patches/ as it "
            f"is now (manifest {claimed_digest[:12]}, repository {actual_digest[:12]})",
            "Either the queue changed after the build or the build used a different "
            "checkout. Rebuild; do not edit the manifest.",
        )
    counts = repo_facts.patch_counts(root)
    claimed_count = get(manifest, "chromium", "patch_queue", "count")
    if claimed_count is not None and claimed_count != counts["count"]:
        report.fail(
            "chromium.patch_queue.count",
            f"the manifest counts {claimed_count} patch(es), the repository has "
            f"{counts['count']}",
            "Regenerate the manifest from the checkout that produced the artifact.",
        )


def check_build_configuration(manifest, profile, report: Report) -> None:
    report.check("build")
    hermetic = get(manifest, "build", "hermetic")
    caches = get(manifest, "build", "caches", default={}) or {}
    enabled = [name for name, cache in caches.items() if cache.get("enabled")]
    if hermetic and enabled:
        report.fail(
            "build.caches",
            f"the build claims to be hermetic but records {', '.join(sorted(enabled))} "
            "as enabled",
            "The release lane builds without caches by design (decision 0013 lane "
            "table). Either the build was not hermetic or the manifest is wrong.",
        )
    if hermetic is False and not caches:
        report.fail(
            "build.caches",
            "a cached build records no cache layer at all",
            "Record siso, ccache and sccache with their enabled state and hit rate, "
            "or assert build.hermetic. Cache configuration is manifest evidence "
            "(testing-and-delivery section 13).",
        )
    if profile == "candidate":
        if get(manifest, "build", "profile") != "release-arm64":
            report.fail(
                "build.profile",
                f'a release candidate was built with "{get(manifest, "build", "profile")}"',
                "Release candidates are built from chromium/args/release-arm64.gn: "
                "official build, symbols retained (decision 0013).",
            )
        if not hermetic:
            report.fail(
                "build.hermetic",
                "a release candidate was not built hermetically",
                "Run the release lane, which builds clean with no cache by design.",
            )
        if get(manifest, "source", "taffy", "dirty"):
            report.fail(
                "source.taffy.dirty",
                "the working tree was dirty when the artifact was built",
                "A release candidate is built from a committed revision and nothing "
                "else. Commit or clean the tree and rebuild.",
            )


def check_signing(manifest, profile, report: Report) -> None:
    report.check("signing")
    signing = manifest.get("signing") or {}
    separation = signing.get("separation") or {}
    source = signing.get("credential_source")
    build_job, signing_job = separation.get("build_job"), separation.get("signing_job")

    if source != "none":
        if build_job and signing_job and build_job == signing_job:
            report.fail(
                "signing.separation",
                f'one job "{build_job}" both builds and signs',
                "PAR-SEC-004: building and signing are separate steps with separate "
                "credentials, and the manifest has to name them separately. The "
                "procedure is docs/development/release-and-incident-runbook.md.",
            )
    if profile == "candidate":
        if source == "none":
            report.fail("signing.credential_source",
                        "a release candidate is unsigned",
                        "Promote through the signing job; an unsigned artifact is not "
                        "a candidate.")
        elif source != "oidc" and source != "hardware-custody":
            report.fail("signing.credential_source",
                        f'"{source}" is not a permitted credential source',
                        "Decision 0013: GitHub OIDC federation, or hardware custody "
                        "for the production key. No long-lived tokens.")


def check_provenance(manifest, artifacts_dir, profile, report: Report) -> None:
    report.check("provenance")
    provenance = manifest.get("provenance") or {}
    claimed_sha = get(provenance, "invocation", "sha")
    source_sha = get(manifest, "source", "taffy", "revision")
    if claimed_sha and source_sha and claimed_sha != source_sha:
        report.fail(
            "provenance.invocation.sha",
            f"provenance names revision {claimed_sha[:12]} but the artifact was built "
            f"from {source_sha[:12]}",
            "Provenance that points at a different revision than the build is worse "
            "than none; regenerate both from the same job.",
        )
    path = provenance.get("path")
    if path:
        full = repo_facts.path_beneath(artifacts_dir, path)
        if full is None:
            report.fail("provenance.path", f"{path} escapes the artifact directory",
                        "Attach provenance beneath the upload directory and record a "
                        "relative path.")
        elif not os.path.isfile(full):
            report.fail("provenance.path", f"{path} is not present",
                        "Attach the attestation the release lane emitted.")
        elif provenance.get("sha256") and repo_facts.sha256_file(full) != provenance["sha256"]:
            report.fail("provenance.sha256", f"{path} does not match its digest",
                        "Regenerate the attestation and the manifest together.")
    elif profile == "candidate":
        report.fail("provenance.path", "no attestation file accompanies the candidate",
                    "The release lane emits one; upload it with the artifact.")


def check_toolchain(manifest, profile, report: Report) -> None:
    report.check("toolchain")
    unpinned = [row["component"] for row in manifest.get("toolchain", [])
                if row.get("version") == UNPINNED]
    if not unpinned:
        return
    message = f"{len(unpinned)} toolchain row(s) are unpinned: {', '.join(unpinned)}"
    remedy = ("Each is retired by the milestone that owns it; see the retirement "
              "table in TOOLCHAIN.md. A release candidate cannot ship with a pin "
              "nobody has verified.")
    if profile == "candidate":
        report.fail("toolchain", message, remedy)
    else:
        report.warn("toolchain", message, remedy)


def check_drill(manifest, artifacts_dir, profile, report: Report) -> None:
    if profile != "drill":
        return
    report.check("drill")
    drill = manifest.get("security_drill") or {}
    path = drill.get("record_path")
    if path:
        full = repo_facts.path_beneath(artifacts_dir, path)
        if full is None:
            report.fail("security_drill.record_path",
                        f"{path} escapes the artifact directory",
                        "Attach the drill record beneath the upload directory and "
                        "record a relative path.")
        elif not os.path.isfile(full):
            report.fail("security_drill.record_path", f"{path} is not present",
                        "The timing record is the drill's whole output. Produce it "
                        "with ./tools/chromium/security-patch report.")
        elif drill.get("record_sha256") and repo_facts.sha256_file(full) != drill["record_sha256"]:
            report.fail("security_drill.record_sha256",
                        f"{path} does not match its digest",
                        "Re-attach the record the drill actually produced.")
    if get(manifest, "chromium", "security_patch_level") == 0:
        report.fail("chromium.security_patch_level",
                    "the drill produced an artifact at security patch level 0",
                    "The fast path bumps chromium/SECURITY_PATCH_LEVEL; an artifact "
                    "that carries no cherry-pick did not exercise the path.")


def _kv(path: str) -> dict:
    values = {}
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if "=" in line:
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip()
    return values
