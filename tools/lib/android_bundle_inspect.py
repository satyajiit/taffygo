#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Structural inspection of a built Android App Bundle.

Play accepts a bundle and not an APK, so `taffy_public_bundle` is the artifact
a store upload carries — and it is the one artifact in this tree that no suite
has ever run. This module asks the questions a zip file can actually answer,
and refuses to ask the ones it cannot.

What it checks, and why each one is here rather than somewhere better:

  * **It is a bundle, not an APK wearing the extension.** An APK carries
    `AndroidManifest.xml` and `classes.dex` at the archive root; a bundle
    carries `BundleConfig.pb` and puts everything under a module directory.
    A packaging step that quietly produced the wrong shape is a file the
    upload would reject hours later, with an error about the manifest.

  * **The base module carries dex, resources and a native library for the ABI
    that was asked for.** A bundle that packaged no `.so` still uploads, still
    installs, and dies at the first `System.loadLibrary`.

  * **Every loadable segment of every shipped `.so` is 16 KiB aligned.**
    Google requires 16 KiB page alignment of new uploads, and the failure is
    silent on the 4 KiB devices this tree has been testing on: the artifact
    runs perfectly on the phone in the drawer and is refused by Play, or
    crashes on a 16 KiB device. It is an ELF program-header property, so it
    is readable here with `struct` and nothing else. `docs/development/
    testing-and-delivery.md` section 9.3 lists this check; this is it.

What it deliberately does not check: anything inside
`base/manifest/AndroidManifest.xml`. That file is aapt2 protobuf, not binary
XML, and a hand-rolled protobuf walker that is wrong reports a target SDK the
artifact does not have — which is worse than no answer. The target SDK is held
by `tools/lib/android_sdk_agreement.py` at the source, and by Play at the
upload; both of those are authorities and this is not.

