#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Verify SCR-801 and PAR-AND-008 in both Android manifest authorities."""

from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
GRADLE_MANIFEST = Path("taffy-core/ui/android/app/src/main/AndroidManifest.xml")
PRODUCT_TEMPLATE = Path("taffy-core/app/android/AndroidManifest.xml.jinja2")
PRODUCT_TARGET = Path("taffy-core/app/android/product_targets.gni")
EXTRACTION_RULES = Path(
    "taffy-core/ui/android/app/src/taskContinuation/res/xml/taffy_data_extraction_rules.xml"
)
LEGACY_RULES = Path(
    "taffy-core/ui/android/app/src/taskContinuation/res/xml/taffy_full_backup_content.xml"
)
ANDROID = "{http://schemas.android.com/apk/res/android}"
SERVICE = "com.taffygo.browser.ui.app.TaskContinuationService"
DOWNLOAD_RECEIVER = "org.chromium.taffy.shell.TaffyDownloadActionReceiver"
#: Cast components this template must NOT remove. It used to require exactly
#: the opposite, on the rationale that they arrived with the Play Billing
#: Library's datatransport carrier and that TaffyGo has no Cast surface. A dump
#: of a built APK refuted both: the merged manifest carried them before that
#: library was vendored, from chrome_java's own Cast dependency, and it also
#: registers CastOptionsProvider and a Cast listener service. Upstream keeps
#: MediaIntentReceiver deliberately. That library has since been removed from
#: the product altogether and the merged manifest of a build made after the
#: removal still carries both components, which settles the carrier half by
#: measurement. The rule stays inverted because the removals are
#: what a rebase or a revert would put back. Changing TaffyGo's media-router
#: posture is a separate decision that would retire this check by name.
CAST_NON_REMOVALS = (
    "com.google.android.gms.cast.framework.media.MediaIntentReceiver",
    "com.google.android.gms.policy_cast_dynamite",
)
#: Decision 0153: this product does not enumerate what a person has installed.
#: Upstream patch 0051 wraps QUERY_ALL_PACKAGES in this block so an extending
#: template can decline it, and the overlay overrides it with nothing. Both
#: halves are checked here because neither is visible anywhere else on a host:
#: the permission's absence exists only in the merged manifest of a built
#: artifact, and a rebase that drops patch 0051 would put it back with nothing
#: to say so. The queries are the same rule read the other way — they are what
#: the browser keeps instead of the list, so losing one silently ends the PDF
#: hand-off, the default-browser comparison, or upstream's own reads of the
#: Play Store package. That last one is the entry with no first-party caller
#: and it is not spare: clause 4's `intent://` fallback never runs in this
#: product and the `market://` hand-off is a chooser rather than a query, but
#: `PasswordManagerUtilBridge.isGooglePlayServicesUpdatable` calls
#: `getPackageInfo("com.android.vending")` and upstream needs no entry of its
#: own only because it declares the permission this product declines. The
#: template's own comment carries the argument.
QUERY_ALL_PACKAGES_BLOCK = "query_all_packages_permission"
REQUIRED_QUERIES = (
    '<data android:scheme="content" android:mimeType="application/pdf" />',
    '<data android:scheme="https" />',
    '<data android:scheme="http" />',
    '<package android:name="com.android.vending" />',
)
VIEW_ACTION = '<action android:name="android.intent.action.VIEW" />'
BLOCK_OVERRIDE = re.compile(
    r"\{%\s*block\s+" + QUERY_ALL_PACKAGES_BLOCK + r"\s*%\}(?P<body>.*?)\{%\s*endblock",
    re.DOTALL,
)
QUERIES_ELEMENT = re.compile(r"<queries>(?P<body>.*?)</queries>", re.DOTALL)
SUBTYPE = "user_initiated_browser_research_with_visible_pause_stop_resume"
BASE_TEMPLATE_EXTENDS = '{% extends "chrome/android/java/AndroidManifest.xml" %}'
MODERN_DOMAINS = {
    "root",
    "database",
    "sharedpref",
    "external",
    "file",
    "device_root",
    "device_database",
    "device_sharedpref",
    "device_file",
}
LEGACY_DOMAINS = {"root", "database", "sharedpref", "external", "file"}


class ManifestContractError(ValueError):
    """A shipping or Gradle projection weakened the closed manifest policy."""


def read(root: Path, relative: Path) -> str:
    try:
        return (root / relative).read_text(encoding="utf-8")
    except OSError as error:
        raise ManifestContractError(str(error)) from error


def parse(root: Path, relative: Path) -> ET.Element:
    try:
        return ET.fromstring(read(root, relative))
    except ET.ParseError as error:
        raise ManifestContractError(f"{relative}: invalid XML: {error}") from error


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ManifestContractError(message)


