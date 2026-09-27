#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The Gradle dependency collector.

Authority boundary: this module enumerates the Android product UI dependencies and
resolves their licenses. It lives apart from the other collectors because it is
the only one with two sources of truth and an offline license lookup, and
because it is the one whose coverage is `partial` until a real build has run.

Owning milestone: M1 (WP-M1-07).

Two sources, in order of authority. A resolved Gradle dependency report is the
transitive truth; the committed version catalog is the declared set and nothing
more. Licenses come from POM files already in the Gradle module cache, so this
stays offline: a coordinate whose POM is not cached is reported unresolved
rather than guessed.

Stdlib only, no network. Nothing here downloads a dependency.
"""

from __future__ import annotations

import os
import re
import xml.etree.ElementTree as ElementTree
from typing import Optional

from sbom_collectors import component, record

# --- Gradle ------------------------------------------------------------------

GRADLE_COORDINATE = re.compile(r"([A-Za-z0-9_.-]+):([A-Za-z0-9_.-]+):([0-9][A-Za-z0-9_.+-]*)")


def collect_gradle(root: str, resolved_report: str = ""):
    """The Android product UI dependencies.

    Two sources, in order of authority. A resolved Gradle dependency report is
    the transitive truth; the committed version catalog is the declared set and
    nothing more. Licenses come from the POM files already in the Gradle module
    cache, so this stays offline: a coordinate whose POM is not cached is
    reported unresolved rather than guessed.
    """
    catalog = os.path.join(root, "gradle", "libs.versions.toml")
    if not os.path.exists(catalog):
        return record("gradle", "unavailable",
                      blocker="gradle/libs.versions.toml is absent (WP-M0-01)")

    if resolved_report and os.path.exists(resolved_report):
        coordinates = _gradle_from_report(resolved_report)
        status, blocker = "complete", ""
        evidence = f"./gradlew dependencies (report: {resolved_report})"
    else:
        coordinates = _gradle_from_catalog(catalog)
        status = "partial"
        evidence = "gradle/libs.versions.toml (declared dependencies)"
        blocker = (
            "this is the declared set from the version catalog, not the transitive "
            "closure, and a platform-managed coordinate carries no version here. "
            "Licenses are read from the Gradle module cache, so a coordinate whose "
            "POM has not been downloaded on this host resolves to nothing. Produce "
            "the resolved report with `./gradlew :app:dependencies --configuration "
            "releaseRuntimeClasspath > gradle-dependencies.txt` after a build, and "
            "pass it as --gradle-report."
        )

    cache = _gradle_module_cache()
    components = []
    for group, artifact, version in coordinates:
        license_id = _gradle_license(cache, group, artifact, version) if version else ""
        purl = f"pkg:maven/{group}/{artifact}"
        if version:
            purl += f"@{version}"
        components.append(component("gradle", f"{group}:{artifact}", version,
                                    license_id=license_id, purl=purl, source="maven"))
    return record("gradle", status, components, evidence_command=evidence, blocker=blocker)


def _gradle_from_report(path: str):
    found = set()
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            # Gradle prints "\--- group:artifact:asked -> resolved"; take the
            # resolved coordinate when the arrow is present.
            candidate = line.split("->")[-1] if "->" in line else line
            for match in GRADLE_COORDINATE.finditer(candidate):
                found.add(match.groups())
            if "->" in line:
                head = GRADLE_COORDINATE.search(line.split("->")[0])
                resolved = line.split("->")[-1].strip()
                if head and re.fullmatch(r"[0-9][A-Za-z0-9_.+-]*", resolved):
                    found.add((head.group(1), head.group(2), resolved))
    return sorted(found)


def _gradle_from_catalog(path: str):
    catalog = _read_toml(path)
    versions = catalog.get("versions", {})
    coordinates = []
    for entry in catalog.get("libraries", {}).values():
        if isinstance(entry, str):
            match = GRADLE_COORDINATE.fullmatch(entry)
            if match:
                coordinates.append(match.groups())
            continue
        module = entry.get("module", "")
        if ":" not in module:
            continue
        group, artifact = module.split(":", 1)
        version = entry.get("version")
        if isinstance(version, dict):
            version = versions.get(version.get("ref", ""), "")
        coordinates.append((group, artifact, version or ""))
    return sorted(set(coordinates))


def _gradle_module_cache():
    home = os.environ.get("GRADLE_USER_HOME") or os.path.join(os.path.expanduser("~"), ".gradle")
    cache = os.path.join(home, "caches", "modules-2", "files-2.1")
    return cache if os.path.isdir(cache) else ""


def _gradle_license(cache: str, group: str, artifact: str, version: str,
                    seen: frozenset[tuple[str, str, str]] = frozenset()) -> str:
    """Resolve a module license from its cached POM inheritance chain.

    Maven licenses are inherited. Reading only the leaf POM incorrectly marks
    small facade artifacts such as Guava's ``listenablefuture`` unresolved even
    when the cached parent declares the license. Resolution remains offline and
    fail-closed: a missing, malformed, cyclic, or property-based parent yields
    no license instead of a guessed one.
    """
    coordinate = (group, artifact, version)
    if not cache or coordinate in seen or len(seen) >= 16:
        return ""
    directory = os.path.join(cache, group, artifact, version)
    if not os.path.isdir(directory):
        return ""
    parsed_roots = []
    for dirpath, dirnames, filenames in os.walk(directory):
        dirnames.sort()
        for filename in sorted(filenames):
            if not filename.endswith(".pom"):
                continue
            try:
                tree = ElementTree.parse(os.path.join(dirpath, filename))
            except (ElementTree.ParseError, OSError):
                continue
            root = tree.getroot()
            parsed_roots.append(root)
            names = _pom_license_names(root)
            if names:
                return " OR ".join(names)
    next_seen = seen | {coordinate}
    for root in parsed_roots:
        parent = _pom_parent(root)
        if parent:
            inherited = _gradle_license(cache, *parent, seen=next_seen)
            if inherited:
                return inherited
    return ""


def _local(tag: str) -> str:
    """The tag without its XML namespace; Maven POMs carry one, some do not."""
    return tag.rsplit("}", 1)[-1]


def _pom_license_names(root) -> list:
    """Only <project><licenses><license><name>. A POM also carries <name> for
    the project, the developers and the organization, and mistaking one of
    those for a license is how an inventory grows entries like "The Android
    Open Source Project"."""
    names = []
    for licenses in (child for child in root if _local(child.tag) == "licenses"):
        for entry in (child for child in licenses if _local(child.tag) == "license"):
            for field in entry:
                if _local(field.tag) == "name" and field.text and field.text.strip():
                    names.append(field.text.strip())
    return sorted(set(names))


