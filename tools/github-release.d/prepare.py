#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Run the tools a GitHub release depends on, and write the release files.

Called by ./tools/github-release, which resolves the paths, the tools and the
signing key; it is not meant to be run by hand. Two sub-commands:

  prepare         run apksigner, aapt2, keytool, jarsigner and bundletool over
                  the APK and the signed bundle, judge what they print with
                  facts.py, and write the release files only when every check
                  passed. Every check runs before the answer, so one refusal
                  names all of them;
  check-prepared  re-read what prepare wrote, refuse if any of it changed, and
                  print the release title and the APK's file name.

The key's password arrives in the environment as TAFFY_RELEASE_KEY_PASSWORD
and reaches keytool as `-storepass:env`. It is never an argument and never
written, only keytool's own process is given it, and it is scrubbed from any
tool output this prints.

Exit codes: 0 = succeeded, 1 = a refusal, 2 = it could not run.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import tomllib
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import facts  # noqa: E402  (a sibling module, found through the line above)

PASSWORD_ENV = "TAFFY_RELEASE_KEY_PASSWORD"
PROFILE = "release-arm64"
ABI = "arm64-v8a"
TOOL_TIMEOUT = 900


class CannotRun(Exception):
    """A tool or an input this command depends on is not usable."""


def step(message: str) -> None:
    print(f"==> {message}", file=sys.stderr)


def ok(message: str) -> None:
    print(f"  ok   {message}", file=sys.stderr)


def bad(message: str) -> None:
    print(f"  fail {message}", file=sys.stderr)


def warn(message: str) -> None:
    print(f"  warn {message}", file=sys.stderr)


def note(message: str) -> None:
    print(f"       {message}", file=sys.stderr)


def report(checks: list[facts.Check]) -> int:
    failures = 0
    for passed, message in checks:
        if passed:
            ok(message)
        else:
            bad(message)
            failures += 1
    return failures


def scrub(text: str) -> str:
    secret = os.environ.get(PASSWORD_ENV, "")
    return text.replace(secret, "<redacted>") if secret else text


def run(argv: list, *, with_password: bool = False) -> tuple[int, str, str]:
    env = dict(os.environ)
    if not with_password:
        env.pop(PASSWORD_ENV, None)
    words = [str(word) for word in argv]
    try:
        done = subprocess.run(
            words, capture_output=True, text=True, timeout=TOOL_TIMEOUT, env=env,
            stdin=subprocess.DEVNULL, check=False,
        )
    except OSError as error:
        raise CannotRun(f"{words[0]} could not be started: {error}") from error
    except subprocess.TimeoutExpired as error:
        raise CannotRun(f"{Path(words[0]).name} did not finish in {TOOL_TIMEOUT} s") from error
    return done.returncode, done.stdout, done.stderr


def first_error(text: str) -> str:
    lines = [
        line.strip() for line in scrub(text).splitlines()
        if line.strip() and not line.startswith("Picked up JAVA_TOOL_OPTIONS")
    ]
    return lines[0] if lines else "(no message)"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def write_text(path: Path, text: str) -> None:
    partial = path.with_name(path.name + ".partial")
    partial.write_text(text, encoding="utf-8")
    os.replace(partial, path)


def inside(path: Path, root: Path) -> bool:
    real, base = Path(os.path.realpath(path)), Path(os.path.realpath(root))
    return real == base or base in real.parents


# --- what the repository records ----------------------------------------------


def read_floor(root: Path) -> int:
    path = root / "tools" / "github-release.d" / "version-floor"
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise CannotRun(f"cannot read {path}: {error}") from error
    for line in lines:
        key, separator, value = line.partition("=")
        if separator and key.strip() == "play_accepted_version_code" and value.strip().isdigit():
            return int(value.strip())
    raise CannotRun(f"{path} records no play_accepted_version_code")


def read_package(root: Path) -> str:
    try:
        with (root / "tools" / "play.d" / "store.toml").open("rb") as handle:
            return tomllib.load(handle)["application"]["package"]
    except (OSError, KeyError, tomllib.TOMLDecodeError) as error:
        raise CannotRun(f"tools/play.d/store.toml names no application package: {error}") from error


def read_chromium(root: Path) -> str:
    try:
        lines = (root / "chromium" / "REVISION").read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise CannotRun(f"cannot read chromium/REVISION: {error}") from error
    for line in lines:
        if line.startswith("tag="):
            return line.partition("=")[2].strip()
    raise CannotRun("chromium/REVISION records no tag")


