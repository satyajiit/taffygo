#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Generate `catalog/baseline.json` from the catalog source of truth.

WHY THIS IS GENERATED AND NOT HAND-WRITTEN

`baseline.json` is the catalog compiled into the release: the layer a device
routes against on first run and offline. Decision 0015 makes provider and model
metadata versioned data rather than model router code and requires a build to fail on
a stale embedded baseline. A hand-maintained baseline cannot satisfy that — it
has no source to be stale against.

So the facts live once, in `catalog/source/`, in the units the vendor publishes
them in, beside the page they were read from and the date they were read. This
script is the only thing that turns them into the shape the model router decodes. Two
of its jobs are the reason it exists at all rather than being a copy step:

  * **Money is converted, never transcribed.** The source carries what the
    vendor prints — "$0.44 per M input tokens" as the string `"0.44"` — and this
    script converts it to integer micro-units per million tokens with `Decimal`.
    A rate is never typed twice in two units, so the two can never disagree.
  * **A model that cannot call tools is not cataloged for a task role.** The
    source records `tool_calling` per model and this script refuses to emit a
    model that claims PRIMARY_REASONING, FAST_BROWSING or VISION without it.
    That rule is a product decision; enforcing it here means it cannot be
    forgotten in a review of a 300-line JSON file. The fact itself is emitted
    too, because this script only ever sees the source of the *embedded*
    layer: a served overlay and a user's own provider reach the router without
    passing through it, so the same rule is checked again in
    `catalog::validate`, which is the one gate all three layers share. Refusing
    here and checking there are not duplicates — one stops a bad publish, the
    other stops a bad merge.

A third job keeps the source honest about what it knows. Every attributed field
of a model is named in exactly one of `provenance.vendor_published` or
`provenance.taffygo_chosen`, and the second half carries a reason. A number
nobody attributed is a finding, so "the vendor published this" and "we picked
this" can never quietly swap places between one review and the next.

What this script does NOT do is judge the catalog. Invariants — a model whose
provider is absent, a ladder with no supported rung, unordered price tiers —
belong to `catalog::validate` in the model router, which is where a served overlay and
a user override are judged too. The checks here are the ones that only make
sense against the *source*: the units money is published in, and the provenance
every attributed field has to carry.

Stdlib only. Deterministic: nothing reads a clock, a host name, or the
environment, and `generated_at` is a committed field of the source.

    generate_baseline.py --write       rewrite catalog/baseline.json
    generate_baseline.py --check       fail if it is stale
    generate_baseline.py --self-test   check that the rules above still fire

