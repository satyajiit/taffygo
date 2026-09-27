#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Four tables decide a subscription sign-in, and only three of them agree.

WHY THIS EXISTS

A vendor sign-in works only when four separate places say the same thing, and
each is written in a different language because each owns a different facet:

  * the **catalog** decides whether the vendor is offered at all — whether its
    row declares `OAUTH`, and whether it ships switched on
    (`model-router/catalog/source/providers.json`);
  * the **isolated core** decides whether a row is actionable, from a compiled
    list of the vendors this binary can run a flow for
    (`core-runtime/src/composition/provider_catalog.rs`, `SIGN_IN_VENDORS`);
  * the **Android flow map** carries each flow's shape and its acknowledgement
    obligation (`ProviderSignInFlows.kt`);
  * the **browser process** carries the flow's shape again, its pinned origins
    and its client identity
    (`providerauth/provider_auth_configuration.cc`, `kVendors`).

Decision 0081 keeps the last three compiled rather than served on purpose — a
served document that could move a token endpoint would be exfiltrating refresh
tokens — and says a sign-in "works only when all three agree". That sentence is
true of the three and was false of the system, because the catalog is a fourth
table and nothing compared it against them. The three named seven vendors and
agreed exactly; the catalog offered `OAUTH` to one of the seven. Five compiled,
tested, complete sign-in flows were unreachable, and every gate was green.

So the rule this file enforces is stronger than "the compiled tables match":

  * a vendor with a compiled flow whose catalog row does not offer `OAUTH` is
    a **failure**. Nothing can start it, and nothing says so;
  * a catalog row offering `OAUTH` for a vendor with no compiled flow is also
    a failure, and the worse of the two — it renders a sign-in control that
    cannot start, so it fails in a person's hands rather than in a gate.

A row switched off (`enabled: false`) is the one deliberate state, and it is
recognised by name rather than skipped: see `deliberate_states` below.

This is a static reader. It parses each table where it lives rather than
generating any of them from one source, because generating three languages from
one table would put a build step between a security-relevant list and the person
reading it — and the reason these lists are compiled is that a reader can see
them. Reading source text means the extraction can silently match nothing, which
would turn this into a gate that always passes, so every extractor refuses
rather than returns empty, and `--self-test` proves it on a file with no table.
That proof lives in `vendor_agreement_self_test.py`, which reads no repository
file at all, so a failure there is about the comparisons rather than the tree.

Stdlib only, no checkout, no network.

    check_vendor_agreement.py             compare and report
    check_vendor_agreement.py --self-test prove every comparison can fail

Exit status: 0 agree, 1 disagree.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

_ROOT_MARKERS = ("chromium/REVISION", "TOOLCHAIN.md", "Cargo.toml")

CATALOG = (
    "taffy-core/components/intelligence/core/rust/model-router/catalog/source/providers.json"
)
CORE = (
    "taffy-core/components/intelligence/core/rust/core-runtime/src/composition/"
    "provider_catalog.rs"
)
KOTLIN = (
    "taffy-core/ui/android/core/providerauth/src/main/kotlin/com/taffygo/browser/ui/core/"
    "providerauth/ProviderSignInFlows.kt"
)
BROWSER = "taffy-core/browser/providerauth/provider_auth_configuration.cc"

#: One flow shape, spelled as each of the two languages spells it. A shape in
#: one table and not this map is a finding rather than a pass, because the two
#: spellings cannot be compared without a rule saying they are the same.
FLOW_KINDS = {"PKCE": "kPkce", "DEVICE_CODE": "kDeviceCode"}

#: The order the four edits are made in, quoted in every failure so the finding
#: names the remedy rather than only the symptom.
_ORDER = (
    "adding or enabling a vendor is a change to its catalog row, the core's "
    "SIGN_IN_VENDORS, the Android flow map and the browser's vendor table"
)


class TableUnreadable(Exception):
    """A table could not be found where it lives.

    Its own class rather than a bare exit, because "the extractor matched
    nothing" and "the tables disagree" are different failures and only the
    second is about the product. A regex that quietly matched nothing would
    make this file a gate that always passes, which is the exact defect it
    exists to prevent, so every extractor raises this instead of returning an
    empty set.
    """


