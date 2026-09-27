#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Cross-builds the CPython interpreter TaffyGo links into its utility binary.

The interpreter ships inside the installer and the library is fetched, for the
reasons decision 0046 records. This tool builds the half that ships. It answers
the two questions spike SP-07 asks of a build -- what the interpreter costs per
ABI, and whether a 16 KiB page-size Android build links -- and it refuses to
report either as a pass without having measured it.

Three rules make the output shippable rather than merely built:

  * **No shared extension module.** A sandboxed utility process cannot `dlopen`
    anything, so an extension that landed as a `.so` is a module that would be
    missing at run time with no error anyone would see until an import failed.
    Every module is compiled in, and a build that produced even one `.so` is
    refused.
  * **Every loadable segment aligns to 16 KiB.** Android devices with a 16 KiB
    page size refuse to load a binary aligned for 4 KiB. The linker flag that
    sets this is easy to lose, and losing it is invisible on the emulator most
    people test on.
  * **The module set is the one the sandbox permits.** `_ctypes` is arbitrary
    native calls and `_socket` is network; the utility process has neither, so
    the build is configured without them rather than shipping them unused.

It builds nothing on its own toolchain: the compiler, the sysroot and the
linker all come from the Chromium checkout, because that is what will link the
result. It never downloads anything.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

#: The Android ABIs, and the two names each one needs. The first is the GNU
#: triple `configure` wants; the second is the target clang wants, with the API
#: level appended, and it is the product's `minSdk` rather than CPython's own
#: default of 24.
ANDROID_PLATFORMS = {
    "android-arm64": ("aarch64-linux-android", "aarch64-linux-android29"),
    "android-x64": ("x86_64-linux-android", "x86_64-linux-android29"),
}

#: Modules left out, and why. The first two are refusals the sandbox makes for
#: us; the rest are cross-build hygiene, where `configure` found a host header
#: for a library the Android sysroot does not carry and would have produced a
#: link nobody could satisfy.
DISABLED_MODULES = {
    "_ctypes": "arbitrary native calls, which the utility sandbox exists to prevent",
    "_socket": "network, which a compute worker does not have and must not appear to",
    "_remote_debugging": "attaching a debugger to a live interpreter, the opposite of this process",
    "_lzma": "liblzma is not in the Android sysroot; configure found the host's",
    "_zstd": "libzstd is not in the Android sysroot; configure found the host's",
    "_uuid": "libuuid is not in the Android sysroot; configure found the host's",
}

#: Files that must exist for a directory to be a CPython source tree. Cheap,
#: and it turns "you pointed at the wrong directory" into a message.
REQUIRED_SOURCES = ("configure", "Include/Python.h", "Lib/os.py", "Modules/Setup")

#: Android's smallest supported page size is 4 KiB and its largest is 16 KiB. A
#: segment aligned for the smaller one will not load on a device using the
#: larger, so this is the alignment every loadable segment must have or exceed.
REQUIRED_SEGMENT_ALIGNMENT = 16384

_LOAD_SEGMENT = re.compile(r"^\s+LOAD\s+.*?(0x[0-9a-f]+)\s*$", re.MULTILINE)
_NEEDED = re.compile(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]")
_EXTENSION_SUFFIX = ".so"


def platform_toolchain(platform: str) -> tuple[str, str]:
    """The GNU triple and clang target for one platform, or a refusal."""
    if platform not in ANDROID_PLATFORMS:
        known = ", ".join(sorted(ANDROID_PLATFORMS))
        raise ValueError(f"{platform}: not an Android platform this tool builds ({known})")
    return ANDROID_PLATFORMS[platform]


def source_findings(entries: set[str]) -> list[str]:
    """Whether a directory listing is a CPython source tree."""
    return [f"{name}: missing, so this is not a CPython source tree"
            for name in REQUIRED_SOURCES if name not in entries]


def parse_segment_alignments(readelf_segments: str) -> list[int]:
    """The alignment of each loadable segment, in the order they are listed."""
    return [int(value, 16) for value in _LOAD_SEGMENT.findall(readelf_segments)]


