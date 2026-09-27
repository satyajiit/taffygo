#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Turns a snapshot of the pi model catalog into rows of this catalog's source.

Decision 0118 admits `https://pi.dev/api/models/providers/<id>` as a source of
record for a model row. This tool is the only thing that reads it, and what it
writes is ordinary source: `generate_baseline.py --check` judges the result
against exactly the rules a hand-written row is judged against, and nothing
here is exempt from them.

Three properties are the point of it being a tool rather than a paste.

**It is additive and never destructive.** A provider id this catalog already
declares keeps its own row, and a model id it already carries keeps its own —
including its provenance, which is usually a vendor page read by hand and is
better evidence than this. The tool adds what is missing and reports what it
left alone.

**It refuses rather than approximates.** A model served on a wire family this
product has no adapter for, a base URL carrying a substitution placeholder, a
provider whose recorded origin disagrees with the one the snapshot names — each
is skipped by name with the reason printed. The failure this avoids is a row
that looks reachable and is not: decision 0094 section 1 is explicit that such
a row is worse than no row, because its failure is a plausible 404.

**It reaches the network only when asked.** `--fetch` writes a snapshot;
every other mode reads one from disk. No gate runs this, and no build does.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pi_catalog_register import FAMILIES, PLACEHOLDERS, PROVIDERS, REFUSED

_HERE = os.path.dirname(os.path.abspath(__file__))
_CATALOG = os.path.dirname(_HERE)
_SOURCE = os.path.join(_CATALOG, "source")

#: Where a snapshot is taken from, one provider per request.
PI_PROVIDER_URL = "https://pi.dev/api/models/providers/{provider}"
PI_PROVIDER_INDEX = "https://pi.dev/api/models/providers"

def price(value):
    """A rate, in the spelling the source's own price pattern accepts.

    The snapshot serves rates as JSON numbers, so a rate that is exactly three
    arrives as 2.9999999999999996. Formatting to six places and trimming is
    what recovers the figure the vendor published rather than the one binary
    floating point could represent; six is the source pattern's own bound.
    """
    if value is None:
        return None
    number = float(value)
    if number < 0:
        return None
    text = f"{number:.6f}".rstrip("0").rstrip(".")
    return text or "0"


def roles_for(reasoning, vision):
    """The roles a model is offered for, by this product's own standing rule.

    Deliberately the same rule `roles_for` in the core's `user_catalog.rs`
    applies to a model read from a provider's own listing, and for the same
    reasons written there: a model that thinks is the one to plan with and only
    that, because the fast rung is latency-sensitive and a thinking phase is
    the opposite of low latency; a model that does not think serves both rungs,
    because somebody whose only server has one such model must still be able to
    run a task; and accepting a picture is added to whatever else the model
    does rather than substituted for it. One rule in two places beats two
    rules, and a role invented per provider here would be exactly the kind of
    judgement this tool must not make.
    """
    roles = ["PRIMARY_REASONING"] if reasoning else ["FAST_BROWSING", "PRIMARY_REASONING"]
    if vision:
        roles.append("VISION")
    return roles


def thinking_levels(model):
    """The rung map, in this catalog's spelling, or nothing.

    A rung the snapshot maps to `null` is one the source calls unsupported and
    is carried as such: the decoder reads a null as "this rung is not offered"
    and dropping the key instead would say something different — that nobody
    stated anything about it.

    A model that does not think carries no map at all, whatever the snapshot
    says about it. The catalog's own validator refuses a non-reasoning model
    that cannot turn thinking *off* (`NonReasoningModelCannotDisableThinking`),
    and the snapshot has rows — OpenAI's two chat-latest aliases among them —
    that state no reasoning and then map the off rung to unsupported. Both
    halves cannot be true, and the reasoning flag is the one the router acts
    on, so the map is the half dropped.
    """
    if not model.get("reasoning"):
        return None
    raw = model.get("thinkingLevelMap")
    if not isinstance(raw, dict) or not raw:
        return None
    out = {}
    for level, mapped in raw.items():
        key = level.upper()
        if mapped is None:
            out[key] = None
        elif isinstance(mapped, str) and mapped:
            out[key] = mapped
    return out or None


