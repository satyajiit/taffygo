#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Proves every mutating bridge entry returns through the publication seam.

The browser reads core state only from the sequenced status publication, and
the bridge publishes only through the three functions in
`service_bridge_status.rs` (`response_after_change`,
`response_after_task_change`, `state_after_change`) or their shared projection
`next_state`. A bridge entry that mutates the runtime and returns without
reaching one of them leaves every surface holding a projection of state that no
longer exists — the delivery-bridge staleness class: nothing fails, screens
simply stop being true until the process restarts.

This check makes that class structural. It reads the `extern "Rust"` block in
`service_bridge.rs`, takes every entry that receives `&mut ServiceBridge` as a
mutating entry, and requires each one to follow a declared discipline:

- **Publishing** (the default): a call path from the entry's implementation to
  the publication set. The reachability is name-level over the bridge crate's
  own functions — an over-approximation that can miss a violation behind two
  same-named functions, never one that fails a correct tree — and it needs no
  toolchain, checkout, or device.
- **Commit-staging** (``COMMIT_STAGING``): the entry applies a command in
  memory and deliberately withholds every visible consequence until the
  storage commit is durable; the completion-delivery entries publish. That
  withholding is a runtime property this file cannot prove — the runtime's
  own tests do (`begin_submit_only_encodes_and_withholds_task_effects`,
  `successful_commit_releases_task_effects_exactly_once`) — so a row here is
  a declaration of which discipline the entry follows, not a waiver.
- **Exempt** (``PUBLICATION_EXEMPT``): the entry never publishes, with the
  written reason. Checked in both directions: an exempt entry that in fact
  reaches the publication seam is a paid debt still on the books, and the
  check fails until the row is removed.

A new mutating entry that returns a bare response and appears in neither
register fails, which is the review seam working: whoever adds it must say
which discipline it follows, in a place a reviewer reads.

The inverse rule is equally important for task-owned Library and Memory
effects: their terminal handlers stage an ``AppendTaskCommit`` and must not
publish the in-memory task outcome before that commit's browser terminal.
Those modules therefore may not reach or import the publication seam at all.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

BRIDGE_DIR = Path(__file__).resolve().parent.parent
ENTRY_FILE = "service_bridge.rs"

# The publication seam (service_bridge_status.rs). `next_state` is the shared
# projection the other three call; `Initialization` reaches it directly
# because its return type carries the state itself rather than a response.
PUBLICATION = {
    "response_after_change",
    "response_after_task_change",
    "state_after_change",
    "next_state",
}

CAUSAL_TASK_EFFECT_MODULES = {
    "service_bridge_task_effect/task_library.rs",
    "service_bridge_task_effect/task_memory.rs",
}
CAUSAL_TASK_EFFECT_FUNCTIONS = {"complete", "deliver_storage_completion"}

# Entries that apply a command in memory and withhold every visible
# consequence until the storage commit is durable. Their publication is the
# completion-delivery entries' (`DeliverStorageCompletion` and its peers),
# which reach the seam through `deliver_task_completion`.
COMMIT_STAGING = {
    "SubmitSkillMutation": (
        "stages one exact-version saved-skill storage change and withholds "
        "the new catalogue projection until DeliverStorageCompletion installs "
        "the prepared change and publishes it"
    ),
    "SubmitAssistantConfiguration": (
        "stages the profile configuration storage commit and withholds the "
        "new configuration until that commit is durable; "
        "DeliverStorageCompletion installs it and publishes through "
        "deliver_assistant_configuration_completion"
    ),
    "SubmitStartTask": (
        "stages the task-open storage commit; state is withheld until the "
        "commit is durable and its delivery publishes"
    ),
    "SubmitTask": (
        "stages one reducer command's storage commit; state is withheld "
        "until the commit is durable and its delivery publishes"
    ),
    "CompleteTaskSettlement": (
        "stages the settlement command's storage commit through the same "
        "begin_submit discipline as SubmitTask"
    ),
    "CompleteTaskEffect": (
        "stages the completion command's storage commit; a model reply "
        "additionally drives the walk, whose accepted command is again a "
        "staged commit"
    ),
}

