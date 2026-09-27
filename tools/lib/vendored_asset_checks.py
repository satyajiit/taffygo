# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Mechanical checks for facts recorded beside vendored assets."""

from __future__ import annotations

import hashlib
import os
import re
import tempfile


#: `key=value`, the same shape `tools/lib/pins.sh` reads, so a provenance file
#: stays human prose with one machine-readable line per checked fact.
_KV = re.compile(r"^[ \t]*(?P<key>[A-Za-z0-9_-]+)[ \t]*=[ \t]*(?P<value>\S+)[ \t]*$")

#: One vendored Phosphor glyph: a top-level `val Name: ImageVector`.
_GLYPH = re.compile(r"^    val [A-Za-z][A-Za-z0-9]*: ImageVector\b", re.MULTILINE)


def recorded(path: str) -> dict[str, str]:
    """Every `key=value` fact a provenance file records."""
    facts: dict[str, str] = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            match = _KV.match(line)
            if match:
                facts[match.group("key")] = match.group("value")
    return facts


def measured(kind: str, path: str) -> str:
    """What the asset itself says, for the fact named `kind`."""
    if kind == "sha256":
        with open(path, "rb") as handle:
            return hashlib.sha256(handle.read()).hexdigest()
    if kind == "glyphs":
        with open(path, encoding="utf-8") as handle:
            return str(len(_GLYPH.findall(handle.read())))
    raise AssertionError(f"unknown vendored-asset fact: {kind}")


def fact_key(name: str) -> str:
    """The `key=` a directory entry's provenance file uses for one file in it."""
    return "sha256_" + re.sub(r"[^A-Za-z0-9]", "_", name)


def bytes_fact_key(name: str) -> str:
    """The optional byte-count fact for one file in a vendored directory."""
    return "bytes_" + re.sub(r"[^A-Za-z0-9]", "_", name)


def directory_findings(
    provenance: str,
    facts: dict[str, str],
    directory: str,
    separately_registered: set[str] | None = None,
) -> list[str]:
    """One checksum and optional byte count per directory file, both ways.

    The `directory` check exists because one vendored library can be a set of
    files rather than a file. The register names the directory and the
    provenance file carries one checksum line per file in it. A record that
    carries any `bytes_<file>=` line opts the whole directory into checked byte
    counts too. Both disk-to-record and record-to-disk directions are checked.
    """
    if separately_registered is None:
        separately_registered = set()
    findings: list[str] = []
    names = sorted(os.listdir(directory))
    if not names:
        return [f"{provenance}: names {directory}, which is empty"]

    expected = set()
    expected_bytes = set()
    checks_bytes = any(key.startswith("bytes_") for key in facts)
    for name in names:
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            findings.append(f"{provenance}: {name} in the vendored directory is not a file")
            continue
        if os.path.normpath(path) in separately_registered:
            continue
        key = fact_key(name)
        expected.add(key)
        if key not in facts:
            findings.append(f"{provenance}: records no `{key}=` line, so nothing checks {name}")
            continue
        actual = measured("sha256", path)
        if facts[key] != actual:
            findings.append(
                f"{provenance}: records {key}={facts[key]}, but {name} measures {actual}"
            )
        if checks_bytes:
            bytes_key = bytes_fact_key(name)
            expected_bytes.add(bytes_key)
            if bytes_key not in facts:
                findings.append(
                    f"{provenance}: records byte counts but has no `{bytes_key}=` line, "
                    f"so the size of {name} is unchecked"
                )
            else:
                actual_bytes = str(os.path.getsize(path))
                if facts[bytes_key] != actual_bytes:
                    findings.append(
                        f"{provenance}: records {bytes_key}={facts[bytes_key]}, but {name} "
                        f"measures {actual_bytes} bytes"
                    )

    for key in sorted(facts):
        if key.startswith("sha256_") and key not in expected:
            findings.append(
                f"{provenance}: records `{key}=` but no such file is vendored, so the "
                "record describes bytes that are not in the tree"
            )
        if key.startswith("bytes_") and key not in expected_bytes:
            findings.append(
                f"{provenance}: records `{key}=` but no such file is vendored, so the "
                "record describes bytes that are not in the tree"
            )
    return findings