def repository_root() -> str:
    root = os.path.dirname(os.path.realpath(__file__))
    while True:
        if all(os.path.exists(os.path.join(root, marker)) for marker in _ROOT_MARKERS):
            return root
        parent = os.path.dirname(root)
        if parent == root:
            raise SystemExit("catalog vendor agreement: could not find the repository root")
        root = parent


# --- reading the four tables --------------------------------------------------


def catalog_rows(text: str) -> dict[str, tuple[bool, bool]]:
    """Each provider row, as `(offers OAUTH, ships switched on)`."""
    rows = json.loads(text)
    if not isinstance(rows, list) or not rows:
        raise TableUnreadable(f"{CATALOG}: no provider rows to compare against")
    return {
        row["provider_id"]: ("OAUTH" in row.get("auth_methods", []), bool(row.get("enabled")))
        for row in rows
    }


def core_vendors(text: str) -> set[str]:
    """The compiled list the isolated core reads actionability from.

    The declared array length is compared against the entries found, so a
    partial match — the shape a loosened regex takes — is a named failure
    rather than a shorter set that quietly agrees with less.
    """
    match = re.search(r"const SIGN_IN_VENDORS:\s*\[&str;\s*(\d+)\]\s*=\s*\[(.*?)\];", text, re.S)
    if match is None:
        raise TableUnreadable(
            f"{CORE}: no SIGN_IN_VENDORS array found. If it was renamed, rename it here "
            "too rather than dropping the check"
        )
    declared = int(match.group(1))
    found = re.findall(r'"([^"]+)"', match.group(2))
    if len(found) != declared:
        raise TableUnreadable(
            f"{CORE}: SIGN_IN_VENDORS declares {declared} entries and this read {len(found)}"
        )
    return set(found)


#: Everything between one `ProviderSignInFlow(` and the next, and no further —
#: a lazy `.*?` across a multi-entry file will happily match the shape from one
#: row and the flag from the row after it, which is the silent mis-comparison
#: this file exists to prevent.
_WITHIN_ONE_ENTRY = r"(?:(?!ProviderSignInFlow\().)*?"

_KOTLIN_ENTRY = re.compile(
    r'ProviderSignInFlow\(\s*"([^"]+)"'
    + _WITHIN_ONE_ENTRY
    + r"ProviderFlowKind\.(\w+)"
    + _WITHIN_ONE_ENTRY
    + r"browserWillStart\s*=\s*(true|false)",
    re.S,
)


def kotlin_flows(text: str) -> dict[str, tuple[str, bool]]:
    """Each vendor's flow shape and whether the browser will start it.

    Two facts, because the flow map answers two different questions and a
    surface needs both: `ProviderFlowKind` is what this binary would run, and
    `browserWillStart` is whether the browser process would let it begin. A row
    can carry a complete flow and still be refused — see
    `_kotlin_agrees_about_starting`.
    """
    entries = _KOTLIN_ENTRY.findall(text)
    declared = text.count("ProviderSignInFlow(")
    if not entries:
        raise TableUnreadable(f"{KOTLIN}: no ProviderSignInFlow entries found")
    if len(entries) != declared:
        raise TableUnreadable(
            f"{KOTLIN}: the file names ProviderSignInFlow {declared} times and this read "
            f"{len(entries)} complete entries. A row is missing a flow shape or a "
            "browserWillStart, or the entry shape changed — fix the row or this reader "
            "rather than comparing the ones that happened to parse"
        )
    return {vendor: (kind, flag == "true") for vendor, kind, flag in entries}


#: A `kVendors` row is an aggregate initialiser with no field names, so the
#: three facts read out of one are found by shape: the addresses come first and
#: end at the first `/*name=*/` bool, and the identity and its scope follow the
#: last one. These are the counts that shape produces, asserted rather than
#: assumed — a struct that gains or loses a column changes them, and a
#: positional read against the wrong column is exactly the silent
#: mis-comparison this file exists to prevent.
_BROWSER_ADDRESSES = 11
_BROWSER_TRAILING = 3


