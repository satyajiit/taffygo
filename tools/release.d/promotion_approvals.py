#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Named-person approval checks used by the promotion-policy module."""

from __future__ import annotations

APPROVAL_ROLES = ("product", "browser", "security", "privacy", "release")


def check(manifest, report) -> None:
    approvals = manifest.get("approvals", [])
    by_role: dict[str, list[dict]] = {}
    for approval in approvals:
        by_role.setdefault(approval.get("role", ""), []).append(approval)
    missing = [role for role in APPROVAL_ROLES if role not in by_role]
    if missing:
        report.fail(
            "approvals",
            f"no {', '.join(missing)} approval",
            "A candidate carries named product, browser, security, privacy and release "
            "approvals with timestamps. Each identity means that person read the "
            "evidence for this immutable candidate.",
        )
    duplicated = [role for role, rows in by_role.items() if len(rows) > 1]
    if duplicated:
        report.fail(
            "approvals",
            f"more than one {', '.join(sorted(duplicated))} approval",
            "Record one final approval per role so fragment appends cannot preserve a "
            "withdrawn or superseded approval.",
        )
    identities = {row.get("identity") for row in approvals if row.get("identity")}
    if len(identities) < 3:
        report.fail(
            "approvals",
            f"only {len(identities)} distinct approving identity(ies)",
            "Use at least three accountable people. One person holding every role is "
            "a release blocker, not five approvals.",
        )
