// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The exhaustive transition table: every durable state against every command.
//!
//! The table in `task_engine::transition` is the documentation, and this file is
//! what keeps it honest. For each durable state times every closed command
//! commands it builds a real reducer in that state, applies a real payload, and
//! checks the reducer did what the table says.
//!
//! A cell whose guards a fixture cannot satisfy — proposing an action decision
//! for a draft that has never proposed one, for instance — must refuse with
//! that guard's own refusal. That is still a defined, documented outcome, which
//! is what the specification asks for.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::command::CommandKind;
use task_engine::task::TaskState;
use task_engine::transition::{disposition, Disposition};

use common::for_cell as fixture_for;

#[test]
fn every_state_times_every_command_has_the_outcome_the_table_names() {
    let mut accepted = 0_usize;
    let mut refused_by_table = 0_usize;
    let mut refused_by_guard = 0_usize;

    for state in TaskState::ALL {
        for kind in CommandKind::ALL {
            let cell = disposition(*state, *kind);
            let mut fixture = fixture_for(*state, cell.guards());
            assert_eq!(fixture.state(), *state, "fixture for {}", state.label());

            let command = common::command::command_for(*kind, &fixture);
            let envelope = fixture.envelope(command);
            let outcome = fixture.reducer.apply(envelope);

            match (cell, outcome) {
                (Disposition::Refused(expected), Err(refusal)) => {
                    assert_eq!(
                        refusal.reason,
                        expected,
                        "{} x {} refused for the wrong reason",
                        state.label(),
                        kind.label()
                    );
                    refused_by_table += 1;
                }
                (Disposition::Refused(expected), Ok(committed)) => panic!(
                    "{} x {} should refuse with {} but reached {}",
                    state.label(),
                    kind.label(),
                    expected.label(),
                    committed.to.label()
                ),
                (Disposition::Transition { targets, .. }, Ok(committed)) => {
                    assert!(
                        targets.contains(&committed.to),
                        "{} x {} reached {} which the table does not list",
                        state.label(),
                        kind.label(),
                        committed.to.label()
                    );
                    assert_eq!(committed.from, *state);
                    assert!(
                        committed
                            .events
                            .iter()
                            .any(task_engine::TaskEvent::is_transition),
                        "{} x {} moved without a cause event",
                        state.label(),
                        kind.label()
                    );
                    accepted += 1;
                }
                (Disposition::RecordedOrTransition { targets, .. }, Ok(committed)) => {
                    assert!(
                        committed.to == *state || targets.contains(&committed.to),
                        "{} x {} reached {} which the table does not list",
                        state.label(),
                        kind.label(),
                        committed.to.label()
                    );
                    assert_eq!(committed.from, *state);
                    assert_eq!(
                        committed
                            .events
                            .iter()
                            .any(task_engine::TaskEvent::is_transition),
                        committed.to != *state,
                        "{} x {} transition event disagrees with its state change",
                        state.label(),
                        kind.label()
                    );
                    accepted += 1;
                }
                (Disposition::Recorded { .. }, Ok(committed)) => {
                    assert_eq!(
                        committed.to,
                        *state,
                        "{} x {} moved a task the table says it records",
                        state.label(),
                        kind.label()
                    );
                    accepted += 1;
                }
                (cell, Err(refusal)) => {
                    let permitted: Vec<_> =
                        cell.guards().iter().map(|guard| guard.refusal()).collect();
                    assert!(
                        permitted.contains(&refusal.reason),
                        "{} x {} refused with {}, which is not one of its guards",
                        state.label(),
                        kind.label(),
                        refusal.reason.label()
                    );
                    refused_by_guard += 1;
                }
            }
        }
    }

    let cells = TaskState::ALL.len() * CommandKind::ALL.len();
    assert_eq!(accepted + refused_by_table + refused_by_guard, cells);
    assert_eq!(cells, TaskState::ALL.len() * CommandKind::ALL.len());
    // Most cells are refusals, which is the point of a fail-closed machine.
    assert!(accepted > 0 && refused_by_table > 0);
}

