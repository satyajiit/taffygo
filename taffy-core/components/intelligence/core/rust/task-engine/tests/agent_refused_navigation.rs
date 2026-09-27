// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A step the browser refused is the model's to answer, not the person's.
//!
//! On a phone an errand followed a link, the site answered with a redirect to
//! plain http, and the browser cancelled the request. The browser reported
//! that as `POSTCONDITION_FAILED`, which this taxonomy classes as an effect
//! that *may* have landed, so the action became `OUTCOME_UNKNOWN`, the reducer
//! asked for reconciliation, and the task handed itself to the person without
//! the model ever reading the refusal. The browser now reports its own
//! refusal as the refusal it is, and these tests pin what the core does with
//! each of the two codes (decision 0228).
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{DispatchId, MonotonicMillis};
use bip_types::ActionResultCode;
use task_engine::action::{ActionOutcome, ActionState};
use task_engine::{Command, Effect, ModelStopReason, TurnResidency};

use task_engine::agent::ModelToolCall;

use common::agent::{navigate_call, page, read_call, with_recorded_turn, Digest};
use common::Fixture;

/// Walks `call`, the one call of a recorded turn, to its dispatch and answers
/// it with `code`, handing back what the reducer did with the answer.
fn dispatched_and_answered(
    call: ModelToolCall,
    code: ActionResultCode,
) -> (Fixture, TurnResidency, Vec<Effect>) {
    let (mut fixture, residency) = with_recorded_turn(ModelStopReason::ToolCall, vec![call]);
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected a proposal, got {next:?}");
    };
    fixture.must_apply(Command::ProposeAction(proposal));
    let action_id = fixture
        .reducer
        .actions()
        .last()
        .map(|action| action.action_id().clone())
        .expect("the proposal minted an action");
    fixture.must_apply(Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(common::authorize()),
        dispatch_id: Some(DispatchId::new("dispatch_0")),
    });
    let answered = fixture
        .apply(Command::RecordActionOutcome {
            action_id,
            outcome: Box::new(ActionOutcome {
                code,
                dispatch_id: Some(DispatchId::new("dispatch_0")),
                observed_at: MonotonicMillis(10),
                observation: None,
                discovered_source: None,
            }),
        })
        .expect("the outcome is the dispatched action's own");
    (fixture, residency, answered.effects)
}

fn is_reconciliation(effect: &Effect) -> bool {
    matches!(effect, Effect::ReconcileAction { .. })
}

#[test]
fn a_request_the_browser_refused_ends_the_turn_and_the_model_is_asked_again() {
    for code in [
        ActionResultCode::EgressNotAuthorized,
        ActionResultCode::DestinationClassRestricted,
    ] {
        let (_, handle) = page();
        let (fixture, residency, effects) = dispatched_and_answered(read_call(handle), code);
        let action = fixture.reducer.actions().last().unwrap();
        assert_eq!(action.state(), ActionState::Failed, "{code:?}");
        assert!(
            !effects.iter().any(is_reconciliation),
            "{code:?} is settled; there is nothing to reconcile, got {effects:?}"
        );
        let next = fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap();
        assert!(
            matches!(next, Some(Command::RequestModelTurn { .. })),
            "{code:?} must reach the model, got {next:?}"
        );
    }
}

/// Why the browser may not report its own refusal under this code: the core
/// cannot tell it from an effect that landed unconfirmed, and it must not.
/// The step is a navigation, because a navigation can have landed; a read
/// cannot, and is the next test's subject.
#[test]
fn a_contradicted_postcondition_is_still_reconciled_before_anything_else() {
    let (fixture, _, effects) = dispatched_and_answered(
        navigate_call("https://example.test/next"),
        ActionResultCode::PostconditionFailed,
    );
    let action = fixture.reducer.actions().last().unwrap();
    assert_eq!(action.state(), ActionState::OutcomeUnknown);
    assert!(
        effects.iter().any(is_reconciliation),
        "an effect that may have landed is reconciled, got {effects:?}"
    );
}

/// A read has no side effect, so there is nothing for reconciliation to find.
/// On a phone a read whose answer the core could not decode came back
/// `OUTCOME_UNKNOWN`, was reconciled, and the bridge handed the errand to the
/// person for a page Taffy could have read again (decision 0234).
#[test]
fn a_reading_whose_answer_was_lost_goes_back_to_the_model() {
    for code in [
        ActionResultCode::OutcomeUnknown,
        ActionResultCode::PostconditionFailed,
    ] {
        let (_, handle) = page();
        let (fixture, residency, effects) = dispatched_and_answered(read_call(handle), code);
        let action = fixture.reducer.actions().last().unwrap();
        assert_eq!(action.state(), ActionState::OutcomeUnknown, "{code:?}");
        assert!(
            !effects.iter().any(is_reconciliation),
            "{code:?} on a read has nothing to reconcile, got {effects:?}"
        );
        let next = fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap();
        assert!(
            matches!(next, Some(Command::RequestModelTurn { .. })),
            "{code:?} on a read must reach the model, got {next:?}"
        );
    }
}