Exit codes: 0 = every check passed, 1 = a finding, 2 = it could not run.
"""

from __future__ import annotations

import argparse
import io
import os
import struct
import sys
import zipfile

#: Google's requirement for new uploads, in bytes.
REQUIRED_SEGMENT_ALIGNMENT = 16 * 1024

#: How much of a shared object to pull out of the archive. The ELF header is
#: 64 bytes and the program headers follow it almost immediately; a megabyte is
#: several orders of magnitude of headroom and bounds the memory this uses over
#: a 300 MB library.
ELF_HEAD_BYTES = 1 << 20

PT_LOAD = 1

#: Entries that mean "this is an APK", checked before anything else so the
#: report names the real problem instead of a dozen missing bundle paths.
APK_MARKERS = ("AndroidManifest.xml", "classes.dex", "resources.arsc")


class Finding(Exception):
    """A structural defect in the artifact."""


def _abi_dir(abi: str) -> str:
    return f"base/lib/{abi}/"


def elf_load_alignments(blob: bytes) -> list[int]:
    """Return `p_align` for every PT_LOAD segment of a 64-bit little-endian ELF.

    Raises Finding for anything that is not one, because a shipped `.so` that
    is not a 64-bit ELF is a finding in its own right rather than a reason to
    skip the alignment question.
    """
    if len(blob) < 64 or blob[:4] != b"\x7fELF":
        raise Finding("not an ELF file")
    ei_class, ei_data = blob[4], blob[5]
    if ei_class != 2:
        raise Finding("not a 64-bit ELF (ELFCLASS32)")
    if ei_data != 1:
        raise Finding("not little-endian (ELFDATA2MSB)")
    e_phoff, = struct.unpack_from("<Q", blob, 32)
    e_phentsize, e_phnum = struct.unpack_from("<HH", blob, 54)
    if e_phentsize < 56:
        raise Finding(f"program header entries are {e_phentsize} bytes, expected 56")
    end = e_phoff + e_phentsize * e_phnum
    if end > len(blob):
        raise Finding(
            f"program headers end at {end}, past the {len(blob)} bytes read"
        )
    alignments = []
    for index in range(e_phnum):
        offset = e_phoff + index * e_phentsize
        p_type, = struct.unpack_from("<I", blob, offset)
        if p_type != PT_LOAD:
            continue
        p_align, = struct.unpack_from("<Q", blob, offset + 48)
        alignments.append(p_align)
    if not alignments:
        raise Finding("no PT_LOAD segments")
    return alignments


def check_alignment(name: str, blob: bytes, report: list[str]) -> bool:
    try:
        alignments = elf_load_alignments(blob)
    except Finding as finding:
        report.append(f"{name}: {finding}")
        return False
    bad = [value for value in alignments if value < REQUIRED_SEGMENT_ALIGNMENT]
    if bad:
        report.append(
            f"{name}: {len(bad)} of {len(alignments)} loadable segments align to "
            + ", ".join(str(value) for value in sorted(set(bad)))
            + f" bytes, under the required {REQUIRED_SEGMENT_ALIGNMENT}. Play "
            "refuses this and a 16 KiB device cannot map it."
        )
        return False
    return True


def inspect(path: str, abi: str) -> tuple[int, list[str], list[str]]:
    """Return (exit code, findings, facts)."""
    findings: list[str] = []
    facts: list[str] = []
    if not os.path.isfile(path):
        return 2, [f"no bundle at {path}"], facts
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as error:
        return 2, [f"{path} is not readable as a zip archive: {error}"], facts

    with archive:
        names = set(archive.namelist())

        apk_shaped = [marker for marker in APK_MARKERS if marker in names]
        if apk_shaped:
            findings.append(
                f"this is an APK, not a bundle: it carries {', '.join(apk_shaped)} "
                "at the archive root. The build produced the wrong artifact."
            )
            return 1, findings, facts

        for required in ("BundleConfig.pb", "base/manifest/AndroidManifest.xml"):
            if required not in names:
                findings.append(f"missing {required}")

        dex = sorted(n for n in names if n.startswith("base/dex/") and n.endswith(".dex"))
        if not dex:
            findings.append("the base module carries no dex")
        else:
            facts.append(f"dex: {len(dex)} file(s)")

        if not any(n.startswith("base/res/") for n in names) and \
                "base/resources.pb" not in names:
            findings.append("the base module carries no resources")

        libs = sorted(
            n for n in names
            if n.startswith("base/lib/") and n.endswith(".so")
        )
        abis = sorted({n.split("/")[2] for n in libs})
        if not libs:
            findings.append(
                "the base module carries no native library. It would install "
                "and die at the first System.loadLibrary."
            )
        else:
            facts.append(f"native: {len(libs)} library(ies) for {', '.join(abis)}")

        if abi:
            wanted = _abi_dir(abi)
            if not any(n.startswith(wanted) for n in libs):
                findings.append(
                    f"no native library for {abi}; the bundle carries "
                    + (", ".join(abis) if abis else "none")
                )
            unexpected = [value for value in abis if value != abi]
            if unexpected:
                findings.append(
                    f"the bundle carries {', '.join(unexpected)} beside {abi}; "
                    "this profile builds one ABI"
                )

        aligned = 0
        for name in libs:
            with archive.open(name) as member:
                blob = member.read(ELF_HEAD_BYTES)
            if check_alignment(name, blob, findings):
                aligned += 1
        if libs and aligned == len(libs):
            facts.append(
                f"16 KiB alignment: {aligned}/{len(libs)} library(ies) pass"
            )

        total = sum(info.file_size for info in archive.infolist())
        facts.append(
            f"size: {os.path.getsize(path)} bytes on disk, {total} uncompressed"
        )

    return (1 if findings else 0), findings, facts


# --- self-test ---------------------------------------------------------------
#
# Synthesised in memory: a real bundle is a 300 MB build artifact and no host
# that runs the gates has one, so a checker whose only test is a real bundle is
# a checker that is never tested. Every fixture below is built from the same
# writer the checks read, and each asserts a verdict rather than an exit code
# alone, because "it failed" and "it failed for the reason we meant" are
# different claims.


def _elf(alignments: list[int]) -> bytes:
    phnum = len(alignments)
    header = bytearray(64)
    header[0:4] = b"\x7fELF"
    header[4] = 2  # ELFCLASS64
    header[5] = 1  # little-endian
    header[6] = 1  # EV_CURRENT
    struct.pack_into("<H", header, 16, 3)   # e_type = ET_DYN
    struct.pack_into("<H", header, 18, 183)  # e_machine = AArch64
    struct.pack_into("<Q", header, 32, 64)   # e_phoff
    struct.pack_into("<H", header, 54, 56)   # e_phentsize
    struct.pack_into("<H", header, 56, phnum)
    body = bytearray()
    for align in alignments:
        entry = bytearray(56)
        struct.pack_into("<I", entry, 0, PT_LOAD)
        struct.pack_into("<Q", entry, 48, align)
        body += entry
    return bytes(header + body)


def _bundle(members: dict) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name, blob in members.items():
            archive.writestr(name, blob)
    return buffer.getvalue()


def _good_members(alignments=(16384, 16384)) -> dict:
    return {
        "BundleConfig.pb": b"\x00",
        "base/manifest/AndroidManifest.xml": b"\x00",
        "base/dex/classes.dex": b"\x00",
        "base/resources.pb": b"\x00",
        "base/lib/arm64-v8a/libtaffy.so": _elf(list(alignments)),
    }


def self_test() -> int:
    import tempfile

    cases = []

    def case(name, members, abi, expect_code, expect_text):
        cases.append((name, members, abi, expect_code, expect_text))

    case("a well-formed bundle", _good_members(), "arm64-v8a", 0, "")

    apk = {"AndroidManifest.xml": b"\x00", "classes.dex": b"\x00"}
    case("an APK named .aab", apk, "arm64-v8a", 1, "this is an APK")

    no_config = _good_members()
    del no_config["BundleConfig.pb"]
    case("no BundleConfig.pb", no_config, "arm64-v8a", 1, "missing BundleConfig.pb")

    no_lib = _good_members()
    del no_lib["base/lib/arm64-v8a/libtaffy.so"]
    case("no native library", no_lib, "arm64-v8a", 1, "no native library")

    no_dex = _good_members()
    del no_dex["base/dex/classes.dex"]
    case("no dex", no_dex, "arm64-v8a", 1, "carries no dex")

    case("4 KiB alignment", _good_members((4096, 16384)), "arm64-v8a", 1,
         "under the required 16384")

    wrong_abi = _good_members()
    wrong_abi["base/lib/x86_64/libtaffy.so"] = \
        wrong_abi.pop("base/lib/arm64-v8a/libtaffy.so")
    case("the wrong ABI", wrong_abi, "arm64-v8a", 1, "no native library for arm64-v8a")

    not_elf = _good_members()
    not_elf["base/lib/arm64-v8a/libtaffy.so"] = b"not an elf at all"
    case("a shipped file that is not an ELF", not_elf, "arm64-v8a", 1, "not an ELF")

    thirty_two = _good_members()
    header = bytearray(_elf([16384]))
    header[4] = 1  # ELFCLASS32
    thirty_two["base/lib/arm64-v8a/libtaffy.so"] = bytes(header)
    case("a 32-bit ELF", thirty_two, "arm64-v8a", 1, "not a 64-bit ELF")

    failures = 0
    with tempfile.TemporaryDirectory() as directory:
        for name, members, abi, expect_code, expect_text in cases:
            path = os.path.join(directory, "TaffyGo.aab")
            with open(path, "wb") as handle:
                handle.write(_bundle(members))
            code, findings, _ = inspect(path, abi)
            joined = " | ".join(findings)
            if code != expect_code or (expect_text and expect_text not in joined):
                failures += 1
                print(
                    f"  fail {name}: exit {code} (expected {expect_code}); "
                    f"findings: {joined or '(none)'}",
                    file=sys.stderr,
                )
        missing = os.path.join(directory, "absent.aab")
        code, _, _ = inspect(missing, "arm64-v8a")
        if code != 2:
            failures += 1
            print("  fail a missing file must not be a finding", file=sys.stderr)

    if failures:
        print(f"bundle inspection self-test: {failures} case(s) failed", file=sys.stderr)
        return 1
    print(f"bundle inspection self-test: {len(cases) + 1} cases passed")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bundle", help="path to the built .aab")
    parser.add_argument(
        "--abi",
        default="",
        help="the one ABI this profile builds, e.g. arm64-v8a",
    )
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.bundle:
        parser.error("--bundle is required unless --self-test is given")

    code, findings, facts = inspect(args.bundle, args.abi)
    for fact in facts:
        print(f"  {fact}")
    for finding in findings:
        print(f"  {finding}", file=sys.stderr)
    if code == 0:
        print(f"bundle inspection: {os.path.basename(args.bundle)} is well-formed")
    return code


if __name__ == "__main__":
    sys.exit(main())