def browser_vendors(text: str) -> dict[str, tuple[str, str, bool, bool]]:
    """Each vendor's flow shape and the two facts that decide registration.

    Read as `(flow kind, terms reviewed on, presents an identity, has one)`,
    which is what `ProviderAuthVendorRegistered` in the same file reads to
    decide whether a flow may be offered at all: a dated terms review, and an
    identity where the row presents one. Both are read here so this check can
    ask the browser's own question rather than invent a second one.
    """
    table = re.search(r"kVendors\s*=\s*\{\{(.*?)\n\}\};", text, re.S)
    if table is None:
        raise TableUnreadable(
            f"{BROWSER}: no kVendors table found. If it was renamed, rename it here too "
            "rather than dropping the check"
        )
    rows: dict[str, tuple[str, str, bool, bool]] = {}
    for entry in re.finditer(
        r'\{\s*\n\s*"([^"]+)",\s*\n\s*ProviderAuthFlowKind::(\w+),(.*?)\n\s*\},',
        table.group(1),
        re.S,
    ):
        body = entry.group(3)
        head, _, tail = body.partition("/*")
        addresses = re.findall(r'"([^"]*)"', head)
        trailing = re.findall(r'"([^"]*)"', tail.rpartition("*/")[2])
        presents = re.search(r"/\*presents_client_identity=\*/\s*(true|false)", body)
        if (
            len(addresses) != _BROWSER_ADDRESSES
            or len(trailing) != _BROWSER_TRAILING
            or presents is None
        ):
            raise TableUnreadable(
                f"{BROWSER}: the kVendors row for {entry.group(1)!r} is not the shape this "
                f"check reads — {len(addresses)} addresses and {len(trailing)} trailing "
                "values, and a named presents_client_identity was "
                f"{'found' if presents else 'not found'}. The struct gained or lost a "
                "column; update _BROWSER_ADDRESSES/_BROWSER_TRAILING rather than letting a "
                "positional read land on the wrong one"
            )
        rows[entry.group(1)] = (
            entry.group(2),
            addresses[-1],
            presents.group(1) == "true",
            bool(trailing[0]),
        )
    if not rows:
        raise TableUnreadable(f"{BROWSER}: kVendors was found and named no vendor")
    return rows


# --- the comparison -----------------------------------------------------------


def _compiled_tables_agree(
    core: set[str],
    kotlin: dict[str, tuple[str, bool]],
    browser: dict[str, tuple[str, str, bool, bool]],
) -> list[str]:
    """The three compiled tables name one set, with one shape per vendor."""
    findings: list[str] = []
    for vendor in sorted(core - set(kotlin)):
        findings.append(
            f"{vendor}: the core says this binary can sign in and the Android flow map "
            "carries no flow, so the row is offered and the tap does nothing"
        )
    for vendor in sorted(set(kotlin) - core):
        findings.append(
            f"{vendor}: the Android flow map carries a flow and the core does not list the "
            "vendor, so the row is never actionable and the flow is unreachable"
        )
    for vendor in sorted(core - set(browser)):
        findings.append(
            f"{vendor}: the core says this binary can sign in and the browser's vendor table "
            "has no configuration, so the flow fails at its first network leg"
        )
    for vendor in sorted(set(browser) - core):
        findings.append(
            f"{vendor}: the browser carries a configuration the core will never ask it to use"
        )
    for vendor in sorted(set(kotlin) & set(browser)):
        named = kotlin[vendor][0]
        expected = FLOW_KINDS.get(named)
        shape = browser[vendor][0]
        if expected is None:
            findings.append(
                f"{vendor}: the Android flow map names shape {named}, which this "
                "check cannot compare — add it to FLOW_KINDS"
            )
        elif expected != shape:
            findings.append(
                f"{vendor}: the Android flow map says {named} and the browser says "
                f"{shape}; a surface drawing one shape over a flow running the other shows a "
                "person a screen that cannot complete"
            )
    return findings


