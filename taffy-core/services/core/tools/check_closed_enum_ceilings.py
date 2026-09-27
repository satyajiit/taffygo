#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Prove every `ClosedEnum` ceiling is the enumeration's own last member.

`ClosedEnum(value, last)` in `rust_core_task_effect_records.h` refuses a value
above `last`. Its own comment states the rule: "the ceiling is the LAST member
of the enum, never the last one a caller happens to handle." Nothing enforced
it, and a ceiling written as a member name goes stale the moment a member is
added after it — silently, because adding an enumeration member compiles
everywhere and the ceiling is still a valid expression.

Both failures this checker exists for were found on a phone rather than by any
gate, and both were the same shape.

`TaskActionOperationKind::kDownloadCancel` was last when the reconcile
projection was written. Reload, stop, history and bookmarks were added after
it, so a `browser.reload` whose outcome could not be confirmed produced a
reconcile effect the browser could not project — and a task effect that cannot
be projected refuses the whole state publication, which ends the core and takes
every task in the profile with it.

`BipSensitivity::kUnknownSensitive` was last when the policy projection was
written. `kOneTimeCode` and `kChallengeResponse` were added after it, so a page
carrying a one-time code or a challenge — an OTP box or a captcha, which is
every sign-in an errand must hand over at — failed that projection instead of
being described.

A ceiling that is deliberately narrower than the enumeration belongs in
`NARROW_BY_DESIGN` with a reason. The register is checked in both directions,
so an entry whose ceiling has become the last member cannot be left on the
books.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

BRIDGE_DIR = Path(__file__).resolve().parent.parent
REPOSITORY_ROOT = BRIDGE_DIR.parent.parent.parent
ENUM_FILE = (
    REPOSITORY_ROOT
    / "taffy-core/contracts/core-service/generated/mojom/core_service.mojom"
)
SOURCE_DIRS = (
    REPOSITORY_ROOT / "taffy-core/services/core",
    REPOSITORY_ROOT / "taffy-core/browser",
)

# Mojo's C++ bindings generate this alias for every enumeration, and it is the
# only ceiling that cannot go stale. A site naming it is correct by
# construction and is never compared against a member.
GENERATED_CEILING = "kMaxValue"

# Ceilings that are narrower than the enumeration on purpose, each with a
# reason. The value is the member the site names; when that member becomes the
# enumeration's last one the entry is stale and this checker says so, because a
# register that cannot be wrong proves nothing.
NARROW_BY_DESIGN: dict[tuple[str, str, str], str] = {
    (
        "rust_core_task_effect.cc",
        "TaskActionOperationKind",
        "kLibraryRemove",
    ): (
        "A library tool effect may only name a library operation, and the "
        "three of them end at kLibraryRemove. The ceiling is the family's "
        "bound rather than the enumeration's."
    ),
    (
        "rust_core_task_effect.cc",
        "TaskActionOperationKind",
        "kMemoryDelete",
    ): (
        "A memory tool effect may only name a memory operation, and the four "
        "of them end at kMemoryDelete. The ceiling is the family's bound "
        "rather than the enumeration's."
    ),
}

CALL = re.compile(
    r"ClosedEnum\(\s*([^,()]+?)\s*,\s*mojom::(\w+)::(k\w+)\s*\)", re.S
)
ENUM = re.compile(r"enum (\w+) \{(.*?)\}", re.S)
MEMBER = re.compile(r"(k\w+)\s*=\s*(\d+)")


def last_members(text: str) -> dict[str, str]:
    """The highest-valued member of every enumeration in the contract."""
    found: dict[str, str] = {}
    for match in ENUM.finditer(text):
        members = MEMBER.findall(match.group(2))
        if members:
            found[match.group(1)] = max(members, key=lambda pair: int(pair[1]))[0]
    return found