# Mutating entries that deliberately never publish, each with the argument.
# A row here is a claim, not a waiver: when the entry gains a path to the
# publication seam the row must be removed, and this check fails until it is.
PUBLICATION_EXEMPT = {
    "EvaluatePolicy": (
        "decides and returns a verdict; durable state changes only through "
        "the command the browser then submits, and that submission publishes"
    ),
    "PrepareForShutdown": (
        "ends the generation; no caller remains to hand a state to, and the "
        "next generation's Initialization republishes from durable fact"
    ),
    "ExpireDueOperations": (
        "an expired operation is a failed storage commit: the runtime is a "
        "revision ahead of storage and marked for replay, so a published "
        "state here would be the one terminal the browser could tell apart"
    ),
    "SubmitComposer": (
        "a composer suggestion is offered rather than true, so it is not "
        "published state (decision 0097 section 6): it reaches its surface as "
        "a DELIVER_COMPOSER_COMPLETION effect the browser carries out, and "
        "the single-flight bookkeeping this entry does keep — which request "
        "is running — is read by no status projection. Publishing here would "
        "put a value that changes several times a second into every state "
        "generation"
    ),
    "DeliverComposerCompletion": (
        "the same argument as SubmitComposer from the other side: the "
        "suggestion travels to the surface as the push this entry returns, "
        "and settling the flight changes nothing any status projection reads"
    ),
    "CancelComposer": (
        "the fourth face of the same argument: a withdrawal releases the "
        "single flight and names the dispatch the browser must stop, and that "
        "instruction travels on the submission this entry returns rather than "
        "through a status snapshot. Which request is running is read by no "
        "status projection (decision 0097 section 6), so there is no published "
        "state here to leave stale"
    ),
    "RecordComposerDelivery": (
        "the third face of the same argument: the push has already reached "
        "the surface by the time its terminal arrives, so there is nothing "
        "left to carry, and all this entry does is release a flight no status "
        "projection reads. It is not the delivery-bridge defect, which was a "
        "state change nobody could see — a suggestion is not published state "
        "at all (decision 0097 section 6), and whether anybody was watching "
        "is journalled nowhere and charged to nothing"
    ),
    "DeliverModelStreamChunk": (
        "consumes one bounded transport fragment into the held model reader "
        "and advances bridge-local correlation counters; neither is part of "
        "a status projection. The only surface-visible consequence is the "
        "sanitized answer-event vector returned by this call, which the "
        "service publishes through PublishTaskAnswerEvents before it "
        "acknowledges the chunk. Reducer state changes only when the terminal "
        "completion is recorded, and CompleteTaskEffect follows the staged "
        "commit discipline for that change"
    ),
    "PlanEntitlementRefresh": (
        "planning marks a fetch in flight and stamps its rate bookkeeping, "
        "none of which any status projection reads; the plan row a surface "
        "draws changes only when the mint's summary is delivered, and "
        "DeliverEntitlementFetchResult publishes on exactly that verdict"
    ),
    "ExportPageSnapshot": (
        "renders and returns immutable bytes directly to the requesting page "
        "surface; its replay cache is bounded correlation state that no "
        "CoreStatus projection reads, and the result itself is the only "
        "surface-visible consequence"
    ),
    "CancelPageSnapshotExport": (
        "removes one live export correlation entry and returns the terminal "
        "answer directly; neither the live request nor its cancellation is "
        "represented in CoreStatus"
    ),
    "MatchSiteSkills": (
        "decodes one request-bound page and returns immutable catalogue "
        "matches directly; it changes neither the restored catalogue nor any "
        "fact represented in CoreStatus"
    ),
    "QuerySavedFlows": (
        "reads the existing catalogue and retained completed task goals, "
        "returning bounded immutable reviews directly to the requesting "
        "surface; it writes no query, goal, catalogue record or CoreStatus "
        "fact and emits no effects"
    ),
    "PlanBackupRestore": (
        "retains one validated restore plan in protocol-private phase state; "
        "the returned binding is the only visible result and CoreStatus has "
        "no restore-session projection"
    ),
    "ConfirmBackupRestorePlan": (
        "consumes exact plan confirmation and returns stage authority directly; "
        "the retained restore phase is not represented in CoreStatus"
    ),
    "ReportBackupRestoreStageVerified": (
        "consumes exact staged-snapshot proof and returns commit authority "
        "directly; no restored data is published by the source Core"
    ),
    "ReportBackupRestoreCommitOutcome": (
        "settles only protocol-private commit phase state; candidate visibility "
        "is owned by the browser's dormant-profile lifecycle, not CoreStatus"
    ),
    "ChooseBackupRestoreResolution": (
        "returns a consumptive accept-or-discard authorization directly while "
        "the browser keeps the candidate hidden; CoreStatus carries neither"
    ),
    "ChooseBackupRestoreRecoveryResolution": (
        "returns one consumptive physical resolution authorization over an "
        "exact durable recovery prefix; the protocol bookkeeping and hidden "
        "candidate are not CoreStatus state"
    ),
    "ReportBackupRestoreResolutionOutcome": (
        "settles only protocol-private candidate resolution state after the "
        "browser reports durable physical truth; no CoreStatus fact changes"
    ),
    "ReportBackupRestoreRecoveryResolutionOutcome": (
        "settles only protocol-private restart-aware resolution custody from "
        "durable history; candidate visibility remains browser-owned"
    ),
    "CancelBackupRestoreBeforeCommit": (
        "withdraws a retained private plan before commit issuance and returns "
        "the exact terminal result directly; no CoreStatus fact changes"
    ),
}

