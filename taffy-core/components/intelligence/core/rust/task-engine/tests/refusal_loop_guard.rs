// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Three identical refusals end the attempt, and a rebuild reaches the same
//! state.
//!
//! Decision 0054 section 5: a model asked in a system prompt not to loop will
//! still loop, because the request that loops looks locally reasonable every
//! single time. Counting is the only thing that sees the pattern, so the count
//! lives in reducer state and is re-derived by replaying the journal rather
//! than restored from a snapshot of itself.
//!
//! The suite drives the whole path — propose, authorize, dispatch, refuse — so
//! that what is asserted is the reducer's behaviour and not the ledger's, which
//! `task_engine::tool::repetition` already covers on its own.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{DispatchId, MonotonicMillis, SemanticNodeId};
use bip_types::ActionResultCode;
use task_engine::action::ActionOutcome;
use task_engine::authority::{Denial, ProposalDecision};
use task_engine::command::Command;
use task_engine::reducer::Reducer;
use task_engine::transition::RefusalReason;
use task_engine::{
    CallFingerprint, ManualClock, SequentialIds, MAX_IDENTICAL_REFUSALS, MAX_TRACKED_REFUSALS,
};

/// The node every proposal in this suite targets, so that every call in it has
/// one fingerprint.
const NODE: &str = "node_7";
const LOOP_TOOL: &str = "browser.dom.scroll";

fn targeting(tool: &str, key: &str, node: Option<&str>) -> task_engine::action::ActionProposal {
    assert_eq!(tool, LOOP_TOOL);
    common::proposal_for(
        task_engine::action::BrowserIntent::DomScroll {
            tab: bip_types::identity::TabId::new("tab_1"),
            direction: task_engine::action::ScrollDirection::ToNode,
            target: node.map(SemanticNodeId::new),
        },
        key,
    )
}

fn refused(code: ActionResultCode, dispatch: &str) -> ActionOutcome {
    ActionOutcome {
        code,
        dispatch_id: Some(DispatchId::new(dispatch)),
        observed_at: MonotonicMillis(10),
        // A refusal carries no observation. `ActionRecord::accepts_outcome`
        // requires that, and it is the rule that stops a refused call from
        // smuggling page evidence in behind a failure.
        observation: None,
        discovered_source: None,
    }
}

/// Proposes, authorizes, dispatches and refuses one call. Returns whether the
/// proposal was admitted at all.
fn refuse_once(
    fixture: &mut common::Fixture,
    round: usize,
    tool: &str,
    node: Option<&str>,
    code: ActionResultCode,
) -> Result<(), RefusalReason> {
    let key = format!("loop_key_{round}");
    let existing = fixture
        .reducer
        .actions()
        .map(|action| action.action_id().clone())
        .collect::<Vec<_>>();
    let envelope = fixture.envelope(Command::ProposeAction(Box::new(targeting(
        tool, &key, node,
    ))));
    fixture.reducer.apply(envelope).map_err(|it| it.reason)?;
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| !existing.contains(action.action_id()))
        .map(|action| action.action_id().clone())
        .expect("the proposal minted an action");
    let dispatch = format!("dispatch_{round}");
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(common::authorize()),
        dispatch_id: Some(DispatchId::new(&dispatch)),
    });
    fixture.must_apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(refused(code, &dispatch)),
    });
    Ok(())
}

/// [`MAX_IDENTICAL_REFUSALS`] as a round count.
fn rounds() -> usize {
    usize::try_from(MAX_IDENTICAL_REFUSALS).unwrap()
}

fn call_of(tool: &str) -> CallFingerprint {
    CallFingerprint::of_target(tool, Some("tab_1"), Some(NODE))
}

fn call() -> CallFingerprint {
    call_of(LOOP_TOOL)
}

#[test]
fn the_third_identical_refusal_ends_the_attempt() {
    let mut fixture = common::running();
    for round in 0..rounds() {
        refuse_once(
            &mut fixture,
            round,
            LOOP_TOOL,
            Some(NODE),
            ActionResultCode::NodeGone,
        )
        .expect("each of the first three proposals is admitted");
    }
    assert_eq!(
        fixture
            .reducer
            .refusals()
            .count_of(call(), ActionResultCode::NodeGone),
        MAX_IDENTICAL_REFUSALS
    );
    assert_eq!(
        refuse_once(
            &mut fixture,
            9,
            LOOP_TOOL,
            Some(NODE),
            ActionResultCode::NodeGone
        ),
        Err(RefusalReason::RepeatedRefusalsAbandoned),
        "a fourth attempt at the same call must not be admitted"
    );
}

