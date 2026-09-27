#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""`check_vendor_agreement.py`'s own proof, on tables written to fail.

Split from the checker rather than left beside it, and the seam is the one the
two halves are read across: the checker is read by somebody asking what the
four tables must agree about, and this file by somebody asking whether the
answer can be trusted. Nothing here reads the repository — every table is a
literal — so a failure in this file is always about the comparisons and never
about the tree they usually run over.

The rule it exists for: an extractor that quietly matched nothing would turn
the whole check into a gate that always passes. So each one is handed a table
that is there and a file with no table at all, and has to tell them apart.

Imported by the checker and run as `check_vendor_agreement.py --self-test`;
there is no second entry point, because a proof nobody runs is not one.
"""

from __future__ import annotations

import sys

from check_vendor_agreement import (
    TableUnreadable,
    browser_vendors,
    compare,
    core_vendors,
    deliberate_states,
    kotlin_flows,
)

# --- the tool's own checks ----------------------------------------------------

_FAKE_CORE = 'const SIGN_IN_VENDORS: [&str; 2] = [\n    "alpha",\n    "beta",\n];\n'
_FAKE_KOTLIN = (
    'ProviderSignInFlow("alpha", ProviderFlowKind.PKCE, requiresAcknowledgement = false,\n'
    "    browserWillStart = false),\n"
    'ProviderSignInFlow("beta", ProviderFlowKind.DEVICE_CODE, requiresAcknowledgement = false,\n'
    "    browserWillStart = true),\n"
)

#: The same two rows with `beta`'s flag missing. A reader that skipped the row
#: it could not parse would compare one vendor and report agreement, so the
#: extractor has to refuse this outright.
_FAKE_KOTLIN_HALF_READ = (
    'ProviderSignInFlow("alpha", ProviderFlowKind.PKCE, requiresAcknowledgement = false,\n'
    "    browserWillStart = false),\n"
    'ProviderSignInFlow("beta", ProviderFlowKind.DEVICE_CODE, requiresAcknowledgement = false),\n'
)
#: One row in the real shape: nine addresses, three named bools, then the
#: identity and its scope. `beta` carries a dated review and an identity, which
#: is what a launched vendor looks like; `alpha` carries neither, which is what
#: every shipping row looks like today.
_FAKE_BROWSER = """constexpr std::array<ProviderAuthVendor, 2> kVendors = {{
    {
        "alpha",
        ProviderAuthFlowKind::kPkce,
        ProviderAuthRedirectKind::kManualCode,
        ProviderAuthExchangeKind::kOauthTokenPair,
        "https://alpha.example/authorize",
        "",
        "https://alpha.example/token",
        "",
        "",
        "",
        "",
        "",
        "",
        "alpha's command-line tool",
        {},
        {},
        "",
        /*authorization_displays_code=*/false,
        /*state_is_pkce_verifier=*/false,
        /*presents_client_identity=*/true,
        "",
        "",
        "openid",
    },
    {
        "beta",
        ProviderAuthFlowKind::kDeviceCode,
        ProviderAuthRedirectKind::kManualCode,
        ProviderAuthExchangeKind::kOauthTokenPair,
        "",
        "https://beta.example/device",
        "https://beta.example/token",
        "",
        "",
        "",
        "",
        "",
        "",
        "beta's command-line tool",
        {},
        {},
        "2026-08-29",
        /*authorization_displays_code=*/false,
        /*state_is_pkce_verifier=*/false,
        /*presents_client_identity=*/true,
        "beta-client-id",
        "",
        "openid",
    },
}};
"""


def _agreeing() -> dict:
    return {
        "catalog": {"alpha": (True, True), "beta": (True, True), "keyed": (False, True)},
        "core": {"alpha", "beta"},
        # `alpha` waits on its review and `beta` is past it, so each row's
        # flag matches what the browser beneath it would answer.
        "kotlin": {"alpha": ("PKCE", False), "beta": ("DEVICE_CODE", True)},
        # A vendor waiting on its review and one that is past it, so both the
        # permitted state and the rule that starts applying after it are live.
        "browser": {
            "alpha": ("kPkce", "", True, False),
            "beta": ("kDeviceCode", "2026-08-29", True, True),
        },
    }


def _with(key: str, **changes) -> dict:
    """The agreeing tables with one table's entries added to or replaced."""
    table = _agreeing()
    table[key] = {**table[key], **changes}
    return table