def model_row(entry, imported, snapshot_date, plan_backed):
    """One source row, or a reason it was refused."""
    model_id = entry.get("id")
    if not isinstance(model_id, str) or not model_id:
        return None, "the snapshot row names no model id"
    family = FAMILIES.get(entry.get("api"))
    if family is None:
        return None, f"served on {entry.get('api')!r}, a family with no adapter"
    base_url = entry.get("baseUrl") or ""
    if any(mark in base_url for mark in PLACEHOLDERS):
        return None, f"its base URL {base_url!r} substitutes a value"
    if not base_url.startswith(imported.origin):
        return None, (f"its base URL {base_url!r} is not beneath the origin "
                      f"{imported.origin!r} this row records")

    context_window = entry.get("contextWindow")
    max_output = entry.get("maxTokens")
    if not isinstance(context_window, int) or context_window <= 0:
        return None, "it states no context window"
    if not isinstance(max_output, int) or max_output <= 0:
        return None, "it states no answer allowance"
    # The snapshot reports an allowance equal to the window wherever the vendor
    # applies no separate ceiling. Carried as it is when it fits, clamped when
    # it does not, because the source refuses a row whose answer cannot fit in
    # its own context.
    max_output = min(max_output, context_window)

    cost = entry.get("cost") or {}
    rates = {
        "usd_per_million_input": price(cost.get("input")),
        "usd_per_million_output": price(cost.get("output")),
        "usd_per_million_cache_read": price(cost.get("cacheRead")),
        "usd_per_million_cache_write": price(cost.get("cacheWrite")),
    }
    stated = sorted(name for name, value in rates.items() if value is not None)
    if rates["usd_per_million_input"] is None or rates["usd_per_million_output"] is None:
        return None, "it states no input or no output rate"
    for name in ("usd_per_million_cache_read", "usd_per_million_cache_write"):
        if rates[name] is None:
            rates[name] = rates["usd_per_million_input"]
    # A plan-backed row prices what a call was *worth*, because nobody is
    # billed for it, and the source reports zero for some of them — its own
    # upstream has no rate to give where a subscription pays. Zero is not an
    # imputed value: it is a row that would let a plan-backed turn spend no
    # budget at all, which is the one thing the catalog's baseline test refuses
    # by name. There is nothing to impute from here, so the row is refused.
    if plan_backed and (rates["usd_per_million_input"] == "0"
                        or rates["usd_per_million_output"] == "0"):
        return None, ("it is reached on a plan and the source states no rate to "
                      "impute from, so a turn against it would spend nothing")

    modalities = entry.get("input") or ["text"]
    vision = "image" in modalities
    reasoning = bool(entry.get("reasoning"))

    row = {
        "model_id": model_id,
        "provider_id": imported.provider_id,
        "display_name": (entry.get("name") or model_id)[:128],
        "roles": roles_for(reasoning, vision),
        "input_modalities": ["TEXT", "IMAGE"] if vision else ["TEXT"],
        "reasoning": reasoning,
        "tool_calling": True,
        "context_window": context_window,
        "max_output_tokens": max_output,
    }
    if family != imported.wire_api:
        row["wire_api"] = family
    levels = thinking_levels(entry)
    if levels is not None:
        row["thinking_levels"] = levels

    # A row on a provider reached with a plan says its price was imputed, not
    # metered: the person already paid, so a per-token figure here is this
    # product's estimate of what the call was worth and never a meter reading.
    # The catalog's own baseline test refuses the other spelling by name, and
    # it is right to — a METERED row at a rate nobody is billed feeds a spend
    # cap that would then enforce a fiction.
    row["price"] = {
        "snapshot_version": f"pi-{snapshot_date}",
        "basis": "IMPLIED" if plan_backed else "METERED",
        "usd_per_million_input": rates["usd_per_million_input"],
        "usd_per_million_output": rates["usd_per_million_output"],
        "usd_per_million_cache_read": rates["usd_per_million_cache_read"],
        "usd_per_million_cache_write": rates["usd_per_million_cache_write"],
    }
    row["enabled"] = True

    chosen = {
        "roles": (
            "The source states no role taxonomy. These are this product's own "
            "standing rule for a model whose capabilities were read rather than "
            "assigned, the same rule roles_for applies in the core's "
            "user_catalog.rs: a thinking model plans and does not serve the "
            "latency-sensitive rung, a model that does not think serves both, "
            "and image input adds the vision role rather than replacing one."),
    }
    for name in ("usd_per_million_cache_read", "usd_per_million_cache_write"):
        if name not in stated:
            chosen[name] = (
                "The source states no rate for this, so it is recorded at the "
                "ordinary input rate rather than at a discount nobody "
                "published — the higher of the two readings, as decision 0094 "
                "section 2 requires.")
    if levels is None:
        chosen["thinking_levels"] = (
            "No map is carried, because the source states no rung mapping for "
            "this model. A ladder here would be rungs this product invented, "
            "and a rung the provider does not accept is a refusal nobody can "
            "explain.")
    if max_output != entry.get("maxTokens"):
        chosen["max_output_tokens"] = (
            "The source states an answer allowance larger than the window it "
            "states beside it, which cannot both be true. The allowance is "
            "recorded at the window, which is the only reading under which the "
            "row means anything.")

    published = ["context_window", "input_modalities", "reasoning", "tool_calling",
                 "usd_per_million_input", "usd_per_million_output"]
    if "max_output_tokens" not in chosen:
        published.append("max_output_tokens")
    if levels is not None:
        published.append("thinking_levels")
    published += [name for name in stated
                  if name.startswith("usd_per_million_cache")]

    note = (
        "Imported by import_pi_catalog.py from the pi catalog under decision "
        "0118, snapshot taken on " + snapshot_date + ". tool_calling is stated "
        "by the source set rather than by this row: that catalog's generator "
        "admits a model only where its own upstream states tool_call is true, "
        "so every model it publishes is one whose tools support was asserted "
        "before it was listed. The wire family, the context window, the answer "
        "allowance, the input modalities, the reasoning flag and every rate "
        "come from the same snapshot row.")
    if "wire_api" in row:
        chosen["wire_api"] = (
            "The source serves this model on a different family from the one "
            "its provider row records, and this catalog says so on the model "
            "rather than by adding a second descriptor for one vendor "
            "(decision 0029 section 2).")

    row["provenance"] = {
        "quoted_from": PI_PROVIDER_URL.format(provider=imported.pi_id),
        "quoted_on": snapshot_date,
        "vendor_published": sorted(set(published)),
        "taffygo_chosen": dict(sorted(chosen.items())),
        "note": note,
    }
    return row, None