# --- reading the artifacts ----------------------------------------------------


def key_certificate(args: argparse.Namespace) -> str | None:
    if args.no_key:
        note(f"no signing identity: none in the environment and none at {args.key_source}")
        return None
    code, out, err = run(
        [args.keytool, "-list", "-v", "-keystore", args.key_store, "-alias", args.key_alias,
         "-storepass:env", PASSWORD_ENV],
        with_password=True,
    )
    digest = facts.key_certificate(out) if code == 0 else None
    if digest is None:
        note(f"keytool could not read the key {args.key_alias!r} named by {args.key_source}: "
             f"{first_error(err or out)}")
        return None
    ok(f"{args.key_alias} from {args.key_source}: certificate SHA-256 {facts.colon_form(digest)}")
    return digest


def inspect_apk(path: Path, apksigner: str, aapt2: str) -> facts.Artifact:
    artifact = facts.Artifact("APK", str(path), present=path.is_file())
    if not artifact.present:
        return artifact
    code, out, err = run([apksigner, "verify", "--print-certs", path])
    artifact.verified = code == 0
    if code != 0:
        note(f"apksigner: {first_error(err or out)}")
    artifact.signers = facts.apk_signers(out)
    code, out, err = run([aapt2, "dump", "badging", path])
    if code != 0:
        note(f"aapt2: {first_error(err)}")
    artifact.manifest = facts.badging_manifest(out)
    return artifact


def inspect_bundle(path: Path, keytool: str, jarsigner: str, bundletool: str) -> facts.Artifact:
    artifact = facts.Artifact("bundle", str(path), present=path.is_file())
    if not artifact.present:
        return artifact
    code, out, _ = run([jarsigner, "-verify", path])
    artifact.verified = code == 0 and "jar verified." in out
    _, out, _ = run([keytool, "-printcert", "-jarfile", path])
    artifact.signers = facts.jar_signers(out)
    code, out, err = run([bundletool, "dump", "manifest", "--bundle", path])
    if code != 0:
        note(f"bundletool: {first_error(err)}")
    artifact.manifest = facts.bundle_manifest(out)
    return artifact


# --- prepare --------------------------------------------------------------------


def prepare(args: argparse.Namespace) -> int:
    root, out = Path(args.root), Path(args.out)
    apk_path, aab_path = Path(args.apk), Path(args.aab)
    if inside(out, root):
        bad(f"{out} is inside this repository, where an APK is one `git add` from a commit")
        note("Pass --out a directory outside it.")
        return 1
    policy = facts.Policy(
        key=None, floor=read_floor(root), minimum=args.min_version_code,
        package=read_package(root), chromium=read_chromium(root), abi=ABI,
    )
    failures = 0

    step("release notes")
    notes = Path(args.notes)
    body = notes.read_text(encoding="utf-8") if notes.is_file() else ""
    failures += report([(True, f"release notes at {notes}")] if body.strip() else
                       [(False, f"no release notes at {notes}; write them first")])

    step("build log")
    if args.build_log:
        log = Path(args.build_log)
        if log.is_file():
            written = [(label, path.stat().st_mtime)
                       for label, path in (("APK", apk_path), ("bundle", aab_path)) if path.is_file()]
            failures += report(facts.judge_build_log(
                log.read_text(encoding="utf-8", errors="replace"), log.stat().st_mtime,
                written, PROFILE))
        else:
            failures += report([(False, f"no build log at {log}")])
    else:
        warn("no --build-log, so nothing here shows these files came from a build that finished")

    step("the configured key")
    policy.key = key_certificate(args)

    step("reading the APK and the bundle")
    apk = inspect_apk(apk_path, args.apksigner, args.aapt2)
    bundle = inspect_bundle(aab_path, args.keytool, args.jarsigner, args.bundletool)

    step(f"the rules: signed by that key, version code above {policy.floor}, "
         f"Chromium {policy.chromium}")
    failures += report(facts.judge(apk, bundle, policy))

    if failures:
        bad(f"refused: {failures} check(s) failed, and nothing was written to {out}")
        return 1
    write_release(args, policy, apk, body)
    return 0


