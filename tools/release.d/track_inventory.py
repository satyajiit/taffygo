#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Produce Chromium and packaged-native inventory inputs on the build track.

The Chromium list comes from upstream's shipped-only license traversal for the
actual TaffyGo product target. The native list comes from the non-incremental
APK and resolves prebuilt libraries back to the exact AAR bytes and adjacent
README.chromium metadata. No row is inferred from a declared dependency alone.

Stdlib only; the command reads an existing Chromium checkout and build and
never contacts the network.
"""

from __future__ import annotations

import argparse
import ast
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import zipfile

import track_inventory_native as native

SCHEMA_VERSION = 1
PRODUCT_TARGET = "//taffy/app/android:taffy_public_apk"
LICENSE_TIMEOUT_SECONDS = 600


class InventoryError(RuntimeError):
    """A claimed observation could not be made exactly."""


def write_json(path: Path, document: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=2, sort_keys=False)
        handle.write("\n")


def metadata_fields(readme: Path) -> dict[str, str]:
    if not readme.is_file():
        return {}
    fields = {}
    for line in readme.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip() and fields:
            break
        match = re.match(r"^([A-Za-z][A-Za-z ]+):\s*(.*?)\s*$", line)
        if match:
            fields[match.group(1)] = match.group(2)
    return fields


def chromium_version(source: Path) -> str:
    fields = {}
    version_file = source / "chrome" / "VERSION"
    if version_file.is_file():
        for line in version_file.read_text(encoding="utf-8").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                fields[key.strip()] = value.strip()
    parts = [fields.get(key, "") for key in ("MAJOR", "MINOR", "BUILD", "PATCH")]
    return ".".join(parts) if all(parts) else ""


def source_revision(source: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=False,
    )
    revision = result.stdout.strip()
    if result.returncode != 0 or not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise InventoryError(f"{source} is not a readable Chromium git checkout")
    return revision


def upstream_revision(source: Path, revision: str) -> str:
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise InventoryError("the repository supplied no full lowercase Chromium pin")
    ancestry = subprocess.run(
        ["git", "-C", str(source), "merge-base", "--is-ancestor", revision, "HEAD"],
        capture_output=True, text=True, check=False,
    )
    if ancestry.returncode != 0:
        raise InventoryError(
            f"Chromium checkout HEAD does not descend from the pinned upstream {revision}"
        )
    return revision


def safe_source_path(source: Path, relative: str) -> Path:
    posix = PurePosixPath(relative)
    if posix.is_absolute() or ".." in posix.parts:
        raise InventoryError(f"upstream license output contains unsafe path: {relative}")
    # Keep this lexical. `src/taffy` is deliberately the repository-owned
    # symlink mount, so resolving it would reject the one first-party subtree
    # the upstream traversal is required to include. Absolute paths and parent
    # components are already refused above; nothing is extracted through it.
    return source.joinpath(*posix.parts)


def row_version(source: Path, row: dict, revision: str) -> str:
    directory = row.get("dir", "")
    if not directory:
        return chromium_version(source) or revision[:12]
    readme = safe_source_path(source, f"{directory}/README.chromium")
    fields = metadata_fields(readme)
    if not fields:
        return ""
    declared_name = (fields.get("Name") or fields.get("Short Name") or "").casefold()
    observed_name = row["Name"].casefold()
    if declared_name and declared_name != observed_name \
            and fields.get("Short Name", "").casefold() != observed_name:
        # Aggregate directories such as third_party/node emit several projects
        # from one metadata root; its aggregate version does not belong to each.
        return ""
    return fields.get("Version", "")


def parse_chromium_license_output(text: str, source: Path, revision: str) -> list[dict]:
    components = []
    seen = set()
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = ast.literal_eval(line)
        except (SyntaxError, ValueError) as error:
            raise InventoryError(
                f"upstream license output line {line_number} is not a metadata record"
            ) from error
        if not isinstance(row, dict):
            raise InventoryError(f"upstream license output line {line_number} is not a dictionary")
        name = row.get("Name")
        license_id = row.get("License")
        shipped = str(row.get("Shipped", "")).casefold()
        directory = row.get("dir", "")
        if not isinstance(name, str) or not name.strip():
            raise InventoryError(f"upstream license row {line_number} has no name")
        if not isinstance(license_id, str) or not license_id.strip():
            raise InventoryError(f"upstream license row {name!r} has no resolved license")
        if shipped != "yes":
            raise InventoryError(
                f"upstream --shipped-only output unexpectedly contains {name!r} ({shipped!r})"
            )
        if not isinstance(directory, str):
            raise InventoryError(f"upstream license row {name!r} has a non-string directory")
        if directory:
            safe_source_path(source, directory)
        relative_source = directory or "LICENSE"
        version = row_version(source, row, revision)
        identity = (name, version, relative_source)
        if identity in seen:
            raise InventoryError(
                f"upstream license traversal repeated {name}@{version} from {relative_source}"
            )
        seen.add(identity)
        components.append({
            "name": name,
            "version": version,
            "license": license_id,
            "purl": "",
            "source": relative_source,
        })
    if not components:
        raise InventoryError("upstream license traversal returned no shipped components")
    return sorted(components, key=lambda item: (
        item["name"].casefold(), item["version"], item["source"]
    ))


def chromium_inventory(source: Path, profile: str, expected_upstream: str) -> dict:
    output = source / "out" / profile
    script = source / "tools" / "licenses" / "licenses.py"
    if not script.is_file():
        raise InventoryError(f"upstream license tool is absent: {script}")
    if not output.is_dir():
        raise InventoryError(f"Chromium output does not exist: {output}")
    command = [
        sys.executable, str(script), "list", "--target-os=android",
        "--gn-out-dir", str(output), "--gn-target", PRODUCT_TARGET,
        "--shipped-only", "--verbose",
    ]
    try:
        result = subprocess.run(
            command, cwd=source, capture_output=True, text=True,
            timeout=LICENSE_TIMEOUT_SECONDS, check=False,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise InventoryError(f"upstream license traversal could not run: {error}") from error
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip().splitlines()
        raise InventoryError(
            "upstream license traversal failed: " + (detail[-1] if detail else "no diagnostic")
        )
    revision = source_revision(source)
    upstream = upstream_revision(source, expected_upstream)
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "chromium-shipped-license-inventory",
        "source_revision": revision,
        "upstream_revision": upstream,
        "profile": profile,
        "target": PRODUCT_TARGET,
        "producer": "tools/licenses/licenses.py list --shipped-only --verbose",
        "components": parse_chromium_license_output(result.stdout, source, revision),
    }


def generate(args) -> int:
    source = Path(args.chromium_src).resolve()
    out = Path(args.out)
    chromium = chromium_inventory(source, args.profile, args.upstream_revision)
    native_document = native.native_inventory(source, args.profile, args.apk, chromium)
    chromium_path = out / "chromium-inventory.json"
    native_path = out / "android-native-inventory.json"
    write_json(chromium_path, chromium)
    write_json(native_path, native_document)
    print(f"  chromium       complete  {len(chromium['components']):>5} component(s)")
    print(f"  android-native complete  {len(native_document['components']):>5} component(s)")
    print(f"  {chromium_path}")
    print(f"  {native_path}")
    return 0


def self_test() -> int:
    checks = 0
    failures = 0

    def check(condition: bool, message: str) -> None:
        nonlocal checks, failures
        checks += 1
        if not condition:
            failures += 1
            print(f"FAIL  {message}")

    with tempfile.TemporaryDirectory(prefix="taffy-track-inventory-") as temporary:
        root = Path(temporary)
        (root / "chrome").mkdir()
        (root / "chrome" / "VERSION").write_text(
            "MAJOR=141\nMINOR=0\nBUILD=1\nPATCH=2\n", encoding="utf-8"
        )
        package = root / "third_party" / "sample"
        package.mkdir(parents=True)
        (package / "README.chromium").write_text(
            "Name: Sample\nVersion: 3\nLicense: Apache-2.0\n\nDescription.\n",
            encoding="utf-8",
        )
        verbose = "\n".join((
            "{'Name': 'The Chromium Project', 'License': 'BSD-3-Clause', "
            "'Shipped': 'yes', 'dir': ''}",
            "{'Name': 'Sample', 'License': 'Apache-2.0', 'Shipped': 'Yes', "
            "'dir': 'third_party/sample'}",
        ))
        parsed = parse_chromium_license_output(verbose, root, "a" * 40)
        check(len(parsed) == 2, "the shipped-only parser retains every observed row")
        check(parsed[0]["version"] == "3" or parsed[1]["version"] == "3",
              "README.chromium supplies an exact component version")
        check(any(item["version"] == "141.0.1.2" for item in parsed),
              "the Chromium row uses the checkout's product version")

        aar = package / "sample.aar"
        library = b"native-library"
        with zipfile.ZipFile(aar, "w") as archive:
            archive.writestr("jni/arm64-v8a/libsample.so", library)
        index = native.aar_library_index(root)
        check(("arm64-v8a", "libsample.so", native.digest(library)) in index,
              "the AAR index keys provenance by ABI, filename and exact bytes")

        apk = root / "sample.apk"
        with zipfile.ZipFile(apk, "w") as archive:
            archive.writestr("lib/arm64-v8a/libsample.so", library)
            archive.writestr("lib/arm64-v8a/libfix.crbug.384638.so", b"")
        fake_components = [
            {"name": "The Chromium Project", "version": "141.0.1.2",
             "license": "BSD-3-Clause", "source": "LICENSE"},
            {"name": "Crashpad", "version": "1", "license": "Apache-2.0",
             "source": "third_party/crashpad"},
        ]
        natives = native.native_components(root, apk, fake_components)
        check(len(natives) == 2, "the APK scan includes native code and its packaging sentinel")
        check(natives[0]["sha256"] or natives[1]["sha256"],
              "each packaged entry carries its observed content digest")

        sentinel = root / "sentinel.apk"
        with zipfile.ZipFile(sentinel, "w") as archive:
            archive.writestr("lib/arm64-v8a/libfix.crbug.384638.so", b"")
        try:
            native.native_components(root, sentinel, fake_components)
        except native.NativeInventoryError:
            refused = True
        else:
            refused = False
        check(refused, "an incremental APK cannot masquerade as a complete native inventory")

        try:
            parse_chromium_license_output(
                "{'Name': 'Missing', 'Shipped': 'yes', 'dir': 'third_party/sample'}",
                root, "a" * 40,
            )
        except InventoryError:
            refused = True
        else:
            refused = False
        check(refused, "a shipped component without a license is refused")

    print()
    print(f"track inventory: {checks} check(s), {failures} failure(s)")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--chromium-src", help="Chromium src checkout")
    parser.add_argument("--profile", default="release-arm64")
    parser.add_argument("--upstream-revision", help="commit pinned by chromium/REVISION")
    parser.add_argument("--apk", default="", help="exact non-incremental product APK")
    parser.add_argument("--out", help="directory for the two inventory inputs")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.chromium_src or not args.out or not args.upstream_revision:
        parser.error("--chromium-src, --upstream-revision and --out are required")
    try:
        return generate(args)
    except (InventoryError, native.NativeInventoryError) as error:
        print(f"track inventory: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