def verify_gradle_manifest(root: Path) -> None:
    manifest = parse(root, GRADLE_MANIFEST)
    permissions = {
        node.get(f"{ANDROID}name") for node in manifest.findall("uses-permission")
    }
    for permission in (
        "android.permission.POST_NOTIFICATIONS",
        "android.permission.FOREGROUND_SERVICE",
        "android.permission.FOREGROUND_SERVICE_SPECIAL_USE",
    ):
        require(permission in permissions, f"{GRADLE_MANIFEST}: missing {permission}")
    require(
        "android.permission.FOREGROUND_SERVICE_DATA_SYNC" not in permissions,
        f"{GRADLE_MANIFEST}: task continuation must not claim dataSync",
    )

    application = manifest.find("application")
    require(application is not None, f"{GRADLE_MANIFEST}: missing application")
    assert application is not None
    require(
        application.get(f"{ANDROID}allowBackup") == "false",
        f"{GRADLE_MANIFEST}: android:allowBackup must be false",
    )
    require_backup_attributes(application, GRADLE_MANIFEST)
    service = next(
        (
            node
            for node in application.findall("service")
            if node.get(f"{ANDROID}name") in (SERVICE, ".TaskContinuationService")
        ),
        None,
    )
    require(service is not None, f"{GRADLE_MANIFEST}: missing task continuation service")
    assert service is not None
    verify_service(service, GRADLE_MANIFEST)


def require_backup_attributes(application: ET.Element, source: Path) -> None:
    require(
        application.get(f"{ANDROID}dataExtractionRules") ==
        "@xml/taffy_data_extraction_rules",
        f"{source}: missing TaffyGo data-extraction policy",
    )
    require(
        application.get(f"{ANDROID}fullBackupContent") ==
        "@xml/taffy_full_backup_content",
        f"{source}: missing TaffyGo legacy backup policy",
    )


def verify_service(service: ET.Element, source: Path) -> None:
    require(
        service.get(f"{ANDROID}exported") == "false",
        f"{source}: task continuation service must not be exported",
    )
    require(
        service.get(f"{ANDROID}foregroundServiceType") == "specialUse",
        f"{source}: task continuation must use only specialUse",
    )
    properties = {
        (node.get(f"{ANDROID}name"), node.get(f"{ANDROID}value"))
        for node in service.findall("property")
    }
    require(
        ("android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE", SUBTYPE) in properties,
        f"{source}: missing exact specialUse subtype",
    )


def verify_product_template(root: Path) -> None:
    text = read(root, PRODUCT_TEMPLATE)
    require(
        text.startswith(BASE_TEMPLATE_EXTENDS),
        f"{PRODUCT_TEMPLATE}: Chromium manifest inheritance must be the first bytes",
    )
    require(
        "<?xml" not in text,
        f"{PRODUCT_TEMPLATE}: the Chromium base owns the XML declaration",
    )
    required = (
        BASE_TEMPLATE_EXTENDS,
        "{% block extra_uses_permissions %}",
        "android.permission.FOREGROUND_SERVICE_SPECIAL_USE",
        "{% block extra_application_attributes %}",
        'android:dataExtractionRules="@xml/taffy_data_extraction_rules"',
        'android:fullBackupContent="@xml/taffy_full_backup_content"',
        "{% block extra_application_definitions %}",
        f'android:name="{SERVICE}"',
        'android:exported="false"',
        'android:foregroundServiceType="specialUse"',
        "android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE",
        f'android:value="{SUBTYPE}"',
    )
    for marker in required:
        require(marker in text, f"{PRODUCT_TEMPLATE}: missing {marker}")
    require(
        text.count("{{ super() }}") == 3,
        f"{PRODUCT_TEMPLATE}: every overlay block must preserve its Chromium base",
    )
    require(
        "FOREGROUND_SERVICE_DATA_SYNC" not in text and
        'foregroundServiceType="dataSync"' not in text,
        f"{PRODUCT_TEMPLATE}: task continuation must not claim dataSync",
    )
    receiver_tags = [
        tag for tag in re.findall(r"<receiver\b[^>]*/>", text, re.DOTALL)
        if f'android:name="{DOWNLOAD_RECEIVER}"' in tag
    ]
    require(
        len(receiver_tags) == 1,
        f"{PRODUCT_TEMPLATE}: missing exact download control receiver",
    )
    require(
        'android:exported="false"' in receiver_tags[0],
        f"{PRODUCT_TEMPLATE}: download control receiver must not be exported",
    )
    verify_cast_untouched(text)
    verify_package_visibility(text)