def write_release(args: argparse.Namespace, policy: facts.Policy, apk: facts.Artifact,
                  body: str) -> None:
    out, apk_path, aab_path = Path(args.out), Path(args.apk), Path(args.aab)
    step(f"writing {out}")
    out.mkdir(parents=True, exist_ok=True)
    apk_sha = sha256_file(apk_path)
    partial = out / (args.apk_file + ".partial")
    shutil.copyfile(apk_path, partial)
    if sha256_file(partial) != apk_sha:
        partial.unlink()
        raise CannotRun(f"the copy of {apk_path} in {out} differs from it; is the disk full?")
    os.replace(partial, out / args.apk_file)
    key = policy.key or ""
    write_text(out / "SHA256SUMS", facts.sums_line(apk_sha, args.apk_file))
    write_text(out / "release-notes.md", body.rstrip() + "\n\n" + facts.verify_section(
        apk_file=args.apk_file, apk_sha=apk_sha, key=key, manifest=apk.manifest,
        abi=policy.abi))
    receipt = {
        "version": args.version,
        "tag": f"v{args.version}",
        "title": facts.release_title(args.version, policy.chromium),
        "apk": {"file": args.apk_file, "sha256": apk_sha,
                "size": (out / args.apk_file).stat().st_size, "source": str(apk_path)},
        "bundle": {"source": str(aab_path), "sha256": sha256_file(aab_path)},
        "certificate_sha256": key,
        "version_code": apk.manifest.code,
        "version_name": apk.manifest.name,
        "chromium": policy.chromium,
        "package": policy.package,
        "abi": policy.abi,
        "prepared_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    write_text(out / "receipt.json", json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    for name in (args.apk_file, "SHA256SUMS", "release-notes.md", "receipt.json"):
        ok(f"wrote {out / name}")
    note("receipt.json is for draft and is never uploaded. The bundle is not copied: it is for Play.")
    note(f"Next: read release-notes.md, then ./tools/github-release draft --version {args.version} "
         "--repo <owner>/<name>")


# --- check-prepared -------------------------------------------------------------


def prepared_problems(out: Path, version: str, receipt: dict) -> list[str]:
    problems = []
    if receipt.get("version") != version:
        problems.append(f"{out} was prepared for {receipt.get('version')}, not {version}")
    apk_file, apk_sha = receipt["apk"]["file"], receipt["apk"]["sha256"]
    apk = out / apk_file
    if not apk.is_file():
        problems.append(f"{apk} is gone")
    elif sha256_file(apk) != apk_sha:
        problems.append(f"{apk_file} no longer matches the SHA-256 prepare recorded")
    sums = out / "SHA256SUMS"
    if not sums.is_file() or sums.read_text(encoding="utf-8") != facts.sums_line(apk_sha, apk_file):
        problems.append("SHA256SUMS is not the line prepare wrote")
    notes = out / "release-notes.md"
    if not notes.is_file() or apk_sha not in notes.read_text(encoding="utf-8"):
        problems.append("release-notes.md does not carry the APK's SHA-256")
    return problems


def check_prepared(args: argparse.Namespace) -> int:
    out = Path(args.out)
    try:
        receipt = json.loads((out / "receipt.json").read_text(encoding="utf-8"))
        problems = prepared_problems(out, args.version, receipt)
        title, apk_file = receipt["title"], receipt["apk"]["file"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        bad(f"nothing usable is prepared in {out}: {error}")
        note(f"Run: ./tools/github-release prepare --version {args.version}")
        return 1
    if problems:
        for problem in problems:
            bad(problem)
        note(f"Run prepare again: ./tools/github-release prepare --version {args.version}")
        return 1
    ok(f"{apk_file}, SHA256SUMS and release-notes.md are what prepare wrote")
    print(title)
    print(apk_file)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    each = commands.add_parser("prepare")
    for name in ("root", "version", "apk", "aab", "apk-file", "notes", "out",
                 "apksigner", "aapt2", "keytool", "jarsigner", "bundletool"):
        each.add_argument(f"--{name}", required=True)
    each.add_argument("--build-log")
    each.add_argument("--min-version-code", type=int, default=0)
    each.add_argument("--key-store")
    each.add_argument("--key-alias")
    each.add_argument("--key-source", default="")
    each.add_argument("--no-key", action="store_true")
    check = commands.add_parser("check-prepared")
    check.add_argument("--out", required=True)
    check.add_argument("--version", required=True)
    args = parser.parse_args(argv)
    if args.command == "prepare" and not args.no_key and not (args.key_store and args.key_alias):
        parser.error("prepare needs --key-store and --key-alias, or --no-key")
    try:
        return prepare(args) if args.command == "prepare" else check_prepared(args)
    except CannotRun as error:
        print(f"error: {scrub(str(error))}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