def provenance_classification_failures(provenance_files) -> list[str]:
    """A `vendor/*.txt` is a record or a licence text, and the register says which.

    Written because the rule used to be a list of three filename infixes —
    `.OFL.`, `.MIT.`, `.GPL3.` — which is not the number of licences a
    vendored tree can arrive under. An Apache-2.0 text was read as an
    unregistered provenance record, and the mirror case is worse: a licence
    text no entry names was skipped rather than reported.
    """
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="taffy-provenance-") as root:
        vendor = os.path.join(root, "component", "vendor")
        os.makedirs(vendor)
        for name in ("thing.txt", "thing.MIT.txt", "other.Apache-2.0.txt", "loose.BSD.txt"):
            with open(os.path.join(vendor, name), "w", encoding="utf-8") as handle:
                handle.write("x\n")
        register = {
            "component/vendor/thing.txt": {
                "assets": [],
                "licences": [
                    "component/vendor/thing.MIT.txt",
                    "component/vendor/other.Apache-2.0.txt",
                ],
                "checks": [],
            }
        }
        found = set(provenance_files(root, register))
        if "component/vendor/thing.txt" not in found:
            failures.append("a provenance record was not returned for register checking")
        if "component/vendor/other.Apache-2.0.txt" in found:
            failures.append("a registered Apache-2.0 licence text was read as a record")
        if "component/vendor/thing.MIT.txt" in found:
            failures.append("a registered MIT licence text was read as a record")
        if "component/vendor/loose.BSD.txt" not in found:
            failures.append("a licence text no entry names was skipped instead of reported")
    return failures


def self_test(unregistered_binary_findings, provenance_files=None) -> list[str]:
    """Exercise directory facts and the disk-first sweep against fixtures."""
    failures: list[str] = []
    if provenance_files is not None:
        failures.extend(provenance_classification_failures(provenance_files))
    with tempfile.TemporaryDirectory(prefix="taffy-vendored-assets-") as directory:
        paths = {"one.bin": b"one", "two.bin": b"two-two"}
        for name, payload in paths.items():
            with open(os.path.join(directory, name), "wb") as handle:
                handle.write(payload)

        hashes = {
            fact_key(name): hashlib.sha256(payload).hexdigest()
            for name, payload in paths.items()
        }
        byte_counts = {
            bytes_fact_key(name): str(len(payload)) for name, payload in paths.items()
        }

        if directory_findings("fixture.txt", hashes | byte_counts, directory):
            failures.append("complete byte facts did not pass")

        missing = hashes | byte_counts
        del missing[bytes_fact_key("two.bin")]
        if not any(
            "bytes_two_bin" in finding and "has no" in finding
            for finding in directory_findings("fixture.txt", missing, directory)
        ):
            failures.append("a missing byte fact did not fail")

        stale = hashes | byte_counts | {"bytes_absent_bin": "1"}
        if not any(
            "bytes_absent_bin" in finding and "no such file" in finding
            for finding in directory_findings("fixture.txt", stale, directory)
        ):
            failures.append("a stale byte fact did not fail")

        wrong = hashes | byte_counts | {bytes_fact_key("one.bin"): "999"}
        if not any(
            "bytes_one_bin=999" in finding
            for finding in directory_findings("fixture.txt", wrong, directory)
        ):
            failures.append("a wrong byte count did not fail")

        if directory_findings("fixture.txt", hashes, directory):
            failures.append("a checksum-only record lost backward compatibility")

        # A record that sits in the directory it describes. `run()` passes its
        # own path in as separately registered, because a file cannot record
        # the checksum of bytes that include that record. The exclusion has to
        # be exactly one file wide: the second half of this fixture is an
        # unrecorded artifact beside it, which must still be named.
        record = os.path.join(directory, "PROVENANCE.txt")
        with open(record, "w", encoding="utf-8") as handle:
            handle.write("a record that lives beside what it describes\n")
        excused = {os.path.normpath(record)}
        if directory_findings("PROVENANCE.txt", hashes | byte_counts, directory, excused):
            failures.append("a record beside its own assets was asked to checksum itself")

        with open(os.path.join(directory, "three.bin"), "wb") as handle:
            handle.write(b"three")
        if not any(
            "sha256_three_bin" in finding and "nothing checks" in finding
            for finding in directory_findings(
                "PROVENANCE.txt", hashes | byte_counts, directory, excused
            )
        ):
            failures.append("an unrecorded file beside an excused record was not reported")

    sweep_findings = unregistered_binary_findings(
        {"module/src/main/res/raw/known.bin", "module/src/main/res/raw/unknown.bin"},
        {"module/src/main/res/raw/known.bin"},
    )
    if sweep_findings != [
        "module/src/main/res/raw/unknown.bin: shipping binary has no provenance "
        "or derivation record"
    ]:
        failures.append("the disk-first shipping-binary sweep did not reject an unknown file")
    return failures
