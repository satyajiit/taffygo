#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Assemble the CycloneDX software bill of materials and the license inventory.

Authority boundary: this module owns the two documents — their shape, their
determinism, and the coverage summary that says which ecosystem is fully
inventoried and which is not. Finding the components is sbom_collectors.py;
deciding whether the coverage is good enough to promote an artifact is
./tools/release verify.

Owning milestone: M1 (WP-M1-07). Requirement: PAR-SEC-010 in
docs/product/browser-parity-matrix.md — a reproducible inventory, produced by
CI from M1.

Reproducibility. Two runs from the same tree produce byte-identical documents:
components are sorted, the serial number is derived from the component set,
and the timestamp comes from SOURCE_DATE_EPOCH when the caller sets it. The
release lane sets it; a developer run does not need to.

  --out DIR                       write sbom.cdx.json and licenses.json
  --gradle-report FILE            resolved ./gradlew dependencies output
  --chromium-inventory FILE       the Chromium track's third-party inventory
  --android-native-inventory FILE the packaged native library inventory
  --self-test                     structural and determinism checks

Exit status: 0 written, 1 a collector failed hard, 2 bad arguments.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import sys
import tempfile
import tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gradle_inventory  # noqa: E402
import sbom_collectors as collectors  # noqa: E402  (after sys.path setup)

SPEC_VERSION = "1.6"
INVENTORY_VERSION = 1
CHROMIUM_PRODUCER = "the Chromium third-party license tooling on the Linux builder"
NATIVE_PRODUCER = "the packaged native library scan of the built product target"
CHROMIUM_KIND = "chromium-shipped-license-inventory"
NATIVE_KIND = "android-packaged-native-inventory"


def chromium_pin(root: str) -> str:
    path = os.path.join(root, "chromium", "REVISION")
    try:
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                if line.startswith("commit="):
                    return line.split("=", 1)[1].strip()
    except OSError:
        return ""
    return ""


def timestamp() -> str:
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    moment = (
        datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc)
        if epoch and epoch.isdigit()
        else datetime.datetime.now(datetime.timezone.utc)
    )
    return moment.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def collect_all(root: str, gradle_report: str = "", chromium_inventory: str = "",
                native_inventory: str = "") -> list:
    upstream = chromium_pin(root)
    records = [
        collectors.collect_rust(root),
        collectors.collect_javascript(root),
        gradle_inventory.collect_gradle(root, gradle_report),
        collectors.collect_external("chromium", chromium_inventory,
                                    "Chromium track", CHROMIUM_PRODUCER,
                                    CHROMIUM_KIND, upstream),
        collectors.collect_external("android-native", native_inventory,
                                    "Chromium track", NATIVE_PRODUCER,
                                    NATIVE_KIND, upstream),
    ]
    external = {record["name"]: record for record in records[-2:]}
    if all(external[name]["status"] == "complete"
           for name in ("chromium", "android-native")):
        chromium_meta = external["chromium"]["metadata"]
        native_meta = external["android-native"]["metadata"]
        shared = ("source_revision", "upstream_revision", "profile", "target")
        disagreements = [
            field for field in shared if chromium_meta.get(field) != native_meta.get(field)
        ]
        if disagreements:
            detail = ", ".join(disagreements)
            for record in external.values():
                record["status"] = "unavailable"
                record["blocker"] = (
                    "the Chromium and packaged-native inventories disagree on " + detail
                )
                record["components"] = []
    return records


def first_party_names(root: str) -> set:
    """Workspace members are ours; they are inventoried but never counted as an
    unresolved third-party license."""
    names = set()
    cargo = os.path.join(root, "Cargo.toml")
    if os.path.exists(cargo):
        try:
            with open(cargo, "rb") as handle:
                workspace = tomllib.load(handle)
        except (OSError, tomllib.TOMLDecodeError):
            workspace = {}
        members = workspace.get("workspace", {}).get("members", [])
        for member in members if isinstance(members, list) else []:
            if not isinstance(member, str):
                continue
            manifest = os.path.join(root, member, "Cargo.toml")
            try:
                with open(manifest, "rb") as handle:
                    package = tomllib.load(handle).get("package", {})
            except (OSError, tomllib.TOMLDecodeError):
                continue
            name = package.get("name")
            if isinstance(name, str) and name:
                names.add(name)
    for relative in ("website", "services"):
        base = os.path.join(root, relative)
        candidates = [base] if os.path.exists(os.path.join(base, "package.json")) else [
            os.path.join(base, entry) for entry in sorted(os.listdir(base))
        ] if os.path.isdir(base) else []
        for candidate in candidates:
            package = os.path.join(candidate, "package.json")
            if os.path.exists(package):
                try:
                    with open(package, encoding="utf-8") as handle:
                        name = json.load(handle).get("name")
                except (OSError, json.JSONDecodeError):
                    continue
                if name:
                    names.add(name)
    return names


