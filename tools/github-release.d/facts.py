#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""What must be true of an APK before a GitHub release may carry it.

Authority boundary: reading the output of apksigner, aapt2, keytool and
bundletool, and the rules decision 0252 sets for the artifacts a GitHub release
is cut from. This module runs no tool, reads no key and reaches no network.
`prepare.py` runs the tools and hands their output here, and
`facts_selftest.py` hands it recorded output instead, which is how the rules
are tested on a host with no Android SDK.

Every rule answers with a verdict and a sentence, never a bare boolean, so a
refusal names what it saw.
"""

from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field

ANDROID_NS = "{http://schemas.android.com/apk/res/android}"

#: A release version: three whole numbers, no leading zeros, no suffix. A
#: suffix would be a second kind of release, and decision 0008 allows one.
VERSION = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")

#: apksigner has printed its signer lines in more than one shape across
#: build-tools releases: "Signer #1 certificate SHA-256 digest: <hex>", the
#: scheme-labelled form build-tools 37 prints, and a rotation-aware
#: "Signer (minSdkVersion=33, ...) certificate ..." form. The label is kept so a
#: source stamp, which is not an app signer, can be told apart.
_APKSIGNER_CERT = re.compile(
    r"^(?P<label>[^\n]*?Signer[^\n]*?):? certificate SHA-256 digest: "
    r"(?P<hex>[0-9a-fA-F]{64})[ \t]*$",
    re.MULTILINE,
)
_KEYTOOL_SHA256 = re.compile(
    r"^[ \t]*SHA256:[ \t]*((?:[0-9A-Fa-f]{2}:){31}[0-9A-Fa-f]{2})[ \t]*$", re.MULTILINE
)
_KEYTOOL_SIGNER = re.compile(r"^Signer #\d+:[ \t]*$", re.MULTILINE)

Check = tuple[bool, str]


def normalize_digest(text: str) -> str:
    """One spelling for a SHA-256: lowercase hex, no separators."""
    return text.replace(":", "").strip().lower()


def colon_form(digest: str) -> str:
    """The spelling keytool prints: uppercase pairs joined by colons."""
    upper = normalize_digest(digest).upper()
    return ":".join(upper[index:index + 2] for index in range(0, len(upper), 2))


def release_title(version: str, chromium: str) -> str:
    """`TaffyGo 1.0 (Chromium 152.0.7977.42)` for 1.0.0; a patch release keeps its third number."""
    match = VERSION.match(version)
    if not match:
        raise ValueError(f"{version!r} is not a release version (X.Y.Z)")
    major, minor, patch = match.groups()
    short = f"{major}.{minor}" if patch == "0" else version
    return f"TaffyGo {short} (Chromium {chromium})"


# --- reading tool output ------------------------------------------------------


def apk_signers(output: str) -> list[str]:
    """Every app-signer certificate `apksigner verify --print-certs` printed."""
    return [
        normalize_digest(match["hex"])
        for match in _APKSIGNER_CERT.finditer(output)
        if "source stamp" not in match["label"].lower()
    ]


def jar_signers(output: str) -> list[str]:
    """The leaf certificate of each signer `keytool -printcert -jarfile` listed.

    A signer's section lists its chain leaf first, so only the first
    fingerprint of each section is the signer's own. An unsigned file prints
    "Not a signed jar file" and yields nothing.
    """
    parts = _KEYTOOL_SIGNER.split(output)
    sections = parts[1:] if len(parts) > 1 else parts
    digests = []
    for section in sections:
        match = _KEYTOOL_SHA256.search(section)
        if match:
            digests.append(normalize_digest(match.group(1)))
    return digests


def key_certificate(output: str) -> str | None:
    """The key's own certificate from `keytool -list -v -alias`: the chain's first."""
    match = _KEYTOOL_SHA256.search(output)
    return normalize_digest(match.group(1)) if match else None


@dataclass
class Manifest:
    package: str | None = None
    code: int | None = None
    name: str | None = None
    abis: tuple[str, ...] = ()


def _whole_number(text: str | None) -> int | None:
    return int(text) if text and text.isdigit() else None


def badging_manifest(output: str) -> Manifest:
    """Package, version and native ABIs from `aapt2 dump badging`."""
    lines = output.splitlines()
    package = next((line for line in lines if line.startswith("package:")), "")
    native = next((line for line in lines if line.startswith("native-code:")), "")

    def attribute(key: str) -> str | None:
        match = re.search(rf"(?:^|\s){key}='([^']*)'", package)
        return match.group(1) if match else None

    return Manifest(
        package=attribute("name"),
        code=_whole_number(attribute("versionCode")),
        name=attribute("versionName"),
        abis=tuple(re.findall(r"'([^']+)'", native)),
    )


def bundle_manifest(output: str) -> Manifest:
    """Package and version from `bundletool dump manifest`, which prints XML."""
    try:
        root = ET.fromstring(output.strip())
    except ET.ParseError:
        return Manifest()
    if root.tag != "manifest":
        return Manifest()
    return Manifest(
        package=root.get("package"),
        code=_whole_number(root.get(f"{ANDROID_NS}versionCode")),
        name=root.get(f"{ANDROID_NS}versionName"),
    )


# --- the rules ------------------------------------------------------------------


@dataclass
class Artifact:
    label: str
    path: str
    present: bool = True
    verified: bool = False
    signers: list[str] = field(default_factory=list)
    manifest: Manifest = field(default_factory=Manifest)


@dataclass
class Policy:
    key: str | None
    floor: int
    minimum: int
    package: str
    chromium: str
    abi: str


def _judge_signature(artifact: Artifact, key: str | None) -> list[Check]:
    distinct = sorted(set(artifact.signers))
    if not distinct:
        return [(False, f"the {artifact.label} carries no signing certificate")]
    checks: list[Check] = []
    if not artifact.verified:
        checks.append((False, f"the {artifact.label}'s signature does not verify"))
    if key is not None and distinct != [key]:
        seen = ", ".join(colon_form(digest) for digest in distinct)
        checks.append((
            False,
            f"the {artifact.label} is signed by {seen}, not by the configured key "
            f"{colon_form(key)}",
        ))
    elif key is not None and artifact.verified:
        checks.append((True, f"the {artifact.label} is signed by the configured key"))
    return checks


def _judge_version(artifact: Artifact, policy: Policy) -> list[Check]:
    manifest = artifact.manifest
    label = artifact.label
    checks: list[Check] = []
    if manifest.package != policy.package:
        checks.append((
            False,
            f"the {label}'s package is {manifest.package or 'unreadable'}, not {policy.package}",
        ))
    if manifest.code is None:
        checks.append((False, f"the {label}'s version code is unreadable"))
    elif manifest.code <= policy.floor:
        checks.append((
            False,
            f"the {label}'s version code {manifest.code} is not above {policy.floor}, "
            "the last one Google Play accepted",
        ))
    elif manifest.code <= policy.minimum:
        checks.append((
            False,
            f"the {label}'s version code {manifest.code} is not above "
            f"--min-version-code {policy.minimum}",
        ))
    else:
        bar = max(policy.floor, policy.minimum)
        checks.append((True, f"the {label}'s version code {manifest.code} is above {bar}"))
    if manifest.name != policy.chromium:
        checks.append((
            False,
            f"the {label}'s version name is {manifest.name or 'unreadable'}, not the "
            f"pinned Chromium {policy.chromium}",
        ))
    elif manifest.package == policy.package:
        checks.append((True, f"the {label} is {manifest.package}, version name {manifest.name}"))
    return checks


def judge_artifact(artifact: Artifact, policy: Policy) -> list[Check]:
    if not artifact.present:
        return [(False, f"no {artifact.label} at {artifact.path}")]
    checks = _judge_signature(artifact, policy.key)
    checks.extend(_judge_version(artifact, policy))
    if policy.abi and artifact.label == "APK":
        if policy.abi in artifact.manifest.abis:
            checks.append((True, f"the APK carries native code for {policy.abi}"))
        else:
            carried = ", ".join(artifact.manifest.abis) or "no ABI at all"
            checks.append((False, f"the APK carries native code for {carried}, not {policy.abi}"))
    return checks


def judge_pair(apk: Artifact, bundle: Artifact) -> list[Check]:
    """Equal versions are evidence the two came from one build; unequal ones refute it."""
    if not (apk.present and bundle.present):
        return []
    first, second = apk.manifest, bundle.manifest
    codes_differ = None not in (first.code, second.code) and first.code != second.code
    if not codes_differ and first.name == second.name:
        return []
    return [(
        False,
        f"the APK is {first.code} ({first.name}) and the bundle is {second.code} "
        f"({second.name}): they are not from one build",
    )]


def judge(apk: Artifact, bundle: Artifact, policy: Policy) -> list[Check]:
    checks: list[Check] = []
    if policy.key is None:
        checks.append((
            False,
            "the configured key's certificate is unknown, so neither signature can be "
            "compared with it",
        ))
    checks.extend(judge_artifact(apk, policy))
    checks.extend(judge_artifact(bundle, policy))
    checks.extend(judge_pair(apk, bundle))
    return checks


def judge_build_log(
    text: str, log_mtime: float, artifacts: list[tuple[str, float]], profile: str
) -> list[Check]:
    """Whether a build log shows a finished build of `profile` that wrote these artifacts."""
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    checks: list[Check] = []
    last = lines[-1] if lines else "(the log is empty)"
    if last == "BUILD_EXIT=0":
        checks.append((True, "the build log ends with BUILD_EXIT=0"))
    else:
        checks.append((False, f"the build log does not end with BUILD_EXIT=0; it ends {last!r}"))
    failed = sum(1 for line in lines if "FAILED:" in line)
    if failed:
        checks.append((False, f"the build log has {failed} FAILED: line(s)"))
    else:
        checks.append((True, "the build log has no FAILED: line"))
    if profile not in text:
        checks.append((False, f"the build log never names the {profile} profile"))
    for label, mtime in artifacts:
        if mtime > log_mtime + 1:
            checks.append((
                False,
                f"the {label} was written after the build log last changed, so the log "
                "does not describe it",
            ))
    return checks


# --- what is written ----------------------------------------------------------


def sums_line(digest: str, file_name: str) -> str:
    """One `sha256sum` line: digest, two spaces, name."""
    return f"{digest}  {file_name}\n"


def verify_section(
    *, apk_file: str, apk_sha: str, key: str, manifest: Manifest, abi: str
) -> str:
    """The section appended to the release notes, so a reader can check the download."""
    return f"""## Verify the download

The APK is signed with the TaffyGo release key. Its certificate's SHA-256
fingerprint is:

```text
{normalize_digest(key)}
{colon_form(key)}
```

| File | SHA-256 |
|---|---|
| `{apk_file}` | `{apk_sha}` |

It is version code {manifest.code}, version name {manifest.name} (the Chromium version it is
built on), for 64-bit ARM phones ({abi}).

Put the APK and `SHA256SUMS` in one folder and run:

```bash
sha256sum -c SHA256SUMS
apksigner verify --print-certs {apk_file}
```

The first command must print `{apk_file}: OK` (on macOS, run
`shasum -a 256 -c SHA256SUMS` instead). The second must print a
`certificate SHA-256 digest` line carrying the first line of the fingerprint
above. If either does not, do not install the file. `apksigner` comes with the
Android SDK build-tools.

A copy installed from Google Play is signed with a key Google holds, so its
fingerprint is different from this one.
"""