def sites(directories) -> list[tuple[str, int, str, str, str]]:
    """Every `ClosedEnum` call: file, line, field, enumeration, ceiling."""
    out = []
    for directory in directories:
        for path in sorted(directory.glob("*.cc")):
            if path.name.endswith("unittest.cc") or path.name.endswith(
                "browsertest.cc"
            ):
                continue
            text = path.read_text(encoding="utf-8")
            for match in CALL.finditer(text):
                line = text[: match.start()].count("\n") + 1
                field = " ".join(match.group(1).split())
                out.append((path.name, line, field, match.group(2), match.group(3)))
    return out


def audit(enum_text: str, found) -> list[str]:
    last = last_members(enum_text)
    findings: list[str] = []
    used_register: set[tuple[str, str, str]] = set()
    for name, line, field, enum, ceiling in found:
        if ceiling == GENERATED_CEILING:
            continue
        if enum not in last:
            continue
        key = (name, enum, ceiling)
        if ceiling == last[enum]:
            if key in NARROW_BY_DESIGN:
                used_register.add(key)
                findings.append(
                    f"{name}:{line}: {enum}::{ceiling} is registered as narrow "
                    f"by design, and it is now the enumeration's last member. "
                    f"Delete the register entry."
                )
            continue
        if key in NARROW_BY_DESIGN:
            used_register.add(key)
            continue
        findings.append(
            f"{name}:{line}: ClosedEnum({field}, mojom::{enum}::{ceiling}) "
            f"refuses every member above {ceiling}, and {enum}'s last member "
            f"is {last[enum]}. Name mojom::{enum}::kMaxValue, or register the "
            f"narrower bound with a reason."
        )
    for key in sorted(NARROW_BY_DESIGN):
        if key not in used_register:
            findings.append(
                f"{key[0]}: no ClosedEnum site names mojom::{key[1]}::{key[2]}, "
                f"so its register entry is stale. Delete it."
            )
    return findings


SELF_TEST_ENUMS = """
enum Colour {
  kRed = 0,
  kGreen = 1,
  kBlue = 2,
};
"""


def self_test() -> int:
    """The stale ceiling this checker exists for, and the two shapes around it."""
    last = last_members(SELF_TEST_ENUMS)
    if last != {"Colour": "kBlue"}:
        print(f"self-test: last member read as {last}", file=sys.stderr)
        return 1
    # The register is about the real tree, so a synthetic site list always
    # leaves its entries unused. Only the findings about this site are the
    # subject here.
    stale = [
        finding
        for finding in audit(
            SELF_TEST_ENUMS, [("a.cc", 7, "input.c", "Colour", "kGreen")]
        )
        if finding.startswith("a.cc:")
    ]
    if len(stale) != 1 or "kBlue" not in stale[0]:
        print(f"self-test: a stale ceiling was not named: {stale}", file=sys.stderr)
        return 1
    exact = audit(SELF_TEST_ENUMS, [("a.cc", 7, "input.c", "Colour", "kBlue")])
    generated = audit(
        SELF_TEST_ENUMS, [("a.cc", 7, "input.c", "Colour", "kMaxValue")]
    )
    unknown = audit(SELF_TEST_ENUMS, [("a.cc", 7, "input.s", "Shape", "kSquare")])
    for name, result in (
        ("the last member", exact),
        ("the generated ceiling", generated),
        ("an enumeration this contract does not declare", unknown),
    ):
        if [f for f in result if f.startswith("a.cc:")]:
            print(f"self-test: {name} was reported: {result}", file=sys.stderr)
            return 1
    print("self-test: 3 shapes checked, 0 finding(s)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="check this tool against its own adversarial cases and exit",
    )
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()

    found = sites(SOURCE_DIRS)
    if not found:
        print("no ClosedEnum call site found; the scan is wrong", file=sys.stderr)
        return 1
    findings = audit(ENUM_FILE.read_text(encoding="utf-8"), found)
    for finding in findings:
        print(finding, file=sys.stderr)
    print(
        f"checked {len(found)} ClosedEnum ceiling(s) against "
        f"{ENUM_FILE.name}: {len(findings)} finding(s)"
    )
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