def verify_package_visibility(text: str) -> None:
    """Decision 0153: the permission is declined and the queries replace it."""
    override = BLOCK_OVERRIDE.search(text)
    require(
        override is not None,
        f"{PRODUCT_TEMPLATE}: missing the {QUERY_ALL_PACKAGES_BLOCK} override. "
        "Upstream patch 0051 declares that block around QUERY_ALL_PACKAGES so "
        "this template can decline it (decision 0153); without the override the "
        "product ships with package visibility switched off for everything",
    )
    require(
        not override.group("body").strip(),
        f"{PRODUCT_TEMPLATE}: {QUERY_ALL_PACKAGES_BLOCK} must be overridden with "
        "nothing. Anything rendered there is declared by this document, which is "
        "also why tools:node=\"remove\" cannot take it back out",
    )
    declared = [
        tag
        for tag in re.findall(r"<uses-permission\b[^>]*>", text, re.DOTALL)
        if "QUERY_ALL_PACKAGES" in tag
    ]
    require(
        not declared,
        f"{PRODUCT_TEMPLATE}: QUERY_ALL_PACKAGES must not be declared here. The "
        "merger ignores a node operation on an element the same document "
        "declares, so a removal marker beside it would read as a removal and "
        "ship the permission",
    )
    element = QUERIES_ELEMENT.search(text)
    require(element is not None, f"{PRODUCT_TEMPLATE}: missing the <queries> element")
    body = " ".join(element.group("body").split())
    for fragment in REQUIRED_QUERIES:
        require(
            fragment in body,
            f"{PRODUCT_TEMPLATE}: the <queries> element must name {fragment}",
        )
    require(
        body.count(VIEW_ACTION) == 3,
        f"{PRODUCT_TEMPLATE}: each queried intent states the view action",
    )


def verify_cast_untouched(text: str) -> None:
    """No Cast component is removed by this template. See CAST_NON_REMOVALS."""
    tags = re.findall(r"<[a-z]+\b[^>]*?/>", text, re.DOTALL)
    for component in CAST_NON_REMOVALS:
        named = [tag for tag in tags if f'android:name="{component}"' in tag]
        require(
            not named,
            f"{PRODUCT_TEMPLATE}: {component} must not be named here. It did not "
            "arrive with the Play Billing Library this repository used to vendor — "
            "the merged manifest carried it before that library arrived and still "
            "carries it now that it is gone, from chrome_java's own Cast "
            "dependency — and this product registers CastOptionsProvider besides. "
            "Removing it deletes a shipping component on a false rationale; "
            "changing TaffyGo's media-router posture needs its own decision",
        )


def verify_product_target(root: Path) -> None:
    text = read(root, PRODUCT_TARGET)
    projected = re.findall(r'^\s*jinja_input\s*=\s*"([^"]+)"', text, re.MULTILINE)
    require(
        projected == ["//taffy/app/android/AndroidManifest.xml.jinja2"],
        f"{PRODUCT_TARGET}: shipping APK must select exactly the TaffyGo manifest",
    )
    require(
        re.search(r"^\s*backup_key\s*=", text, re.MULTILINE) is None,
        f"{PRODUCT_TARGET}: backup_key would re-enable Chromium backup",
    )


def excluded_domains(section: ET.Element, source: Path) -> set[str]:
    require(not section.findall("include"), f"{source}: backup policy may not include data")
    excludes = section.findall("exclude")
    require(
        all(node.get("path") == "." for node in excludes),
        f"{source}: every exclusion must cover its complete domain",
    )
    return {node.get("domain", "") for node in excludes}


def verify_backup_rules(root: Path) -> None:
    modern = parse(root, EXTRACTION_RULES)
    for name in ("cloud-backup", "device-transfer"):
        section = modern.find(name)
        require(section is not None, f"{EXTRACTION_RULES}: missing {name}")
        assert section is not None
        actual = excluded_domains(section, EXTRACTION_RULES)
        require(
            actual == MODERN_DOMAINS,
            f"{EXTRACTION_RULES}: {name} domains are {sorted(actual)}; "
            f"expected {sorted(MODERN_DOMAINS)}",
        )
    legacy = parse(root, LEGACY_RULES)
    actual = excluded_domains(legacy, LEGACY_RULES)
    require(
        actual == LEGACY_DOMAINS,
        f"{LEGACY_RULES}: domains are {sorted(actual)}; expected {sorted(LEGACY_DOMAINS)}",
    )


def verify(root: Path) -> None:
    verify_gradle_manifest(root)
    verify_product_template(root)
    verify_product_target(root)
    verify_backup_rules(root)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    try:
        if arguments.self_test:
            from check_product_manifest_selftest import self_test

            self_test()
            print("product manifest contract self-test passed")
        else:
            verify(arguments.root.resolve())
            print("Gradle and shipping product manifest contracts match")
        return 0
    except (ManifestContractError, AssertionError) as error:
        print(f"product manifest contract: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
