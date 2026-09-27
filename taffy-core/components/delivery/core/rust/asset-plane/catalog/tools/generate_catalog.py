#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate the asset catalog Rust table from its committed source.

WHY THIS IS GENERATED AND NOT DECODED AT RUN TIME

Every asset's digest, byte length and origin path is a constant of the build.
A catalog fetched or parsed at run time would mean a file — or a service — that
can point a device at bytes other than the ones the product was built against,
and `AGENTS.md` states that this repository installs nothing unpinned at run
time. Compiling the catalog in removes the parser, the parser's failure modes,
and the attack the parser exists to survive, all at once.

What is left is staleness, and that is what `--check` is for.

WHAT THE SOURCE MAY SAY, AND WHAT IT MAY NOT

An asset is specified before its bytes are built. The source may therefore
declare a variant `unpublished`, which names no path, no length and no digest,
and which the plane refuses to fetch. What the source may NOT do is carry a
digest for bytes nobody produced: a `published` variant must name all four
facts, and an `unpublished` one must name none of them. That rule is the whole
reason a pinned catalog can be honest about an artifact that does not exist yet
instead of holding a placeholder that later reads as a pin.

WHAT A MODEL ROW MUST ADD

A row of kind `model-weights` or `model-tokenizer` names what the artifact *is*
as well as what it is for, and names where its bytes came from. Both rules and
their fixtures live in `model_rows.py`, which this file imports, raises for,
and runs the self-test of; its module docstring is the record of why the
provenance half cannot be left to the `files` lane.

Stdlib only. Deterministic: nothing reads a clock, a host name, or the
environment. `CATALOG_FINGERPRINT` is derived from the rows themselves, so it
cannot be forgotten and cannot be stale.

    generate_catalog.py --write       rewrite src/catalog/generated.rs
    generate_catalog.py --check       fail if it is stale
    generate_catalog.py --self-test   check that the rules above still fire

Exit status: 0 clean, 1 stale or malformed.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import re
import sys

import bundled_rows
import model_rows

HERE = os.path.dirname(os.path.abspath(__file__))
CRATE = os.path.dirname(HERE)
ROOT = os.path.dirname(CRATE)
SOURCE = os.path.join(CRATE, "source", "assets.json")
GENERATED = os.path.join(ROOT, "src", "catalog", "generated.rs")
#: Six levels above the crate: asset-plane, rust, core, delivery, components,
#: taffy-core. A model row names its provenance record from the repository
#: root, because that is where a person reads and writes such a path.
REPO_ROOT = os.path.abspath(os.path.join(ROOT, *([os.pardir] * 6)))

#: The platforms a catalog row may publish for. A name here that
#: `platform::Platform` does not carry is a build failure in Rust; a name there
#: that is missing here is refused below.
#:
#: `unsupported` is deliberately absent. The enum carries it so a build with no
#: published artifacts can say so instead of naming someone else's platform,
#: and a catalog row that named it would be publishing bytes for exactly the
#: builds that must fetch none.
PLATFORMS = (
    "android-arm64",
    "android-x64",
    "macos-arm64",
    "macos-x64",
    "windows-x64",
    "windows-arm64",
)
PLATFORM_VARIANTS = {
    "android-arm64": "AndroidArm64",
    "android-x64": "AndroidX64",
    "macos-arm64": "MacosArm64",
    "macos-x64": "MacosX64",
    "windows-x64": "WindowsX64",
    "windows-arm64": "WindowsArm64",
}

#: Mirrors `catalog::entry::Kind`.
KIND_VARIANTS = {
    "python-stdlib": "PythonStdlib",
    "python-packages": "PythonPackages",
    "model-weights": "ModelWeights",
    "model-tokenizer": "ModelTokenizer",
    "filter-list": "FilterList",
    "country-flags": "CountryFlags",
    "start-scenes": "StartScenes",
}

NECESSITY_VARIANTS = {"required": "Required", "on-demand": "OnDemand"}
CONTAINER_VARIANTS = {"raw": "Raw", "zip": "Zip"}
PUBLICATION_VARIANTS = {"published": "Published", "unpublished": "Unpublished"}

ASSET_ID = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
DIGEST_HEX = re.compile(r"^[0-9a-f]{64}$")