Exit status: 0 clean, 1 stale or malformed.
"""

from __future__ import annotations

import argparse
import copy
import json
import os
import re
import sys
from decimal import Decimal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from baseline_audit import LEVELS, RATES, audit  # noqa: E402

#: Micro-units in one unit of currency. A rate is quoted per million tokens and
#: stored per million tokens, so this is the only conversion factor there is.
MICROS_PER_UNIT = 1_000_000

#: A published price, as the vendor prints it. Six decimal places is exactly
#: what a micro-unit resolves; a seventh would round, and a rounded price is a
#: price nobody published.
_ROOT_MARKERS = ("chromium/REVISION", "TOOLCHAIN.md", "Cargo.toml")


def repo_root() -> str:
    """Finds the repository root without coupling the tool to crate placement."""
    root = os.path.dirname(os.path.realpath(__file__))
    while True:
        if all(os.path.exists(os.path.join(root, marker)) for marker in _ROOT_MARKERS):
            return root
        parent = os.path.dirname(root)
        if parent == root:
            raise SystemExit("catalog: could not find the repository root")
        root = parent


def source_dir(root: str) -> str:
    return os.path.join(
        root,
        "taffy-core",
        "components",
        "intelligence",
        "core",
        "rust",
        "model-router",
        "catalog",
        "source",
    )


def output_path(root: str) -> str:
    return os.path.join(
        root,
        "taffy-core",
        "components",
        "intelligence",
        "core",
        "rust",
        "model-router",
        "catalog",
        "baseline.json",
    )


def load(root: str) -> dict:
    """The three source files, as one value."""
    base = source_dir(root)
    out = {}
    for name in ("catalog", "providers", "models"):
        path = os.path.join(base, f"{name}.json")
        with open(path, encoding="utf-8") as handle:
            out[name] = json.load(handle)
    return out


# --- the rules --------------------------------------------------------------


def micros(amount: str) -> int:
    """A published amount per million tokens, as integer micro-units."""
    return int(Decimal(str(amount)) * MICROS_PER_UNIT)


def render_provider(entry: dict, schema_version: int) -> dict:
    rendered = {
        "provider_id": entry["provider_id"],
        "display_name": entry["display_name"],
        "wire_api": entry["wire_api"],
        "default_endpoint": entry["default_endpoint"],
        "auth_methods": list(entry["auth_methods"]),
        "subscription": entry["subscription"],
        "model_source": entry["model_source"],
        "enabled": entry["enabled"],
        "schema_version": schema_version,
    }
    # Emitted only when the vendor said something, so a row that knows nothing
    # extra renders exactly as it did before the column existed — which is what
    # keeps a served document byte-comparable against this render.
    presentation = {
        key: entry["presentation"][key]
        for key in ("key_prefix", "get_key_url", "docs_url")
        if entry.get("presentation", {}).get(key)
    }
    if presentation:
        rendered["presentation"] = presentation
    # The same rule for the second way in, and for the same reason: a vendor
    # with one way in renders exactly as it did before these columns existed,
    # so a served document stays byte-comparable against this render.
    for key in ("oauth_wire_api", "oauth_endpoint"):
        if entry.get(key):
            rendered[key] = entry[key]
    return rendered


def render_model(entry: dict, schema_version: int, currency: str) -> dict:
    price = entry["price"]
    cost = {"snapshot_version": price["snapshot_version"], "currency": currency,
            "basis": price["basis"]}
    for field, key in RATES:
        cost[field] = micros(price[key])
    if "long_context_tiers" in price:
        cost["long_context_tiers"] = [
            {"min_input_tokens": tier["min_input_tokens"],
             **{field: micros(tier[key]) for field, key in RATES}}
            for tier in price["long_context_tiers"]
        ]
    out = {
        "model_id": entry["model_id"],
        "provider_id": entry["provider_id"],
    }
    # The two route overrides, emitted only when the row states one, so a model
    # that takes its provider's family and address renders exactly as it did
    # before these columns existed and a served document stays byte-comparable
    # against this render. They are written here rather than later because the
    # decoder's struct declares them here, and a reader comparing the two
    # should not have to hold a reordering in their head.
    for key in ("wire_api", "endpoint"):
        if entry.get(key):
            out[key] = entry[key]
    out.update({
        "display_name": entry["display_name"],
        "roles": list(entry["roles"]),
        "input_modalities": list(entry["input_modalities"]),
        "reasoning": entry["reasoning"],
        # Emitted, never omitted. The model router decodes this as a required
        # field, so a baseline that left it out would drop every model rather
        # than route on a default nobody chose.
        "tool_calling": entry["tool_calling"],
    })
    if "thinking_levels" in entry:
        out["thinking_levels"] = {
            level: entry["thinking_levels"][level]
            for level in LEVELS
            if level in entry["thinking_levels"]
        }
    for key in ("compat", "sampling_defaults"):
        if entry.get(key):
            out[key] = dict(sorted(entry[key].items()))
    out["context_window"] = entry["context_window"]
    out["max_output_tokens"] = entry["max_output_tokens"]
    out["cost"] = cost
    out["enabled"] = entry["enabled"]
    out["schema_version"] = schema_version
    return out


def render(source: dict) -> str:
    header = source["catalog"]
    version = header["schema_version"]
    document = {
        "generated_by": (
            "taffy-core/components/intelligence/core/rust/model-router/"
            "catalog/tools/generate_baseline.py"
        ),
        "schema_version": version,
        "catalog_version": header["catalog_version"],
        "generated_at": header["generated_at"],
        "providers": [
            render_provider(entry, version)
            for entry in sorted(source["providers"], key=lambda item: item["provider_id"])
        ],
        "models": [
            render_model(entry, version, header["currency"])
            for entry in sorted(
                source["models"], key=lambda item: (item["provider_id"], item["model_id"])
            )
        ],
    }
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


# --- the generator's own checks ---------------------------------------------

#: One deliberate break each, and the phrase the finding for it must contain.
#: A rule nobody has watched fail is a rule that can quietly stop firing.
_BREAKS = (
    ("a task role without tool calling", lambda s: s["models"][1].update(tool_calling=False),
     "cannot call tools"),
    ("an image role without image input",
     # Found by role rather than by index: an inserted row must not quietly
     # repoint this break at a model the rule never applied to.
     lambda s: next(m for m in s["models"] if "VISION" in m["roles"]).update(
         input_modalities=["TEXT"]),
     "does not accept images"),
    ("a model under no provider", lambda s: s["models"][0].update(provider_id="ghost"),
     "not declared"),
    ("a field attributed to nobody",
     lambda s: s["models"][1]["provenance"]["vendor_published"].remove("context_window"),
     "attributed to nobody"),
    ("a field attributed twice",
     lambda s: s["models"][1]["provenance"]["taffygo_chosen"].update(
         context_window="a reason long enough to pass"),
     "at once"),
    ("a price with too many decimals",
     lambda s: s["models"][1]["price"].update(usd_per_million_input="0.4400000001"),
     "six decimal places"),
    ("an unknown field", lambda s: s["models"][1].update(priority=1), "unknown field"),
    ("an answer allowance larger than the window",
     lambda s: s["models"][1].update(max_output_tokens=99_999_999),
     "does not fit inside"),
    ("an unknown role", lambda s: s["models"][1].update(roles=["ORACLE"]), "not one of"),
    ("a provider with no decision behind it",
     lambda s: s["providers"][0]["provenance"].pop("decided_by"), "names no decision record"),
    ("long-context tiers out of order",
     lambda s: s["models"][1]["price"].update(long_context_tiers=[
         {"min_input_tokens": 400000, "usd_per_million_input": "2",
          "usd_per_million_output": "6", "usd_per_million_cache_read": "2",
          "usd_per_million_cache_write": "2"},
         {"min_input_tokens": 200000, "usd_per_million_input": "4",
          "usd_per_million_output": "12", "usd_per_million_cache_read": "4",
          "usd_per_million_cache_write": "4"}]),
     "strictly ascending"),
    # The four per-model overrides. Each break is the shape that would otherwise
    # reach a person's key: a family nobody implements, an address that is not
    # one, a map past the decoder's bound, and an override nobody justified.
    ("an unknown per-model wire family",
     lambda s: s["models"][1].update(wire_api="OPEN_AI_HARMONY"), "not one of"),
    ("a per-model endpoint that is not https",
     lambda s: s["models"][1].update(endpoint="http://api.example.test/v1"),
     "not an https address"),
    ("a compat map past the decoder's bound",
     lambda s: s["models"][1].update(
         compat={f"key{index}": "value" for index in range(33)}),
     "over the 32"),
    ("a per-model override attributed to nobody",
     lambda s: s["models"][1].update(wire_api="OPEN_AI_RESPONSES"),
     "attributed to nobody"),
    ("a tier rate with too many decimals",
     lambda s: s["models"][1]["price"].update(long_context_tiers=[
         {"min_input_tokens": 200000, "usd_per_million_input": "4.0000000001",
          "usd_per_million_output": "12", "usd_per_million_cache_read": "4",
          "usd_per_million_cache_write": "4"}]),
     "six decimal places"),
)


def self_test(source: dict) -> int:
    failures = []
    if audit(source):
        failures.append("the committed source does not pass its own audit")
    for name, break_it, phrase in _BREAKS:
        broken = copy.deepcopy(source)
        break_it(broken)
        findings = audit(broken)
        if not any(phrase in finding for finding in findings):
            failures.append(f"{name}: no finding mentioned {phrase!r}; got {findings}")
    if micros("0.44") != 440_000 or micros("0.014") != 14_000 or micros("0") != 0:
        failures.append("a published amount no longer converts to the micro-units it should")
    for failure in failures:
        print(f"catalog self-test: {failure}", file=sys.stderr)
    if failures:
        print(f"catalog self-test: {len(failures)} failure(s)", file=sys.stderr)
        return 1
    print(f"catalog self-test: {len(_BREAKS)} rules fire, and money converts exactly")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true", help="rewrite baseline.json")
    action.add_argument("--check", action="store_true", help="fail if baseline.json is stale")
    action.add_argument("--self-test", action="store_true", help="check the rules above")
    args = parser.parse_args(argv)

    root = repo_root()
    source = load(root)
    if args.self_test:
        return self_test(source)

    findings = audit(source)
    for finding in findings:
        print(f"catalog: {finding}", file=sys.stderr)
    if findings:
        print(f"catalog: {len(findings)} finding(s); nothing was generated", file=sys.stderr)
        return 1

    generated = render(source)
    target = output_path(root)
    relative = os.path.relpath(target, root)
    if args.write:
        with open(target, "w", encoding="utf-8") as handle:
            handle.write(generated)
        print(f"catalog: wrote {relative}")
        return 0

    if not os.path.exists(target):
        print(f"catalog: {relative} does not exist; run with --write", file=sys.stderr)
        return 1
    with open(target, encoding="utf-8") as handle:
        committed = handle.read()
    if committed != generated:
        print(
            f"catalog: {relative} is stale — it no longer matches "
            "taffy-core/components/intelligence/core/rust/model-router/catalog/source/. Run:\n"
            "    python3 taffy-core/components/intelligence/core/rust/model-router/"
            "catalog/tools/generate_baseline.py --write",
            file=sys.stderr,
        )
        return 1
    print(f"catalog: {relative} matches the source of truth")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
