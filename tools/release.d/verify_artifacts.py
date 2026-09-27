#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The checks that read the upload: artifacts, digests, and the inventory.

Authority boundary: everything here is a claim about files that accompany the
artifact, re-checked against the files themselves. Claims about the checkout —
revision, pins, patch queue, signing topology, provenance — are
verify_checks.py, and the rules both enforce belong to
docs/development/testing-and-delivery.md sections 9.5 and 13, and to
PAR-SEC-004 and PAR-SEC-010 in the browser parity matrix.

Owning milestone: M1 (WP-M1-07).
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import repo_facts  # noqa: E402  (after sys.path setup)
from verify_checks import get  # noqa: E402

# Artifact kinds a drill must carry. Candidate policy is deliberately deeper:
# promotion_policy.py owns its complete fixed evidence set and cross-links.
# A development manifest lists whatever it built.
REQUIRED_KINDS = {
    "development": [],
    "drill": [("apk", "bundle"), ("symbols",), ("sbom",), ("license-inventory",),
              ("drill-record",)],
    "candidate": [],
}

# Filenames that must never appear in an artifact set. Signing material is
# handed to the signing job by the platform, never carried beside the build.
KEY_SUFFIXES = (".jks", ".keystore", ".p12", ".pfx", ".pepk", ".pem", ".key", ".der")



def check_artifacts(manifest, artifacts_dir, profile, report: Report) -> None:
    report.check("artifacts")
    artifacts = manifest.get("artifacts") or []
    present_kinds = {artifact.get("kind") for artifact in artifacts}
    seen_files: dict[str, int] = {}
    seen_file_ids: dict[tuple[int, int], int] = {}
    for options in REQUIRED_KINDS.get(profile, []):
        if not present_kinds.intersection(options):
            report.fail(
                "artifacts." + "|".join(options),
                f'a "{profile}" manifest carries no {" or ".join(options)} artifact',
                f"Produce it and list it: ./tools/release manifest "
                f"--artifact {options[0]}:<path>. "
                "The evidence set is testing-and-delivery section 13.",
            )
    for index, artifact in enumerate(artifacts):
        path = artifact.get("path", "")
        pointer = f"artifacts[{index}] {path}"
        if path.lower().endswith(KEY_SUFFIXES):
            report.fail(
                pointer,
                "looks like signing key material and must never travel with an artifact",
                "Signing material is held by the signing job's platform credential "
                "store, never uploaded beside the build (PAR-SEC-004).",
            )
            continue
        full = repo_facts.path_beneath(artifacts_dir, path)
        if full is None:
            report.fail(
                pointer,
                "escapes the artifact directory",
                "Artifact paths are relative names beneath the upload directory. "
                "Copy the evidence into that directory before composing the manifest.",
            )
            continue
        duplicate_path = full in seen_files
        if duplicate_path:
            report.fail(
                pointer,
                f"resolves to the same file as artifacts[{seen_files[full]}]",
                "List each uploaded file once with its real artifact kind. One file "
                "cannot satisfy two evidence identities through duplicate or aliased paths.",
            )
        else:
            seen_files[full] = index
        if not os.path.isfile(full):
            report.fail(
                pointer,
                "is listed in the manifest and is not present",
                f"Look for it in {artifacts_dir}. An artifact named in a manifest and "
                "missing from the upload cannot be promoted; re-run the build job or "
                "point --artifacts-dir at the real upload.",
            )
            continue
        file_stat = os.stat(full)
        file_id = (file_stat.st_dev, file_stat.st_ino)
        if not duplicate_path and file_id in seen_file_ids:
            report.fail(
                pointer,
                f"is a hard link to the same file as artifacts[{seen_file_ids[file_id]}]",
                "List each uploaded file once with its real artifact kind. One file "
                "cannot satisfy two evidence identities through duplicate or aliased paths.",
            )
        else:
            seen_file_ids[file_id] = index
        actual = repo_facts.sha256_file(full)
        if actual != artifact.get("sha256"):
            report.fail(
                pointer,
                f"digest mismatch (manifest {str(artifact.get('sha256'))[:12]}, "
                f"file {actual[:12]})",
                "The file changed after the manifest was written. Rebuild; never "
                "update a digest to match a file.",
            )
        size = repo_facts.file_size(full)
        if size != artifact.get("size_bytes"):
            report.fail(pointer, f"size mismatch (manifest {artifact.get('size_bytes')}, "
                                 f"file {size})",
                        "Same cause as a digest mismatch: rebuild rather than edit.")
    if profile in ("drill", "candidate"):
        abis = set(get(manifest, "platform", "abis", default=[]) or [])
        covered = {a.get("abi") for a in artifacts if a.get("kind") == "symbols"}
        missing = abis - covered
        if missing and artifacts:
            report.fail(
                "artifacts.symbols",
                f"no symbol archive for {', '.join(sorted(missing))}",
                "A crash on an ABI with no symbols is an unreadable crash. Attach one "
                "symbol archive per packaged ABI.",
            )