#[test]
fn every_transition_the_table_names_is_actually_reachable() {
    for state in TaskState::ALL {
        for kind in CommandKind::ALL {
            let cell = disposition(*state, *kind);
            let (targets, guards, conditional) = match cell {
                Disposition::Transition { targets, guards } => (targets, guards, false),
                Disposition::RecordedOrTransition { targets, guards } => (targets, guards, true),
                Disposition::Recorded { .. } | Disposition::Refused(_) => continue,
            };
            let mut fixture = fixture_for(*state, guards);
            let command = if conditional && *kind == CommandKind::RecordPolicyDecision {
                task_engine::Command::RecordPolicyDecision {
                    action_id: fixture
                        .action_id
                        .clone()
                        .unwrap_or_else(|| unreachable!("fixture has a known action")),
                    decision: Box::new(task_engine::ProposalDecision::RequireApproval),
                    dispatch_id: None,
                }
            } else {
                common::command::command_for(*kind, &fixture)
            };
            let envelope = fixture.envelope(command);
            let committed = fixture.reducer.apply(envelope).unwrap_or_else(|refusal| {
                panic!(
                    "{} x {} is a transition the fixtures cannot reach: {refusal:?}",
                    state.label(),
                    kind.label()
                )
            });
            assert!(targets.contains(&committed.to));
        }
    }
}

#[test]
fn a_command_written_against_a_stale_revision_is_refused() {
    let mut fixture = common::running();
    let stale = task_engine::CommandEnvelope::new(
        task_engine::IdempotencyKey::new("stale"),
        fixture.reducer.task().revision().saturating_sub(1),
        task_engine::TraceId::new("trace_test"),
        task_engine::Command::RequestUserInput,
    );
    let refusal = fixture
        .reducer
        .apply(stale)
        .err()
        .unwrap_or_else(|| unreachable!("a stale revision is refused"));
    assert_eq!(refusal.reason, task_engine::RefusalReason::RevisionConflict);
    assert_eq!(fixture.state(), TaskState::Running);
}

#[test]
fn a_duplicate_command_returns_the_original_result_and_performs_nothing() {
    let mut fixture = common::running();
    let envelope = fixture.envelope(task_engine::Command::PauseTask {
        cause: task_engine::PauseCause::User,
    });
    let first = fixture
        .reducer
        .apply(envelope.clone())
        .unwrap_or_else(|refusal| unreachable!("pausing a running task: {refusal:?}"));
    assert!(!first.duplicate);
    assert!(!first.effects.is_empty());
    let journal_len = fixture.reducer.journal().len();

    let second = fixture
        .reducer
        .apply(envelope)
        .unwrap_or_else(|refusal| unreachable!("a repeat returns the original: {refusal:?}"));
    assert!(second.duplicate);
    assert_eq!(second.to, first.to);
    assert_eq!(second.revision, first.revision);
    assert!(
        second.effects.is_empty(),
        "a duplicate must perform no effect"
    );
    assert_eq!(
        fixture.reducer.journal().len(),
        journal_len,
        "a duplicate must append nothing"
    );
}

#[test]
fn completed_is_unreachable_with_a_result_that_has_a_gap() {
    let mut fixture = common::in_state(TaskState::Completing, false);
    let envelope = fixture.envelope(task_engine::Command::CompleteResultValidated(
        common::partial_result(),
    ));
    let refusal = fixture
        .reducer
        .apply(envelope)
        .err()
        .unwrap_or_else(|| unreachable!("a gapped result cannot complete"));
    assert_eq!(
        refusal.reason,
        task_engine::RefusalReason::ResultHasUnmetRequirements
    );
    assert_eq!(fixture.state(), TaskState::Completing);

    let envelope = fixture.envelope(task_engine::Command::PartialResultValidated(
        common::partial_result(),
    ));
    let committed = fixture
        .reducer
        .apply(envelope)
        .unwrap_or_else(|refusal| unreachable!("a gapped result is partial: {refusal:?}"));
    assert_eq!(committed.to, TaskState::Partial);
    assert_eq!(
        fixture.reducer.task().display_state(),
        Some(task_engine::DisplayState::PartlyDone)
    );
}

