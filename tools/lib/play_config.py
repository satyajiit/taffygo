#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The static half of Play publishing: everything checkable with no network.

`tools/play.d/store.toml` states the listing and the track ladder. This module
is the one interpretation of that file, and the `play` lane of
./tools/check fast runs it on every host — no credential, no Google, no
Chromium checkout. ./tools/play reads the same file through the same loader,
so the gate and the upload cannot disagree about what the listing says.

Four claims are worth naming, because each one is a defect this closes rather
than a box being ticked:

  * **The package name has one owner.** taffy_branding.gni's own header says a
    second file spelling `com.taffygo.browser` will be wrong after the first
    rename. A store config is exactly such a second file, so it is checked
    against the owner instead of trusted.

  * **Store copy is user-facing text.** docs/voice-and-naming.md section 4
    binds it, and until now nothing applied it outside Markdown: the listing
    is the most widely read text this product will publish and it was the one
    surface with no vocabulary gate. The rules are imported from
    `docs_lint.BANNED_RULES`, not copied — a fourth rule added there applies
    here the same day.

  * **Play's field limits are refusals, not warnings.** A 31-character title
    is rejected at upload, after the artifact has gone up.

  * **No credential may enter this file.** The `secrets` lane scans for a few
    high-confidence shapes; this looks at the one file whose whole job is to
    hold configuration that sits next to a credential.

Exit codes: 0 = clean, 1 = findings, 2 = it could not run.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from docs_lint import BANNED_RULES  # noqa: E402  (path is set immediately above)

REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
)
CONFIG = os.path.join("tools", "play.d", "store.toml")
BRANDING = os.path.join(
    "taffy-core", "resources", "branding", "taffy_branding.gni"
)

#: Where the service-account key may live. Both are untracked; the checker
#: proves that rather than assuming it.
CREDENTIAL_PATHS = (
    os.path.join(".taffy", "play-service-account.json"),
)

SCHEMA_VERSION = 1

#: Play's published limits for a store listing, in characters.
LIMITS = {"title": 30, "short_description": 80, "full_description": 4000}

#: Release notes, per language, per release.
RELEASE_NOTES_LIMIT = 500

PACKAGE_OWNER = re.compile(r'^taffy_application_id\s*=\s*"([^"]+)"', re.M)

#: Shapes that must never appear in a tracked configuration file. Deliberately
#: narrower than a general secret scanner and aimed at what a Play credential
#: actually looks like: a service-account JSON's private key, its client
#: secret, or an OAuth refresh token pasted in for convenience.
CREDENTIAL_VALUES = re.compile(
    r"BEGIN [A-Z ]*PRIVATE KEY"
    r"|\bya29\.[A-Za-z0-9_-]{20,}"
    r"|\b1//[A-Za-z0-9_-]{20,}"
    r"|AIza[0-9A-Za-z_-]{35}",
)
CREDENTIAL_KEYS = re.compile(
    r"private_key|client_secret|refresh_token|service_account|credential|password",
    re.I,
)


def load(root: str = REPO_ROOT):
    """Return (config, raw text). Raises OSError or tomllib errors."""
    path = os.path.join(root, CONFIG)
    with open(path, "rb") as handle:
        raw = handle.read()
    return tomllib.loads(raw.decode("utf-8")), raw.decode("utf-8")


def owner_package(root: str = REPO_ROOT) -> str | None:
    try:
        with open(os.path.join(root, BRANDING), encoding="utf-8") as handle:
            match = PACKAGE_OWNER.search(handle.read())
    except OSError:
        return None
    return match.group(1) if match else None


