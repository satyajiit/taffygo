#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Per-ecosystem dependency collectors behind ./tools/release sbom.

Authority boundary: each collector below knows how to enumerate the
dependencies of exactly one ecosystem this repository owns, and how to say
what it could not enumerate. Assembling the CycloneDX document and the
license inventory is sbom.py; nothing here writes a file.

Owning milestone: M1 (WP-M1-07), for PAR-SEC-010 — CI produces the SBOM from
M1.

Every collector returns the same record and every collector reports one of
four coverage states, because "no components found" and "the tool that finds
components is not installed" are different facts and an inventory that
confuses them is worthless:

  complete     the authoritative command ran and every component is listed
  partial      a real but incomplete source was read; `blocker` names what is
               missing and the command that would close it
  unavailable  nothing could be read here; `blocker` names why
  deferred     another track owns this ecosystem; `owner` names it and
               `blocker` names the input file that fills it in

Stdlib only, no network. A collector never installs anything and never
resolves a dependency that is not already on disk.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess

TIMEOUT_SECONDS = 180


def record(name, status, components=None, evidence_command="", blocker="", owner="",
           metadata=None):
    return {
        "name": name,
        "status": status,
        "components": components or [],
        "evidence_command": evidence_command,
        "blocker": blocker,
        "owner": owner,
        "metadata": metadata or {},
    }


def component(ecosystem, name, version, license_id="", purl="", source=""):
    return {
        "ecosystem": ecosystem,
        "name": name,
        "version": version,
        "license": license_id,
        "purl": purl,
        "source": source,
    }


def run(command, cwd, timeout=TIMEOUT_SECONDS):
    """(stdout, error). Never raises: a missing tool is data, not a crash."""
    if not shutil.which(command[0]) and not os.path.exists(os.path.join(cwd, command[0])):
        return "", f"{command[0]} is not installed"
    try:
        result = subprocess.run(
            command, cwd=cwd, capture_output=True, text=True, timeout=timeout, check=False,
        )
    except (OSError, subprocess.SubprocessError) as error:
        return "", str(error)
    if result.returncode != 0:
        tail = (result.stderr or result.stdout).strip().splitlines()
        return "", (tail[-1] if tail else f"exit status {result.returncode}")
    return result.stdout, ""


# --- Rust --------------------------------------------------------------------

def collect_rust(root: str):
    """The Cargo workspace. Licenses come from cargo metadata, which reads the
    manifests already vendored in the Cargo home — no network."""
    manifest = os.path.join(root, "Cargo.toml")
    if not os.path.exists(manifest):
        return record("rust", "unavailable",
                      blocker="no Cargo workspace at the repository root (WP-M0-01)")

    stdout, error = run(["cargo", "metadata", "--format-version", "1", "--locked"], root)
    if not error:
        try:
            data = json.loads(stdout)
        except json.JSONDecodeError as decode_error:
            error = f"cargo metadata did not return JSON ({decode_error})"
        else:
            components = []
            for package in data.get("packages", []):
                components.append(component(
                    "rust", package["name"], package["version"],
                    license_id=package.get("license") or "",
                    purl=f"pkg:cargo/{package['name']}@{package['version']}",
                    source=package.get("source") or "workspace",
                ))
            return record("rust", "complete", components,
                          evidence_command="cargo metadata --format-version 1 --locked")

    components = _rust_from_lockfile(os.path.join(root, "Cargo.lock"))
    if components is None:
        return record("rust", "unavailable",
                      blocker=f"{error}, and Cargo.lock is absent",
                      evidence_command="cargo metadata --format-version 1 --locked")
    return record(
        "rust", "partial", components,
        evidence_command="cargo metadata --format-version 1 --locked",
        blocker=(f"{error}; component names and versions were read from Cargo.lock, "
                 "which records no license. Install the pinned toolchain "
                 "(rust-toolchain.toml selects it) and re-run."),
    )


def _rust_from_lockfile(path: str):
    if not os.path.exists(path):
        return None
    components = []
    name = version = None
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line == "[[package]]":
                name = version = None
            elif line.startswith("name = "):
                name = line.split("=", 1)[1].strip().strip('"')
            elif line.startswith("version = "):
                version = line.split("=", 1)[1].strip().strip('"')
                if name:
                    components.append(component(
                        "rust", name, version, purl=f"pkg:cargo/{name}@{version}",
                        source="Cargo.lock",
                    ))
                    name = version = None
    return components