def _pom_parent(root) -> Optional[tuple[str, str, str]]:
    """Return one literal Maven parent coordinate, never an interpolated one."""
    parent = next((child for child in root if _local(child.tag) == "parent"), None)
    if parent is None:
        return None
    fields = {
        _local(child.tag): child.text.strip()
        for child in parent
        if child.text and child.text.strip()
    }
    coordinate = tuple(fields.get(name, "") for name in ("groupId", "artifactId", "version"))
    if not all(coordinate) or any("${" in value for value in coordinate):
        return None
    return coordinate


# --- a very small TOML reader ------------------------------------------------

def _read_toml(path: str) -> dict:
    try:
        import tomllib  # Python 3.11+
    except ImportError:
        return _read_toml_minimal(path)
    with open(path, "rb") as handle:
        return tomllib.load(handle)


INLINE_TABLE = re.compile(r'(\w+(?:\.\w+)?)\s*=\s*("([^"]*)"|\{[^}]*\})')


def _read_toml_minimal(path: str) -> dict:
    """Enough of TOML for gradle/libs.versions.toml on a host older than 3.11:
    `[section]`, `key = "value"`, and one level of inline table."""
    document: dict = {}
    section = document
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("[") and line.endswith("]"):
                section = document.setdefault(line[1:-1], {})
                continue
            if "=" not in line:
                continue
            key, value = (part.strip() for part in line.split("=", 1))
            if value.startswith("{"):
                table: dict = {}
                for match in INLINE_TABLE.finditer(value):
                    inner_key, _raw, inner_value = match.group(1), match.group(2), match.group(3)
                    if "." in inner_key:
                        head, tail = inner_key.split(".", 1)
                        table.setdefault(head, {})[tail] = inner_value
                    else:
                        table[inner_key] = inner_value
                section[key] = table
            else:
                section[key] = value.strip('"')
    return document