def _check_listing(config, findings: list[str]) -> None:
    listings = config.get("listing")
    if not isinstance(listings, dict) or not listings:
        findings.append("listing: at least one language is required")
        return
    default = config.get("application", {}).get("default_language")
    if default and default not in listings:
        findings.append(
            f"application.default_language is {default!r}, which has no listing"
        )
    for language, listing in sorted(listings.items()):
        if not isinstance(listing, dict):
            findings.append(f"listing.{language}: must be a table")
            continue
        for field, limit in LIMITS.items():
            value = listing.get(field)
            if not isinstance(value, str) or not value.strip():
                findings.append(f"listing.{language}.{field}: required, non-empty")
                continue
            length = len(value.strip())
            if length > limit:
                findings.append(
                    f"listing.{language}.{field}: {length} characters, over "
                    f"Play's limit of {limit}. Play refuses this at upload."
                )
            for rule, pattern, hint in BANNED_RULES:
                match = pattern.search(value)
                if match:
                    findings.append(
                        f"listing.{language}.{field}: \"{match.group(0)}\" is "
                        f"banned vocabulary ({rule}). {hint}"
                    )
        notes = listing.get("release_notes")
        if notes is not None:
            if not isinstance(notes, str):
                findings.append(f"listing.{language}.release_notes: must be text")
            elif len(notes.strip()) > RELEASE_NOTES_LIMIT:
                findings.append(
                    f"listing.{language}.release_notes: {len(notes.strip())} "
                    f"characters, over Play's limit of {RELEASE_NOTES_LIMIT}"
                )


def _check_tracks(config, findings: list[str]) -> None:
    tracks = config.get("tracks")
    if not isinstance(tracks, list) or not tracks:
        findings.append("tracks: at least one track is required")
        return
    ids = []
    for index, track in enumerate(tracks):
        if not isinstance(track, dict):
            findings.append(f"tracks[{index}]: must be a table")
            continue
        identifier = track.get("id")
        if not isinstance(identifier, str) or not identifier:
            findings.append(f"tracks[{index}].id: required")
            continue
        ids.append(identifier)
        if not isinstance(track.get("description"), str) or not track["description"]:
            findings.append(f"tracks[{identifier}].description: required")
        if not isinstance(track.get("requires_approval"), bool):
            findings.append(f"tracks[{identifier}].requires_approval: required boolean")
    duplicates = sorted({value for value in ids if ids.count(value) > 1})
    if duplicates:
        findings.append(f"tracks: duplicate id(s) {', '.join(duplicates)}")

    known = set(ids)
    for track in tracks:
        if not isinstance(track, dict):
            continue
        identifier, target = track.get("id"), track.get("promotes_to")
        if target in (None, ""):
            continue
        if target == identifier:
            findings.append(f"tracks[{identifier}].promotes_to names itself")
        elif target not in known:
            findings.append(
                f"tracks[{identifier}].promotes_to is {target!r}, which is not a "
                "declared track"
            )

    if "production" in known:
        production = next(t for t in tracks if t.get("id") == "production")
        if production.get("requires_approval") is not True:
            findings.append(
                "tracks[production].requires_approval must be true. Production is "
                "the public listing and decision 0008 allows exactly one release "
                "into it."
            )

    default = config.get("application", {}).get("default_track")
    if default not in known:
        findings.append(
            f"application.default_track is {default!r}, which is not a declared track"
        )
    elif default == "production":
        findings.append(
            "application.default_track must not be production: an upload that "
            "lands on the public listing by default is one typo from publishing"
        )


def _check_credentials(raw: str, config, findings: list[str],
                       path: str = CONFIG) -> None:
    """No credential may enter a tracked configuration file, whichever one."""
    match = CREDENTIAL_VALUES.search(raw)
    if match:
        findings.append(
            f"{path} carries credential material matching "
            f"/{CREDENTIAL_VALUES.pattern.split('|')[0]}/ and must not"
        )

    def walk(node, where):
        if isinstance(node, dict):
            for key, value in node.items():
                if CREDENTIAL_KEYS.fullmatch(str(key)):
                    findings.append(
                        f"{where}{key}: a credential never lives in a tracked file. "
                        "The Play key is read at run time from "
                        f"{CREDENTIAL_PATHS[0]} or $TAFFY_PLAY_CREDENTIAL."
                    )
                walk(value, f"{where}{key}.")
        elif isinstance(node, list):
            for index, value in enumerate(node):
                walk(value, f"{where}[{index}].")

    walk(config, "")


