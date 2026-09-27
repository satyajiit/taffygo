#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Packaged-native half of the Chromium-track release inventory."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile

SCHEMA_VERSION = 1
PRODUCT_TARGET = "//taffy/app/android:taffy_public_apk"
BUILD_CONFIG = "gen/taffy/app/android/taffy_public_apk.build_config.json"


class NativeInventoryError(RuntimeError):
    """The package could not be tied exactly to its native sources."""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


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


def checkout_revision(source: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=False,
    )
    revision = result.stdout.strip()
    if result.returncode != 0 or not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise NativeInventoryError(f"{source} is not a readable Chromium git checkout")
    return revision


def build_config(source: Path, profile: str) -> tuple[Path, dict]:
    output = source / "out" / profile
    path = output / BUILD_CONFIG
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise NativeInventoryError(
            f"product build configuration could not be read: {path} ({error})"
        ) from error
    apk_relative = data.get("apk_path")
    if not isinstance(apk_relative, str) or not apk_relative.endswith(".apk"):
        raise NativeInventoryError(f"product build configuration has no APK path: {path}")
    apk = (output / apk_relative).resolve()
    if apk.name.endswith("_incremental.apk"):
        raise NativeInventoryError(
            f"{path} names an incremental installer, not the complete product package"
        )
    return apk, data


def aar_metadata(aar: Path) -> dict[str, str]:
    fields = metadata_fields(aar.parent / "README.chromium")
    required = ("Name", "Version", "License")
    return fields if all(fields.get(key) for key in required) else {}


def aar_library_index(source: Path) -> dict[tuple[str, str, str], list[dict]]:
    index: dict[tuple[str, str, str], list[dict]] = {}
    for aar in sorted((source / "third_party").rglob("*.aar")):
        fields = aar_metadata(aar)
        if not fields:
            continue
        try:
            with zipfile.ZipFile(aar) as archive:
                for member in archive.infolist():
                    parts = PurePosixPath(member.filename).parts
                    if len(parts) != 3 or parts[0] != "jni" or not parts[2].endswith(".so"):
                        continue
                    key = (parts[1], parts[2], digest(archive.read(member)))
                    index.setdefault(key, []).append({
                        "name": fields["Name"],
                        "version": fields["Version"],
                        "license": fields["License"],
                        "source": f"{aar.relative_to(source).as_posix()}!/{member.filename}",
                    })
        except (OSError, zipfile.BadZipFile) as error:
            raise NativeInventoryError(f"could not inspect prebuilt archive {aar}: {error}") from error
    return index


def name_tokens(value: str) -> set[str]:
    return {
        token for token in re.split(r"[^a-z0-9]+", value.casefold())
        if token and token not in {"lib", "jni", "so", "androidx"}
    }


def choose_aar_candidate(library_name: str, candidates: list[dict]) -> dict:
    if len(candidates) == 1:
        return candidates[0]
    library_tokens = name_tokens(library_name)
    scored = [
        (len(library_tokens & name_tokens(f"{item['name']} {item['source']}")), item)
        for item in candidates
    ]
    best_score = max(score for score, _ in scored)
    best = [item for score, item in scored if score == best_score]
    if len(best) == 1:
        return best[0]
    obligations = {(item["license"], item["version"]) for item in best}
    if len(obligations) == 1:
        combined = dict(best[0])
        combined["source"] = ";".join(sorted(item["source"] for item in best))
        return combined
    sources = ", ".join(sorted(item["source"] for item in best))
    raise NativeInventoryError(f"{library_name} has ambiguous prebuilt provenance: {sources}")


def find_chromium_component(components: list[dict], needle: str) -> dict:
    matches = [item for item in components if needle in item["name"].casefold()]
    if len(matches) != 1:
        raise NativeInventoryError(
            f"expected one Chromium license component matching {needle!r}, found {len(matches)}"
        )
    return matches[0]


def native_components(source: Path, apk: Path, chromium_components: list[dict]) -> list[dict]:
    aar_index = aar_library_index(source)
    chrome = find_chromium_component(chromium_components, "chromium project")
    crashpad = find_chromium_component(chromium_components, "crashpad")
    components = []
    nonempty = 0
    try:
        with zipfile.ZipFile(apk) as archive:
            members = sorted(
                (
                    member for member in archive.infolist()
                    if not member.is_dir() and member.filename.startswith("lib/")
                    and member.filename.endswith(".so")
                ),
                key=lambda member: member.filename,
            )
            for member in members:
                parts = PurePosixPath(member.filename).parts
                if len(parts) != 3:
                    raise NativeInventoryError(
                        f"unexpected packaged native path: {member.filename}"
                    )
                abi, filename = parts[1], parts[2]
                data = archive.read(member)
                content_digest = digest(data)
                nonempty += bool(data)
                if filename == "libfix.crbug.384638.so" and not data:
                    resolved = chrome
                elif filename == "libchrome.so":
                    resolved = chrome
                elif "crashpad" in filename.casefold():
                    resolved = crashpad
                else:
                    candidates = aar_index.get((abi, filename, content_digest), [])
                    if not candidates:
                        raise NativeInventoryError(
                            f"{member.filename} ({content_digest}) has no exact source AAR match"
                        )
                    resolved = choose_aar_candidate(filename, candidates)
                components.append({
                    "name": member.filename,
                    "version": resolved.get("version", ""),
                    "license": resolved["license"],
                    "purl": "",
                    "source": resolved["source"],
                    "sha256": content_digest,
                    "size_bytes": member.file_size,
                })
    except (OSError, zipfile.BadZipFile) as error:
        raise NativeInventoryError(f"product APK could not be inspected: {apk} ({error})") from error
    if not components:
        raise NativeInventoryError(f"{apk} contains no packaged native entries")
    if not nonempty:
        raise NativeInventoryError(
            f"{apk} contains only the incremental-install sentinel, not product native code"
        )
    return components


def native_inventory(source: Path, profile: str, apk_argument: str,
                     chromium_document: dict) -> dict:
    expected_apk, config = build_config(source, profile)
    apk = Path(apk_argument).resolve() if apk_argument else expected_apk
    if apk != expected_apk:
        raise NativeInventoryError(
            f"APK {apk} does not match the product build output recorded in {BUILD_CONFIG}: "
            f"{expected_apk}"
        )
    if not apk.is_file():
        raise NativeInventoryError(
            f"complete product APK is absent: {apk}; build {PRODUCT_TARGET} without incremental install"
        )
    revision = checkout_revision(source)
    if chromium_document.get("source_revision") != revision:
        raise NativeInventoryError("Chromium and native inventory revisions do not agree")
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "android-packaged-native-inventory",
        "source_revision": revision,
        "upstream_revision": chromium_document["upstream_revision"],
        "profile": profile,
        "target": PRODUCT_TARGET,
        "package_name": config.get("package_name", ""),
        "artifact": apk.name,
        "artifact_sha256": file_digest(apk),
        "producer": "ZIP entry scan with exact source-AAR byte matching",
        "components": native_components(source, apk, chromium_document["components"]),
    }
