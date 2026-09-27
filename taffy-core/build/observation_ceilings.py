#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""A browser-side shape bound may not be stricter than the endpoint's own.

Three files hold the same observation bounds and none can see the others. The
renderer declares what its endpoint will serve in
`taffy-core/renderer/observation_limits.json`; the browser holds its own
ceiling on what this process will accept in
`taffy-core/components/intelligence/content/budget_clamp.cc`; and
`OnDemandBudget` in
`taffy-core/browser/taffy_page_intelligence_host_observation.cc` writes a third
copy as the budget it grants one task observation. The duplication
is structural rather than accidental — `taffy-core/browser/DEPS` states
`no +taffy/renderer`, which is exactly why the browser cannot read the owner
file — so no compiler and no suite can notice them drifting apart. This
check is the only thing that can, which is why it is a static gate and not a
test.

Only the **shape** bounds are checked, and the asymmetry is the point. A
stricter node, byte or deadline ceiling in the browser is defence in depth and
is the whole reason a second ceiling exists: it bounds the work this process
will do on an answer it did not compose. A stricter *shape* ceiling bounds no
work at all. Nodes and bytes already cap the cost; cutting depth below what the
endpoint serves does not save anything, it removes whichever subtree happens to
be deepest.

That is not hypothetical, and it happened twice in the same afternoon.
`kMaxDepth` was 24 while the endpoint declared 64, so the browser recut every
page to a depth the renderer would never have stopped at. On a phone the
subtree that went was the result links on a search page: the model was handed
the search box, the tab strip and "Sign in", could not see a single result to
follow, and typed a hostname it had invented instead. Correcting the clamp
raised the depth to 32 and not to 64, because `OnDemandBudget` held a third
copy — and that copy is the one that binds, since a task effect narrows nodes,
bytes, frames and the deadline and carries no depth at all. Decision 0170
states the rule; OD-142 holds the question of whether the copies should exist
at all.
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OWNER = ROOT / "taffy-core/renderer/observation_limits.json"
CLAMP = ROOT / "taffy-core/components/intelligence/content/budget_clamp.cc"
GRANT = ROOT / "taffy-core/browser/taffy_page_intelligence_host_observation.cc"

# The browser constant, and the endpoint field that owns the same bound. Shape
# bounds only; see the module docstring for why a stricter node, byte or
# deadline ceiling in the browser is not a finding.
SHAPE_BOUNDS = {"kMaxDepth": "snapshot_max_depth"}

# The same rule one layer down. `OnDemandBudget` is the budget the browser
# grants a task observation, and it is the copy that actually binds: the task
# effect narrows nodes, bytes, frames and the deadline and carries no depth at
# all, so whatever this function assigns is the depth every task observation
# gets. It stood at 32 while the clamp above it said 64 and the endpoint said
# 64, which is exactly the shape of drift this file exists to catch.
GRANT_BOUNDS = {"max_depth": "snapshot_max_depth"}

# A constant whose value is not a literal cannot be compared without compiling
# the file, so the check reads only digits and the two operators the clamp uses
# to spell a byte count.
LITERAL = re.compile(r"^[\d\s*+]+$")
DEFINITION = re.compile(r"^constexpr uint32_t (\w+)\s*=\s*([^;]+);", re.MULTILINE)
ASSIGNMENT = re.compile(r"^\s*budget\.(\w+)\s*=\s*([^;]+);", re.MULTILINE)


def declared_bounds():
    fields = json.loads(OWNER.read_text(encoding="utf-8"))["fields"]
    return {field["name"]: int(field["value"]) for field in fields}


def clamp_bounds(text):
    return literal_assignments(DEFINITION, text)


def grant_bounds(text):
    """The fields `OnDemandBudget` assigns, read from that function alone.

    Scoped to the one function because `budget.max_depth` is also assigned by
    the narrowing code beside it, and a sweep of the whole file would read
    whichever came last.
    """
    start = text.find("ObservationBudget OnDemandBudget()")
    if start < 0:
        return {}
    end = text.find("\n}", start)
    return literal_assignments(ASSIGNMENT, text[start:end if end > 0 else None])


def literal_assignments(pattern, text):
    found = {}
    for match in pattern.finditer(text):
        expression = match.group(2).strip()
        if LITERAL.match(expression):
            found[match.group(1)] = int(eval(expression))  # digits, * and + only
    return found


def findings_for(clamp_text, grant_text):
    declared = declared_bounds()
    findings = []
    for held, bounds, where in (
        (clamp_bounds(clamp_text), SHAPE_BOUNDS, "budget_clamp.cc"),
        (grant_bounds(grant_text), GRANT_BOUNDS, "OnDemandBudget"),
    ):
        for name, owner_field in bounds.items():
            if name not in held:
                findings.append(f"{name} is not a literal bound in {where}")
            elif owner_field not in declared:
                findings.append(f"{owner_field} is not declared in observation_limits.json")
            elif held[name] < declared[owner_field]:
                findings.append(
                    f"{where}'s {name} = {held[name]} is stricter than the endpoint's "
                    f"declared {owner_field} = {declared[owner_field]}. A shape bound "
                    "below what the endpoint serves recuts every page and bounds no "
                    "work (decision 0170)."
                )
    return findings


def self_test():
    """Reject a stricter shape bound in either copy, and one that hides its value."""
    clamp_text = CLAMP.read_text(encoding="utf-8")
    grant_text = GRANT.read_text(encoding="utf-8")
    failures = []
    clamp_cases = [
        ("a clamp depth below the declared one", "constexpr uint32_t kMaxDepth = 24;"),
        ("a clamp depth behind a name", "constexpr uint32_t kMaxDepth = kSomething;"),
    ]
    for description, replacement in clamp_cases:
        broken = DEFINITION.sub(
            lambda m: replacement if m.group(1) == "kMaxDepth" else m.group(0), clamp_text
        )
        if broken == clamp_text:
            failures.append(f"the self-test could not build {description}; kMaxDepth moved")
        elif not findings_for(broken, grant_text):
            failures.append(f"{description} was not caught")
    grant_cases = [
        ("a granted depth below the declared one", "  budget.max_depth = 32;"),
        ("a granted depth behind a name", "  budget.max_depth = kSomething;"),
    ]
    for description, replacement in grant_cases:
        broken = grant_text.replace("  budget.max_depth = 64;", replacement)
        if broken == grant_text:
            failures.append(f"the self-test could not build {description}; the grant moved")
        elif not findings_for(clamp_text, broken):
            failures.append(f"{description} was not caught")
    return failures


def main(argv):
    if "--self-test" in argv:
        failures = self_test()
        for failure in failures:
            print(f"observation ceilings: {failure}")
        if failures:
            return 1
        print("observation ceilings: every broken copy the self-test built was rejected")
        return 0
    findings = findings_for(
        CLAMP.read_text(encoding="utf-8"), GRANT.read_text(encoding="utf-8")
    )
    for finding in findings:
        print(f"observation ceilings: {finding}")
    if findings:
        return 1
    print(
        f"observation ceilings: {len(SHAPE_BOUNDS) + len(GRANT_BOUNDS)} shape bound(s) "
        "across 2 copies are no stricter than the endpoint declares"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