FN_DEF = re.compile(r"(?:^|\n)\s*(?:pub(?:\([^)]*\))?\s+)?(?:const\s+)?fn\s+(\w+)")
CALL = re.compile(r"\b(\w+)\s*\(")
NOT_CALLS = {
    "fn", "if", "for", "while", "match", "return", "let", "loop", "move",
    "Some", "None", "Ok", "Err", "Box", "Vec", "String",
}


def read_sources(root: Path) -> dict[str, str]:
    sources = {}
    for path in sorted(root.rglob("*.rs")):
        if "tools" in path.relative_to(root).parts:
            continue
        sources[str(path.relative_to(root))] = path.read_text(encoding="utf-8")
    return sources


def strip_comments(text: str) -> str:
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)


def extern_rust_block(text: str) -> str:
    start = text.find('extern "Rust"')
    if start < 0:
        raise SystemExit(
            f"publication-totality: no `extern \"Rust\"` block in {ENTRY_FILE}"
        )
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1 : index]
    raise SystemExit(f"publication-totality: unbalanced braces in {ENTRY_FILE}")


def mutating_entries(block: str) -> list[str]:
    entries = []
    for declaration in block.split(";"):
        match = re.search(r"fn\s+(\w+)\s*\((.*)\)", declaration, re.DOTALL)
        if match and "&mut ServiceBridge" in match.group(2):
            entries.append(match.group(1))
    return entries


def function_bodies(sources: dict[str, str]) -> dict[str, str]:
    """Every function defined in the crate, name -> concatenated bodies."""
    bodies: dict[str, str] = {}
    for text in sources.values():
        text = strip_comments(text)
        for match in FN_DEF.finditer(text):
            name = match.group(1)
            brace = text.find("{", match.end())
            semicolon = text.find(";", match.end())
            if brace < 0 or (0 <= semicolon < brace):
                continue  # a trait or extern declaration carries no body
            depth = 0
            for index in range(brace, len(text)):
                if text[index] == "{":
                    depth += 1
                elif text[index] == "}":
                    depth -= 1
                    if depth == 0:
                        body = text[brace : index + 1]
                        bodies[name] = bodies.get(name, "") + body
                        break
    return bodies