def check_dependencies(manifest, artifacts_dir, profile, report: Report) -> None:
    report.check("dependencies")
    sbom = get(manifest, "dependencies", "sbom", default={}) or {}
    inventory = get(manifest, "dependencies", "license_inventory", default={}) or {}

    for label, section in (("sbom", sbom), ("license_inventory", inventory)):
        path = section.get("path")
        if not path:
            continue
        full = repo_facts.path_beneath(artifacts_dir, path)
        if full is None:
            report.fail(
                f"dependencies.{label}.path",
                f"{path} escapes the artifact directory",
                "Inventory paths are relative names beneath the upload directory. "
                "Attach the inventory rather than pointing outside the upload.",
            )
            continue
        if not os.path.isfile(full):
            report.fail(f"dependencies.{label}.path", f"{path} is not present",
                        "Generate it with ./tools/release sbom --out <dir> and upload "
                        "it with the artifact (PAR-SEC-010).")
        elif section.get("sha256") and repo_facts.sha256_file(full) != section["sha256"]:
            report.fail(f"dependencies.{label}.sha256", f"{path} does not match its digest",
                        "Regenerate the inventory and the manifest together.")

    ecosystems = sbom.get("ecosystems", [])
    for ecosystem in ecosystems:
        name, status = ecosystem.get("name"), ecosystem.get("status")
        if status == "complete":
            continue
        if status == "deferred":
            if not ecosystem.get("owner"):
                report.fail(f"dependencies.sbom.{name}",
                            "is deferred and names no owner",
                            "A deferred ecosystem names the track that produces it and "
                            "the file that imports it. An unowned gap is an unmeasured "
                            "gap.")
            elif profile == "candidate":
                blocker = (ecosystem.get("blocker") or "import the inventory").rstrip(".")
                report.fail(f"dependencies.sbom.{name}",
                            f"is still deferred to {ecosystem.get('owner')} on a "
                            "release candidate",
                            f"Before promoting: {blocker}. PAR-SEC-010 requires the "
                            "inventory to cover Chromium and the packaged native "
                            "libraries.")
            continue
        message = (f'coverage is "{status}" with '
                   f"{ecosystem.get('component_count', 0)} component(s) recorded")
        remedy = ecosystem.get("blocker") or (
            f"Close it with: {ecosystem.get('evidence_command') or './tools/release sbom'}"
        )
        if profile == "candidate":
            report.fail(f"dependencies.sbom.{name}", message, remedy)
        else:
            report.warn(f"dependencies.sbom.{name}", message, remedy)

    external = {
        ecosystem.get("name"): ecosystem for ecosystem in ecosystems
        if ecosystem.get("name") in ("chromium", "android-native")
        and ecosystem.get("status") == "complete"
    }
    required_external = (
        "schema_version", "kind", "source_revision", "upstream_revision",
        "profile", "target",
    )
    for name, ecosystem in external.items():
        missing = [field for field in required_external if not ecosystem.get(field)]
        if name == "android-native":
            missing.extend(
                field for field in ("artifact", "artifact_sha256", "package_name")
                if not ecosystem.get(field)
            )
        if missing:
            report.fail(
                f"dependencies.sbom.{name}",
                "claims complete coverage without " + ", ".join(missing),
                "Regenerate the track inputs with ./tools/release inventory; an "
                "unbound imported list is not evidence about this build.",
            )
            continue
        expected_kind = {
            "chromium": "chromium-shipped-license-inventory",
            "android-native": "android-packaged-native-inventory",
        }[name]
        if ecosystem.get("kind") != expected_kind:
            report.fail(
                f"dependencies.sbom.{name}.kind",
                f"is {ecosystem.get('kind')!r}, expected {expected_kind!r}",
                "Use the matching output from ./tools/release inventory.",
            )
        if ecosystem.get("upstream_revision") != get(
                manifest, "chromium", "commit"):
            report.fail(
                f"dependencies.sbom.{name}.upstream_revision",
                "does not match chromium.commit",
                "Generate the inventory against the checkout that built this artifact.",
            )
        if ecosystem.get("profile") != get(manifest, "build", "profile"):
            report.fail(
                f"dependencies.sbom.{name}.profile",
                "does not match build.profile",
                "Do not reuse an inventory from another GN profile.",
            )
        if ecosystem.get("target") != "//taffy/app/android:taffy_public_apk":
            report.fail(
                f"dependencies.sbom.{name}.target",
                "does not name the shipping TaffyGo product target",
                "Run ./tools/release inventory against the product target.",
            )

    if set(external) == {"chromium", "android-native"}:
        for field in ("source_revision", "upstream_revision", "profile", "target"):
            if external["chromium"].get(field) != external["android-native"].get(field):
                report.fail(
                    f"dependencies.sbom.android-native.{field}",
                    f"does not match the Chromium inventory's {field}",
                    "Generate both inputs in one ./tools/release inventory invocation.",
                )

    native = external.get("android-native")
    if native and native.get("artifact_sha256"):
        matching_apks = [
            artifact for artifact in manifest.get("artifacts", [])
            if artifact.get("kind") == "apk"
            and artifact.get("sha256") == native["artifact_sha256"]
        ]
        if len(matching_apks) != 1:
            report.fail(
                "dependencies.sbom.android-native.artifact_sha256",
                "does not identify exactly one attested APK",
                "Generate the native inventory from the exact APK supplied to "
                "./tools/release manifest --artifact apk:<path>.",
            )
        elif os.path.basename(matching_apks[0].get("path", "")) != native.get("artifact"):
            report.fail(
                "dependencies.sbom.android-native.artifact",
                "does not match the attested APK filename",
                "Keep the scanned package and the attested package together.",
            )
        package_id = get(manifest, "release", "package_id")
        if package_id and native.get("package_name") != package_id:
            report.fail(
                "dependencies.sbom.android-native.package_name",
                "does not match release.package_id",
                "Scan the TaffyGo APK named by this manifest.",
            )

    unresolved = inventory.get("unresolved")
    if profile == "candidate" and unresolved:
        report.fail("dependencies.license_inventory.unresolved",
                    f"{unresolved} component(s) have no resolved license",
                    "Every shipped component needs a license before the notices file "
                    "can be built. licenses.json lists them under unresolved_entries.")
    scan = get(manifest, "dependencies", "vulnerability_scan", default={}) or {}
    if profile == "candidate" and scan.get("status") == "not-run":
        report.fail("dependencies.vulnerability_scan.status",
                    "no dependency vulnerability scan ran",
                    "Run the scan on the release lane and record its result; "
                    "testing-and-delivery section 9.5 requires it before promotion.")