def _catalog_agrees(catalog: dict[str, tuple[bool, bool]], core: set[str]) -> list[str]:
    """The catalog offers exactly the sign-ins the binary can run.

    Both directions fail. A compiled flow with no OAUTH row is a flow nothing
    can reach; an OAUTH row with no compiled flow is a control a person can
    press that cannot start. Neither is visible anywhere else.
    """
    findings: list[str] = []
    for vendor in sorted(core):
        if vendor not in catalog:
            findings.append(
                f"{vendor}: this binary compiles a sign-in flow and the catalog carries no row "
                f"for the vendor at all, so nothing is offered and nothing says why — {_ORDER}"
            )
            continue
        if not catalog[vendor][0]:
            findings.append(
                f"{vendor}: this binary compiles a sign-in flow and the catalog row does not "
                "offer OAUTH, so the flow is unreachable. Add OAUTH to its auth_methods, or "
                "take the vendor out of the three compiled tables"
            )
    for vendor in sorted(set(catalog) - core):
        if not catalog[vendor][0]:
            continue
        # Deliberately not exempted by `enabled: false`. A row switched off is
        # a vendor gate working; a row offering a sign-in this binary cannot
        # run is a promise no switch makes true, and the day the switch is
        # flipped it becomes a control that fails in a person's hands.
        state = "switched on" if catalog[vendor][1] else "switched off"
        findings.append(
            f"{vendor}: the catalog row offers OAUTH and is {state}, and this binary compiles "
            "no sign-in flow for it. A surface would draw a control that cannot start — "
            f"{_ORDER}"
        )
    return findings


def _reviewed_rows_can_start(browser: dict[str, tuple[str, str, bool, bool]]) -> list[str]:
    """A vendor whose terms review is dated must have an identity to present.

    This is `ProviderAuthVendorRegistered`'s own question, asked here rather
    than restated: the date is the gate, and an identity is required only where
    the row presents one. Undated is the deliberate waiting state decision 0081
    describes — the machinery is complete and refuses to start, reporting
    unavailability — so it is not a finding, and `deliberate_states` names it.
    Dated with no identity is the one combination that is neither waiting nor
    working: the review says somebody read what the vendor permits, and the
    first network leg would still fail.
    """
    return [
        f"{vendor}: the browser's row is dated {reviewed} and presents a client identity it "
        "does not carry, so this sign-in is past its review and still fails at the first leg. "
        "Give it an identity (decision 0095) or clear the date until it has one"
        for vendor, (_, reviewed, presents, has_identity) in sorted(browser.items())
        if reviewed and presents and not has_identity
    ]


def browser_starts(row: tuple[str, str, bool, bool]) -> bool:
    """`ProviderAuthVendorRegistered`'s answer, computed rather than restated.

    The predicate in `provider_auth_configuration.cc` reads two things: a dated
    terms review, and an identity wherever the row presents one. Both are
    already extracted for `_reviewed_rows_can_start`, so asking the browser's
    own question here costs nothing and keeps the two readings of it together.
    """
    _, reviewed, presents, has_identity = row
    return bool(reviewed) and (not presents or has_identity)


def _kotlin_agrees_about_starting(
    kotlin: dict[str, tuple[str, bool]], browser: dict[str, tuple[str, str, bool, bool]]
) -> list[str]:
    """The flow map's `browserWillStart` says what the browser would do.

    A surface decides what to draw before anybody presses, so it cannot ask the
    browser at press time and it must not guess. The flow map therefore carries
    the browser's answer as a column, and this is what stops the copy drifting:
    dating a review in `kVendors` and leaving the flow map alone would keep a
    working sign-in hidden behind "not ready yet", and clearing a date without
    the flow map would put back the button that fails in a person's hands.

    Both directions are findings, and the second is the worse one for the same
    reason it is throughout this file: it fails where a person can see it.
    """
    findings: list[str] = []
    for vendor in sorted(set(kotlin) & set(browser)):
        claimed = kotlin[vendor][1]
        actual = browser_starts(browser[vendor])
        if claimed == actual:
            continue
        if claimed:
            findings.append(
                f"{vendor}: the Android flow map says the browser will start this sign-in and "
                "the browser's row refuses it, so a surface draws a Sign in control that "
                "answers unavailable when it is pressed"
            )
        else:
            findings.append(
                f"{vendor}: the browser's row will start this sign-in and the Android flow "
                "map says it will not, so a working sign-in is drawn as unavailable and "
                "nobody can reach it"
            )
    return findings