def reaches_publication(entry: str, bodies: dict[str, str]) -> bool:
    seen = set()
    frontier = [entry]
    while frontier:
        name = frontier.pop()
        if name in seen:
            continue
        seen.add(name)
        body = bodies.get(name)
        if body is None:
            continue
        for match in CALL.finditer(body):
            called = match.group(1)
            if called in PUBLICATION:
                return True
            if called not in NOT_CALLS and called not in seen:
                frontier.append(called)
    return False


def check_causal_task_effect_staging(sources: dict[str, str]) -> list[str]:
    """Requires task-owned domain terminals to withhold staged task state."""
    findings = []
    for path in sorted(CAUSAL_TASK_EFFECT_MODULES):
        source = sources.get(path)
        if source is None:
            findings.append(
                f"publication-totality: causal task-effect module `{path}` "
                "is missing"
            )
            continue
        stripped = strip_comments(source)
        definitions = {match.group(1) for match in FN_DEF.finditer(stripped)}
        missing = sorted(CAUSAL_TASK_EFFECT_FUNCTIONS - definitions)
        if missing:
            findings.append(
                f"publication-totality: causal task-effect module `{path}` "
                f"is missing terminal function(s): {', '.join(missing)}"
            )
        reached = sorted(
            name for name in PUBLICATION
            if re.search(rf"\b{re.escape(name)}\b", stripped)
        )
        if reached:
            findings.append(
                f"publication-totality: causal task-effect module `{path}` "
                "reaches the publication seam before its AppendTaskCommit "
                f"terminal: {', '.join(reached)}"
            )
    return findings


def check(sources: dict[str, str]) -> list[str]:
    findings = []
    entry_source = sources.get(ENTRY_FILE)
    if entry_source is None:
        return [f"publication-totality: {ENTRY_FILE} is missing"]
    entries = mutating_entries(extern_rust_block(strip_comments(entry_source)))
    if not entries:
        return ["publication-totality: no mutating entries found; the parser is broken"]
    bodies = function_bodies(sources)
    for register, label in ((PUBLICATION_EXEMPT, "exemption"),
                            (COMMIT_STAGING, "commit-staging row")):
        for name in sorted(register):
            if name not in entries:
                findings.append(
                    f"publication-totality: {label} for `{name}` names no "
                    "mutating bridge entry; remove the register row"
                )
    for name in sorted(set(PUBLICATION_EXEMPT) & set(COMMIT_STAGING)):
        findings.append(
            f"publication-totality: `{name}` is in both registers; an entry "
            "follows one discipline"
        )
    for entry in entries:
        if entry not in bodies:
            findings.append(
                f"publication-totality: bridge entry `{entry}` has no "
                "implementation in the crate"
            )
            continue
        if entry in COMMIT_STAGING:
            continue
        published = reaches_publication(entry, bodies)
        if entry in PUBLICATION_EXEMPT:
            if published:
                findings.append(
                    f"publication-totality: `{entry}` is exempt but reaches the "
                    "publication seam; the debt is paid — remove its register row"
                )
            continue
        if not published:
            findings.append(
                f"publication-totality: `{entry}` mutates the runtime and returns "
                "without reaching the publication seam; every surface keeps a "
                "stale projection. Return through response_after_change, or "
                "register the entry's discipline with a written reason."
            )
    return findings


SELF_TEST_ENTRY = """
mod ffi {
    extern "Rust" {
        fn Mutates(bridge: &mut ServiceBridge) -> BridgeResponse;
        fn Reads(bridge: &ServiceBridge) -> BridgeResponse;
        fn Quiet(bridge: &mut ServiceBridge) -> BridgeResponse;
        fn Excused(bridge: &mut ServiceBridge) -> bool;
    }
}
pub(crate) fn Mutates(bridge: &mut ServiceBridge) -> BridgeResponse {
    helper(bridge)
}
fn helper(bridge: &mut ServiceBridge) -> BridgeResponse {
    response_after_change(bridge, response())
}
pub(crate) fn Quiet(bridge: &mut ServiceBridge) -> BridgeResponse {
    response()
}
pub(crate) fn Excused(bridge: &mut ServiceBridge) -> bool {
    true
}
"""