def parse_needed_libraries(readelf_dynamic: str) -> list[str]:
    """Every shared library the binary asks the loader for."""
    return _NEEDED.findall(readelf_dynamic)


def alignment_findings(alignments: list[int]) -> list[str]:
    """Whether every loadable segment would load on a 16 KiB page-size device."""
    if not alignments:
        return ["no loadable segment was found, so alignment was never measured"]
    return [
        f"a loadable segment aligns to {value}, and a 16 KiB page-size device "
        f"needs at least {REQUIRED_SEGMENT_ALIGNMENT}"
        for value in alignments
        if value < REQUIRED_SEGMENT_ALIGNMENT
    ]


def shared_extension_findings(module_files: list[str]) -> list[str]:
    """Whether the build produced an extension the sandbox could not load."""
    return [
        f"{name}: built as a shared extension, and a sandboxed process cannot dlopen one"
        for name in sorted(module_files)
        if name.endswith(_EXTENSION_SUFFIX)
    ]


def setup_local(disabled: dict[str, str]) -> str:
    """The `Modules/Setup.local` that states the module set and its reasons."""
    lines = [
        "# TaffyGo: the module set a sandboxed utility worker may have.",
        "#",
        "# Written by taffy-core/third_party/cpython/tools/build_interpreter.py.",
        "# Each name below is followed by the reason it is not here.",
        "#",
    ]
    lines += [f"#   {name}: {reason}" for name, reason in disabled.items()]
    lines += ["", "*disabled*"]
    lines += list(disabled)
    return "\n".join(lines) + "\n"