# --- JavaScript --------------------------------------------------------------

def collect_javascript(root: str):
    """The pnpm workspace: the website, and nothing else since decision 0200
    deleted the Worker that was the other member. Nothing here ships inside the
    Android package, and nothing here is deployed either, but it is first-party
    code with a dependency tree and it is inventoried."""
    if not os.path.exists(os.path.join(root, "pnpm-workspace.yaml")):
        return record("javascript", "unavailable",
                      blocker="no pnpm workspace at the repository root (WP-M0-01)")

    stdout, error = run(["pnpm", "licenses", "list", "--json", "--recursive"], root)
    if not error:
        try:
            components = _javascript_from_licenses(json.loads(stdout))
        except (json.JSONDecodeError, TypeError, KeyError) as decode_error:
            error = f"pnpm licenses list returned an unexpected shape ({decode_error})"
        else:
            return record("javascript", "complete", components,
                          evidence_command="pnpm licenses list --json --recursive")

    components = _javascript_from_lockfile(os.path.join(root, "pnpm-lock.yaml"))
    if components is None:
        return record("javascript", "unavailable",
                      blocker=f"{error}, and pnpm-lock.yaml is absent",
                      evidence_command="pnpm licenses list --json --recursive")
    return record(
        "javascript", "partial", components,
        evidence_command="pnpm licenses list --json --recursive",
        blocker=(f"{error}; names and versions were read from pnpm-lock.yaml, which "
                 "records no license. Run `pnpm install --frozen-lockfile` and re-run."),
    )


def _javascript_from_licenses(data):
    components = []
    entries = []
    if isinstance(data, dict):
        for license_id, group in data.items():
            for entry in group:
                entries.append((license_id, entry))
    else:  # some versions return a flat list carrying its own license field
        entries = [(entry.get("license", ""), entry) for entry in data]
    for license_id, entry in entries:
        name = entry.get("name")
        if not name:
            continue
        versions = entry.get("versions") or ([entry["version"]] if entry.get("version") else [""])
        for version in versions:
            components.append(component(
                "javascript", name, version,
                license_id=entry.get("license") or license_id or "",
                purl=f"pkg:npm/{name}@{version}" if version else f"pkg:npm/{name}",
                source=entry.get("homepage") or "npm",
            ))
    return components


LOCK_ENTRY = re.compile(r"^  (?:/)?(@?[^@\s]+(?:/[^@\s]+)?)@([^:@\s]+):\s*$")


def _javascript_from_lockfile(path: str):
    if not os.path.exists(path):
        return None
    components = []
    in_packages = False
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.startswith(" ") and line.strip().endswith(":"):
                in_packages = line.startswith("packages:") or line.startswith("snapshots:")
                continue
            if not in_packages:
                continue
            match = LOCK_ENTRY.match(line.rstrip("\n"))
            if match:
                name, version = match.group(1), match.group(2)
                components.append(component(
                    "javascript", name, version,
                    purl=f"pkg:npm/{name}@{version}", source="pnpm-lock.yaml",
                ))
    # snapshots: and packages: describe the same set; one entry per coordinate.
    unique = {(c["name"], c["version"]): c for c in components}
    return sorted(unique.values(), key=lambda c: (c["name"], c["version"]))


# --- tracks that another host owns -------------------------------------------