def compare(
    catalog: dict[str, tuple[bool, bool]],
    core: set[str],
    kotlin: dict[str, tuple[str, bool]],
    browser: dict[str, tuple[str, str, bool, bool]],
) -> list[str]:
    """Every disagreement between the four tables, as sentences."""
    return (
        _compiled_tables_agree(core, kotlin, browser)
        + _catalog_agrees(catalog, core)
        + _reviewed_rows_can_start(browser)
        + _kotlin_agrees_about_starting(kotlin, browser)
    )


def deliberate_states(
    catalog: dict[str, tuple[bool, bool]],
    core: set[str],
    browser: dict[str, tuple[str, str, bool, bool]],
) -> list[str]:
    """The agreeing states that are not a plain offer, named rather than silent.

    Two of them, and neither is drift. Both are written generically on purpose:
    which vendors are in either state is a fact about the catalog and the
    browser table on the day this runs, not something this docstring should
    name, because a sentence naming one goes quietly false the day that row
    moves and nothing here would say so.

    A vendor with a compiled flow whose row offers OAUTH and ships switched off
    is the **kill switch** doing its job — a vendor held shut by the catalog
    rather than by a release. Everything else about such a row is still
    checked, so a *disabled* row for a vendor this binary cannot sign in to
    still fails above. No compiled row is in that state at the time of writing;
    the check exists because withdrawing a vendor has to be a published row
    rather than a build.

    A vendor whose browser row carries no dated terms review is **waiting on an
    owner**. Decision 0081 gates a vendor by registration rather than by code:
    until somebody has read that vendor's terms and dated the row, the flow is
    complete and refuses to start, reporting unavailability. Every compiled row
    is dated at the time of writing, so this prints nothing today — the next
    vendor added arrives undated, and a review can be withdrawn.
    """
    named = [
        f"{vendor}: offers OAUTH with a compiled flow and ships switched off — the catalog "
        "kill switch is holding this vendor shut, which is a vendor gate working"
        for vendor in sorted(core & set(catalog))
        if catalog[vendor][0] and not catalog[vendor][1]
    ]
    waiting = sorted(v for v, row in browser.items() if not row[1])
    if waiting:
        named.append(
            f"{', '.join(waiting)}: no dated terms review, so the browser refuses to start "
            "the flow and the product reports it unavailable. Dating the row is an owner's "
            "act (decision 0081); until it happens no sign-in runs for these vendors"
        )
    return named




def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--self-test", action="store_true", help="prove every comparison can fail"
    )
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        # Imported here rather than at the top: the proof imports this module
        # back, and a module-level import of it would be a cycle. Nothing on
        # the checking path needs it.
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from vendor_agreement_self_test import self_test  # noqa: PLC0415

        return self_test()

    root = repository_root()
    try:
        sources = {}
        for key, relative in (
            ("catalog", CATALOG), ("core", CORE), ("kotlin", KOTLIN), ("browser", BROWSER)
        ):
            with open(os.path.join(root, relative), encoding="utf-8") as handle:
                sources[key] = handle.read()
        catalog = catalog_rows(sources["catalog"])
        core = core_vendors(sources["core"])
        findings = compare(catalog, core, kotlin_flows(sources["kotlin"]),
                           browser_vendors(sources["browser"]))
    except (TableUnreadable, OSError, json.JSONDecodeError) as error:
        print(f"  {error}")
        return 1

    for line in deliberate_states(catalog, core, browser_vendors(sources["browser"])):
        print(f"  permitted: {line}")
    for finding in findings:
        print(f"  {finding}")
    if findings:
        print(f"  vendor agreement: {len(findings)} finding(s); {_ORDER}")
        return 1
    print(
        "vendor agreement: the catalog, the core, the Android flow map and the browser's "
        "vendor table offer exactly the sign-ins this binary can run"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
