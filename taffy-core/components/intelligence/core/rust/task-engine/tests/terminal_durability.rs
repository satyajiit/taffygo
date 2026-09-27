// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Terminal task transitions across in-flight and ambiguous action outcomes.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::task::TaskState;

fn dispatch_running_action(fixture: &mut common::Fixture) -> bip_types::identity::ActionId {
    let action_id = fixture
        .action_id
        .clone()
        .unwrap_or_else(|| unreachable!("the running fixture has an action"));
    fixture.must_apply(task_engine::Command::DispatchAction {
        action_id: action_id.clone(),
        dispatch_id: bip_types::identity::DispatchId::new("dispatch_0"),
    });
    action_id
}

fn unknown_outcome() -> task_engine::ActionOutcome {
    task_engine::ActionOutcome {
        code: bip_types::ActionResultCode::OutcomeUnknown,
        dispatch_id: Some(bip_types::identity::DispatchId::new("dispatch_0")),
        observed_at: bip_types::identity::MonotonicMillis(20),
        observation: None,
        discovered_source: None,
    }
}

#[test]
fn cancellation_settles_only_after_in_flight_action_work_returns() {
    let mut fixture = common::running();
    let action_id = dispatch_running_action(&mut fixture);
    fixture.must_apply(task_engine::Command::CancelTask);

    let settle = fixture.envelope(task_engine::Command::CancelSettled);
    let refusal = fixture
        .reducer
        .apply(settle)
        .err()
        .unwrap_or_else(|| unreachable!("an in-flight action blocks cancellation settlement"));
    assert_eq!(
        refusal.reason,
        task_engine::RefusalReason::ActionWorkStillInFlight
    );
    assert_eq!(fixture.state(), TaskState::Cancelling);

    let terminal = fixture.envelope(task_engine::Command::RecordActionOutcome {
        action_id: action_id.clone(),
        outcome: Box::new(unknown_outcome()),
    });
    let recorded = fixture
        .reducer
        .apply(terminal)
        .unwrap_or_else(|refusal| unreachable!("the exact terminal is recorded: {refusal:?}"));
    assert!(
        recorded.effects.is_empty(),
        "settling work must not start reconciliation after authority revocation"
    );
    assert_eq!(
        fixture
            .reducer
            .action(&action_id)
            .map(task_engine::ActionRecord::state),
        Some(task_engine::ActionState::OutcomeUnknown)
    );

    fixture.must_apply(task_engine::Command::CancelSettled);
    assert_eq!(fixture.state(), TaskState::Cancelled);
}

/// A running task whose one authorized action goes back in its tab — a move
/// whose unknown outcome is reconciled, unlike the read `common::running`
/// authorizes.
fn running_back() -> common::Fixture {
    let mut fixture = common::draft();
    fixture.must_apply(task_engine::Command::StartTask(common::preview()));
    fixture.must_apply(task_engine::Command::AcceptInitialConsent(common::receipt()));
    fixture.must_apply(task_engine::Command::ExecutorStarted);
    fixture.must_apply(task_engine::Command::SetPlan(common::plan_draft()));
    fixture.must_apply(task_engine::Command::ProposeAction(Box::new(
        common::proposal_for(
            task_engine::action::BrowserIntent::HistoryBack {
                tab: bip_types::identity::TabId::new("tab_1"),
            },
            "action_key_0",
        ),
    )));
    let action_id = fixture.reducer.actions().next().map_or_else(
        || unreachable!("the proposal minted an action"),
        |action| action.action_id().clone(),
    );
    fixture.must_apply(task_engine::Command::RecordPolicyDecision {
        action_id: action_id.clone(),
        decision: Box::new(common::authorize()),
        dispatch_id: None,
    });
    fixture.action_id = Some(action_id);
    fixture
}