def collect_external(name: str, inventory_path: str, owner: str, produced_by: str,
                     expected_kind: str = "", expected_upstream: str = ""):
    """Chromium and the packaged native libraries.

    Neither can be enumerated from this repository: the Chromium third-party
    inventory is produced by the upstream license tooling against a checkout,
    and the packaged native library list is read out of a built package. Both
    are read here from a file the Chromium track produces, and both report
    `deferred` until that file exists — never an empty `complete`.
    """
    if inventory_path and os.path.exists(inventory_path):
        try:
            with open(inventory_path, encoding="utf-8") as handle:
                data = json.load(handle)
        except (OSError, json.JSONDecodeError) as error:
            return record(name, "unavailable", owner=owner,
                          blocker=f"{inventory_path} could not be read ({error})")
        if not isinstance(data, dict) or not isinstance(data.get("components"), list):
            return record(
                name, "unavailable", owner=owner,
                blocker=(f"{inventory_path} must be a JSON object with a components list; "
                         "regenerate it with the Chromium-track inventory command"),
            )
        if not data["components"]:
            return record(
                name, "unavailable", owner=owner,
                blocker=(f"{inventory_path} contains no components and cannot prove "
                         f"complete {name} coverage"),
            )

        required_metadata = {
            "schema_version": int,
            "kind": str,
            "source_revision": str,
            "upstream_revision": str,
            "profile": str,
            "target": str,
        }
        if name == "android-native":
            required_metadata.update({
                "artifact": str,
                "artifact_sha256": str,
                "package_name": str,
            })
        for field, field_type in required_metadata.items():
            value = data.get(field)
            if not isinstance(value, field_type) or (field_type is str and not value.strip()):
                return record(
                    name, "unavailable", owner=owner,
                    blocker=(f"{inventory_path} has no valid {field}; regenerate it "
                             "with ./tools/release inventory"),
                )
        if data["schema_version"] != 1:
            return record(
                name, "unavailable", owner=owner,
                blocker=(f"{inventory_path} uses unsupported inventory schema "
                         f"{data['schema_version']}; regenerate it"),
            )
        if expected_kind and data["kind"] != expected_kind:
            return record(
                name, "unavailable", owner=owner,
                blocker=(f"{inventory_path} is {data['kind']!r}, expected {expected_kind!r}"),
            )
        for field in ("source_revision", "upstream_revision"):
            if not re.fullmatch(r"[0-9a-f]{40}", data[field]):
                return record(
                    name, "unavailable", owner=owner,
                    blocker=f"{inventory_path} {field} is not a full lowercase git revision",
                )
        if expected_upstream and data["upstream_revision"] != expected_upstream:
            return record(
                name, "unavailable", owner=owner,
                blocker=(f"{inventory_path} was produced for Chromium "
                         f"{data['upstream_revision']}, but chromium/REVISION pins "
                         f"{expected_upstream}"),
            )
        if name == "android-native" and not re.fullmatch(
                r"[0-9a-f]{64}", data["artifact_sha256"]):
            return record(
                name, "unavailable", owner=owner,
                blocker=f"{inventory_path} artifact_sha256 is not a lowercase SHA-256",
            )

        components = []
        seen = set()
        for index, entry in enumerate(data["components"]):
            if not isinstance(entry, dict) or not isinstance(entry.get("name"), str) \
                    or not entry["name"].strip():
                return record(
                    name, "unavailable", owner=owner,
                    blocker=(f"{inventory_path} components[{index}] has no non-empty name; "
                             "regenerate the inventory instead of editing it"),
                )
            version = entry.get("version", "")
            if not isinstance(version, str):
                return record(
                    name, "unavailable", owner=owner,
                    blocker=f"{inventory_path} components[{index}].version must be a string",
                )
            source = entry.get("source", inventory_path)
            if not isinstance(source, str) or not source.strip():
                return record(
                    name, "unavailable", owner=owner,
                    blocker=f"{inventory_path} components[{index}].source must be non-empty",
                )
            key = (entry["name"], version, source)
            if key in seen:
                return record(
                    name, "unavailable", owner=owner,
                    blocker=(f"{inventory_path} repeats component {entry['name']}@{version} "
                             f"from {source}; "
                             "one observed component must have one inventory row"),
                )
            seen.add(key)
            if name == "android-native":
                entry_digest = entry.get("sha256")
                entry_size = entry.get("size_bytes")
                if not isinstance(entry_digest, str) or not re.fullmatch(
                        r"[0-9a-f]{64}", entry_digest):
                    return record(
                        name, "unavailable", owner=owner,
                        blocker=(f"{inventory_path} components[{index}].sha256 is not "
                                 "a lowercase SHA-256"),
                    )
                if not isinstance(entry_size, int) or entry_size < 0:
                    return record(
                        name, "unavailable", owner=owner,
                        blocker=(f"{inventory_path} components[{index}].size_bytes must "
                                 "be a non-negative integer"),
                    )
            components.append(component(
                name, entry["name"], version,
                license_id=entry.get("license", ""),
                purl=entry.get("purl", ""), source=source,
            ))
        metadata = {
            field: data[field] for field in required_metadata
        }
        return record(
            name, "complete", components,
            evidence_command=f"{produced_by} (imported from {inventory_path})",
            metadata=metadata,
        )
    return record(name, "deferred", owner=owner, evidence_command=produced_by,
                  blocker=(f"produced by {produced_by} on the Chromium track and "
                           f"imported with --{name}-inventory <file>"))
