#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Prove a failed state publication cannot dispatch an orphaned effect.

The service bridge is compiled only by GN, so a host-only fast check cannot
rely on Rust's exhaustive destructuring to notice a new `BridgeResponse`
effect vector. This checker derives the vectors from the FFI record, requires
the publication-failure helper to destructure and clear every one, and checks
that both response publication paths call the helper.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

BRIDGE_DIR = Path(__file__).resolve().parent.parent
RESPONSE_FILE = "service_bridge_state_ffi.rs"
STATUS_FILE = "service_bridge_status.rs"
FAILURE_PUBLICATIONS = ("response_after_change", "response_after_task_change")


def strip_comments(text: str) -> str:
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)


def braced_body(text: str, marker: str) -> str | None:
    start = text.find(marker)
    if start < 0:
        return None
    brace = text.find("{", start + len(marker))
    if brace < 0:
        return None
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1 : index]
    return None


def effect_fields(response_source: str) -> list[str]:
    body = braced_body(strip_comments(response_source), "struct BridgeResponse")
    if body is None:
        return []
    fields = []
    for match in re.finditer(
        r"(?m)^\s*(\w+)\s*:\s*Vec\s*<\s*Bridge\w*Effect\s*>\s*,", body
    ):
        fields.append(match.group(1))
    return fields


def destructured_fields(withdrawal_body: str) -> set[str]:
    body = braced_body(withdrawal_body, "let ffi::BridgeResponse")
    if body is None:
        return set()
    return set(
        re.findall(r"(?m)^\s*(\w+)(?:\s*:\s*[^,]+)?\s*,", body)
    )


def function_body(source: str, name: str) -> str | None:
    pattern = re.compile(
        rf"(?:^|\n)\s*(?:pub(?:\([^)]*\))?\s+)?fn\s+{re.escape(name)}\s*\("
    )
    match = pattern.search(strip_comments(source))
    if match is None:
        return None
    return braced_body(source, match.group(0).strip())


def check(response_source: str, status_source: str) -> list[str]:
    findings = []
    effects = effect_fields(response_source)
    if not effects:
        return [
            "effect-withdrawal: BridgeResponse has no parsed effect vectors; "
            "the contract moved or the parser is broken"
        ]

    withdrawal = function_body(status_source, "withdraw_effects")
    if withdrawal is None:
        return ["effect-withdrawal: withdraw_effects has no implementation"]
    destructured = destructured_fields(withdrawal)
    for field in effects:
        if field not in destructured:
            findings.append(
                f"effect-withdrawal: `{field}` is absent from the exhaustive "
                "BridgeResponse destructure"
            )
        if re.search(rf"\b{re.escape(field)}\s*\.\s*clear\s*\(\s*\)\s*;", withdrawal) is None:
            findings.append(
                f"effect-withdrawal: `{field}` is not cleared when state "
                "publication tears down the runtime"
            )

    for name in FAILURE_PUBLICATIONS:
        body = function_body(status_source, name)
        if body is None:
            findings.append(f"effect-withdrawal: `{name}` has no implementation")
        elif re.search(r"\bwithdraw_effects\s*\(", body) is None:
            findings.append(
                f"effect-withdrawal: `{name}` does not withdraw effects on its "
                "failed-publication path"
            )
    return findings


SELF_TEST_RESPONSE = """
struct BridgeResponse {
    alpha_effects: Vec<BridgeAlphaEffect>,
    beta_effects: Vec<BridgeBetaEffect>,
    answer_events: Vec<BridgeAnswerEvent>,
}
"""

SELF_TEST_GOOD_STATUS = """
fn response_after_change() { withdraw_effects(&mut response); }
fn response_after_task_change() { withdraw_effects(&mut response); }
fn withdraw_effects(response: &mut ffi::BridgeResponse) {
    let ffi::BridgeResponse {
        alpha_effects,
        beta_effects,
        answer_events: _,
    } = response;
    alpha_effects.clear();
    beta_effects.clear();
}
"""


def self_test() -> int:
    if check(SELF_TEST_RESPONSE, SELF_TEST_GOOD_STATUS):
        print(
            "effect-withdrawal self-test: complete fixture was refused",
            file=sys.stderr,
        )
        return 1
    missing_clear = SELF_TEST_GOOD_STATUS.replace("    beta_effects.clear();\n", "")
    findings = check(SELF_TEST_RESPONSE, missing_clear)
    if len(findings) != 1 or "`beta_effects` is not cleared" not in findings[0]:
        print(
            f"effect-withdrawal self-test: missed an uncleared vector: {findings}",
            file=sys.stderr,
        )
        return 1
    missing_destructure = SELF_TEST_GOOD_STATUS.replace("        beta_effects,\n", "")
    findings = check(SELF_TEST_RESPONSE, missing_destructure)
    if not any("absent from the exhaustive" in finding for finding in findings):
        print(
            f"effect-withdrawal self-test: missed a new vector: {findings}",
            file=sys.stderr,
        )
        return 1
    missing_call = SELF_TEST_GOOD_STATUS.replace(
        "fn response_after_change() { withdraw_effects(&mut response); }",
        "fn response_after_change() {}",
    )
    findings = check(SELF_TEST_RESPONSE, missing_call)
    if not any("`response_after_change`" in finding for finding in findings):
        print(
            f"effect-withdrawal self-test: missed a publication path: {findings}",
            file=sys.stderr,
        )
        return 1
    print("effect-withdrawal: self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--self-test", action="store_true", help="run the checker against its fixtures"
    )
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    response_path = BRIDGE_DIR / RESPONSE_FILE
    status_path = BRIDGE_DIR / STATUS_FILE
    findings = check(
        response_path.read_text(encoding="utf-8"),
        status_path.read_text(encoding="utf-8"),
    )
    for finding in findings:
        print(finding, file=sys.stderr)
    if findings:
        return 1
    total = len(effect_fields(response_path.read_text(encoding="utf-8")))
    print(
        f"effect-withdrawal: all {total} BridgeResponse effect vectors are "
        "withdrawn after failed state publication"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
