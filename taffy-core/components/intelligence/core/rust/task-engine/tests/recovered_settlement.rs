// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A settled recovery remains replayable across the next process generation.
#![allow(clippy::unwrap_used, clippy::expect_used)]

mod common;

use bip_types::identity::{ActionId, DispatchId};
use task_engine::{
    ActionState, Command, CommandEnvelope, IdempotencyKey, ManualClock, PauseCause, Reducer,
    RefusalReason, TaskState, TraceId,
};

fn interrupted() -> (common::TestReducer, ActionId) {
    let mut fixture = common::running();
    let action_id = fixture
        .action_id
        .clone()
        .expect("the fixture has an action");
    fixture.must_apply(Command::DispatchAction {
        action_id: action_id.clone(),
        dispatch_id: DispatchId::new("dispatch_0"),
    });
    (fixture.reducer, action_id)
}

fn rebuild(reducer: &common::TestReducer) -> common::TestReducer {
    let (restored, recovery) = Reducer::replay(
        common::seed(),
        common::defaults(),
        ManualClock::at(1_000),
        task_engine::ids::SequentialIds::new(),
        reducer.journal(),
    )
    .expect("every committed generation must replay");
    assert_eq!(recovery.leases_restored, 0);
    assert_eq!(restored.task().revision(), reducer.task().revision());
    assert_eq!(restored.journal().entries(), reducer.journal().entries());
    restored
}

fn envelope(reducer: &common::TestReducer, command: Command) -> CommandEnvelope {
    CommandEnvelope::new(
        IdempotencyKey::new(format!("recovery-{}", reducer.task().revision())),
        reducer.task().revision(),
        TraceId::new("trace_test"),
        command,
    )
}

fn apply(reducer: &mut common::TestReducer, command: Command) {
    reducer
        .apply(envelope(reducer, command))
        .expect("the live recovered transition is accepted");
}

fn settled_recovery_survives(stop: Command, settle: Command, expected: TaskState) {
    let (first, action_id) = interrupted();
    let mut recovered = rebuild(&first);
    assert_eq!(
        recovered
            .action(&action_id)
            .map(task_engine::ActionRecord::state),
        Some(ActionState::OutcomeUnknown)
    );
    // An ambiguous browser reconciliation asks the person; it carries no
    // invented action result, and the person then stops or pauses the task.
    apply(&mut recovered, Command::RequestUserInput);
    apply(&mut recovered, stop);
    let settlement = envelope(&recovered, settle);
    recovered
        .apply(settlement.clone())
        .expect("settlement succeeds");
    assert_eq!(recovered.task().state(), expected);

    let second = rebuild(&recovered);
    let mut third = rebuild(&second);
    assert_eq!(third.task().state(), expected);
    assert!(third.task().terminal_result().is_none());
    let action = third.action(&action_id).expect("the action survives");
    assert_eq!(action.state(), ActionState::OutcomeUnknown);
    assert!(!action.is_verified());
    assert!(action.result().is_none());
    let duplicate = third
        .apply(settlement)
        .expect("the persisted receipt survives");
    assert!(duplicate.duplicate);
    assert!(duplicate.effects.is_empty());
}

#[test]
fn stopping_a_recovered_attempt_survives_two_further_restarts() {
    settled_recovery_survives(
        Command::CancelTask,
        Command::CancelSettled,
        TaskState::Cancelled,
    );
}

#[test]
fn pausing_a_recovered_attempt_survives_two_further_restarts() {
    settled_recovery_survives(
        Command::PauseTask {
            cause: PauseCause::User,
        },
        Command::PauseSettled,
        TaskState::Paused,
    );
}

#[test]
fn a_live_in_flight_attempt_still_blocks_both_settlements() {
    for (stop, settle) in [
        (Command::CancelTask, Command::CancelSettled),
        (
            Command::PauseTask {
                cause: PauseCause::User,
            },
            Command::PauseSettled,
        ),
    ] {
        let (mut live, _) = interrupted();
        apply(&mut live, stop);
        let refused = live
            .apply(envelope(&live, settle))
            .expect_err("work is in flight");
        assert_eq!(refused.reason, RefusalReason::ActionWorkStillInFlight);
    }
}

#[test]
fn recovery_without_a_committed_settlement_does_not_finish_the_stop() {
    let (first, _) = interrupted();
    let mut recovered = rebuild(&first);
    apply(&mut recovered, Command::CancelTask);
    let next = rebuild(&recovered);
    assert_eq!(next.task().state(), TaskState::Cancelling);
    assert!(next.task().terminal_result().is_none());
}