class _Report:
    def __init__(self) -> None:
        self.findings: list[dict] = []
        self.checks = 0

    def check(self, _name: str) -> None:
        self.checks += 1

    def fail(self, item: str, message: str, remediation: str) -> None:
        self.findings.append({"item": item, "message": message,
                              "remediation": remediation})

    def warn(self, item: str, message: str, remediation: str) -> None:
        self.findings.append({"item": item, "message": message,
                              "remediation": remediation})


def self_test() -> int:
    failures = 0
    with tempfile.TemporaryDirectory(prefix="taffy-artifact-upload-") as base:
        upload = os.path.join(base, "upload")
        os.mkdir(upload)
        path = os.path.join(upload, "artifact.bin")
        with open(path, "wb") as handle:
            handle.write(b"attested artifact\n")
        entry = {
            "path": "artifact.bin",
            "kind": "trace",
            "sha256": repo_facts.sha256_file(path),
            "size_bytes": repo_facts.file_size(path),
        }
        manifest = {"artifacts": [entry], "platform": {"abis": []}}

        report = _Report()
        check_artifacts(manifest, upload, "development", report)
        if report.findings:
            failures += 1
            print(f"FAIL  an in-directory artifact should verify: {report.findings}")

        escaped = {"artifacts": [dict(entry, path="../outside.bin")],
                   "platform": {"abis": []}}
        report = _Report()
        check_artifacts(escaped, upload, "development", report)
        if not any("escapes" in finding["message"] for finding in report.findings):
            failures += 1
            print("FAIL  ../ artifact traversal was not rejected")

        absolute = {"artifacts": [dict(entry, path=path)], "platform": {"abis": []}}
        report = _Report()
        check_artifacts(absolute, upload, "development", report)
        if not any("escapes" in finding["message"] for finding in report.findings):
            failures += 1
            print("FAIL  absolute artifact path was not rejected")

        outside = os.path.join(base, "outside.bin")
        with open(outside, "wb") as handle:
            handle.write(b"outside\n")
        os.symlink(outside, os.path.join(upload, "linked.bin"))
        symlink = {"artifacts": [dict(entry, path="linked.bin")],
                   "platform": {"abis": []}}
        report = _Report()
        check_artifacts(symlink, upload, "development", report)
        if not any("escapes" in finding["message"] for finding in report.findings):
            failures += 1
            print("FAIL  artifact symlink escape was not rejected")

        mismatch = {"artifacts": [dict(entry, sha256="0" * 64)],
                    "platform": {"abis": []}}
        report = _Report()
        check_artifacts(mismatch, upload, "development", report)
        if not any("digest mismatch" in finding["message"] for finding in report.findings):
            failures += 1
            print("FAIL  artifact digest mismatch was not rejected")

        duplicate = {
            "artifacts": [entry, dict(entry, kind="test-report", path="./artifact.bin")],
            "platform": {"abis": []},
        }
        report = _Report()
        check_artifacts(duplicate, upload, "development", report)
        if not any("same file" in finding["message"] for finding in report.findings):
            failures += 1
            print("FAIL  one file under two artifact identities was not rejected")

        hardlink_path = os.path.join(upload, "hardlink.bin")
        os.link(path, hardlink_path)
        hardlink = {
            "artifacts": [entry, dict(entry, kind="test-report", path="hardlink.bin")],
            "platform": {"abis": []},
        }
        report = _Report()
        check_artifacts(hardlink, upload, "development", report)
        if not any("hard link" in finding["message"] for finding in report.findings):
            failures += 1
            print("FAIL  one hard-linked file under two identities was not rejected")

    print()
    print(f"artifact verification: 7 case(s), {failures} failure(s)")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if not args.self_test:
        parser.error("--self-test is required when running this module directly")
    return self_test()


if __name__ == "__main__":
    raise SystemExit(main())