#[test]
fn two_refusals_are_not_yet_a_loop() {
    // The contrast that makes the test above mean something: the guard has to
    // be the count and not the mere fact of a refusal.
    let mut fixture = common::running();
    for round in 0..2 {
        refuse_once(
            &mut fixture,
            round,
            LOOP_TOOL,
            Some(NODE),
            ActionResultCode::NodeGone,
        )
        .expect("two refusals leave the call admissible");
    }
    assert!(!fixture.reducer.refusals().is_abandoned(call()));
    assert!(refuse_once(
        &mut fixture,
        2,
        LOOP_TOOL,
        Some(NODE),
        ActionResultCode::NodeGone
    )
    .is_ok());
}

#[test]
fn a_different_call_is_not_a_repeat_of_this_one() {
    let mut fixture = common::running();
    for round in 0..rounds() {
        refuse_once(
            &mut fixture,
            round,
            LOOP_TOOL,
            Some(NODE),
            ActionResultCode::NodeGone,
        )
        .expect("the first three are admitted");
    }
    // A proposal against a different node is a different call, and the count is
    // per call rather than per task. A task that has genuinely moved on is not
    // punished for what it tried before.
    assert!(refuse_once(
        &mut fixture,
        9,
        LOOP_TOOL,
        Some("node_8"),
        ActionResultCode::NodeGone
    )
    .is_ok());
}

#[test]
fn a_full_distinct_refusal_register_stays_abandoned_and_replays_that_way() {
    let mut fixture = common::running();
    for round in 0..=MAX_TRACKED_REFUSALS {
        let node = format!("distinct_node_{round}");
        refuse_once(
            &mut fixture,
            round,
            LOOP_TOOL,
            Some(&node),
            ActionResultCode::NodeGone,
        )
        .expect("the refusal that crosses the register ceiling is recorded");
    }
    assert!(
        fixture.reducer.refusals().is_full(),
        "only {} distinct refusals were recorded",
        fixture.reducer.refusals().distinct(),
    );
    assert!(fixture.reducer.refusals().is_abandoned(call()));
    assert_eq!(
        refuse_once(
            &mut fixture,
            MAX_TRACKED_REFUSALS + 1,
            LOOP_TOOL,
            Some("a_call_the_register_never_saw"),
            ActionResultCode::NodeGone,
        ),
        Err(RefusalReason::RepeatedRefusalsAbandoned),
    );

    let journal = fixture.reducer.journal().clone();
    let (rebuilt, _) = Reducer::replay(
        common::seed(),
        common::defaults(),
        ManualClock::at(1_000),
        SequentialIds::new(),
        &journal,
    )
    .expect("the register-full abandonment replays");
    assert_eq!(rebuilt.refusals(), fixture.reducer.refusals());
    assert!(rebuilt.refusals().is_abandoned(call()));
}

#[test]
fn a_verified_outcome_is_never_counted_as_a_refusal() {
    let mut fixture = common::running();
    let action_id = fixture
        .action_id
        .clone()
        .expect("the running fixture has an action");
    fixture.must_apply(Command::DispatchAction {
        action_id: action_id.clone(),
        dispatch_id: DispatchId::new("dispatch_verified"),
    });
    let mut outcome = common::verified_outcome();
    outcome.dispatch_id = Some(DispatchId::new("dispatch_verified"));
    fixture.must_apply(Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(outcome),
    });
    assert_eq!(fixture.reducer.refusals().total(), 0);
}

#[test]
fn an_approval_the_task_is_parked_on_is_not_counted() {
    // `APPROVAL_REQUIRED` is not a refusal: it is an answer that has not
    // arrived. Counting it would abandon a task for waiting on a person, which
    // is the one thing this product asks tasks to do.
    let mut fixture = common::running();
    refuse_once(
        &mut fixture,
        0,
        LOOP_TOOL,
        Some(NODE),
        ActionResultCode::ApprovalRequired,
    )
    .expect("the proposal is admitted");
    assert_eq!(fixture.reducer.refusals().total(), 0);
}

#[test]
fn an_ordinary_tool_at_the_same_ceiling_is_refused() {
    // The contrast that makes the test above mean something. The exact-node
    // scroll reaches the ceiling by the same three refusals against the same node, and
    // the only thing separating the two verdicts is `tool::is_unconditional`.
    let mut fixture = common::running();
    for round in 0..rounds() {
        refuse_once(
            &mut fixture,
            round,
            LOOP_TOOL,
            Some(NODE),
            ActionResultCode::NodeGone,
        )
        .expect("the first three are admitted");
    }
    assert_eq!(
        refuse_once(
            &mut fixture,
            9,
            LOOP_TOOL,
            Some(NODE),
            ActionResultCode::NodeGone
        ),
        Err(RefusalReason::RepeatedRefusalsAbandoned)
    );
}