def flatten(records: list, root: str) -> list:
    """One sorted entry per observed component across every ecosystem.

    Source participates in identity because a shipping graph can contain two
    independently vendored copies of the same named version. Collapsing those
    rows would lose a distinct license/provenance obligation.
    """
    ours = first_party_names(root)
    entries: dict = {}
    for record in records:
        for component in record["components"]:
            classification = (
                "first-party"
                if component["name"] in ours or component["source"] == "workspace"
                else "third-party"
            )
            entry = dict(component)
            entry["classification"] = classification
            key = (entry["ecosystem"], entry["name"], entry["version"], entry["source"])
            existing = entries.get(key)
            # Prefer the record that resolved a license.
            if existing is None or (not existing["license"] and entry["license"]):
                entries[key] = entry
    return [entries[key] for key in sorted(entries)]


def ecosystem_summary(records: list) -> list:
    return [
        {
            "name": record["name"],
            "status": record["status"],
            "component_count": len(record["components"]),
            "evidence_command": record["evidence_command"],
            "blocker": record["blocker"],
            "owner": record["owner"],
            **record.get("metadata", {}),
        }
        for record in records
    ]


def serial_number(entries: list) -> str:
    """Deterministic: the same component set always yields the same serial."""
    digest = hashlib.sha256(
        "\n".join(
            f"{e['ecosystem']}|{e['name']}|{e['version']}|{e['source']}" for e in entries
        ).encode()
    ).hexdigest()
    return (f"urn:uuid:{digest[0:8]}-{digest[8:12]}-4{digest[13:16]}"
            f"-a{digest[17:20]}-{digest[20:32]}")


def cyclonedx(entries: list, ecosystems: list, product_version: str) -> dict:
    components = []
    for entry in entries:
        source_digest = hashlib.sha256(entry["source"].encode()).hexdigest()[:16]
        component = {
            "type": "library" if entry["classification"] == "third-party" else "application",
            "bom-ref": entry["purl"] or (
                f"{entry['ecosystem']}:{entry['name']}@{entry['version']}#{source_digest}"
            ),
            "name": entry["name"],
        }
        if entry["version"]:
            component["version"] = entry["version"]
        if entry["purl"]:
            component["purl"] = entry["purl"]
        if entry["license"]:
            component["licenses"] = [{"license": {"name": entry["license"]}}]
        components.append(component)
    return {
        "bomFormat": "CycloneDX",
        "specVersion": SPEC_VERSION,
        "serialNumber": serial_number(entries),
        "version": 1,
        "metadata": {
            "timestamp": timestamp(),
            "tools": {"components": [
                {"type": "application", "name": "tools/release", "version": str(INVENTORY_VERSION)}
            ]},
            "component": {
                "type": "application",
                "bom-ref": "taffygo",
                "name": "TaffyGo",
                "version": product_version,
            },
            "properties": [
                {"name": f"taffygo:ecosystem:{eco['name']}", "value": eco["status"]}
                for eco in ecosystems
            ],
        },
        "components": components,
    }


def license_inventory(entries: list, ecosystems: list, product_version: str) -> dict:
    third_party = [e for e in entries if e["classification"] == "third-party"]
    unresolved = [e for e in third_party if not e["license"]]
    by_license: dict = {}
    for entry in third_party:
        by_license[entry["license"] or "unresolved"] = (
            by_license.get(entry["license"] or "unresolved", 0) + 1
        )
    return {
        "inventory_version": INVENTORY_VERSION,
        "generated_at": timestamp(),
        "product_version": product_version,
        "ecosystems": ecosystems,
        "entry_count": len(entries),
        "first_party_count": len(entries) - len(third_party),
        "unresolved": len(unresolved),
        "unresolved_entries": [
            {"ecosystem": e["ecosystem"], "name": e["name"], "version": e["version"]}
            for e in unresolved
        ],
        "by_license": dict(sorted(by_license.items())),
        "entries": entries,
    }


