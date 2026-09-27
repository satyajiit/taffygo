#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""What a model row must say, and the provenance record it must point at.

Authority boundary: the two rules a catalog row of kind `model-weights` or
`model-tokenizer` is held to, and nothing else. Row shape, digests, paths,
publication and staleness stay in `generate_catalog.py`, which imports this
module, raises what it returns, and runs its self-test inside its own.

WHY IT IS A SEPARATE FILE

Every other rule in the generator is about the row itself. These two are about
the row's relationship to the repository: bytes that came from somewhere, and
terms that have to be a file in this tree. They also have to be broken
deliberately in a self-test, and a rule that can be broken against a fixture is
a rule that can be tested without writing a wrong row into the committed
source.

THE PRECONDITION

**No model artifact ships without an upstream URL and a licence.** That is not
a convention here. Decision 0060 records that a reused export with no upstream
repository recorded and no licence beside it cannot be published from this
repository, and the `files` lane that would normally catch an unrecorded binary
**cannot see one of these at all**: it sweeps shipping binaries from disk, and
a delivery-plane artifact is never on this disk. It lives at the origin, and
the catalog row is the only thing in the tree that names it. So the check has
to be made here, and it has to be at least as strict as the sweep — a row is
refused unless a provenance record in the tree carries the terms.

The record format is Chromium's `README.chromium`, which is what
`taffy-core/third_party/flag-icons` and `taffy-core/third_party/cpython`
already are. Nothing parallel is introduced: a model artifact is vendored the
way everything else this product did not write is vendored, and the row names
the record rather than restating it.