def validate(config, raw: str, package_owner: str | None) -> list[str]:
    findings: list[str] = []
    if config.get("schema_version") != SCHEMA_VERSION:
        findings.append(
            f"schema_version must be {SCHEMA_VERSION}, not "
            f"{config.get('schema_version')!r}"
        )
    application = config.get("application")
    if not isinstance(application, dict):
        findings.append("application: required table")
        application = {}
    package = application.get("package")
    if not isinstance(package, str) or not package:
        findings.append("application.package: required")
    elif package_owner is None:
        findings.append(
            f"cannot read the package owner at {BRANDING}; the agreement is unchecked"
        )
    elif package != package_owner:
        findings.append(
            f"application.package is {package!r} and "
            f"taffy_branding.gni owns {package_owner!r}. That file is the single "
            "owner; change it there and project it here."
        )
    _check_listing(config, findings)
    _check_tracks(config, findings)
    _check_credentials(raw, config, findings)
    return findings


def check_untracked(root: str) -> list[str]:
    """The credential paths must be ignored by git, not merely absent."""
    import subprocess

    findings = []
    for relative in CREDENTIAL_PATHS:
        tracked = subprocess.run(
            ["git", "-C", root, "ls-files", "--error-unmatch", relative],
            capture_output=True,
        )
        if tracked.returncode == 0:
            findings.append(f"{relative} is tracked by git and must never be")
            continue
        ignored = subprocess.run(
            ["git", "-C", root, "check-ignore", "-q", relative], capture_output=True
        )
        if ignored.returncode != 0:
            findings.append(
                f"{relative} is not gitignored, so a key written there could be "
                "committed by accident. Add it to .gitignore."
            )
    return findings


def external_credential_findings(path: str, root: str = REPO_ROOT) -> list[str]:
    """What is wrong with a credential this repository does not own.

    `check_untracked` walks this repository's own paths and can say nothing
    about a key somewhere else on the disk — and $TAFFY_PLAY_CREDENTIAL is
    exactly that: a path handed in at run time, deliberately outside the tree so
    the `secrets` lane can never be the thing that has to catch it.

    Two conditions are worth naming, and both are **warnings**. Whose key this
    is, and where it lives, is an owner decision recorded outside this file; a
    checker that refused it would be overruling that decision rather than
    informing it. What it can honestly do is say what it sees:

      * the file is tracked by some *other* git work tree, so its bytes are in a
        history and reach everyone who can read that repository;
      * its mode is looser than 0600, so every account on this host can read it.

    Nothing here reads the file's contents, and neither the path nor any key
    material is recorded in this repository.
    """
    import stat
    import subprocess

    findings: list[str] = []
    if not path or not os.path.exists(path):
        return findings
    absolute = os.path.abspath(path)
    if absolute.startswith(os.path.abspath(root) + os.sep):
        return findings  # check_untracked owns the repository's own paths

    directory, name = os.path.split(absolute)
    tracked = subprocess.run(
        ["git", "-C", directory, "ls-files", "--error-unmatch", name],
        capture_output=True,
    )
    if tracked.returncode == 0:
        findings.append(
            "the Play credential is tracked by a git work tree, so its bytes are "
            "in a history: everyone who can read that repository holds this key. "
            "Rotating it is a Cloud console act, not a change here."
        )
    mode = stat.S_IMODE(os.stat(absolute).st_mode)
    if mode & 0o077:
        findings.append(
            f"the Play credential is mode {mode:04o}; every account on this host "
            "can read it. `chmod 600` it."
        )
    return findings


# --- self-test ---------------------------------------------------------------


def _good():
    return {
        "schema_version": 1,
        "application": {
            "package": "com.taffygo.browser",
            "default_language": "en-US",
            "default_track": "internal",
        },
        "listing": {
            "en-US": {
                "title": "TaffyGo",
                "short_description": "A browser.",
                "full_description": "A browser with an assistant.",
            }
        },
        "tracks": [
            {"id": "internal", "promotes_to": "production",
             "description": "testers", "requires_approval": False},
            {"id": "production", "promotes_to": "",
             "description": "the listing", "requires_approval": True},
        ],
    }


