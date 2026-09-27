#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Break one fact at a time in a copy of the real authorities, and check that
the product manifest contract names it.

Every fixture is the committed file itself, copied into a temporary tree and
mutated once: a checker written against a hand-made manifest proves nothing
about the manifest that ships. The split from check_product_manifest.py is the
line cap's, and the seam is the honest one — that module states the contract,
this one is the adversary for it.
"""

from __future__ import annotations

import re
import tempfile
from pathlib import Path

from check_product_manifest import (
    CAST_NON_REMOVALS,
    DOWNLOAD_RECEIVER,
    EXTRACTION_RULES,
    GRADLE_MANIFEST,
    LEGACY_RULES,
    MEDIA_CAPTURE_BLOCKS,
    PRODUCT_TARGET,
    PRODUCT_TEMPLATE,
    QUERY_ALL_PACKAGES_BLOCK,
    REQUIRED_QUERIES,
    ROOT,
    BASE_TEMPLATE_EXTENDS,
    ManifestContractError,
    read,
    require,
    verify,
)


def write_fixture(root: Path) -> None:
    for relative in (
        GRADLE_MANIFEST,
        PRODUCT_TEMPLATE,
        PRODUCT_TARGET,
        EXTRACTION_RULES,
        LEGACY_RULES,
    ):
        destination = root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(read(ROOT, relative), encoding="utf-8")


def rewrite(root: Path, relative: Path, old: str, new: str, regex: bool = False) -> None:
    """Break exactly one fact in a fixture copy of a real authority."""
    path = root / relative
    text = path.read_text(encoding="utf-8")
    path.write_text(
        re.sub(old, new, text, count=1) if regex else text.replace(old, new, 1),
        encoding="utf-8",
    )


def expect_failure(root: Path, fragment: str) -> None:
    try:
        verify(root)
    except ManifestContractError as error:
        require(fragment in str(error), f"self-test expected {fragment!r}, got {error!r}")
        return
    raise AssertionError(f"fixture unexpectedly passed; wanted {fragment!r}")


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="taffy-manifest-") as directory:
        root = Path(directory)
        write_fixture(root)
        verify(root)

        rules = root / EXTRACTION_RULES
        rules.write_text(
            rules.read_text(encoding="utf-8").replace(
                '<exclude domain="device_file" path="." />', "", 1
            ),
            encoding="utf-8",
        )
        expect_failure(root, "cloud-backup domains")

        write_fixture(root)
        manifest = root / GRADLE_MANIFEST
        manifest.write_text(
            manifest.read_text(encoding="utf-8").replace(
                'android:exported="false"', 'android:exported="true"', 1
            ),
            encoding="utf-8",
        )
        expect_failure(root, "must not be exported")

        write_fixture(root)
        target = root / PRODUCT_TARGET
        target.write_text(target.read_text(encoding="utf-8") + '\nbackup_key = "x"\n')
        expect_failure(root, "would re-enable Chromium backup")

        write_fixture(root)
        template = root / PRODUCT_TEMPLATE
        template.write_text(
            template.read_text(encoding="utf-8").replace("{{ super() }}", "", 1),
            encoding="utf-8",
        )
        expect_failure(root, "preserve its Chromium base")

        write_fixture(root)
        template = root / PRODUCT_TEMPLATE
        template.write_text(
            "<!-- rendered before the inherited XML declaration -->\n"
            + template.read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        expect_failure(root, "inheritance must be the first bytes")

        write_fixture(root)
        template = root / PRODUCT_TEMPLATE
        template.write_text(
            template.read_text(encoding="utf-8").replace(
                BASE_TEMPLATE_EXTENDS,
                '<?xml version="1.0" encoding="utf-8"?>\n'
                + BASE_TEMPLATE_EXTENDS,
                1,
            ),
            encoding="utf-8",
        )
        expect_failure(root, "inheritance must be the first bytes")

        write_fixture(root)
        template = root / PRODUCT_TEMPLATE
        template.write_text(
            template.read_text(encoding="utf-8").replace(DOWNLOAD_RECEIVER, "missing", 1),
            encoding="utf-8",
        )
        expect_failure(root, "missing exact download control receiver")

        # Decision 0153, both halves. The first two are what a revert or a
        # half-applied rebase leaves behind, and the merged manifest is the
        # only other place either would show.
        write_fixture(root)
        rewrite(
            root,
            PRODUCT_TEMPLATE,
            r"\{% block " + QUERY_ALL_PACKAGES_BLOCK + r" %\}\{% endblock %\}",
            "",
            regex=True,
        )
        expect_failure(root, f"missing the {QUERY_ALL_PACKAGES_BLOCK} override")

        write_fixture(root)
        rewrite(
            root,
            PRODUCT_TEMPLATE,
            "{% block " + QUERY_ALL_PACKAGES_BLOCK + " %}{% endblock %}",
            "{% block " + QUERY_ALL_PACKAGES_BLOCK + " %}\n"
            '<uses-permission android:name="android.permission.QUERY_ALL_PACKAGES" '
            'tools:node="remove" />\n{% endblock %}',
        )
        expect_failure(root, "must be overridden with")

        write_fixture(root)
        # Every entry, not one chosen by position: an index survives a
        # shortened tuple as a different rule, or as an IndexError that reads
        # like a checker defect rather than an un-updated self-test.
        for fragment in REQUIRED_QUERIES:
            write_fixture(root)
            rewrite(root, PRODUCT_TEMPLATE, fragment, "")
            expect_failure(root, f"must name {fragment}")

        write_fixture(root)
        rewrite(
            root,
            PRODUCT_TEMPLATE,
            '<uses-permission android:name="android.permission.FOREGROUND_SERVICE_SPECIAL_USE" />',
            '<uses-permission android:name="android.permission.FOREGROUND_SERVICE_SPECIAL_USE" />\n'
            '<uses-permission android:name="android.permission.QUERY_ALL_PACKAGES" />',
        )
        expect_failure(root, "QUERY_ALL_PACKAGES must not be declared here")

        # Decision 0252, point 6: each block, and a permission put back beside
        # the overrides, which a revert of patch 0052's overlay half would do.
        for block in MEDIA_CAPTURE_BLOCKS:
            write_fixture(root)
            rewrite(root, PRODUCT_TEMPLATE, "{% block " + block + " %}{% endblock %}", "")
            expect_failure(root, f"missing the {block} override")

            write_fixture(root)
            rewrite(
                root,
                PRODUCT_TEMPLATE,
                "{% block " + block + " %}{% endblock %}",
                "{% block " + block + " %}"
                'android:foregroundServiceType="mediaPlayback"{% endblock %}',
            )
            expect_failure(root, f"{block} must be overridden with nothing")

        write_fixture(root)
        rewrite(
            root,
            PRODUCT_TEMPLATE,
            '<uses-permission android:name="android.permission.FOREGROUND_SERVICE_SPECIAL_USE" />',
            '<uses-permission android:name="android.permission.FOREGROUND_SERVICE_SPECIAL_USE" />\n'
            '<uses-permission android:name="android.permission.FOREGROUND_SERVICE_CAMERA" />',
        )
        expect_failure(root, "FOREGROUND_SERVICE_CAMERA must not be declared here")

        # A revert or a rebase putting either Cast removal back is the shape
        # this rule exists to catch, so the mutation adds one rather than
        # taking one away.
        for component in CAST_NON_REMOVALS:
            write_fixture(root)
            rewrite(
                root,
                PRODUCT_TEMPLATE,
                f'<receiver\n    android:name="{DOWNLOAD_RECEIVER}"',
                f'<receiver android:name="{component}" tools:node="remove" />\n'
                f'<receiver\n    android:name="{DOWNLOAD_RECEIVER}"',
            )
            expect_failure(root, f"{component} must not be named here")
