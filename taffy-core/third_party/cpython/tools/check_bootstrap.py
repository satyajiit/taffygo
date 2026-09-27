#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Drives the frozen bootstrap over an archive, with `sys.path` empty.

`runtime/taffy_stdlib_boot.py` is the module decision 0046 calls the frozen
bootstrap: it makes a memory-mapped zip importable so a sandboxed worker can
have a standard library without having a filesystem. This tool proves it does
that, and proves the refusals that keep it honest.

Every check runs in a child interpreter whose `sys.path` is emptied before the
first import, and which is handed the archive as a descriptor that is closed as
soon as it is mapped. That is the only arrangement in which a pass means what
it says: with a path still set, an import that silently fell through to the
filesystem would look identical to one the bootstrap served.

The host interpreter carries `zlib` and `mmap` as shared extensions, so the
child imports both before it clears the path. A shipping build has them
compiled in -- `build_interpreter.py` refuses a build that does not -- and the
child reports which of the two it got rather than assuming.
"""

from __future__ import annotations

import argparse
import json
import os
import py_compile
import subprocess
import sys
import tempfile
import zipfile

RUNTIME = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "runtime")
BOOTSTRAP = os.path.join(RUNTIME, "taffy_stdlib_boot.py")

#: A package, a module inside it, and a top-level module. Small on purpose: the
#: standard library is what this mechanism carries in production, and a rule
#: that needs the standard library to fire is a rule nobody can run here.
FIXTURE = {
    "taffypkg/__init__.py": "NAME = 'taffypkg'\n",
    "taffypkg/alpha.py": "from taffypkg import NAME\n\ndef greet():\n    return f'{NAME}.alpha'\n",
    "taffylone.py": "VALUE = 41\n\ndef answer():\n    return VALUE + 1\n",
}

CHILD = r'''
import json, mmap, os, sys, zlib

boot_path, archive_path = sys.argv[1], sys.argv[2]
with open(boot_path, "rb") as handle:
    boot_source = handle.read()

fd = os.open(archive_path, os.O_RDONLY)
mapping = mmap.mmap(fd, 0, prot=mmap.PROT_READ)
os.close(fd)

sys.path.clear()
sys.path_importer_cache.clear()

boot = type(sys)("taffy_stdlib_boot")
result = {"compiled_in": sorted(
    name for name in ("zlib", "mmap") if name in sys.builtin_module_names)}
try:
    exec(compile(boot_source, "<frozen taffy_stdlib_boot>", "exec"), boot.__dict__)
    result["members"] = boot.install(mapping)
    import taffypkg.alpha
    import taffylone
    result["greet"] = taffypkg.alpha.greet()
    result["answer"] = taffylone.answer()
    result["origin"] = taffypkg.alpha.__spec__.origin
    result["package"] = taffypkg.__spec__.submodule_search_locations == []
    result["absent"] = None
    try:
        import taffymissing  # noqa: F401
    except ModuleNotFoundError:
        result["absent"] = "refused"
except Exception as error:
    result["error"] = f"{type(error).__name__}: {error}"
result["path"] = sys.path
print(json.dumps(result))
'''


def write_archive(directory: str, byte_code: bool, corrupt_magic: bool = False) -> str:
    """One archive of the fixture, as source or as byte code."""
    staged = os.path.join(directory, "staged")
    for name, text in FIXTURE.items():
        path = os.path.join(staged, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)

    archive = os.path.join(directory, "byte-code.zip" if byte_code else "source.zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as writer:
        for name in FIXTURE:
            source = os.path.join(staged, name)
            if not byte_code:
                writer.write(source, name)
                continue
            compiled = source + "c"
            py_compile.compile(
                source, cfile=compiled, dfile=name, doraise=True,
                invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH,
            )
            payload = open(compiled, "rb").read()
            if corrupt_magic:
                payload = bytes([payload[0] ^ 0xFF]) + payload[1:]
            writer.writestr(name + "c", payload)
    return archive


def drive(archive: str) -> dict:
    """Runs the child and answers what it reported."""
    completed = subprocess.run(
        [sys.executable, "-I", "-S", "-c", CHILD, BOOTSTRAP, archive],
        capture_output=True, text=True, check=False,
    )
    if completed.returncode != 0:
        return {"error": f"child exited {completed.returncode}: {completed.stderr.strip()}"}
    return json.loads(completed.stdout)


def _self_test() -> int:
    checks: list[tuple[str, bool]] = []

    def check(name: str, condition: bool) -> None:
        checks.append((name, condition))

    if not os.path.exists(BOOTSTRAP):
        print(f"FAIL the bootstrap is not at {BOOTSTRAP}")
        return 1

    with tempfile.TemporaryDirectory() as directory:
        for label, byte_code in (("source", False), ("byte code", True)):
            room = os.path.join(directory, label.replace(" ", "-"))
            os.makedirs(room)
            result = drive(write_archive(room, byte_code))
            check(f"{label}: the child ran", "error" not in result)
            check(f"{label}: every member is seen", result.get("members") == len(FIXTURE))
            check(f"{label}: a package member imports", result.get("greet") == "taffypkg.alpha")
            check(f"{label}: a top-level module imports", result.get("answer") == 42)
            check(f"{label}: the origin names the archive",
                  str(result.get("origin", "")).startswith("taffy-archive:"))
            check(f"{label}: a package searches nowhere else", result.get("package") is True)
            check(f"{label}: an absent module is refused", result.get("absent") == "refused")
            check(f"{label}: the path stayed empty", result.get("path") == [])

        room = os.path.join(directory, "wrong-magic")
        os.makedirs(room)
        result = drive(write_archive(room, byte_code=True, corrupt_magic=True))
        check("byte code built for another interpreter is refused",
              "magic" in str(result.get("error", "")))

        room = os.path.join(directory, "not-an-archive")
        os.makedirs(room)
        path = os.path.join(room, "empty.zip")
        with open(path, "wb") as handle:
            handle.write(b"not a zip, and long enough not to be an empty mapping")
        check("a mapping that is not an archive is refused",
              "ArchiveError" in str(drive(path).get("error", "")))

    failed = [name for name, passed in checks if not passed]
    for name, passed in checks:
        print(f"{'ok  ' if passed else 'FAIL'} {name}")
    print(f"{len(checks) - len(failed)}/{len(checks)} rules hold")
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--archive", help="drive the bootstrap over one archive and report")
    parser.add_argument("--self-test", action="store_true",
                        help="drive every rule over synthetic archives, refusals included")
    args = parser.parse_args(argv)
    if args.self_test:
        return _self_test()
    if not args.archive:
        parser.error("required: --archive or --self-test")
    print(json.dumps(drive(args.archive), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