def write_json(path: str, document: dict) -> str:
    text = json.dumps(document, indent=2, sort_keys=False) + "\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    return hashlib.sha256(text.encode()).hexdigest()


def cmd_generate(args) -> int:
    records = collect_all(args.root, args.gradle_report, args.chromium_inventory,
                          args.android_native_inventory)
    entries = flatten(records, args.root)
    ecosystems = ecosystem_summary(records)
    os.makedirs(args.out, exist_ok=True)
    bom_path = os.path.join(args.out, "sbom.cdx.json")
    inventory_path = os.path.join(args.out, "licenses.json")
    bom_digest = write_json(bom_path, cyclonedx(entries, ecosystems, args.product_version))
    inventory = license_inventory(entries, ecosystems, args.product_version)
    inventory_digest = write_json(inventory_path, inventory)

    # Paths in the summary are basenames: a manifest records artifact paths
    # relative to the upload it describes, never absolute paths on the machine
    # that happened to build it.
    summary = {
        "dependencies": {
            "sbom": {
                "format": "CycloneDX", "spec_version": SPEC_VERSION,
                "path": os.path.basename(bom_path), "sha256": bom_digest,
                "component_count": len(entries),
                "ecosystems": [
                    {k: v for k, v in eco.items() if v != ""} for eco in ecosystems
                ],
            },
            "license_inventory": {
                "path": os.path.basename(inventory_path), "sha256": inventory_digest,
                "entry_count": len(entries), "unresolved": inventory["unresolved"],
            },
        }
    }
    if args.summary:
        with open(args.summary, "w", encoding="utf-8") as handle:
            json.dump(summary, handle, indent=2)
            handle.write("\n")

    for eco in ecosystems:
        detail = eco["blocker"] or eco["evidence_command"]
        print(f"  {eco['name']:<15} {eco['status']:<12} "
              f"{eco['component_count']:>5} component(s)  {detail}")
    print()
    print(f"  {len(entries)} component(s), {inventory['first_party_count']} first-party, "
          f"{inventory['unresolved']} third-party with no resolved license")
    print(f"  {bom_path}")
    print(f"  {inventory_path}")
    return 0


