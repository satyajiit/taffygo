#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The release rules in facts.py, held against recorded tool output.

This is the half of `./tools/github-release self-test` that needs no Android
SDK. The output below is written in the shapes the tools print, with invented
digests and names: a real certificate's owner line names a person, and a
fixture has no reason to carry one. Each case asserts the sentence a refusal
gives, not only that it refused, because "it failed" and "it failed for the
reason we meant" are different claims.

Exit codes: 0 = every case passed, 1 = at least one did not.
"""

from __future__ import annotations

import os
import sys
from dataclasses import replace

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "lib"))

import facts  # noqa: E402  (siblings, found through the lines above)
from docs_lint import BANNED_RULES  # noqa: E402

KEY = "ab" * 32
OTHER = "cd" * 32
CHAIN = "ef" * 32
FLOOR = 797704204
CHROMIUM = "152.0.7977.42"


def keytool_cert(digest: str) -> str:
    return (
        "Owner: CN=fixture\nIssuer: CN=fixture\nSerial number: 1\n"
        "Certificate fingerprints:\n"
        f"\t SHA1: {facts.colon_form('11' * 20)}\n"
        f"\t SHA256: {facts.colon_form(digest)}\n"
        "Signature algorithm name: SHA256withECDSA\n"
    )


APKSIGNER_BY_SCHEME = (
    "V2 Signer: certificate DN: CN=fixture\n"
    f"V2 Signer: certificate SHA-256 digest: {KEY}\n"
    f"V2 Signer: certificate SHA-1 digest: {'11' * 20}\n"
)
APKSIGNER_NUMBERED = (
    "Signer #1 certificate DN: CN=fixture\n"
    f"Signer #1 certificate SHA-256 digest: {KEY}\n"
    f"Signer #1 public key SHA-256 digest: {OTHER}\n"
)
APKSIGNER_ROTATION = (
    f"Signer (minSdkVersion=33, maxSdkVersion=2147483647) certificate SHA-256 digest: {OTHER}\n"
)
APKSIGNER_STAMPED = (
    APKSIGNER_BY_SCHEME + f"Source Stamp Signer: certificate SHA-256 digest: {OTHER}\n"
)
JAR_ONE = f"Signer #1:\n\nCertificate #1:\n{keytool_cert(KEY)}"
JAR_TWO = (
    f"Signer #1:\n\nCertificate #1:\n{keytool_cert(KEY)}\n"
    f"Signer #2:\n\nCertificate #1:\n{keytool_cert(OTHER)}\nCertificate #2:\n{keytool_cert(CHAIN)}"
)
JAR_UNSIGNED = "Not a signed jar file\n"
KEY_LIST = (
    "Alias name: fixture\nEntry type: PrivateKeyEntry\nCertificate chain length: 2\n"
    f"Certificate[1]:\n{keytool_cert(KEY)}\nCertificate[2]:\n{keytool_cert(CHAIN)}"
)
BADGING = (
    f"package: name='com.taffygo.browser' versionCode='{FLOOR + 1}' "
    f"versionName='{CHROMIUM}' platformBuildVersionName='17' compileSdkVersion='37'\n"
    "minSdkVersion:'29'\ntargetSdkVersion:'37'\napplication-label:'TaffyGo'\n"
    "native-code: 'arm64-v8a'\n"
)
BUNDLE_XML = (
    '<manifest xmlns:android="http://schemas.android.com/apk/res/android" '
    f'android:versionCode="{FLOOR + 1}" android:versionName="{CHROMIUM}" '
    'package="com.taffygo.browser">\n  <uses-sdk android:minSdkVersion="29"/>\n</manifest>\n'
)
GOOD_LOG = "==> gn gen out/release-arm64 (profile release-arm64)\nok built\nBUILD_EXIT=0\n"


def policy(**changes) -> facts.Policy:
    base = facts.Policy(key=KEY, floor=FLOOR, minimum=0, package="com.taffygo.browser",
                        chromium=CHROMIUM, abi="arm64-v8a")
    return replace(base, **changes)


def apk(**changes) -> facts.Artifact:
    base = facts.Artifact("APK", "/fixture/TaffyGo.apk", verified=True, signers=[KEY],
                          manifest=facts.badging_manifest(BADGING))
    return replace(base, **changes)


def bundle(**changes) -> facts.Artifact:
    base = facts.Artifact("bundle", "/fixture/TaffyGo.signed.aab", verified=True,
                          signers=[KEY], manifest=facts.bundle_manifest(BUNDLE_XML))
    return replace(base, **changes)


def failures(checks: list[facts.Check]) -> str:
    return " | ".join(message for passed, message in checks if not passed)


CASES = []


def case(function):
    CASES.append(function)
    return function


def expect(condition: bool, detail: str) -> None:
    if not condition:
        raise AssertionError(detail)


def refused_for(checks: list[facts.Check], text: str) -> None:
    found = failures(checks)
    expect(text in found, f"expected a refusal containing {text!r}; got {found or '(none)'}")


@case
def apksigner_shapes():
    expect(facts.apk_signers(APKSIGNER_BY_SCHEME) == [KEY], "scheme-labelled signer line")
    expect(facts.apk_signers(APKSIGNER_NUMBERED) == [KEY], "numbered signer, public key ignored")
    expect(facts.apk_signers(APKSIGNER_ROTATION) == [OTHER], "rotation-aware signer line")
    expect(facts.apk_signers(APKSIGNER_STAMPED) == [KEY], "a source stamp is not an app signer")
    expect(facts.apk_signers("DOES NOT VERIFY\n") == [], "no signer lines")


@case
def keytool_shapes():
    expect(facts.jar_signers(JAR_ONE) == [KEY], "one signer")
    expect(facts.jar_signers(JAR_TWO) == [KEY, OTHER], "two signers, leaf of each only")
    expect(facts.jar_signers(JAR_UNSIGNED) == [], "an unsigned file has no signer")
    expect(facts.key_certificate(KEY_LIST) == KEY, "the key's own certificate is the chain's first")
    expect(facts.key_certificate("SHA256: not-a-digest\n") is None, "a malformed fingerprint")


@case
def manifest_shapes():
    read = facts.badging_manifest(BADGING)
    expect((read.package, read.code, read.name, read.abis)
           == ("com.taffygo.browser", FLOOR + 1, CHROMIUM, ("arm64-v8a",)), f"badging: {read}")
    expect(facts.badging_manifest("ERROR: dump failed\n") == facts.Manifest(), "no package line")
    read = facts.bundle_manifest(BUNDLE_XML)
    expect((read.package, read.code, read.name) == ("com.taffygo.browser", FLOOR + 1, CHROMIUM),
           f"bundle manifest: {read}")
    expect(facts.bundle_manifest("[BT:1.18] Error: not a bundle") == facts.Manifest(), "not XML")


@case
def titles_and_digests():
    expect(facts.release_title("1.0.0", CHROMIUM) == f"TaffyGo 1.0 (Chromium {CHROMIUM})", "1.0.0")
    expect(facts.release_title("1.0.1", CHROMIUM) == f"TaffyGo 1.0.1 (Chromium {CHROMIUM})", "1.0.1")
    for wrong in ("1.0", "01.0.0", "1.0.0-rc1", "v1.0.0"):
        try:
            facts.release_title(wrong, CHROMIUM)
        except ValueError:
            continue
        raise AssertionError(f"{wrong!r} was accepted as a release version")
    expect(facts.colon_form(KEY).startswith("AB:AB:") and len(facts.colon_form(KEY)) == 95,
           "colon form")
    expect(facts.normalize_digest(facts.colon_form(KEY)) == KEY, "the two spellings round-trip")


@case
def a_good_pair_passes():
    found = failures(facts.judge(apk(), bundle(), policy()))
    expect(not found, f"a good pair was refused: {found}")


@case
def signature_refusals():
    refused_for(facts.judge(apk(), bundle(), policy(key=None)), "certificate is unknown")
    refused_for(facts.judge(apk(signers=[OTHER]), bundle(), policy()),
                f"the APK is signed by {facts.colon_form(OTHER)}")
    refused_for(facts.judge(apk(signers=[KEY, OTHER]), bundle(), policy()), "the APK is signed by")
    refused_for(facts.judge(apk(), bundle(signers=[]), policy()),
                "the bundle carries no signing certificate")
    refused_for(facts.judge(apk(verified=False), bundle(), policy()),
                "the APK's signature does not verify")


@case
def version_refusals():
    at_floor = facts.Manifest("com.taffygo.browser", FLOOR, CHROMIUM, ("arm64-v8a",))
    refused_for(facts.judge(apk(manifest=at_floor), bundle(), policy()),
                f"version code {FLOOR} is not above {FLOOR}")
    refused_for(facts.judge(apk(), bundle(), policy(minimum=FLOOR + 5)),
                "is not above --min-version-code")
    ahead = facts.Manifest("com.taffygo.browser", FLOOR + 2, CHROMIUM, ("arm64-v8a",))
    refused_for(facts.judge(apk(manifest=ahead), bundle(), policy()), "not from one build")
    renamed = facts.Manifest("com.taffygo.browser", FLOOR + 1, "1.2.3.4", ("arm64-v8a",))
    refused_for(facts.judge(apk(manifest=renamed), bundle(), policy()), "not the pinned Chromium")
    other = facts.Manifest("org.chromium.chrome", FLOOR + 1, CHROMIUM, ("arm64-v8a",))
    refused_for(facts.judge(apk(manifest=other), bundle(), policy()), "not com.taffygo.browser")
    no_native = facts.Manifest("com.taffygo.browser", FLOOR + 1, CHROMIUM, ())
    refused_for(facts.judge(apk(manifest=no_native), bundle(), policy()), "no ABI at all")
    refused_for(facts.judge(apk(present=False), bundle(), policy()), "no APK at /fixture/")


@case
def build_logs():
    found = failures(facts.judge_build_log(GOOD_LOG, 100.0, [("APK", 90.0)], "release-arm64"))
    expect(not found, f"a good log was refused: {found}")
    refused_for(facts.judge_build_log("FAILED: obj/x.o\n" + GOOD_LOG, 100.0, [], "release-arm64"),
                "1 FAILED: line(s)")
    refused_for(facts.judge_build_log(GOOD_LOG.replace("=0", "=1"), 100.0, [], "release-arm64"),
                "does not end with BUILD_EXIT=0")
    refused_for(facts.judge_build_log(GOOD_LOG + "more output\n", 100.0, [], "release-arm64"),
                "does not end with BUILD_EXIT=0")
    refused_for(facts.judge_build_log(GOOD_LOG, 100.0, [], "dev-arm64"), "never names the dev-arm64")
    refused_for(facts.judge_build_log(GOOD_LOG, 100.0, [("bundle", 200.0)], "release-arm64"),
                "the bundle was written after the build log")
    refused_for(facts.judge_build_log("", 100.0, [], "release-arm64"), "the log is empty")


@case
def what_is_written():
    expect(facts.sums_line("00" * 32, "TaffyGo-1.0.0-arm64.apk")
           == f"{'00' * 32}  TaffyGo-1.0.0-arm64.apk\n", "sha256sum's two-space format")
    section = facts.verify_section(apk_file="TaffyGo-1.0.0-arm64.apk", apk_sha="12" * 32, key=KEY,
                                   manifest=facts.badging_manifest(BADGING),
                                   abi="arm64-v8a")
    for needed in (KEY, facts.colon_form(KEY), "12" * 32, "sha256sum -c SHA256SUMS",
                   "apksigner verify --print-certs TaffyGo-1.0.0-arm64.apk", str(FLOOR + 1)):
        expect(needed in section, f"the Verify section does not carry {needed!r}")
    for rule, pattern, _ in BANNED_RULES:
        hit = pattern.search(section)
        expect(hit is None, f"the Verify section breaks the {rule} rule: {hit and hit.group(0)!r}")


def main() -> int:
    failed = 0
    for function in CASES:
        try:
            function()
        except AssertionError as error:
            failed += 1
            print(f"  fail {function.__name__}: {error}", file=sys.stderr)
    if failed:
        print(f"release rules self-test: {failed} of {len(CASES)} case(s) failed", file=sys.stderr)
        return 1
    print(f"release rules self-test: {len(CASES)} cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