def toolchain_wrappers(directory: str, llvm: str, sysroot: str, target: str) -> dict[str, str]:
    """Writes the compiler drivers `configure` needs, and answers where they are.

    Chromium's checkout carries a full LLVM and the NDK's sysroot but not the
    NDK's own clang launcher scripts, which are the only thing that pins a
    target and a sysroot. These are those scripts, and nothing else: a build
    that used a different compiler from the one that will link the result would
    be measuring something the product never runs.
    """
    written = {}
    for name, driver in ((f"{target}-clang", "clang"), (f"{target}-clang++", "clang++")):
        path = os.path.join(directory, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(
                "#!/bin/sh\n"
                f'exec "{llvm}/{driver}" --target={target} '
                f'--sysroot="{sysroot}" --unwindlib=none "$@"\n'
            )
        os.chmod(path, 0o755)
        written[name] = path
    ranlib = os.path.join(directory, "llvm-ranlib")
    with open(ranlib, "w", encoding="utf-8") as handle:
        # Chromium ships llvm-ar but not llvm-ranlib; `llvm-ar s` is the same
        # operation and is what Chromium's own build uses.
        handle.write(f'#!/bin/sh\nexec "{llvm}/llvm-ar" s "$@"\n')
    os.chmod(ranlib, 0o755)
    written["llvm-ranlib"] = ranlib
    return written


def build_environment(llvm: str, wrappers: dict[str, str], target: str) -> dict[str, str]:
    """The environment the cross build runs in, and every flag it depends on."""
    environment = dict(os.environ)
    environment.update(
        CC=wrappers[f"{target}-clang"],
        CXX=wrappers[f"{target}-clang++"],
        AR=f"{llvm}/llvm-ar",
        RANLIB=wrappers["llvm-ranlib"],
        NM=f"{llvm}/llvm-nm",
        READELF=f"{llvm}/llvm-readelf",
        STRIP=f"{llvm}/llvm-strip",
        LD=f"{llvm}/ld.lld",
        # Bionic's page size is a run-time value on a 16 KiB device, so the
        # macro that hard-codes it must not be visible.
        CFLAGS="-D__BIONIC_NO_PAGE_SIZE_MACRO -fPIC",
        LDFLAGS=(
            "-Wl,--build-id=sha1 -Wl,--no-rosegment "
            f"-Wl,-z,max-page-size={REQUIRED_SEGMENT_ALIGNMENT} -lm"
        ),
        # Without this every extension is a shared object, which is the one
        # shape a sandboxed process cannot load.
        MODULE_BUILDTYPE="static",
    )
    return environment


def run(command: list[str], cwd: str, environment: dict[str, str], log: str) -> None:
    with open(log, "w", encoding="utf-8") as handle:
        completed = subprocess.run(
            command, cwd=cwd, env=environment, stdout=handle, stderr=subprocess.STDOUT,
            check=False,
        )
    if completed.returncode != 0:
        with open(log, encoding="utf-8") as handle:
            tail = "".join(handle.readlines()[-25:])
        raise SystemExit(f"{command[0]} failed; last lines of {log}:\n{tail}")


def build(args: argparse.Namespace) -> dict[str, object]:
    """Runs the build and answers what it measured. Never answers otherwise."""
    triple, target = platform_toolchain(args.platform)
    source = os.path.abspath(args.source)
    findings = source_findings({
        name for name in REQUIRED_SOURCES
        if os.path.exists(os.path.join(source, name))
    })
    if findings:
        raise SystemExit("\n".join(findings))

    llvm = os.path.join(args.chromium, "third_party/llvm-build/Release+Asserts/bin")
    sysroot = os.path.join(
        args.chromium,
        "third_party/android_toolchain/ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot",
    )
    for path in (llvm, sysroot, args.build_python):
        if not os.path.exists(path):
            raise SystemExit(f"{path}: not found; the Chromium checkout is where the toolchain is")

    output = os.path.abspath(args.output)
    shutil.rmtree(output, ignore_errors=True)
    os.makedirs(os.path.join(output, "toolchain"), exist_ok=True)
    wrappers = toolchain_wrappers(os.path.join(output, "toolchain"), llvm, sysroot, target)
    environment = build_environment(llvm, wrappers, target)

    run(
        [
            os.path.join(source, "configure"),
            f"--host={triple}",
            f"--build={args.build_triple}",
            f"--with-build-python={args.build_python}",
            "--without-ensurepip",
            # The worker installs a counting allocator before initialisation.
            # Mimalloc bypasses that allocator for some object arenas, which
            # would make the memory budget a suggestion rather than a bound.
            "--without-mimalloc",
            "--without-remote-debug",
            "--disable-test-modules",
            "--disable-ipv6",
            f"--prefix={output}/prefix",
        ],
        output, environment, os.path.join(output, "configure.log"),
    )
    with open(os.path.join(output, "Modules/Setup.local"), "w", encoding="utf-8") as handle:
        handle.write(setup_local(DISABLED_MODULES))
    run(["make", "-j", str(os.cpu_count() or 1)], output, environment,
        os.path.join(output, "make.log"))

    modules = os.path.join(output, "Modules")
    findings = shared_extension_findings(os.listdir(modules) if os.path.isdir(modules) else [])

    interpreter = os.path.join(output, "python")
    stripped = os.path.join(output, "python.stripped")
    shutil.copyfile(interpreter, stripped)
    subprocess.run([f"{llvm}/llvm-strip", "--strip-all", stripped], check=True)

    readelf = f"{llvm}/llvm-readelf"
    segments = subprocess.run([readelf, "-lW", stripped], check=True,
                              capture_output=True, text=True).stdout
    dynamic = subprocess.run([readelf, "-dW", stripped], check=True,
                             capture_output=True, text=True).stdout
    alignments = parse_segment_alignments(segments)
    findings += alignment_findings(alignments)
    if findings:
        raise SystemExit("\n".join(findings))

    return {
        "platform": args.platform,
        "triple": triple,
        "clang_target": target,
        "interpreter_bytes": os.path.getsize(stripped),
        "archive_bytes": os.path.getsize(os.path.join(output, "libpython3.14.a")),
        "segment_alignments": alignments,
        "needed_libraries": parse_needed_libraries(dynamic),
        "disabled_modules": sorted(DISABLED_MODULES),
        "shared_extensions": 0,
    }


def _self_test() -> int:
    """Drives every rule over synthetic inputs, refusals included."""
    checks: list[tuple[str, bool]] = []

    def check(name: str, condition: bool) -> None:
        checks.append((name, condition))

    try:
        platform_toolchain("android-arm64")
        check("a known platform resolves", True)
    except ValueError:
        check("a known platform resolves", False)
    try:
        platform_toolchain("linux-x64")
        check("an unknown platform is refused", False)
    except ValueError:
        check("an unknown platform is refused", True)

    check("a complete source tree passes", not source_findings(set(REQUIRED_SOURCES)))
    check("a tree with no configure is refused",
          bool(source_findings(set(REQUIRED_SOURCES) - {"configure"})))

    aligned = "  LOAD  0x000000 0x0 0x0 0x6d3 0x6d3 R E 0x4000\n"
    small = "  LOAD  0x000000 0x0 0x0 0x6d3 0x6d3 R E 0x1000\n"
    check("16 KiB alignment is read from readelf",
          parse_segment_alignments(aligned + aligned) == [16384, 16384])
    check("16 KiB alignment passes", not alignment_findings(parse_segment_alignments(aligned)))
    check("4 KiB alignment is refused", bool(alignment_findings(parse_segment_alignments(small))))
    check("no segment at all is refused", bool(alignment_findings([])))

    check("a needed library is read",
          parse_needed_libraries(
              "  0x0000000000000001 (NEEDED)  Shared library: [libc.so]\n") == ["libc.so"])

    check("an all-static Modules directory passes",
          not shared_extension_findings(["_json.o", "config.c", "Setup.local"]))
    check("a shared extension is refused",
          bool(shared_extension_findings(["_json.cpython-314-aarch64-linux-android.so"])))

    local = setup_local(DISABLED_MODULES)
    check("the disabled set states a reason for every name",
          all(reason in local for reason in DISABLED_MODULES.values()))
    check("the disabled set is tagged for makesetup", "*disabled*" in local)
    check("_ctypes is refused", "\n_ctypes\n" in local)
    check("_socket is refused", "\n_socket\n" in local)

    with tempfile.TemporaryDirectory() as directory:
        wrappers = toolchain_wrappers(directory, "/llvm", "/sysroot", "aarch64-linux-android29")
        with open(wrappers["aarch64-linux-android29-clang"], encoding="utf-8") as handle:
            driver = handle.read()
        check("the driver pins the target", "--target=aarch64-linux-android29" in driver)
        check("the driver pins the sysroot", '--sysroot="/sysroot"' in driver)
        check("the driver is executable", os.access(wrappers["llvm-ranlib"], os.X_OK))
        environment = build_environment("/llvm", wrappers, "aarch64-linux-android29")
        check("extensions are compiled in", environment["MODULE_BUILDTYPE"] == "static")
        check("the linker is told the page size",
              "-Wl,-z,max-page-size=16384" in environment["LDFLAGS"])
        check("bionic's page-size macro is suppressed",
              "-D__BIONIC_NO_PAGE_SIZE_MACRO" in environment["CFLAGS"])

    failed = [name for name, passed in checks if not passed]
    for name, passed in checks:
        print(f"{'ok  ' if passed else 'FAIL'} {name}")
    print(f"{len(checks) - len(failed)}/{len(checks)} rules hold")
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--platform", help=f"one of: {', '.join(sorted(ANDROID_PLATFORMS))}")
    parser.add_argument("--source", help="a CPython source tree at the pinned version")
    parser.add_argument("--chromium", help="the Chromium checkout whose toolchain will link this")
    parser.add_argument("--build-python", help="a CPython of the same version, built for this host")
    parser.add_argument("--build-triple", default="x86_64-pc-linux-gnu",
                        help="the GNU triple of this host")
    parser.add_argument("--output", help="where to build")
    parser.add_argument("--self-test", action="store_true",
                        help="drive every rule over synthetic inputs, refusals included")
    args = parser.parse_args(argv)

    if args.self_test:
        return _self_test()
    missing = [name for name in ("platform", "source", "chromium", "build_python", "output")
               if not getattr(args, name)]
    if missing:
        parser.error("required: " + ", ".join(f"--{name.replace('_', '-')}" for name in missing))
    print(json.dumps(build(args), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
