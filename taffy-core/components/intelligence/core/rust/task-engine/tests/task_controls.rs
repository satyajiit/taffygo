// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Truthful task controls are derived from reducer state, not display phases.

mod common;

use task_engine::{Command, ManualClock, PauseCause, Reducer, SequentialIds, TaskControlKind};

fn replay(fixture: &common::Fixture) -> Reducer<ManualClock, SequentialIds> {
    Reducer::replay(
        common::seed(),
        common::defaults(),
        ManualClock::at(1_000),
        SequentialIds::new(),
        fixture.reducer.journal(),
    )
    .unwrap_or_else(|error| unreachable!("a reducer-written journal replays: {error:?}"))
    .0
}

#[test]
fn running_and_waiting_tasks_offer_only_real_transitions() {
    let running = common::running();
    assert_eq!(
        running.reducer.allowed_task_controls(),
        [
            TaskControlKind::Pause,
            TaskControlKind::TakeOver,
            TaskControlKind::Stop,
        ]
    );

    let waiting = common::in_state(task_engine::TaskState::WaitingUser, false);
    assert_eq!(
        waiting.reducer.allowed_task_controls(),
        [
            TaskControlKind::Pause,
            TaskControlKind::TakeOver,
            TaskControlKind::Stop,
        ]
    );
}

#[test]
fn initial_consent_offers_only_stop_and_cannot_be_paused() {
    let mut fixture = common::in_state(task_engine::TaskState::AwaitingConsent, false);
    assert_eq!(
        fixture.reducer.allowed_task_controls(),
        [TaskControlKind::Stop]
    );
    let refusal = fixture
        .apply(Command::PauseTask {
            cause: PauseCause::User,
        })
        .err()
        .unwrap_or_else(|| unreachable!("a task with no consent cannot become resumable"));
    assert_eq!(
        refusal.reason,
        task_engine::RefusalReason::NotAwaitingInTaskApproval
    );
    fixture.must_apply(Command::CancelTask);
    assert_eq!(fixture.state(), task_engine::TaskState::Cancelling);
    assert!(fixture.reducer.allowed_task_controls().is_empty());
}

#[test]
fn user_pause_is_resumable_only_after_settlement_and_after_restart() {
    let mut fixture = common::running();
    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::User,
    });
    assert!(fixture.reducer.allowed_task_controls().is_empty());

    fixture.must_apply(Command::PauseSettled);
    assert_eq!(
        fixture.reducer.allowed_task_controls(),
        [TaskControlKind::Resume, TaskControlKind::Stop]
    );
    assert_eq!(
        replay(&fixture).allowed_task_controls(),
        [TaskControlKind::Resume, TaskControlKind::Stop]
    );
}

#[test]
fn replayed_pause_command_is_duplicate_without_a_second_settlement() {
    let mut fixture = common::running();
    let envelope = fixture.envelope(Command::PauseTask {
        cause: PauseCause::User,
    });

    let first = fixture
        .reducer
        .apply(envelope.clone())
        .unwrap_or_else(|_| unreachable!());
    let revision = fixture.reducer.task().revision();
    let duplicate = fixture
        .reducer
        .apply(envelope)
        .unwrap_or_else(|_| unreachable!());

    assert!(!first.duplicate);
    assert!(!first.effects.is_empty());
    assert!(duplicate.duplicate);
    assert!(duplicate.effects.is_empty());
    assert_eq!(duplicate.revision, revision);
    assert!(fixture.reducer.allowed_task_controls().is_empty());
}

#[test]
fn takeover_settles_to_the_same_explicit_resume_choice() {
    let mut fixture = common::running();
    fixture.must_apply(Command::TakeOver);
    fixture.must_apply(Command::PauseSettled);
    assert_eq!(
        fixture.reducer.allowed_task_controls(),
        [TaskControlKind::Resume, TaskControlKind::Stop]
    );
}

/// A platform hold is the *most* resumable one, not the least.
///
/// This asserted the opposite, on the reasoning that the platform rather than
/// the person had decided. The reasoning does not survive asking who is
/// reading the pill: `BackgroundRestricted` is raised because the app is not
/// in front of anybody, so by the time a Paused pill can be read at all the
/// condition that caused it has ended. Offering nothing there left a task held
/// forever with no way out.
#[test]
fn a_platform_pause_offers_the_resume_that_lifts_it() {
    let mut fixture = common::running();
    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::BackgroundRestricted,
    });
    fixture.must_apply(Command::PauseSettled);
    assert_eq!(
        fixture.reducer.allowed_task_controls(),
        [TaskControlKind::Resume, TaskControlKind::Stop]
    );
    assert_eq!(
        replay(&fixture).allowed_task_controls(),
        [TaskControlKind::Resume, TaskControlKind::Stop]
    );
}

#[test]
fn automatic_approval_settlement_does_not_expose_resume() {
    let mut fixture = common::in_state(task_engine::TaskState::AwaitingConsent, true);
    fixture.must_apply(Command::DenyAction {
        approval: common::receipt(),
    });
    fixture.must_apply(Command::PauseSettled);

    assert_eq!(
        fixture.reducer.allowed_task_controls(),
        [TaskControlKind::Stop]
    );
    assert_eq!(
        replay(&fixture).allowed_task_controls(),
        [TaskControlKind::Stop]
    );
}