def self_test() -> int:
    import copy

    cases = []

    def case(name, mutate, expect):
        config = copy.deepcopy(_good())
        raw = mutate(config) or ""
        cases.append((name, config, raw, expect))

    case("a clean configuration", lambda c: None, None)
    case("a renamed package",
         lambda c: c["application"].__setitem__("package", "com.example.other"),
         "single owner")
    case("an over-long title",
         lambda c: c["listing"]["en-US"].__setitem__("title", "T" * 31),
         "over Play's limit of 30")
    case("an over-long short description",
         lambda c: c["listing"]["en-US"].__setitem__("short_description", "s" * 81),
         "over Play's limit of 80")
    case("banned vocabulary in the listing",
         lambda c: c["listing"]["en-US"].__setitem__(
             "full_description", "Join the beta today."),
         "banned vocabulary")
    case("an empty title",
         lambda c: c["listing"]["en-US"].__setitem__("title", "  "),
         "required, non-empty")
    case("a default language with no listing",
         lambda c: c["application"].__setitem__("default_language", "fr-FR"),
         "no listing")
    case("a promotion to a track that does not exist",
         lambda c: c["tracks"][0].__setitem__("promotes_to", "nowhere"),
         "not a declared track")
    case("a track that promotes to itself",
         lambda c: c["tracks"][0].__setitem__("promotes_to", "internal"),
         "names itself")
    case("production without approval",
         lambda c: c["tracks"][1].__setitem__("requires_approval", False),
         "requires_approval must be true")
    case("production as the default upload track",
         lambda c: c["application"].__setitem__("default_track", "production"),
         "one typo from publishing")
    case("a default track that does not exist",
         lambda c: c["application"].__setitem__("default_track", "ghost"),
         "not a declared track")
    case("the wrong schema version",
         lambda c: c.__setitem__("schema_version", 2),
         "schema_version must be 1")
    case("a duplicate track id",
         lambda c: c["tracks"].append(dict(c["tracks"][0])),
         "duplicate id")
    case("a credential key in the table",
         lambda c: c["application"].__setitem__("private_key", "x"),
         "a credential never lives in a tracked file")
    case("a private key pasted into the file",
         lambda c: "-----BEGIN PRIVATE KEY-----",
         "credential material")
    case("an over-long release note",
         lambda c: c["listing"]["en-US"].__setitem__("release_notes", "n" * 501),
         "over Play's limit of 500")

    failures = 0
    for name, config, raw, expect in cases:
        findings = validate(config, raw, "com.taffygo.browser")
        joined = " | ".join(findings)
        if expect is None and findings:
            failures += 1
            print(f"  fail {name}: expected none, got {joined}", file=sys.stderr)
        elif expect is not None and expect not in joined:
            failures += 1
            print(
                f"  fail {name}: expected {expect!r}, got {joined or '(none)'}",
                file=sys.stderr,
            )

    findings = validate(_good(), "", None)
    if not any("cannot read the package owner" in f for f in findings):
        failures += 1
        print("  fail an unreadable owner must be a finding", file=sys.stderr)

    if failures:
        print(f"play config self-test: {failures} case(s) failed", file=sys.stderr)
        return 1
    print(f"play config self-test: {len(cases) + 1} cases passed")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=REPO_ROOT)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--credential-posture", metavar="PATH", default=None,
                        help="report on a credential outside this repository "
                             "(warnings only; always exits 0)")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    if args.credential_posture is not None:
        for finding in external_credential_findings(args.credential_posture, args.root):
            print(f"  warn  {finding}", file=sys.stderr)
        return 0

    try:
        config, raw = load(args.root)
    except OSError as error:
        print(f"play config: {error}", file=sys.stderr)
        return 2
    except tomllib.TOMLDecodeError as error:
        print(f"play config: {CONFIG} is not valid TOML: {error}", file=sys.stderr)
        return 2

    findings = validate(config, raw, owner_package(args.root))
    findings += check_untracked(args.root)
    for finding in findings:
        print(f"  {finding}", file=sys.stderr)
    if findings:
        return 1
    languages = len(config.get("listing", {}))
    tracks = len(config.get("tracks", []))
    print(
        f"play config: {CONFIG} — {languages} listing(s), {tracks} track(s), "
        f"package {config['application']['package']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
