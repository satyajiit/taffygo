#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Every shipping binary and vendored asset has checked provenance.

Authority boundary: the agreement between a `vendor/*.txt` provenance file and
the bytes it describes. It says nothing about whether the terms recorded there
are the right terms — that is a legal reading, and decision 0027 owns it — and
nothing about version pins, which are `pins.sh` reading TOOLCHAIN.md.

Owning milestone: M0 (WP-M0-09).

Why it exists. This repository carries a number of files it did not write —
the Space Grotesk font in two formats, the Phosphor icon subset transcribed
into `TaffyIcon.kt`, the provider brand marks beside them, Gradle's wrapper
jar, and the Dagger jars the Chromium build compiles and processes
with. Each has a provenance file naming the owner, the terms, the source
and a checksum or a count, and decision 0027 was written on the premise that
inbound licences are tracked exactly. Nothing checked that premise: a glyph was
added to `TaffyIcon.kt` in August 2026 and the file that says how many glyphs
are vendored kept saying forty-six for a day. A provenance file that undercounts
is a licensing defect, not a stale number, so the count is checked the way a pin
is.

The register below holds third-party and individually authored assets to their
records. Independently, the gate scans every binary below an Android
`src/main/res` directory first. Each discovered binary must be named either by
this register or by the checked first-party icon derivation manifest. That
disk-first direction prevents an unregistered binary from silently entering
the APK.