def provider_row(imported, snapshot_date):
    presentation = {}
    if imported.get_key_url:
        presentation["get_key_url"] = imported.get_key_url
    if imported.docs_url:
        presentation["docs_url"] = imported.docs_url
    path = ""
    if imported.prefix or imported.version:
        path = (
            " The vendor serves this family beneath "
            f"{imported.prefix + (imported.version or '/v1')!r}, so this row is "
            "paired with an entry in kProviderPathPrefixes in "
            "taffy-core/browser/model/profile_model_broker_routes.cc.")
    row = {
        "provider_id": imported.provider_id,
        "display_name": imported.display_name,
        "wire_api": imported.wire_api,
        "default_endpoint": imported.origin,
        "auth_methods": ["API_KEY"],
        "subscription": imported.subscription,
        "model_source": "STATIC_CATALOG",
        "enabled": True,
    }
    if presentation:
        row["presentation"] = presentation
    row["provenance"] = {
        "decided_by": "docs/decisions/0118-the-model-catalog-is-imported-from-a-source-of-record.md",
        "note": (
            "Added by import_pi_catalog.py under decision 0118 from a pi "
            f"catalog snapshot taken on {snapshot_date}." + path +
            " The address was reached before the row was written: an "
            "unauthenticated POST to the address this family composes answered "
            "with the vendor's own error rather than a 404, which is what "
            "decision 0094 section 1 asks of a row before it ships. One "
            "descriptor for the vendor, as decision 0029 section 2 requires. No "
            "key_prefix is recorded, because the source states none and this "
            "catalog does not invent one."),
    }
    return row


def load_snapshot(directory):
    snapshot = {}
    for imported in PROVIDERS:
        path = os.path.join(directory, imported.pi_id + ".json")
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as handle:
            body = json.load(handle)
        rows = body.get("models") if isinstance(body, dict) and "models" in body else body
        if isinstance(rows, dict):
            rows = list(rows.values())
        snapshot[imported.pi_id] = [row for row in (rows or []) if isinstance(row, dict)]
    return snapshot


#: The service answers 403 to a request that names no agent, so one is named.
#: Nothing about this identifies a person or a machine; it says which tool is
#: asking, which is what a service refusing anonymous automation is asking for.
USER_AGENT = "TaffyGo-catalog-import/1 (+https://taffygo.com)"


