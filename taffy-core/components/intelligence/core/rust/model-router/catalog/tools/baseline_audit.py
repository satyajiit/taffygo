#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The baseline source's vocabulary and its own rules, judged before render.

Split from `generate_baseline.py` along its one seam: everything here names
the closed vocabulary and judges the source against it, and everything there
renders. The generator imports `audit` and refuses to emit on any finding;
`tools/lib/catalog_publish.py` runs the same audit before a publish, so the
two consumers cannot drift on what a valid source is.
"""

from __future__ import annotations

import json
import os
import re

_PRICE = re.compile(r"^[0-9]+(\.[0-9]{1,6})?$")

#: Roles a model may only hold if it can call tools. EMBEDDING is absent on
#: purpose: retrieval support is not a task role and calls nothing.
TASK_ROLES = ("PRIMARY_REASONING", "FAST_BROWSING", "VISION")

ROLES = TASK_ROLES + ("EMBEDDING",)
WIRE_APIS = (
    "ANTHROPIC_MESSAGES",
    "OPEN_AI_RESPONSES",
    "OPEN_AI_COMPLETIONS",
    "GOOGLE_GENERATIVE_LANGUAGE",
    "OPEN_AI_CODEX_RESPONSES",
    "GOOGLE_CLOUD_CODE_ASSIST",
)
AUTH_METHODS = ("API_KEY", "OAUTH")
MODEL_SOURCES = ("STATIC_CATALOG", "DYNAMIC_LISTING")
BASES = ("METERED", "IMPLIED")
MODALITIES = ("TEXT", "IMAGE")
LEVELS = ("OFF", "MINIMAL", "LOW", "MEDIUM", "HIGH", "XHIGH", "MAX")

#: The four rates, in the order they are emitted, paired with the source key
#: each is converted from.
RATES = (
    ("input_micros_per_million", "usd_per_million_input"),
    ("output_micros_per_million", "usd_per_million_output"),
    ("cache_read_micros_per_million", "usd_per_million_cache_read"),
    ("cache_write_micros_per_million", "usd_per_million_cache_write"),
)

#: Every field of a model whose origin has to be stated. A number here is either
#: something the vendor published or something TaffyGo chose, and the source
#: says which.
ATTRIBUTED = (
    "context_window",
    "max_output_tokens",
    "input_modalities",
    "reasoning",
    "tool_calling",
    # What a model is offered *for* is almost never something a vendor states,
    # and it decides which model a turn reaches. Leaving it off this register
    # let the one field TaffyGo always decides be the one field nobody had to
    # justify.
    "roles",
    # The ladder is money-adjacent in the same way a price is: a rung the
    # vendor does not support is a request the provider refuses, and a rung
    # invented here is a refusal a person cannot explain. A model with no map
    # is still an answer and still has to be attributed to whoever gave it.
    "thinking_levels",
    "usd_per_million_input",
    "usd_per_million_output",
    "usd_per_million_cache_read",
    "usd_per_million_cache_write",
)

_HEADER_KEYS = {"schema_version", "catalog_version", "generated_at", "currency"}
_PROVIDER_KEYS = {
    "provider_id",
    "display_name",
    "wire_api",
    "default_endpoint",
    "auth_methods",
    "subscription",
    "model_source",
    "enabled",
    # Optional. The three setup facts a screen cannot derive (decision 0094
    # section 3); a vendor the catalog knows nothing extra about carries no
    # object at all rather than one full of empty strings.
    "presentation",
    # Optional, and independent of each other. One descriptor per vendor
    # (decision 0029 section 2), so a vendor a subscription reaches by another
    # family or at another address says so here rather than by being a second
    # row. A vendor with one way in carries neither.
    "oauth_wire_api",
    "oauth_endpoint",
    "provenance",
}
#: The provider keys that are optional, so the required set is the rest.
_PROVIDER_OPTIONAL = {"presentation", "oauth_wire_api", "oauth_endpoint"}
_MODEL_KEYS = {
    "model_id",
    "provider_id",
    # The four per-model overrides. Each is optional and each is independent:
    # a model whose vendor serves it at another family or another address says
    # so here rather than by being a second provider row, and a model whose
    # dialect or sampling differs from its family's says that here rather than
    # in adapter code (decision 0015). A row that overrides nothing renders
    # exactly as it did before these columns existed.
    "wire_api",
    "endpoint",
    "compat",
    "sampling_defaults",
    "display_name",
    "roles",
    "input_modalities",
    "reasoning",
    "tool_calling",
    "context_window",
    "max_output_tokens",
    "thinking_levels",
    "price",
    "enabled",
    "provenance",
}
#: The model keys that are optional, so the required set is the rest. The four
#: overrides sit here rather than in the required set because a row that states
#: none of them is complete, not unfinished.
_MODEL_OPTIONAL = {
    "thinking_levels",
    "provenance",
    "wire_api",
    "endpoint",
    "compat",
    "sampling_defaults",
}
#: Attributed only when the row carries them. They are origin-bearing in the
#: same way a price is — an override decides which address a person's key is
#: spent at — so a row that states one must say where it came from. Adding them
#: to ATTRIBUTED outright would demand that all forty-seven existing rows
#: justify four fields they do not carry.
ATTRIBUTED_WHEN_PRESENT = (
    "wire_api",
    "endpoint",
    "compat",
    "sampling_defaults",
)
#: The decoder's own bounds on an override map, mirrored here so the source is
#: refused where it is written rather than where it is read
#: (src/catalog/decode/limits.rs: MAX_OVERRIDES, MAX_OVERRIDE_LEN).
_MAX_OVERRIDES = 32
_MAX_OVERRIDE_LEN = 256
_PRICE_KEYS = {"snapshot_version", "basis"} | {key for _, key in RATES}
#: A long-context tier re-prices the whole request once total input reaches its
#: threshold. Optional, because most models publish one flat rate; when
#: present, every tier carries all four rates in the same vendor units as the
#: base price, and thresholds are strictly ascending because the model router
#: refuses any other order.
_TIER_KEYS = {"min_input_tokens"} | {key for _, key in RATES}
_MODEL_PROVENANCE_KEYS = {
    "quoted_from",
    "quoted_on",
    "vendor_published",
    "taffygo_chosen",
    "note",
}


def _keys(where: str, value: dict, allowed: set[str], required: set[str]) -> list[str]:
    findings = [f"{where}: unknown field {name!r}" for name in sorted(set(value) - allowed)]
    findings += [f"{where}: missing field {name!r}" for name in sorted(required - set(value))]
    return findings


def _member(where: str, field: str, value, allowed: tuple[str, ...]) -> list[str]:
    if value not in allowed:
        return [f"{where}: {field} is {value!r}, not one of {', '.join(allowed)}"]
    return []


def audit_header(header: dict) -> list[str]:
    findings = _keys("catalog.json", header, _HEADER_KEYS, _HEADER_KEYS)
    if findings:
        return findings
    if header["schema_version"] != 1:
        findings.append("catalog.json: schema_version is not 1")
    if header["currency"] != "USD":
        findings.append("catalog.json: currency is not USD; the credit unit is defined in USD")
    if not re.match(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$", str(header["generated_at"])):
        findings.append("catalog.json: generated_at is not a canonical UTC timestamp")
    if not str(header["catalog_version"]):
        findings.append("catalog.json: catalog_version is empty")
    return findings


def audit_provider(entry: dict) -> list[str]:
    where = f"provider {entry.get('provider_id', '?')!r}"
    findings = _keys(where, entry, _PROVIDER_KEYS, _PROVIDER_KEYS - _PROVIDER_OPTIONAL)
    if findings:
        return findings
    findings += _member(where, "wire_api", entry["wire_api"], WIRE_APIS)
    findings += _member(where, "model_source", entry["model_source"], MODEL_SOURCES)
    if not entry["auth_methods"]:
        findings.append(f"{where}: declares no auth method")
    for method in entry["auth_methods"]:
        findings += _member(where, "auth_methods", method, AUTH_METHODS)
    if not str(entry["default_endpoint"]).startswith("https://"):
        findings.append(f"{where}: default_endpoint is not an absolute https URL")
    findings += audit_presentation(where, entry.get("presentation"))
    findings += audit_oauth_reach(where, entry)
    if not str(entry["provenance"].get("decided_by", "")):
        findings.append(f"{where}: provenance names no decision record")
    return findings


def audit_oauth_reach(where: str, entry: dict) -> list[str]:
    """The second way in, when a row declares one.

    Both halves are optional and independent, so neither implies the other.
    What a row may not do is describe a way in it does not accept: an OAuth
    family or an OAuth address on a provider that declares no OAUTH method is
    a fact nothing will ever read, and the reason it is a finding rather than
    dead data is that the row it belongs on is probably a different one.
    """
    findings: list[str] = []
    family = entry.get("oauth_wire_api")
    endpoint = entry.get("oauth_endpoint")
    if family is not None:
        findings += _member(where, "oauth_wire_api", family, WIRE_APIS)
    if endpoint is not None and not str(endpoint).startswith("https://"):
        findings.append(f"{where}: oauth_endpoint is not an absolute https URL")
    if (family is not None or endpoint is not None) and "OAUTH" not in entry["auth_methods"]:
        findings.append(
            f"{where}: describes how a subscription reaches it but does not accept OAUTH, "
            "so nothing would ever read either field"
        )
    return findings


def audit_price(where: str, price: dict) -> list[str]:
    findings = _keys(where, price, _PRICE_KEYS | {"long_context_tiers"}, _PRICE_KEYS)
    if findings:
        return findings
    findings += _member(where, "basis", price["basis"], BASES)
    for _, key in RATES:
        if not _PRICE.match(str(price[key])):
            findings.append(
                f"{where}: {key} is {price[key]!r}, which is not a plain amount with "
                "at most six decimal places"
            )
    previous_threshold = 0
    for index, tier in enumerate(price.get("long_context_tiers", [])):
        tier_where = f"{where} tier {index}"
        tier_findings = _keys(tier_where, tier, _TIER_KEYS, _TIER_KEYS)
        findings += tier_findings
        if tier_findings:
            continue
        threshold = tier["min_input_tokens"]
        if not isinstance(threshold, int) or threshold <= previous_threshold:
            findings.append(
                f"{tier_where}: min_input_tokens is {threshold!r}; thresholds must be "
                "positive integers, strictly ascending, because the model router refuses "
                "any other order"
            )
        else:
            previous_threshold = threshold
        for _, key in RATES:
            if not _PRICE.match(str(tier[key])):
                findings.append(
                    f"{tier_where}: {key} is {tier[key]!r}, which is not a plain amount "
                    "with at most six decimal places"
                )
    return findings


#: The presentation fields that are addresses, and are held to the decoder's
#: own rule for one (`https_link` in src/catalog/decode/provider.rs).
_PRESENTATION_LINKS = ("get_key_url", "docs_url")


def audit_presentation(where: str, presentation) -> list[str]:
    """The setup facts a screen cannot derive, judged where they are written.

    This exists because the two halves disagreed once and the disagreement was
    expensive to find. The decoder refuses a presentation link carrying a
    fragment or a space, and this audit did not look at presentation at all —
    so a row naming a vendor console's `#/api-key` deep link passed every host
    gate, was written into the baseline, and failed in a Rust test with a
    message about an https URL that named no field a person had typed. A rule
    the decoder enforces is a rule the source has to be judged against.
    """
    if presentation is None:
        return []
    if not isinstance(presentation, dict):
        return [f"{where}: presentation is not a set of named values"]
    findings = []
    for field in _PRESENTATION_LINKS:
        value = presentation.get(field)
        if value is None:
            continue
        text = str(value)
        if not text.startswith("https://") or "#" in text or " " in text:
            findings.append(
                f"{where}: presentation.{field} is not an https address the "
                "decoder accepts — no fragment and no space")
    return findings


def audit_provenance(where: str, provenance: dict, attributed: tuple[str, ...]) -> list[str]:
    findings = _keys(where, provenance, _MODEL_PROVENANCE_KEYS, _MODEL_PROVENANCE_KEYS - {"note"})
    if findings:
        return findings
    published = set(provenance["vendor_published"])
    chosen = set(provenance["taffygo_chosen"])
    both = sorted(published & chosen)
    missing = sorted(set(attributed) - published - chosen)
    unknown = sorted((published | chosen) - set(attributed))
    for name in both:
        findings.append(f"{where}: {name} is attributed to the vendor and to TaffyGo at once")
    for name in missing:
        findings.append(f"{where}: {name} is attributed to nobody")
    for name in unknown:
        findings.append(f"{where}: {name!r} is attributed but is not a field of a model")
    for name, reason in sorted(provenance["taffygo_chosen"].items()):
        if len(str(reason)) < 20:
            findings.append(f"{where}: the reason for choosing {name} says nothing")
    if not str(provenance["quoted_from"]).startswith("https://"):
        findings.append(f"{where}: quoted_from is not a URL")
    if not re.match(r"^\d{4}-\d{2}-\d{2}$", str(provenance["quoted_on"])):
        findings.append(f"{where}: quoted_on is not a date")
    return findings


def audit_model(entry: dict, provider_ids: set[str]) -> list[str]:
    where = f"model {entry.get('model_id', '?')!r}"
    findings = _keys(where, entry, _MODEL_KEYS, _MODEL_KEYS - _MODEL_OPTIONAL)
    if findings:
        return findings
    if entry["provider_id"] not in provider_ids:
        findings.append(f"{where}: names provider {entry['provider_id']!r}, which is not declared")
    if not entry["roles"]:
        findings.append(f"{where}: serves no role")
    for role in entry["roles"]:
        findings += _member(where, "roles", role, ROLES)
    for modality in entry["input_modalities"]:
        findings += _member(where, "input_modalities", modality, MODALITIES)
    levels = entry.get("thinking_levels", {})
    if levels is None:
        # Written as JSON null rather than omitted. The two are not the same
        # thing and only one is a document: the decoder reads an absent map as
        # "this model takes the adapter's defaults" and has nowhere to put a
        # null one. Judged here rather than left to crash this audit with a
        # traceback, which is what it did — and a traceback is not a finding,
        # so it names no row and reads as a broken tool.
        findings.append(
            f"{where}: thinking_levels is null. A model with no ladder omits "
            "the field; null is not the same document")
        levels = {}
    for level in levels:
        findings += _member(where, "thinking_levels", level, LEVELS)

    # The four overrides, each checked against the same rule its provider-row
    # counterpart obeys, because the decoder applies them at the same bounds.
    if "wire_api" in entry:
        findings += _member(where, "wire_api", entry["wire_api"], WIRE_APIS)
    if "endpoint" in entry:
        endpoint = str(entry["endpoint"])
        if not endpoint.startswith("https://"):
            findings.append(f"{where}: endpoint is not an https address")
    for field in ("compat", "sampling_defaults"):
        overrides = entry.get(field)
        if overrides is None:
            continue
        if not isinstance(overrides, dict):
            findings.append(f"{where}: {field} is not a set of named values")
            continue
        # An empty map is a row that overrides nothing while saying it does,
        # which the provenance register would then demand a reason for.
        if not overrides:
            findings.append(f"{where}: {field} is stated and empty")
        if len(overrides) > _MAX_OVERRIDES:
            findings.append(
                f"{where}: {field} names {len(overrides)} values, over the {_MAX_OVERRIDES} "
                "the decoder accepts"
            )
        for name, value in sorted(overrides.items()):
            if not isinstance(value, str):
                findings.append(f"{where}: {field}.{name} is not text")
            elif len(value) > _MAX_OVERRIDE_LEN:
                findings.append(
                    f"{where}: {field}.{name} is longer than the {_MAX_OVERRIDE_LEN} bytes "
                    "the decoder accepts"
                )

    # The rule this generator exists to make unforgettable.
    tasks = sorted(set(entry["roles"]) & set(TASK_ROLES))
    if tasks and not entry["tool_calling"]:
        findings.append(
            f"{where}: is cataloged for {', '.join(tasks)} but cannot call tools. "
            "A model that cannot call tools is not cataloged for a task role at all."
        )
    if "VISION" in entry["roles"] and "IMAGE" not in entry["input_modalities"]:
        findings.append(f"{where}: is cataloged for VISION but does not accept images")
    if not 0 < entry["max_output_tokens"] <= entry["context_window"]:
        findings.append(f"{where}: max_output_tokens does not fit inside context_window")

    findings += audit_price(f"{where} price", entry["price"])
    # An override is attributed only by the rows that state one, so the
    # register this row is judged against is the always-required set plus
    # whichever overrides it carries.
    attributed = ATTRIBUTED + tuple(
        name for name in ATTRIBUTED_WHEN_PRESENT if name in entry
    )
    findings += audit_provenance(f"{where} provenance", entry.get("provenance", {}), attributed)
    return findings


def audit(source: dict) -> list[str]:
    """Everything wrong with the source, in one pass. Empty means emittable."""
    findings = audit_header(source["catalog"])
    seen: set[str] = set()
    for entry in source["providers"]:
        findings += audit_provider(entry)
        if entry.get("provider_id") in seen:
            findings.append(f"provider {entry['provider_id']!r} is declared twice")
        seen.add(entry.get("provider_id"))
    keys: set[tuple[str, str]] = set()
    for entry in source["models"]:
        findings += audit_model(entry, seen)
        key = (entry.get("provider_id"), entry.get("model_id"))
        if key in keys:
            findings.append(f"model {key[1]!r} is declared twice under {key[0]!r}")
        keys.add(key)
    return findings


# --- emitting ---------------------------------------------------------------
