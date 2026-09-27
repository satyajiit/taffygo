#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Keep TaffyGo's shipping and instrumentation APKs on one target SDK."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OWNER = Path("gradle/libs.versions.toml")
CONSUMERS = (
    Path("taffy-core/app/android/product_targets.gni"),
    Path("taffy-core/test/android/ui/BUILD.gn"),
)
TOML_TARGET = re.compile(r'^\s*targetSdk\s*=\s*"([0-9]+)"\s*(?:#.*)?$', re.MULTILINE)
GN_TARGET = re.compile(
    r'^\s*target_sdk_version\s*=\s*"([0-9]+)"\s*(?:#.*)?$',
    re.MULTILINE,
)
APK_TARGET = re.compile(r"^targetSdkVersion:'([0-9]+)'$", re.MULTILINE)


class AgreementError(ValueError):
    """One authority or projection is absent, ambiguous, or inconsistent."""


def one_value(pattern: re.Pattern[str], text: str, label: str) -> str:
    values = pattern.findall(text)
    if len(values) != 1:
        raise AgreementError(f"{label} must record exactly one scalar value; found {len(values)}")
    return values[0]


def expected_target_sdk(root: Path) -> str:
    owner_path = root / OWNER
    try:
        owner_text = owner_path.read_text(encoding="utf-8")
    except OSError as error:
        raise AgreementError(str(error)) from error

    owner = one_value(TOML_TARGET, owner_text, str(OWNER))
    for consumer in CONSUMERS:
        try:
            consumer_text = (root / consumer).read_text(encoding="utf-8")
        except OSError as error:
            raise AgreementError(str(error)) from error
        projected = one_value(GN_TARGET, consumer_text, str(consumer))
        if projected != owner:
            raise AgreementError(
                f"{consumer} targets SDK {projected}, but the machine owner {OWNER} records {owner}"
            )
    return owner


def target_sdk_from_badging(badging: str) -> str:
    return one_value(APK_TARGET, badging, "APK badging")


def verify_apk(apk: Path, aapt2: Path, expected: str) -> None:
    if not apk.is_file():
        raise AgreementError(f"built APK is missing: {apk}")
    if not aapt2.is_file():
        raise AgreementError(f"aapt2 is missing: {aapt2}")
    try:
        result = subprocess.run(
            [str(aapt2), "dump", "badging", str(apk)],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise AgreementError(f"could not inspect {apk} with {aapt2}: {error}") from error
    actual = target_sdk_from_badging(result.stdout)
    if actual != expected:
        raise AgreementError(f"{apk} targets SDK {actual}; expected machine-owned SDK {expected}")


def write_fixture(
    root: Path,
    owner: str,
    shipping: str,
    instrumentation: str | None = None,
) -> None:
    (root / OWNER).parent.mkdir(parents=True, exist_ok=True)
    (root / OWNER).write_text(owner, encoding="utf-8")
    projections = (shipping, instrumentation if instrumentation is not None else shipping)
    for consumer, projection in zip(CONSUMERS, projections, strict=True):
        (root / consumer).parent.mkdir(parents=True, exist_ok=True)
        (root / consumer).write_text(projection, encoding="utf-8")


def expect_failure(root: Path, fragment: str) -> None:
    try:
        expected_target_sdk(root)
    except AgreementError as error:
        if fragment not in str(error):
            raise AssertionError(f"expected {fragment!r} in {error!r}") from error
        return
    raise AssertionError(f"fixture unexpectedly passed; wanted {fragment!r}")


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="taffy-android-sdk-") as directory:
        root = Path(directory)
        write_fixture(root, 'targetSdk = "37"\n', 'target_sdk_version = "37"\n')
        assert expected_target_sdk(root) == "37"

        write_fixture(root, 'targetSdk = "37"\n', 'target_sdk_version = "36"\n')
        expect_failure(root, "targets SDK 36")

        write_fixture(
            root,
            'targetSdk = "37"\n',
            'target_sdk_version = "37"\n',
            'target_sdk_version = "36"\n',
        )
        expect_failure(root, "test/android/ui/BUILD.gn targets SDK 36")

        write_fixture(root, 'targetSdk = "37"\n', "# no shipping projection\n")
        expect_failure(root, "found 0")

        write_fixture(
            root,
            'targetSdk = "37"\n',
            'target_sdk_version = "37"\ntarget_sdk_version = "37"\n',
        )
        expect_failure(root, "found 2")

        assert target_sdk_from_badging("targetSdkVersion:'37'\n") == "37"
        try:
            target_sdk_from_badging("compileSdkVersion='37'\n")
        except AgreementError:
            pass
        else:
            raise AssertionError("badging without a target SDK unexpectedly passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--aapt2", type=Path)
    args = parser.parse_args()

    try:
        if args.self_test:
            self_test()
            print("android target SDK agreement self-test passed")
            return 0
        expected = expected_target_sdk(args.root.resolve())
        if (args.apk is None) != (args.aapt2 is None):
            raise AgreementError("--apk and --aapt2 must be supplied together")
        if args.apk is not None and args.aapt2 is not None:
            verify_apk(args.apk.resolve(), args.aapt2.resolve(), expected)
            print(f"built APK targetSdk {expected} matches {OWNER}")
        else:
            print(f"shipping and instrumentation GN targetSdk {expected} match {OWNER}")
        return 0
    except AgreementError as error:
        print(f"android target SDK agreement: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