#[test]
fn completed_requires_action_work_to_be_terminal_and_unambiguous() {
    let mut in_flight = running_back();
    let action_id = dispatch_running_action(&mut in_flight);
    in_flight.must_apply(task_engine::Command::ResultCandidateReady);

    let complete = in_flight.envelope(task_engine::Command::CompleteResultValidated(
        common::complete_result(),
    ));
    let refusal = in_flight
        .reducer
        .apply(complete)
        .err()
        .unwrap_or_else(|| unreachable!("in-flight action work cannot be called complete"));
    assert_eq!(
        refusal.reason,
        task_engine::RefusalReason::ActionWorkStillInFlight
    );

    let terminal = in_flight.envelope(task_engine::Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(unknown_outcome()),
    });
    in_flight
        .reducer
        .apply(terminal)
        .unwrap_or_else(|refusal| unreachable!("the ambiguous terminal is recorded: {refusal:?}"));
    let complete = in_flight.envelope(task_engine::Command::CompleteResultValidated(
        common::complete_result(),
    ));
    let refusal = in_flight
        .reducer
        .apply(complete)
        .err()
        .unwrap_or_else(|| unreachable!("an ambiguous action cannot be called complete"));
    assert_eq!(
        refusal.reason,
        task_engine::RefusalReason::ActionOutcomeUnresolved
    );

    let mut verified = common::running();
    let action_id = dispatch_running_action(&mut verified);
    verified.must_apply(task_engine::Command::ResultCandidateReady);
    verified.must_apply(task_engine::Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(common::verified_outcome()),
    });
    verified.must_apply(task_engine::Command::CompleteResultValidated(
        common::complete_result(),
    ));
    assert_eq!(verified.state(), TaskState::Completed);
}

/// A read whose answer was lost is never reconciled (decision 0234), so it
/// cannot hold the ending back either: the walk plans the ending, and a
/// refusal of a walk's own command takes the core with it (decision 0248).
#[test]
fn a_read_of_unknown_outcome_does_not_hold_the_ending_back() {
    let mut partial = common::running();
    let action_id = dispatch_running_action(&mut partial);
    partial.must_apply(task_engine::Command::RecordActionOutcome {
        action_id: action_id.clone(),
        outcome: Box::new(unknown_outcome()),
    });
    assert_eq!(
        partial
            .reducer
            .action(&action_id)
            .map(task_engine::ActionRecord::state),
        Some(task_engine::ActionState::OutcomeUnknown)
    );
    partial.must_apply(task_engine::Command::ResultCandidateReady);
    partial.must_apply(task_engine::Command::PartialResultValidated(
        common::partial_result(),
    ));
    assert_eq!(partial.state(), TaskState::Partial);

    let mut complete = common::running();
    let action_id = dispatch_running_action(&mut complete);
    complete.must_apply(task_engine::Command::RecordActionOutcome {
        action_id,
        outcome: Box::new(unknown_outcome()),
    });
    complete.must_apply(task_engine::Command::ResultCandidateReady);
    complete.must_apply(task_engine::Command::CompleteResultValidated(
        common::complete_result(),
    ));
    assert_eq!(complete.state(), TaskState::Completed);
}

#[test]
fn an_empty_research_result_cannot_be_promoted_to_complete() {
    let mut fixture = common::in_state(TaskState::Completing, false);
    let envelope = fixture.envelope(task_engine::Command::CompleteResultValidated(
        task_engine::TaskResult::default(),
    ));
    let refusal = fixture
        .reducer
        .apply(envelope)
        .expect_err("empty research is not a complete result");
    assert_eq!(
        refusal.reason,
        task_engine::RefusalReason::ResultHasUnmetRequirements
    );
}

#[test]
fn a_terminal_task_records_a_late_ambiguous_outcome_without_new_effects() {
    let mut fixture = common::running();
    let action_id = dispatch_running_action(&mut fixture);
    fixture.must_apply(task_engine::Command::FailTask {
        reason: task_engine::FailureReason::UnverifiableAction,
    });
    assert_eq!(fixture.state(), TaskState::Failed);

    let terminal = fixture.envelope(task_engine::Command::RecordActionOutcome {
        action_id: action_id.clone(),
        outcome: Box::new(unknown_outcome()),
    });
    let recorded = fixture
        .reducer
        .apply(terminal)
        .unwrap_or_else(|refusal| unreachable!("the exact late terminal is recorded: {refusal:?}"));
    assert_eq!(recorded.to, TaskState::Failed);
    assert!(recorded.effects.is_empty());
    assert_eq!(
        fixture
            .reducer
            .action(&action_id)
            .map(task_engine::ActionRecord::state),
        Some(task_engine::ActionState::OutcomeUnknown)
    );
}