# The alphabet a path segment may use, and it is not this file's to choose.
#
# `IsRelativePath` in taffy-core/browser/core_asset_validation.cc is what
# actually runs on a phone, and it permits lowercase letters, digits and the
# three separator characters — nothing else, because a component that needed
# escaping did not come from this catalog. Anything wider here is a row this
# generator accepts and the device refuses, and the device refuses it without
# naming the character.
#
# The revision shares the alphabet on purpose. A revision is embedded in its
# own path by every builder in the tree, so a revision that cannot be a path
# segment is a trap laid for whoever publishes it next.
PATH_SEGMENT = re.compile(r"^[a-z0-9._-]+$")
REVISION = re.compile(r"^[a-z0-9]+([.\-][a-z0-9]+)*$")

MAX_VARIANT_BYTES = 4 * 1024 * 1024 * 1024
MAX_ASSETS = 64

PUBLISHED_FIELDS = ("path", "transfer_bytes", "installed_bytes", "digest")

#: Reading the tree is `bundled_rows`' business; where the tree is, is this
#: file's. Bound once here so both facts stay in one place each.
MEASURE_REPO_FILE, LIST_REPO_DIRECTORY = bundled_rows.repo_readers(REPO_ROOT)


class CatalogError(Exception):
    """A source file that cannot become a catalog."""


def fail(message: str) -> None:
    raise CatalogError(message)