Stdlib only. Read-only. The repository is reached through one injected reader
so that a fixture record and a real one are the same code path.
"""

from __future__ import annotations

import copy
import re

#: The kinds that must carry model facts. Mirrors `Kind::is_model`.
MODEL_KINDS = ("model-weights", "model-tokenizer")

#: Mirrors `catalog::entry::ModelFormat`, which mirrors the core-service
#: contract's `ToolModelArtifactKind`. Three names, one vocabulary: the point
#: of spelling them the same is that decision 0101's carrier is a direct
#: mapping rather than a translation with another table to keep in step.
FORMAT_VARIANTS = {
    "litert-tflite": "LitertTflite",
    "onnx-runtime": "OnnxRuntime",
    "gguf": "Gguf",
}

#: Mirrors `catalog::entry::ArtifactRole`.
ROLE_VARIANTS = {"whole": "Whole", "adapter": "Adapter"}

#: Where a provenance record may live. One directory per vendored thing, under
#: the tree that already holds them.
RECORD_PATH = re.compile(r"^taffy-core/third_party/[a-z0-9]+(-[a-z0-9]+)*$")

#: The three `README.chromium` fields a row's record must fill. `URL` and
#: `License` are the precondition itself; `License File` is what makes the
#: licence a file in this tree rather than a word in a header.
RECORD_FIELDS = ("URL", "License", "License File")


def readme_field(text: str, name: str) -> str:
    """The value of one `README.chromium` field, or an empty string."""
    prefix = f"{name}:"
    for line in text.splitlines():
        if line.startswith(prefix):
            return line[len(prefix) :].strip()
    return ""


def record_finding(where: str, record, read) -> str | None:
    """Why `record` is not a provenance record, or None.

    `read` takes a repository-relative path and returns the file's text, or
    None when there is no such file. A directory is therefore never asked
    about directly — what matters is whether the two files are there.
    """
    if not isinstance(record, str) or not RECORD_PATH.match(record):
        return (
            f"{where}: licence_record must name one directory under "
            f"taffy-core/third_party/, not {record!r}"
        )
    readme = read(f"{record}/README.chromium")
    if readme is None:
        return f"{where}: {record} carries no README.chromium"
    fields = {name: readme_field(readme, name) for name in RECORD_FIELDS}
    empty = [name for name in RECORD_FIELDS if not fields[name]]
    if empty:
        return f"{where}: {record}/README.chromium leaves {empty} empty"
    licence = read(f"{record}/{fields['License File']}")
    if not licence:
        return (
            f"{where}: {record}/README.chromium names licence file "
            f"{fields['License File']!r}, and it is not in the tree"
        )
    return None


def model_row_finding(where: str, asset: dict, read) -> str | None:
    """Why this row is not a publishable model row, or None.

    Called for every row, because the rule runs in both directions: a model row
    with no facts and a flag pack that grew them are both refused. A consumer
    reading `Some(ModelFacts)` off the compiled table therefore already knows
    the kind agrees.
    """
    kind = asset.get("kind")
    model = asset.get("model")
    if kind not in MODEL_KINDS:
        if model is not None:
            return (
                f"{where}: model facts on a {kind!r} row — a format and an "
                "adapter flag describe a model artifact and nothing else"
            )
        return None
    if not isinstance(model, dict):
        return (
            f"{where}: a {kind} row carries a `model` block naming its format "
            "and its role, because the kind says what the artifact is for and "
            "a runtime needs to know what it is"
        )
    if model.get("format") not in FORMAT_VARIANTS:
        return f"{where}: unknown model format {model.get('format')!r}"
    role = model.get("role")
    if role not in ROLE_VARIANTS:
        return f"{where}: unknown artifact role {role!r}"
    if kind == "model-tokenizer" and role == "adapter":
        return (
            f"{where}: a tokenizer has no adapter form — an adapter modifies "
            "weights, and a row claiming otherwise would resolve as one"
        )
    provenance = asset.get("provenance")
    if not isinstance(provenance, dict):
        return f"{where}: a model row carries provenance"
    upstream = provenance.get("upstream_url")
    if not isinstance(upstream, str) or not upstream.startswith("https://"):
        return (
            f"{where}: a model row names the https upstream URL its bytes were "
            "taken from — an artifact whose origin nobody wrote down cannot be "
            "published from this repository"
        )
    return record_finding(where, provenance.get("licence_record"), read)


def render_model(asset: dict) -> str:
    """The `Option<ModelFacts>` argument this row contributes to the table."""
    model = asset.get("model")
    if model is None:
        return "None"
    return (
        f"Some(ModelFacts::new(ModelFormat::{FORMAT_VARIANTS[model['format']]}, "
        f"ArtifactRole::{ROLE_VARIANTS[model['role']]}))"
    )


def entry_import(assets: list) -> str:
    """The generated table's one `use` line over `entry`.

    The three model names appear only when a row names them: an import of a
    type nothing uses is an unused import, the workspace denies warnings, and a
    generated file that cannot pass the lint the crate around it is held to
    would leave the `rust` and `catalog` lanes contradicting each other.
    """
    names = {"CatalogEntry", "Container", "Kind", "Necessity", "Publication", "Variant"}
    if any(asset.get("model") is not None for asset in assets):
        names |= {"ArtifactRole", "ModelFacts", "ModelFormat"}
    return "use super::entry::{" + ", ".join(sorted(names)) + "};"


#: A provenance record that is complete, and the row that names it. Both are
#: fixtures: the point is that a rule can be broken here without a wrong row
#: ever existing in the committed source.
_FIXTURE_TREE = {
    "taffy-core/third_party/example/README.chromium": (
        "Name: Example\n"
        "URL: https://example.invalid/example-1.0.tar.gz\n"
        "Version: 1.0\n"
        "License: Apache-2.0\n"
        "License File: vendor/example.Apache.txt\n"
        "Shipped: no\n"
    ),
    "taffy-core/third_party/example/vendor/example.Apache.txt": "Apache-2.0.\n",
}

_FIXTURE_ROW = {
    "kind": "model-weights",
    "model": {"format": "onnx-runtime", "role": "whole"},
    "provenance": {
        "upstream_url": "https://example.invalid/example-1.0/model.onnx",
        "licence_record": "taffy-core/third_party/example",
    },
}


def _cases() -> list:
    """Each way a model row can be wrong, as (name, row) pairs."""
    cases = []

    def case(name, mutate):
        row = copy.deepcopy(_FIXTURE_ROW)
        mutate(row)
        cases.append((name, row))

    case("a model row with no model block", lambda r: r.pop("model"))
    case("an unknown model format", lambda r: r["model"].update({"format": "safetensors"}))
    case("an unknown artifact role", lambda r: r["model"].update({"role": "lora"}))
    case(
        "a tokenizer claiming to be an adapter",
        lambda r: (r.update({"kind": "model-tokenizer"}), r["model"].update({"role": "adapter"})),
    )
    case("a model row with no provenance", lambda r: r.pop("provenance"))
    case("a model row with no upstream URL", lambda r: r["provenance"].pop("upstream_url"))
    case(
        "an upstream that is not https",
        lambda r: r["provenance"].update({"upstream_url": "http://example.invalid/model.onnx"}),
    )
    case("a model row naming no licence record", lambda r: r["provenance"].pop("licence_record"))
    case(
        "a licence record outside third_party",
        lambda r: r["provenance"].update({"licence_record": "taffy-core/browser"}),
    )
    case(
        "a licence record with no README.chromium",
        lambda r: r["provenance"].update({"licence_record": "taffy-core/third_party/absent"}),
    )
    case("model facts on a flag pack", lambda r: r.update({"kind": "country-flags"}))
    return cases


def self_test() -> tuple:
    """Break a model row in each way the rules name.

    Returns `(failures, checks)`: what got through, and how many checks were
    actually made. The count is returned rather than declared beside this
    function, because the generator prints it and both the catalog README and
    decision 0065 say a rule that stopped being checked shows up as a smaller
    number. A hand-kept total makes that claim false for exactly the checks
    somebody forgets to count — so `checks` is incremented where the check
    happens and nowhere else.
    """
    read = _FIXTURE_TREE.get
    failures = []
    checks = 0

    checks += 1
    complete = model_row_finding("fixture", _FIXTURE_ROW, read)
    if complete is not None:
        failures.append(f"a complete model row was refused: {complete}")
    checks += 1
    if model_row_finding("fixture", {"kind": "country-flags"}, read) is not None:
        failures.append("a row that is not a model row was refused")

    for name, row in _cases():
        checks += 1
        if model_row_finding("fixture", row, read) is None:
            failures.append(name)

    # The two record failures a fixture tree can show that a row cannot: a
    # README that fills none of the three fields, and one whose licence file is
    # named and absent. The second is the shape that matters most — a header
    # can say "License: MIT" while no MIT text is anywhere in the tree.
    hollow = dict(_FIXTURE_TREE)
    hollow["taffy-core/third_party/example/README.chromium"] = "Name: Example\n"
    checks += 1
    if record_finding("fixture", "taffy-core/third_party/example", hollow.get) is None:
        failures.append("a README.chromium naming no terms")
    orphaned = dict(_FIXTURE_TREE)
    del orphaned["taffy-core/third_party/example/vendor/example.Apache.txt"]
    checks += 1
    if record_finding("fixture", "taffy-core/third_party/example", orphaned.get) is None:
        failures.append("a licence file named by a README and absent from the tree")

    return failures, checks