def _get(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


def fetch(directory):
    os.makedirs(directory, exist_ok=True)
    index = json.loads(_get(PI_PROVIDER_INDEX))
    for provider in index:
        url = PI_PROVIDER_URL.format(provider=provider)
        try:
            body = _get(url)
        except Exception as error:  # noqa: BLE001 - reported, not raised
            print(f"  {provider}: not fetched ({error})")
            continue
        with open(os.path.join(directory, provider + ".json"), "wb") as handle:
            handle.write(body)
    unknown = sorted(set(index) - {p.pi_id for p in PROVIDERS} - set(REFUSED))
    print(f"fetched {len(index)} providers into {directory}")
    if unknown:
        print("providers named by neither the import table nor the refusal "
              "register: " + ", ".join(unknown))
    return 1 if unknown else 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", default=os.path.join(_CATALOG, "pi-snapshot"),
                        help="directory of one JSON document per pi provider")
    parser.add_argument("--self-test", action="store_true",
                        help="prove every refusal and mapping, reaching nothing")
    parser.add_argument("--fetch", action="store_true",
                        help="refresh the snapshot from pi.dev, then stop")
    parser.add_argument("--write", action="store_true",
                        help="write the added rows into the catalog source")
    parser.add_argument("--date", default=None,
                        help="the date the snapshot was taken, YYYY-MM-DD")
    args = parser.parse_args(argv)

    if args.self_test:
        # Imported here rather than at the top: the self-test is the only
        # caller, and a tool that fetches should not carry its own proof's
        # fixtures into every run.
        import pi_catalog_self_test

        return pi_catalog_self_test.run(model_row)

    if args.fetch:
        return fetch(args.snapshot)

    if not os.path.isdir(args.snapshot):
        print(f"no snapshot at {args.snapshot}; run with --fetch first")
        return 2
    snapshot_date = args.date
    if not snapshot_date:
        print("--date is required: a row records when its source was read")
        return 2

    with open(os.path.join(_SOURCE, "providers.json"), encoding="utf-8") as handle:
        providers = json.load(handle)
    with open(os.path.join(_SOURCE, "models.json"), encoding="utf-8") as handle:
        models = json.load(handle)
    have_providers = {row["provider_id"] for row in providers}
    # What the catalog already says about each provider it declares, so a model
    # added to one is priced the way that row is reached rather than the way
    # this tool's own table guesses.
    plan_backed = {row["provider_id"]: bool(row.get("subscription"))
                   for row in providers}
    have_models = {(row["provider_id"], row["model_id"]) for row in models}

    snapshot = load_snapshot(args.snapshot)
    added_providers, added_models, refused = [], [], []
    for imported in PROVIDERS:
        entries = snapshot.get(imported.pi_id)
        if entries is None:
            refused.append((imported.pi_id, "*", "absent from the snapshot"))
            continue
        rows = []
        for entry in entries:
            row, reason = model_row(entry, imported, snapshot_date,
                                    plan_backed.get(imported.provider_id,
                                                    imported.subscription))
            if row is None:
                refused.append((imported.pi_id, entry.get("id", "?"), reason))
                continue
            if (row["provider_id"], row["model_id"]) in have_models:
                continue
            rows.append(row)
        if not rows:
            continue
        if imported.provider_id not in have_providers:
            if imported.existing:
                refused.append((imported.pi_id, "*",
                                "marked as already carried, but this catalog "
                                "declares no such provider"))
                continue
            added_providers.append(provider_row(imported, snapshot_date))
            have_providers.add(imported.provider_id)
        added_models.extend(rows)
        for row in rows:
            have_models.add((row["provider_id"], row["model_id"]))

    print(f"providers added: {len(added_providers)}   models added: {len(added_models)}")
    by_provider = {}
    for row in added_models:
        by_provider[row["provider_id"]] = by_provider.get(row["provider_id"], 0) + 1
    for provider_id, count in sorted(by_provider.items()):
        mark = "new" if provider_id in {row["provider_id"] for row in added_providers} else "   "
        print(f"  {mark} {provider_id:<28} {count}")
    if refused:
        print(f"refused: {len(refused)}")
        for pi_id, model_id, reason in refused[:40]:
            print(f"  {pi_id}/{model_id}: {reason}")
        if len(refused) > 40:
            print(f"  ... and {len(refused) - 40} more")

    if not args.write:
        print("nothing written; pass --write to add these rows")
        return 0

    providers.extend(added_providers)
    models.extend(added_models)
    providers.sort(key=lambda row: row["provider_id"])
    models.sort(key=lambda row: (row["provider_id"], row["model_id"]))
    for path, payload in ((os.path.join(_SOURCE, "providers.json"), providers),
                          (os.path.join(_SOURCE, "models.json"), models)):
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
    print("written; run generate_baseline.py --check next")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