def self_test() -> int:
    global PUBLICATION_EXEMPT, COMMIT_STAGING
    real_exempt, real_staging = PUBLICATION_EXEMPT, COMMIT_STAGING
    try:
        PUBLICATION_EXEMPT = {"Excused": "self-test row"}
        COMMIT_STAGING = {}
        findings = check({ENTRY_FILE: SELF_TEST_ENTRY})
        if len(findings) != 1 or "`Quiet`" not in findings[0]:
            print("publication-totality self-test: expected exactly the `Quiet` "
                  f"finding, got {findings}", file=sys.stderr)
            return 1
        COMMIT_STAGING = {"Quiet": "stages a commit in the self-test"}
        if check({ENTRY_FILE: SELF_TEST_ENTRY}):
            print("publication-totality self-test: a commit-staging row did "
                  "not settle its entry", file=sys.stderr)
            return 1
        COMMIT_STAGING = {}
        PUBLICATION_EXEMPT = {"Excused": "row", "Ghost": "names nothing"}
        findings = check({ENTRY_FILE: SELF_TEST_ENTRY})
        if not any("Ghost" in finding for finding in findings):
            print("publication-totality self-test: a register row naming no "
                  "entry was not reported", file=sys.stderr)
            return 1
        PUBLICATION_EXEMPT = {"Excused": "row", "Mutates": "stale row"}
        findings = check({ENTRY_FILE: SELF_TEST_ENTRY})
        if not any("remove its register row" in finding for finding in findings):
            print("publication-totality self-test: a paid-off exemption was "
                  "not reported", file=sys.stderr)
            return 1
        PUBLICATION_EXEMPT = {"Excused": "row"}
        COMMIT_STAGING = {"Excused": "same row twice"}
        findings = check({ENTRY_FILE: SELF_TEST_ENTRY})
        if not any("both registers" in finding for finding in findings):
            print("publication-totality self-test: a double-registered entry "
                  "was not reported", file=sys.stderr)
            return 1

        causal_sources = {
            path: (
                "fn complete() { settle_action(); }\n"
                "fn deliver_storage_completion() { settle_action(); }\n"
            )
            for path in CAUSAL_TASK_EFFECT_MODULES
        }
        if check_causal_task_effect_staging(causal_sources):
            print("publication-totality self-test: valid causal staging was "
                  "rejected", file=sys.stderr)
            return 1
        broken_path = sorted(CAUSAL_TASK_EFFECT_MODULES)[0]
        causal_sources[broken_path] += (
            "fn leaks_state() { response_after_change(); }\n"
        )
        findings = check_causal_task_effect_staging(causal_sources)
        if len(findings) != 1 or broken_path not in findings[0]:
            print("publication-totality self-test: premature causal "
                  f"publication was not reported, got {findings}",
                  file=sys.stderr)
            return 1
    finally:
        PUBLICATION_EXEMPT, COMMIT_STAGING = real_exempt, real_staging
    print("publication-totality: self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true",
                        help="run the checker against its own fixtures")
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    sources = read_sources(BRIDGE_DIR)
    findings = check(sources) + check_causal_task_effect_staging(sources)
    for finding in findings:
        print(finding, file=sys.stderr)
    if findings:
        return 1
    total = len(mutating_entries(extern_rust_block(
        strip_comments((BRIDGE_DIR / ENTRY_FILE).read_text(encoding="utf-8")))))
    print(f"publication-totality: {total} mutating entries all follow a "
          f"declared discipline ({len(COMMIT_STAGING)} commit-staging, "
          f"{len(PUBLICATION_EXEMPT)} exempt, the rest publish)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