Stdlib only. Read-only. Exit status: 0 clean, 1 findings.
"""

from __future__ import annotations

import argparse
import os
import sys

from vendored_asset_checks import directory_findings, measured, recorded, self_test

from vendored_asset_sweep import (
    icon_manifest_assets,
    provenance_files,
    registered_asset_files,
    shipping_binary_assets,
    unregistered_binary_findings,
)

ANDROID_UI = "taffy-core/ui/android"
ADBLOCK = "taffy-core/third_party/adblock-rust"
DAGGER = "taffy-core/third_party/dagger"
COMMONMARK = "taffy-core/third_party/commonmark"
EASYLIST = "taffy-core/third_party/easylist"
FLAG_ICONS = "taffy-core/third_party/flag-icons"
ICON_MANIFEST = f"{ANDROID_UI}/tools/icons/manifest.json"
#: The provenance record the delivery artifacts keep beside them. Named
#: once because both the register key and the self-exclusion below read it.
BUNDLED_RECORD = "PROVENANCE.txt"
CPYTHON = "taffy-core/third_party/cpython"
DELIVERY_BUNDLED = "taffy-core/components/delivery/bundled"

# Two register entries live in the onboarding module and its resource paths
# are long enough to wrap. Naming the two roots once keeps each entry on one
# line per fact, which is the form the rest of the register is in.
_ONBOARDING = f"{ANDROID_UI}/feature/onboarding"
_ONBOARDING_RES = f"{_ONBOARDING}/src/main/res"

# Binary resources are swept from disk first. A new file under a shipping
# Android resource directory is therefore a finding even when nobody added a
# provenance record for it. XML and Kotlin sources continue to be checked by
# their own string, source-discipline, and explicit trademark records.
SHIPPING_BINARY_SUFFIXES = {
    ".bin",
    ".gif",
    ".jpeg",
    ".jpg",
    ".litertlm",
    ".mp3",
    ".mp4",
    ".otf",
    ".png",
    ".tflite",
    ".ttf",
    ".webm",
    ".webp",
    ".woff",
    ".woff2",
}

#: Provenance file -> the assets it describes. `checks` are the assertions this
#: gate can make mechanically; the prose in the file carries the rest.
#:
#: `assets` may name a file or a directory. `licences` is a list because one
#: vendored set can arrive under more than one set of terms, and collapsing that
#: to a single field is how a dual-licensed artifact gets recorded as whatever
#: its neighbours are.
REGISTER: dict[str, dict] = {
    f"{FLAG_ICONS}/vendor/flag-icons.txt": {
        # The one entry with no asset in this tree, and deliberately so: the
        # pack is built from a checksummed tarball by the recipe beside this
        # record and published to the delivery plane, so the bytes a device
        # runs were never committed here. What the register holds it to is
        # therefore the licence text — which travels inside the pack as its
        # own LICENSE member — rather than a checksum of a file that does not
        # exist. The pack's own digest and byte length are recorded in the
        # provenance file and are checked where they can be: by the build
        # recipe, against the tarball, on the machine that makes the pack.
        "assets": [],
        "licences": [f"{FLAG_ICONS}/vendor/flag-icons.MIT.txt"],
        "checks": [],
    },
    f"{ADBLOCK}/vendor/adblock.txt": {
        # A crate tree, not a flat jar directory: checksumming every
        # transitive file here would duplicate Cargo.lock without catching
        # a license the crate already carries next to its sources. The
        # record names the engine and the MPL-2.0 text; the directory is
        # the asset so license-headers leave inbound notices alone.
        "assets": [f"{ADBLOCK}/vendor"],
        "licences": [
            f"{ADBLOCK}/vendor/adblock-v0_13/LICENSE",
            f"{ADBLOCK}/vendor/flatbuffers.Apache-2.0.txt",
            f"{ADBLOCK}/vendor/seahash.MIT.txt",
        ],
        "checks": [],
    },
    f"{EASYLIST}/vendor/easylist.txt": {
        # The opposite of flag-icons on purpose: the upstream bytes ARE the
        # vendored assets here, because easylist.to serves only the current
        # list and the tree is the only durable pin. The pack built from them
        # is still not committed — its digest and byte length are recorded in
        # the provenance file and checked by the build recipe, which refuses
        # snapshots whose digests moved.
        "assets": [f"{EASYLIST}/snapshots"],
        "licences": [f"{EASYLIST}/vendor/easylist.GPL3.txt"],
        "checks": [("directory", f"{EASYLIST}/snapshots")],
    },
    f"{ANDROID_UI}/core/designsystem/vendor/space-grotesk.txt": {
        "assets": [
            f"{ANDROID_UI}/core/designsystem/src/main/res/font/taffy_space_grotesk_variable.ttf"
        ],
        "licences": [f"{ANDROID_UI}/core/designsystem/vendor/space-grotesk.OFL.txt"],
        "checks": [
            (
                "sha256",
                f"{ANDROID_UI}/core/designsystem/src/main/res/font/"
                "taffy_space_grotesk_variable.ttf",
            )
        ],
    },
    f"{ANDROID_UI}/core/ui/vendor/taffy-task-scenes.txt": {
        "assets": [
            f"{ANDROID_UI}/core/ui/src/main/res/drawable-nodpi/taffy_scene_browsing.webp",
            f"{ANDROID_UI}/core/ui/src/main/res/drawable-nodpi/taffy_scene_saved_flow.webp",
        ],
        "licences": [],
        "checks": [
            ("directory", f"{ANDROID_UI}/core/ui/src/main/res/drawable-nodpi"),
        ],
    },
    f"{ANDROID_UI}/core/ui/vendor/taffy-task-scene-masters.txt": {
        "assets": [f"{ANDROID_UI}/core/ui/vendor/task-scenes/masters"],
        "licences": [],
        "checks": [("directory", f"{ANDROID_UI}/core/ui/vendor/task-scenes/masters")],
    },
    # The start page's twenty-four delivered plates. They ship in no installer
    # -- the pack builder reads them off this tree -- so the disk-first sweep
    # under `src/main/res` never sees them, and this bidirectional directory
    # check is the only thing holding the published bytes to a record.
    f"{ANDROID_UI}/core/ui/vendor/taffy-start-scenes.txt": {
        "assets": [f"{ANDROID_UI}/core/ui/vendor/start-scenes/webp"],
        "licences": [],
        "checks": [("directory", f"{ANDROID_UI}/core/ui/vendor/start-scenes/webp")],
    },
    f"{ANDROID_UI}/core/ui/vendor/phosphor.txt": {
        "assets": [
            f"{ANDROID_UI}/core/ui/src/main/kotlin/com/taffygo/browser/ui/core/ui/TaffyIcon.kt",
            f"{ANDROID_UI}/core/ui/src/main/kotlin/com/taffygo/browser/ui/core/ui/TaffyIconPaths.kt",
        ],
        "licences": [f"{ANDROID_UI}/core/ui/vendor/phosphor.MIT.txt"],
        "checks": [
            (
                "glyphs",
                f"{ANDROID_UI}/core/ui/src/main/kotlin/com/taffygo/browser/ui/core/ui/TaffyIcon.kt",
            )
        ],
    },
    f"{ANDROID_UI}/core/ui/vendor/provider-marks.txt": {
        "assets": [
            f"{ANDROID_UI}/core/ui/src/main/kotlin/com/taffygo/browser/ui/core/ui/ProviderMark.kt"
        ],
        # Fifteen trademarks, permitted rather than licensed, so there is no
        # licence text to carry beside them; the provenance file names one
        # terms URL per owner. All fifteen are permitted for one use — a model
        # provider's own row, and that vendor's own set-up pages. The two that
        # were permitted for a second use, a TaffyGo account sign-in button,
        # left with screen SCR-701 (decision 0201): a permitted use is not a
        # licence, so a mark whose one use is gone cannot stay. Cloudflare's
        # left on the other side of the same rule (decision 0214) — the use
        # did not move, the row it identified left the catalog. Checked by
        # checksum rather than by count, because the count that matters here
        # is "which vendors, for which use", and that is prose the file states
        # and a reviewer reads. A reviewer sent here from the gate needs it to
        # be right even though nothing enforces it, which is why it is
        # corrected rather than dropped: it said six, four and two while the
        # file carried eighteen.
        "licences": [],
        "checks": [
            (
                "sha256",
                f"{ANDROID_UI}/core/ui/src/main/kotlin/com/taffygo/browser/ui/core/ui/"
                "ProviderMark.kt",
            )
        ],
    },
    "gradle/wrapper/gradle-wrapper.txt": {
        "assets": ["gradle/wrapper/gradle-wrapper.jar"],
        # Apache-2.0, and the text is not vendored: the jar is build tooling
        # that never reaches a shipped artifact, so nothing distributes it.
        "licences": [],
        "checks": [("sha256", "gradle/wrapper/gradle-wrapper.jar")],
    },
    "website/public/fonts/instrument-serif.txt": {
        "assets": ["website/public/fonts/instrument-serif-latin.woff2"],
        "licences": ["website/public/fonts/instrument-serif-OFL.txt"],
        "checks": [("sha256", "website/public/fonts/instrument-serif-latin.woff2")],
    },
    "website/public/fonts/space-grotesk.txt": {
        "assets": ["website/public/fonts/space-grotesk-latin-variable.woff2"],
        "licences": ["website/public/fonts/OFL.txt"],
        "checks": [("sha256", "website/public/fonts/space-grotesk-latin-variable.woff2")],
    },
    f"{ANDROID_UI}/core/ui/vendor/taffy-profile-banners.txt": {
        # Settings identity tiles (a1–c10 + fallback). Fetched from
        # cdn.openally.ai, which is the same owner's platform, so these are
        # first-party bytes despite the foreign-looking origin; persist the
        # id, never a URL. The record sits beside the webp directory so the
        # directory check is not asked to checksum its own provenance file.
        "assets": [
            f"{ANDROID_UI}/core/ui/vendor/profile-banners",
            f"{ANDROID_UI}/core/ui/src/main/assets/profile-banners",
        ],
        "licences": [],
        "checks": [
            (
                "directory",
                f"{ANDROID_UI}/core/ui/vendor/profile-banners",
            )
        ],
    },
    f"{ANDROID_UI}/feature/settings/vendor/taffy-settings-home-icons.txt": {
        # First-party settings hero stills. Registered because a binary under
        # res/ ships; the checksums in the provenance file are the reviewable
        # link. Row icons are Phosphor (decision 0030).
        "assets": [
            f"{ANDROID_UI}/feature/settings/src/main/res/drawable-xxhdpi"
        ],
        "licences": [],
        "checks": [
            (
                "directory",
                f"{ANDROID_UI}/feature/settings/src/main/res/drawable-xxhdpi",
            )
        ],
    },
    f"{ANDROID_UI}/feature/settings/vendor/taffy-settings-home-icons-mdpi.txt": {
        "assets": [f"{ANDROID_UI}/feature/settings/src/main/res/drawable-mdpi"],
        "licences": [],
        "checks": [
            ("directory", f"{ANDROID_UI}/feature/settings/src/main/res/drawable-mdpi")
        ],
    },
    f"{ANDROID_UI}/feature/settings/vendor/taffy-settings-home-icons-hdpi.txt": {
        "assets": [f"{ANDROID_UI}/feature/settings/src/main/res/drawable-hdpi"],
        "licences": [],
        "checks": [
            ("directory", f"{ANDROID_UI}/feature/settings/src/main/res/drawable-hdpi")
        ],
    },
    f"{ANDROID_UI}/feature/settings/vendor/taffy-settings-home-icons-xhdpi.txt": {
        "assets": [f"{ANDROID_UI}/feature/settings/src/main/res/drawable-xhdpi"],
        "licences": [],
        "checks": [
            ("directory", f"{ANDROID_UI}/feature/settings/src/main/res/drawable-xhdpi")
        ],
    },
    # The onboarding films and their posters: two entries, one per directory,
    # because `directory_findings` sweeps one directory against the whole facts
    # map and a combined entry would report each directory's lines as bytes
    # that are not in the tree. Both records carry the same rights boundary —
    # original TaffyGo work carrying no third-party logo or product artwork.
    # Three further films that did carry official third-party and Government of
    # India marks are not in the tree (OD-094), and the bidirectional directory
    # check is what stops either record from describing them anyway.
    f"{_ONBOARDING}/vendor/showcase-films.txt": {
        "assets": [f"{_ONBOARDING_RES}/raw"],
        "licences": [],
        "checks": [("directory", f"{_ONBOARDING_RES}/raw")],
    },
    f"{_ONBOARDING}/vendor/showcase-film-posters.txt": {
        "assets": [f"{_ONBOARDING_RES}/drawable-nodpi"],
        "licences": [],
        "checks": [("directory", f"{_ONBOARDING_RES}/drawable-nodpi")],
    },
    f"{DAGGER}/vendor/dagger.txt": {
        "assets": [f"{DAGGER}/lib"],
        "licences": [f"{DAGGER}/LICENSE", f"{DAGGER}/LICENSE.checker-framework"],
        "checks": [("directory", f"{DAGGER}/lib")],
    },
    f"{COMMONMARK}/vendor/commonmark.txt": {
        "assets": [f"{COMMONMARK}/lib"],
        "licences": [f"{COMMONMARK}/LICENSE"],
        "checks": [("directory", f"{COMMONMARK}/lib")],
    },
    # The only record in this register that describes bytes which are not a
    # resource of an Android module: the four delivery artifacts decision 0202
    # carries in the package. It is a `directory` check over the whole
    # directory rather than four file checks, so a fifth artifact dropped in
    # beside them is a finding here on the day it lands, and the record sits
    # inside what it describes, which is why `run()` excuses it from carrying
    # its own checksum.
    #
    # Their agreement with the *catalogue* is a different question and is
    # answered elsewhere: `validate_bundled` in the asset-plane generator
    # compares each file's measured length and digest against the variants it
    # satisfies, in the `catalog` lane. This record answers where the bytes
    # came from and under what terms.
    f"{DELIVERY_BUNDLED}/{BUNDLED_RECORD}": {
        "assets": [
            f"{DELIVERY_BUNDLED}/easylist-base.zip",
            f"{DELIVERY_BUNDLED}/flags-4x3-webp.zip",
            f"{DELIVERY_BUNDLED}/scenes-4x3-webp.zip",
            f"{DELIVERY_BUNDLED}/stdlib.zip",
        ],
        # Three of the four arrived under terms somebody else set, and the
        # texts are tracked with the components they came from rather than
        # copied here. start-scenes is first-party reserved media and names
        # no licence text, by TRADEMARKS.md.
        "licences": [
            f"{EASYLIST}/vendor/easylist.GPL3.txt",
            f"{CPYTHON}/LICENSE",
            f"{FLAG_ICONS}/vendor/flag-icons.MIT.txt",
        ],
        "checks": [("directory", DELIVERY_BUNDLED)],
    },
}

def run(repo_root: str) -> list[str]:
    findings: list[str] = []
    icon_assets, icon_findings = icon_manifest_assets(repo_root, ICON_MANIFEST)
    findings.extend(icon_findings)
    registered_shipping = registered_asset_files(repo_root, REGISTER) | icon_assets
    findings.extend(
        unregistered_binary_findings(
            shipping_binary_assets(repo_root, ANDROID_UI, SHIPPING_BINARY_SUFFIXES),
            registered_shipping,
        )
    )
    file_owners: dict[str, set[str]] = {}
    for provenance, entry in REGISTER.items():
        for asset in entry["assets"]:
            absolute_asset = os.path.normpath(os.path.join(repo_root, asset))
            if os.path.isfile(absolute_asset):
                file_owners.setdefault(absolute_asset, set()).add(provenance)

    for provenance, entry in REGISTER.items():
        absolute = os.path.join(repo_root, provenance)
        if not os.path.isfile(absolute):
            findings.append(f"{provenance}: provenance file is missing")
            continue

        for asset in entry["assets"]:
            # `exists` rather than `isfile`: a vendored library can be a
            # directory of artifacts, and the `directory` check below is what
            # holds one of those to its record.
            if not os.path.exists(os.path.join(repo_root, asset)):
                findings.append(f"{provenance}: names an asset that is not in the tree: {asset}")

        for licence in entry["licences"]:
            if not os.path.isfile(os.path.join(repo_root, licence)):
                findings.append(f"{provenance}: the licence text it names is missing: {licence}")

        facts = recorded(absolute)
        for kind, asset in entry["checks"]:
            asset_path = os.path.join(repo_root, asset)
            if kind == "directory":
                if os.path.isdir(asset_path):
                    # A record that lives in the directory it describes cannot
                    # carry its own checksum — writing the line changes the
                    # bytes it measures — so it is excused the same way a file
                    # another record owns is. Nothing else is excused: a
                    # second unrecorded file beside it is still a finding,
                    # which the self-test asserts.
                    separately_registered = {
                        path for path, owners in file_owners.items() if owners - {provenance}
                    } | {os.path.normpath(absolute)}
                    findings += directory_findings(
                        provenance,
                        facts,
                        asset_path,
                        separately_registered,
                    )
                continue
            if not os.path.isfile(asset_path):
                continue  # already reported above
            if kind not in facts:
                findings.append(
                    f"{provenance}: records no `{kind}=` line, so nothing checks {asset}"
                )
                continue
            actual = measured(kind, asset_path)
            if facts[kind] != actual:
                findings.append(
                    f"{provenance}: records {kind}={facts[kind]}, but {asset} "
                    f"measures {kind}={actual}"
                )

    for relative in provenance_files(repo_root, REGISTER):
        if relative not in REGISTER:
            findings.append(
                f"{relative}: a vendored asset with no entry in "
                "tools/lib/vendored_assets.py, so nothing checks it"
            )

    return findings


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="exercise checksum and optional byte-count directory fixtures",
    )
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    args = parser.parse_args(argv)

    self_test_failures = self_test(unregistered_binary_findings, provenance_files)
    for failure in self_test_failures:
        print(f"vendored assets self-test: {failure}", file=sys.stderr)
    if self_test_failures:
        return 1
    if args.self_test:
        print("vendored assets self-test: all fixtures passed")
        return 0

    repo_root = os.path.abspath(args.root)

    findings = run(repo_root)
    for finding in findings:
        print(f"vendored assets: {finding}", file=sys.stderr)
    if findings:
        return 1
    # Counted by walking, not by counting register rows: a directory entry is
    # one row and many files, and the number a reader wants is how many files
    # are actually held.
    assets: set[str] = set()
    for entry in REGISTER.values():
        for asset in entry["assets"]:
            path = os.path.join(repo_root, asset)
            if os.path.isdir(path):
                assets.update(
                    os.path.normpath(os.path.join(path, name))
                    for name in os.listdir(path)
                    if os.path.isfile(os.path.join(path, name))
                )
            elif os.path.isfile(path):
                assets.add(os.path.normpath(path))
    print(
        f"vendored assets: {len(REGISTER)} provenance file(s) agree with "
        f"{len(assets)} vendored file(s)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