#[test]
fn an_approval_that_is_no_longer_current_is_refused() {
    let mut fixture = common::in_state(TaskState::AwaitingConsent, true);
    let envelope = fixture.envelope(task_engine::Command::ApproveAction {
        approval: common::receipt(),
        still_current: false,
        expires_at_monotonic_ms: 2_000,
        expires_at_utc_ms: 2_000,
        browser_session_id: task_engine::BrowserSessionId::new("browser_session_1")
            .unwrap_or_else(|_| unreachable!()),
    });
    let refusal = fixture
        .reducer
        .apply(envelope)
        .err()
        .unwrap_or_else(|| unreachable!("a stale approval is refused"));
    assert_eq!(
        refusal.reason,
        task_engine::RefusalReason::ApprovalNoLongerCurrent
    );
    assert_eq!(fixture.state(), TaskState::AwaitingConsent);
}

#[test]
fn permission_result_requires_the_exact_pending_request() {
    let mut fixture = common::running();
    fixture.must_apply(task_engine::Command::RequestPermission(
        common::permission_request(),
    ));
    let wrong = task_engine::PermissionResult::new(
        task_engine::PermissionRequestId::new("permission_wrong")
            .unwrap_or_else(|_| unreachable!()),
        task_engine::PlatformPermission::Notifications,
        task_engine::PermissionDecision::Granted,
    );
    let envelope = fixture.envelope(task_engine::Command::RecordPermissionResult(wrong));
    let refusal = fixture
        .reducer
        .apply(envelope)
        .err()
        .unwrap_or_else(|| unreachable!());
    assert_eq!(
        refusal.reason,
        task_engine::RefusalReason::PermissionResultMismatch
    );
    assert_eq!(fixture.state(), TaskState::WaitingUser);
}

#[test]
fn permission_result_is_durable_and_duplicate_safe() {
    let mut fixture = common::running();
    let request = fixture.envelope(task_engine::Command::RequestPermission(
        common::permission_request(),
    ));
    let requested = fixture
        .reducer
        .apply(request)
        .unwrap_or_else(|_| unreachable!());
    assert!(matches!(
        requested.effects.as_slice(),
        [task_engine::Effect::RequestPermission { .. }]
    ));
    let result = fixture.envelope(task_engine::Command::RecordPermissionResult(
        common::permission_result(),
    ));
    let accepted = fixture
        .reducer
        .apply(result.clone())
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(accepted.to, TaskState::Running);
    assert!(fixture.reducer.pending_permission().is_none());
    let duplicate = fixture
        .reducer
        .apply(result)
        .unwrap_or_else(|_| unreachable!());
    assert!(duplicate.duplicate);
    assert!(duplicate.effects.is_empty());
}

#[test]
fn the_initial_consent_and_an_in_task_approval_are_not_interchangeable() {
    let mut initial = common::in_state(TaskState::AwaitingConsent, false);
    let envelope = initial.envelope(task_engine::Command::ApproveAction {
        approval: common::receipt(),
        still_current: true,
        expires_at_monotonic_ms: 2_000,
        expires_at_utc_ms: 2_000,
        browser_session_id: task_engine::BrowserSessionId::new("browser_session_1")
            .unwrap_or_else(|_| unreachable!()),
    });
    assert_eq!(
        initial.reducer.apply(envelope).err().map(|r| r.reason),
        Some(task_engine::RefusalReason::NotAwaitingInTaskApproval)
    );

    let mut in_task = common::in_state(TaskState::AwaitingConsent, true);
    let envelope = in_task.envelope(task_engine::Command::AcceptInitialConsent(common::receipt()));
    assert_eq!(
        in_task.reducer.apply(envelope).err().map(|r| r.reason),
        Some(task_engine::RefusalReason::NotAwaitingInitialConsent)
    );
}

#[test]
fn the_display_state_of_every_reachable_task_is_the_one_the_table_names() {
    for state in TaskState::ALL {
        let fixture = common::in_state(*state, false);
        assert_eq!(
            fixture.reducer.task().display_state(),
            state.display(),
            "{}",
            state.label()
        );
    }
}