#[test]
fn a_rebuild_from_the_journal_reaches_the_same_counts() {
    // The count is state, and state that a rebuild did not re-derive would be
    // a guard that a process restart clears — which is a restart loop wearing
    // the shape of progress.
    let mut fixture = common::running();
    for round in 0..rounds() {
        refuse_once(
            &mut fixture,
            round,
            LOOP_TOOL,
            Some(NODE),
            ActionResultCode::NodeGone,
        )
        .expect("the first three are admitted");
    }
    let journal = fixture.reducer.journal().clone();
    let (rebuilt, _) = Reducer::replay(
        common::seed(),
        common::defaults(),
        ManualClock::at(1_000),
        SequentialIds::new(),
        &journal,
    )
    .expect("the journal replays");
    assert_eq!(
        rebuilt
            .refusals()
            .count_of(call(), ActionResultCode::NodeGone),
        MAX_IDENTICAL_REFUSALS
    );
    assert!(rebuilt.refusals().is_abandoned(call()));
    assert_eq!(rebuilt.refusals(), fixture.reducer.refusals());
}

#[test]
fn the_ledger_holds_numbers_and_nothing_a_caller_supplied() {
    // The fingerprint is what makes "the same call again" a comparison rather
    // than a retained copy of what was asked. Two calls that differ only in
    // their target differ here, and neither can be read back out.
    let with_node = CallFingerprint::of_target(LOOP_TOOL, Some("tab_1"), Some(NODE));
    let without = CallFingerprint::of_target(LOOP_TOOL, Some("tab_1"), None);
    assert_ne!(with_node.value(), without.value());
    let other_tab = CallFingerprint::of_target(LOOP_TOOL, Some("tab_2"), Some(NODE));
    assert_ne!(with_node.value(), other_tab.value());
}

/// Proposes one call and has policy refuse it with `code`, as `refuse_once`
/// has the browser refuse it. A denied proposal is never dispatched, so it
/// never gets an outcome.
fn deny_once(
    fixture: &mut common::Fixture,
    round: usize,
    code: ActionResultCode,
) -> Result<(), RefusalReason> {
    let existing = fixture
        .reducer
        .actions()
        .map(|action| action.action_id().clone())
        .collect::<Vec<_>>();
    let key = format!("denied_key_{round}");
    let envelope = fixture.envelope(Command::ProposeAction(Box::new(targeting(
        LOOP_TOOL,
        &key,
        Some(NODE),
    ))));
    fixture.reducer.apply(envelope).map_err(|it| it.reason)?;
    let action_id = fixture
        .reducer
        .actions()
        .find(|action| !existing.contains(action.action_id()))
        .map(|action| action.action_id().clone())
        .expect("the proposal minted an action");
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id,
        decision: Box::new(ProposalDecision::Deny(Denial::new(code))),
        dispatch_id: None,
    });
    Ok(())
}

/// A call policy keeps refusing is abandoned at the same count as one the
/// browser keeps refusing (decision 0233).
///
/// Decision 0054's ladder names a policy denial as the case it is written
/// for, and none ever reached it: the register was fed from the outcome
/// path, and a proposal policy denied never gets an outcome. The same call
/// could be proposed and denied for as long as the budget lasted.
#[test]
fn the_third_identical_policy_refusal_ends_the_attempt_too() {
    let mut fixture = common::running();
    for round in 0..rounds() {
        deny_once(&mut fixture, round, ActionResultCode::DeniedByPolicy)
            .expect("each of the first three proposals is admitted");
    }
    assert_eq!(
        fixture
            .reducer
            .refusals()
            .count_of(call(), ActionResultCode::DeniedByPolicy),
        MAX_IDENTICAL_REFUSALS
    );
    assert_eq!(
        deny_once(&mut fixture, 9, ActionResultCode::DeniedByPolicy),
        Err(RefusalReason::RepeatedRefusalsAbandoned),
        "a fourth proposal of a call policy refused three times is not admitted"
    );
    let (rebuilt, _) = Reducer::replay(
        common::seed(),
        common::defaults(),
        ManualClock::at(1_000),
        SequentialIds::new(),
        fixture.reducer.journal(),
    )
    .expect("the policy refusals replay");
    assert_eq!(rebuilt.refusals(), fixture.reducer.refusals());
    assert!(rebuilt.refusals().is_abandoned(call()));
}