def self_test() -> int:
    """Prove every comparison can fail, on tables written to fail."""
    failures: list[str] = []

    if compare(**_agreeing()):
        failures.append(f"four agreeing tables reported {compare(**_agreeing())}")

    cases = (
        ("a compiled flow whose catalog row does not offer OAUTH",
         _with("catalog", alpha=(False, True)), "does not offer OAUTH"),
        ("a catalog row with no compiled flow at all",
         _with("catalog", ghost=(True, True)), "compiles no sign-in flow"),
        ("a switched-off catalog row with no compiled flow",
         _with("catalog", ghost=(True, False)), "switched off"),
        ("a vendor the catalog has no row for",
         {**_agreeing(), "catalog": {"beta": (True, True)}}, "no row for the vendor at all"),
        ("a vendor the Android flow map dropped",
         {**_agreeing(), "kotlin": {"beta": ("DEVICE_CODE", True)}}, "carries no flow"),
        ("a vendor the core dropped",
         {**_agreeing(), "core": {"alpha"}}, "the core does not list the vendor"),
        ("a vendor the browser has no configuration for",
         {**_agreeing(), "browser": {"alpha": ("kPkce", "", True, False)}}, "first network leg"),
        ("two tables that disagree about the shape",
         _with("browser", alpha=("kDeviceCode", "", True, False)), "cannot complete"),
        ("a shape this check cannot compare",
         _with("kotlin", alpha=("SEMAPHORE", False)), "add it to FLOW_KINDS"),
        ("a flow map promising a sign-in the browser refuses",
         _with("kotlin", alpha=("PKCE", True)), "answers unavailable when it is pressed"),
        ("a flow map hiding a sign-in the browser would start",
         _with("kotlin", beta=("DEVICE_CODE", False)), "nobody can reach it"),
        ("a reviewed sign-in with no client identity",
         _with("browser", beta=("kDeviceCode", "2026-08-29", True, False)),
         "past its review and still fails"),
    )
    for name, table in ((n, t) for n, t, _ in cases):
        if not compare(**table):
            failures.append(f"{name}: nothing fired")
    for name, table, phrase in cases:
        found = compare(**table)
        if not any(phrase in line for line in found):
            failures.append(f"{name}: no finding said {phrase!r}; got {found}")

    # The kill switch is recognised and permitted, and only where a compiled
    # flow backs it. A switched-off row with no flow is still a failure above.
    switched_off = _with("catalog", alpha=(True, False))
    if compare(**switched_off):
        failures.append(
            f"the kill switch failed rather than being permitted: {compare(**switched_off)}"
        )
    named = deliberate_states(**{k: switched_off[k] for k in ("catalog", "core", "browser")})
    if not any("kill switch" in line for line in named):
        failures.append(f"the kill switch was not named as a deliberate state; got {named}")

    # An undated terms review is the waiting state, permitted and named; the
    # same row dated and identityless is the failure two cases above.
    agreeing = _agreeing()
    waiting = deliberate_states(**{k: agreeing[k] for k in ("catalog", "core", "browser")})
    if not any("no dated terms review" in line and "alpha" in line for line in waiting):
        failures.append(f"an undated terms review was not named as waiting; got {waiting}")
    if any("kill switch" in line for line in waiting):
        failures.append("a row that ships on was reported as switched off")
    all_dated = _with("browser", alpha=("kPkce", "2026-08-29", True, True))
    if deliberate_states(**{k: all_dated[k] for k in ("catalog", "core", "browser")}):
        failures.append("a fully registered set still reported a deliberate waiting state")

    # A flow map whose rows do not all carry both facts is refused outright.
    # Reading the rows that parsed would compare a subset and call it
    # agreement, which is the same always-passing gate as an empty read.
    try:
        kotlin_flows(_FAKE_KOTLIN_HALF_READ)
        failures.append("the kotlin extractor read a flow map with a row it could not parse")
    except TableUnreadable:
        pass

    # Every extractor refuses a file with no table, because a regex that
    # matched nothing would make this a gate that always passes.
    for reader, source, label in (
        (core_vendors, _FAKE_CORE, "core"),
        (kotlin_flows, _FAKE_KOTLIN, "kotlin"),
        (browser_vendors, _FAKE_BROWSER, "browser"),
    ):
        try:
            if not reader(source):
                failures.append(f"the {label} extractor read nothing from a table that is there")
        except TableUnreadable as error:
            failures.append(f"the {label} extractor refused a table that is there: {error}")
        try:
            reader("// a source file with no table in it at all\n")
        except TableUnreadable:
            pass
        else:
            failures.append(
                f"the {label} extractor returned a value for a file with no table, so this "
                "check would pass on a renamed table having compared nothing"
            )
    # The core extractor also refuses a partial read, which is what a loosened
    # regex produces: a shorter set that agrees with less and says nothing.
    try:
        core_vendors('const SIGN_IN_VENDORS: [&str; 3] = [\n    "alpha",\n];\n')
    except TableUnreadable:
        pass
    else:
        failures.append("the core extractor accepted fewer entries than the array declares")

    for failure in failures:
        print(f"vendor agreement self-test: {failure}", file=sys.stderr)
    if failures:
        print(f"vendor agreement self-test: {len(failures)} failure(s)", file=sys.stderr)
        return 1
    print(
        "vendor agreement: every comparison fires on a known break, the kill switch is "
        "permitted and named, and every extractor refuses a table it cannot find"
    )
    return 0