def load(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def read_repo_file(relative: str):
    """The text of a repository file, or None when there is no such file.

    The one way the provenance rules reach the tree. Injected rather than
    called directly so that a fixture record and a real one are the same code
    path, and so that a rule can be broken in a self-test without a wrong file
    being written into the repository to break it.
    """
    try:
        with open(
            os.path.join(REPO_ROOT, *relative.split("/")), "r", encoding="utf-8"
        ) as handle:
            return handle.read()
    except OSError:
        return None


def rust_literal(value: int) -> str:
    """A byte count as Rust spells one.

    Underscore-grouped because the workspace lints at clippy pedantic, where
    `unreadable_literal` is a warning and warnings are denied. A generated file
    that cannot pass the lint the crate around it is held to would leave the
    `rust` and `catalog` lanes contradicting each other, with the failure
    naming the formatter rather than the generator that wrote the digit.
    """
    digits = str(value)
    groups = []
    while len(digits) > 3:
        groups.insert(0, digits[-3:])
        digits = digits[:-3]
    groups.insert(0, digits)
    return "_".join(groups)


def validate_path(where: str, value: str) -> None:
    if not value:
        fail(f"{where}: a published variant needs a path")
    if value.startswith("/") or value.endswith("/"):
        fail(f"{where}: path {value!r} must not start or end with a separator")
    for segment in value.split("/"):
        if not segment:
            fail(f"{where}: path {value!r} has an empty segment")
        if segment in (".", ".."):
            fail(f"{where}: path {value!r} traverses")
        if not PATH_SEGMENT.match(segment):
            fail(f"{where}: path segment {segment!r} is outside the permitted alphabet")


def validate_variant(where: str, variant: dict, seen: set) -> None:
    platform = variant.get("platform")
    if platform not in PLATFORMS:
        fail(f"{where}: unknown platform {platform!r}")
    if platform in seen:
        fail(f"{where}: platform {platform} appears twice")
    seen.add(platform)

    publication = variant.get("publication")
    if publication not in PUBLICATION_VARIANTS:
        fail(f"{where}: unknown publication {publication!r}")

    present = [field for field in PUBLISHED_FIELDS if field in variant]
    if publication == "unpublished":
        if present:
            fail(
                f"{where}: an unpublished variant names {present} — bytes that do "
                "not exist have no digest, and a placeholder digest later reads "
                "as a pin"
            )
        return

    missing = [field for field in PUBLISHED_FIELDS if field not in variant]
    if missing:
        fail(f"{where}: a published variant is missing {missing}")
    validate_path(where, variant["path"])
    for field in ("transfer_bytes", "installed_bytes"):
        value = variant[field]
        if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
            fail(f"{where}: {field} must be a positive integer")
        if value > MAX_VARIANT_BYTES:
            fail(f"{where}: {field} is past the {MAX_VARIANT_BYTES}-byte maximum")
    if not DIGEST_HEX.match(variant["digest"]):
        fail(f"{where}: digest must be 64 lowercase hexadecimal characters")


def validate(
    source: dict,
    read=read_repo_file,
    measure=MEASURE_REPO_FILE,
    listing=LIST_REPO_DIRECTORY,
) -> None:
    assets = source.get("assets")
    if not isinstance(assets, list) or not assets:
        fail("assets: the catalog needs at least one row")
    if len(assets) > MAX_ASSETS:
        fail(f"assets: {len(assets)} rows is past the {MAX_ASSETS}-row maximum")

    identities = set()
    for asset in assets:
        identity = asset.get("id")
        revision = asset.get("revision")
        where = f"{identity}@{revision}"
        if not isinstance(identity, str) or not ASSET_ID.match(identity):
            fail(f"{where}: id must be lowercase words joined by single hyphens")
        if not isinstance(revision, str) or not REVISION.match(revision):
            fail(f"{where}: revision is outside the permitted alphabet")
        if (identity, revision) in identities:
            fail(f"{where}: two rows share one identity and revision")
        identities.add((identity, revision))

        if asset.get("kind") not in KIND_VARIANTS:
            fail(f"{where}: unknown kind {asset.get('kind')!r}")
        if asset.get("necessity") not in NECESSITY_VARIANTS:
            fail(f"{where}: unknown necessity {asset.get('necessity')!r}")
        if asset.get("container") not in CONTAINER_VARIANTS:
            fail(f"{where}: unknown container {asset.get('container')!r}")
        if not asset.get("description"):
            fail(f"{where}: every row says what it is for")
        finding = model_rows.model_row_finding(where, asset, read)
        if finding:
            fail(finding)

        variants = asset.get("variants")
        if not isinstance(variants, list) or not variants:
            fail(f"{where}: a row publishes for at least one platform")
        seen: set = set()
        for variant in variants:
            validate_variant(where, variant, seen)

    # Last, so that a row broken in one of the ways above is named by the rule
    # it broke rather than by a bundled entry that can no longer find it.
    finding = bundled_rows.bundled_finding(source, measure, listing)
    if finding:
        fail(finding)


def fingerprint(source: dict) -> int:
    """A stable 32-bit digest of the rows themselves.

    Derived rather than declared, so it cannot be forgotten in a review and
    cannot disagree with the rows it names. Only the fields that reach the
    generated table take part: a note or a description is for a reader.
    """
    rows = []
    for asset in source["assets"]:
        variants = [
            [
                variant["platform"],
                variant["publication"],
                variant.get("path", ""),
                variant.get("transfer_bytes", 0),
                variant.get("installed_bytes", 0),
                variant.get("digest", ""),
            ]
            for variant in asset["variants"]
        ]
        rows.append(
            [
                asset["id"],
                asset["revision"],
                asset["kind"],
                asset["necessity"],
                asset["container"],
                sorted(variants),
            ]
        )
    rows.sort()
    canonical = json.dumps(rows, separators=(",", ":"), sort_keys=True)
    digest = hashlib.sha256(canonical.encode("utf-8")).digest()
    return int.from_bytes(digest[:4], "big")


def rust_string(value: str) -> str:
    return json.dumps(value)


def render(source: dict) -> str:
    lines = [
        "// Copyright (c) 2026 Matterward Labs Private Limited.",
        "//",
        "// This Source Code Form is subject to the terms of the Mozilla Public",
        "// License, v. 2.0. If a copy of the MPL was not distributed with this",
        "// file, You can obtain one at https://mozilla.org/MPL/2.0/.",
        "",
        "//! The catalog this build was compiled with.",
        "//!",
        "//! GENERATED FILE — do not edit. Its source is",
        "//! `catalog/source/assets.json` and its generator is",
        "//! `catalog/tools/generate_catalog.py`. Add an asset by adding a row",
        "//! there and running the generator with `--write`; no code changes.",
        "",
        model_rows.entry_import(source["assets"]),
        "use crate::platform::Platform;",
        "",
        "/// A digest of the rows below, so a device can tell one catalog from another.",
        f"pub const CATALOG_FINGERPRINT: u32 = {fingerprint(source):_};",
        "",
    ]
    for index, asset in enumerate(source["assets"]):
        name = "VARIANTS_" + re.sub(r"[^A-Z0-9]", "_", asset["id"].upper()) + f"_{index}"
        lines.append(f"static {name}: &[Variant] = &[")
        for variant in asset["variants"]:
            published = variant["publication"] == "published"
            lines.append("    Variant::new(")
            lines.append(f"        Platform::{PLATFORM_VARIANTS[variant['platform']]},")
            lines.append(
                f"        Publication::{PUBLICATION_VARIANTS[variant['publication']]},"
            )
            lines.append(f"        {rust_string(variant.get('path', ''))},")
            lines.append(f"        {rust_literal(variant.get('transfer_bytes', 0))},")
            lines.append(f"        {rust_literal(variant.get('installed_bytes', 0))},")
            lines.append(f"        {rust_string(variant.get('digest', ''))},")
            lines.append("    ),")
            del published
        lines.append("];")
        lines.append("")
    lines.append("/// Every asset, in source order.")
    lines.append("pub static ENTRIES: &[CatalogEntry] = &[")
    for index, asset in enumerate(source["assets"]):
        name = "VARIANTS_" + re.sub(r"[^A-Z0-9]", "_", asset["id"].upper()) + f"_{index}"
        lines.append("    CatalogEntry::new(")
        lines.append(f"        {rust_string(asset['id'])},")
        lines.append(f"        {rust_string(asset['revision'])},")
        lines.append(f"        Kind::{KIND_VARIANTS[asset['kind']]},")
        lines.append(f"        Necessity::{NECESSITY_VARIANTS[asset['necessity']]},")
        lines.append(f"        Container::{CONTAINER_VARIANTS[asset['container']]},")
        lines.append(f"        {model_rows.render_model(asset)},")
        lines.append(f"        {name},")
        lines.append("    ),")
    lines.append("];")
    lines.append("")
    return "\n".join(lines)


def self_test() -> int:
    """Break the source in each way the rules name and check each one fires."""
    base = load(SOURCE)
    cases = []

    def case(name, mutate):
        cases.append((name, mutate))

    def first_variant(document):
        return document["assets"][0]["variants"][0]

    case("a digest on an unpublished variant", lambda d: first_variant(d).update(
        {"publication": "unpublished", "digest": "0" * 64}))
    case("a published variant with no digest", lambda d: (
        first_variant(d).update({"publication": "published", "path": "a/b",
                                 "transfer_bytes": 1, "installed_bytes": 1}),
        first_variant(d).pop("digest", None)))
    case("an uppercase digest", lambda d: first_variant(d).update(
        {"publication": "published", "path": "a/b", "transfer_bytes": 1,
         "installed_bytes": 1, "digest": "A" * 64}))
    case("a traversing path", lambda d: first_variant(d).update(
        {"publication": "published", "path": "a/../b", "transfer_bytes": 1,
         "installed_bytes": 1, "digest": "0" * 64}))
    # The three characters the browser refuses in a path segment, each on
    # its own case so a failure names which one came back. `IsRelativePath` in
    # taffy-core/browser/core_asset_validation.cc is the authority; these hold
    # this generator to it, because until they existed nothing in the tree
    # compared the two alphabets and this file was the wider of the pair.
    for character in ("+", "A", "%"):
        case(f"a path segment carrying {character!r}",
             lambda d, c=character: first_variant(d).update(
                 {"publication": "published", "path": f"a/b{c}c",
                  "transfer_bytes": 1, "installed_bytes": 1,
                  "digest": "0" * 64}))
    # And the same alphabet on the revision, which every builder in the tree
    # embeds in the path it composes.
    case("a revision carrying '+'", lambda d: d["assets"][0].update(
        {"revision": "3.14.2+taffy.1"}))
    case("an unknown platform", lambda d: first_variant(d).update(
        {"platform": "linux-x64"}))
    case("a duplicated platform", lambda d: d["assets"][0]["variants"].append(
        copy.deepcopy(first_variant(d))))
    case("an identity with a capital", lambda d: d["assets"][0].update({"id": "Python"}))
    case("an unknown kind", lambda d: d["assets"][0].update({"kind": "sausage"}))
    case("a zero-length published variant", lambda d: first_variant(d).update(
        {"publication": "published", "path": "a/b", "transfer_bytes": 0,
         "installed_bytes": 1, "digest": "0" * 64}))
    case("a row with no description", lambda d: d["assets"][0].pop("description"))
    case("two rows with one identity", lambda d: d["assets"].append(
        copy.deepcopy(d["assets"][0])))

    # Two model cases here rather than in `model_rows`, because what they
    # check is that the rule is *wired into* `validate` — the rule itself is
    # tested against fixtures there, and a rule nothing calls passes its own
    # tests forever.
    case("a model kind with no model block", lambda d: d["assets"][0].update(
        {"kind": "model-weights"}))
    case("model facts on a row that is not a model", lambda d: d["assets"][0].update(
        {"model": {"format": "gguf", "role": "whole"}}))

    # Two bundled cases here rather than in `bundled_rows`, and against the
    # committed bytes rather than a fixture, for the reason the model cases
    # above give: what they check is that the rule is *wired into* `validate`,
    # and a rule nothing calls passes its own tests forever. One breaks a row,
    # one breaks the disk listing, because those reach the module by different
    # arguments and a dropped `listing=` would be invisible to the first.
    case("a bundled row recording the wrong digest", lambda d: d["bundled"][0].update(
        {"digest": "0" * 64}))

    failures = []
    wiring_checks = 1
    try:
        validate(
            copy.deepcopy(base),
            listing=lambda _relative: sorted(
                [bundled_rows.BUNDLED_RECORD, "surprise.zip"]
                + [entry["file"] for entry in base["bundled"]]
            ),
        )
    except CatalogError:
        pass
    else:
        failures.append("an unnamed file in the bundled directory, through validate()")

    bundled_failures, bundled_checks = bundled_rows.self_test()
    failures.extend(bundled_failures)

    for name, mutate in cases:
        document = copy.deepcopy(base)
        mutate(document)
        try:
            validate(document)
        except CatalogError:
            continue
        failures.append(name)

    # And one in the accepting direction, against the real reader: a model row
    # whose provenance record is a directory that is actually in this tree.
    # flag-icons is not a model, and is used here only because it is a complete
    # `README.chromium` with its licence file beside it — the case proves the
    # repository reader resolves a real record rather than that a model is
    # cataloged, and it is what would catch REPO_ROOT pointing somewhere else.
    accepted = copy.deepcopy(base)
    accepted["assets"][0]["kind"] = "model-weights"
    accepted["assets"][0]["model"] = {"format": "onnx-runtime", "role": "whole"}
    accepted["assets"][0]["provenance"] = {
        "upstream_url": "https://example.invalid/model.onnx",
        "licence_record": "taffy-core/third_party/flag-icons",
    }
    try:
        validate(accepted)
    except CatalogError as error:
        failures.append(f"a model row naming a record in this tree was refused: {error}")

    model_failures, model_checks = model_rows.self_test()
    failures.extend(model_failures)

    try:
        validate(base)
    except CatalogError as error:
        failures.append(f"the committed source is itself invalid: {error}")

    before = fingerprint(base)
    moved = copy.deepcopy(base)
    moved["assets"].reverse()
    if fingerprint(moved) != before:
        failures.append("the fingerprint changed when only row order did")
    changed = copy.deepcopy(base)
    changed["assets"][0]["revision"] = changed["assets"][0]["revision"] + ".9"
    if fingerprint(changed) == before:
        failures.append("the fingerprint did not change when a revision did")

    for failure in failures:
        print(f"self-test: {failure} was not caught", file=sys.stderr)
    if failures:
        return 1
    print(
        f"self-test: {len(cases) + model_checks + bundled_checks + wiring_checks} rules fire, "
        "a model row naming a real provenance record is accepted, and the "
        "fingerprint tracks the rows"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true", help="rewrite the table")
    group.add_argument("--check", action="store_true", help="fail if it is stale")
    group.add_argument("--self-test", action="store_true", help="check the rules")
    arguments = parser.parse_args()

    if arguments.self_test:
        return self_test()

    try:
        source = load(SOURCE)
        validate(source)
        rendered = render(source)
    except CatalogError as error:
        print(f"asset catalog: {error}", file=sys.stderr)
        return 1

    if arguments.write:
        with open(GENERATED, "w", encoding="utf-8") as handle:
            handle.write(rendered)
        print(f"wrote {os.path.relpath(GENERATED)}")
        return 0

    try:
        with open(GENERATED, "r", encoding="utf-8") as handle:
            existing = handle.read()
    except OSError:
        existing = ""
    if existing != rendered:
        print(
            "asset catalog: the generated table no longer matches its source. "
            "Regenerate with `python3 "
            "taffy-core/components/delivery/core/rust/asset-plane/catalog/tools/"
            "generate_catalog.py --write` and commit the result.",
            file=sys.stderr,
        )
        return 1
    print(f"asset catalog: {len(source['assets'])} rows, up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