def cmd_self_test(args) -> int:
    """Structural and determinism checks against this repository."""
    failures = 0
    checks = 0

    def check(condition: bool, message: str) -> None:
        nonlocal failures, checks
        checks += 1
        if not condition:
            failures += 1
            print(f"FAIL  {message}")

    os.environ["SOURCE_DATE_EPOCH"] = "1755388800"
    records = collect_all(args.root)
    entries = flatten(records, args.root)
    ecosystems = ecosystem_summary(records)

    check(len(records) == 5, "every ecosystem the repository declares is collected")
    check({r["name"] for r in records} == {"rust", "javascript", "gradle", "chromium",
                                           "android-native"},
          "the ecosystem names match the manifest schema enum")
    for record in records:
        check(record["status"] in ("complete", "partial", "unavailable", "deferred"),
              f"{record['name']}: reports a known coverage state")
        if record["status"] in ("partial", "unavailable", "deferred"):
            check(bool(record["blocker"]),
                  f"{record['name']}: a non-complete ecosystem names its blocker")
        if record["status"] == "deferred":
            check(not record["components"] and bool(record["owner"]),
                  f"{record['name']}: a deferred ecosystem lists no components and names an owner")

    document = cyclonedx(entries, ecosystems, "self-test")
    check(document["bomFormat"] == "CycloneDX" and document["specVersion"] == SPEC_VERSION,
          "the document declares CycloneDX and its specification version")
    check(all("name" in c and "type" in c for c in document["components"]),
          "every CycloneDX component carries a type and a name")
    check(len({c["bom-ref"] for c in document["components"]}) == len(document["components"]),
          "every CycloneDX component reference is unique")

    again = cyclonedx(flatten(collect_all(args.root), args.root), ecosystems, "self-test")
    check(json.dumps(document) == json.dumps(again),
          "two runs over the same tree produce an identical document")

    inventory = license_inventory(entries, ecosystems, "self-test")
    check(inventory["unresolved"] == len(inventory["unresolved_entries"]),
          "the unresolved count and the unresolved list agree")
    check(all(e["classification"] in ("first-party", "third-party") for e in entries),
          "every entry is classified")
    check(inventory["first_party_count"]
          == sum(1 for e in entries if e["classification"] == "first-party"),
          "the first-party count and the entry classifications agree")

    with tempfile.TemporaryDirectory(prefix="taffy-gradle-pom-") as cache:
        leaf = os.path.join(cache, "example", "leaf", "1", "hash")
        parent = os.path.join(cache, "example", "parent", "2", "hash")
        os.makedirs(leaf)
        os.makedirs(parent)
        with open(os.path.join(leaf, "leaf.pom"), "w", encoding="utf-8") as handle:
            handle.write(
                "<project><parent><groupId>example</groupId>"
                "<artifactId>parent</artifactId><version>2</version>"
                "</parent><artifactId>leaf</artifactId></project>"
            )
        with open(os.path.join(parent, "parent.pom"), "w", encoding="utf-8") as handle:
            handle.write(
                "<project><licenses><license><name>Example License</name>"
                "</license></licenses></project>"
            )
        check(
            gradle_inventory._gradle_license(cache, "example", "leaf", "1")
            == "Example License",
            "Gradle license resolution follows a cached literal Maven parent",
        )

    with tempfile.TemporaryDirectory(prefix="taffy-external-inventory-") as fixtures:
        empty = os.path.join(fixtures, "empty.json")
        invalid = os.path.join(fixtures, "invalid.json")
        valid = os.path.join(fixtures, "valid.json")
        two_sources = os.path.join(fixtures, "two-sources.json")
        base_metadata = {
            "schema_version": 1,
            "kind": "fixture",
            "source_revision": "a" * 40,
            "upstream_revision": "b" * 40,
            "profile": "fixture",
            "target": "//fixture",
        }
        write_json(empty, {**base_metadata, "components": []})
        write_json(invalid, {**base_metadata, "components": [{"name": ""}]})
        write_json(valid, {**base_metadata, "components": [
            {"name": "observed.so", "version": "1", "license": "Apache-2.0"}
        ]})
        write_json(two_sources, {**base_metadata, "components": [
            {"name": "same", "version": "1", "license": "MIT", "source": "one"},
            {"name": "same", "version": "1", "license": "MIT", "source": "two"},
        ]})
        check(
            collectors.collect_external("fixture", empty, "test", "test")["status"]
            == "unavailable",
            "an empty imported inventory cannot report complete coverage",
        )
        check(
            collectors.collect_external("fixture", invalid, "test", "test")["status"]
            == "unavailable",
            "an imported inventory rejects a component without a name",
        )
        imported = collectors.collect_external("fixture", valid, "test", "test")
        check(
            imported["status"] == "complete" and len(imported["components"]) == 1,
            "a non-empty structurally valid imported inventory reports complete coverage",
        )
        independently_vendored = collectors.collect_external(
            "fixture", two_sources, "test", "test"
        )
        check(
            independently_vendored["status"] == "complete"
            and len(independently_vendored["components"]) == 2,
            "same-name components from distinct source paths remain distinct",
        )

    print()
    print(f"sbom: {checks} check(s), {failures} failure(s)")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(prog="sbom.py", description=__doc__.splitlines()[0])
    parser.add_argument("--root", required=True, help="repository root")
    parser.add_argument("--out", help="directory to write sbom.cdx.json and licenses.json into")
    parser.add_argument("--summary", help="write a machine-readable summary here")
    parser.add_argument("--product-version", default="0.0.0-development")
    parser.add_argument("--gradle-report", default="",
                        help="resolved ./gradlew dependencies output")
    parser.add_argument("--chromium-inventory", default="",
                        help="the Chromium track's third-party inventory")
    parser.add_argument("--android-native-inventory", default="",
                        help="the packaged native library inventory")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return cmd_self_test(args)
    if not args.out:
        parser.error("--out is required unless --self-test is given")
    return cmd_generate(args)


if __name__ == "__main__":
    sys.exit(main())
