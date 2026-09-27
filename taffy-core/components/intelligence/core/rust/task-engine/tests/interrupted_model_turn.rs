// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A model call the dead generation was holding is a gap (decision 0150).
//!
//! This is a wedge with no way out, and it was found on a phone. A task paused
//! while a model turn was in flight — which is what Android's background
//! restriction does to a task that is thinking — came back from the journal in
//! `PAUSING`, still holding the call. `PauseSettled` is guarded on
//! `NoModelTurnInFlight`, so the browser's settlement was refused on every
//! start, for the life of the profile. `PAUSING` offers no controls at all:
//! not Resume, not even Stop. The bar showed "Paused" and the task view showed
//! nothing to press, and there was no sequence of taps that could change it.
#![allow(clippy::unwrap_used, clippy::expect_used)]

mod common;

use task_engine::{
    Command, CommandEnvelope, IdempotencyKey, ManualClock, PauseCause, Reducer, TaskControlKind,
    TaskState, TraceId,
};

fn paused_mid_turn() -> common::Fixture {
    let mut fixture = common::agent::running();
    let turn = common::agent::request_turn(&fixture);
    fixture.must_apply(turn);
    assert!(fixture.reducer.model_turn_in_flight());
    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::BackgroundRestricted,
    });
    assert_eq!(fixture.state(), TaskState::Pausing);
    fixture
}

fn replayed(fixture: &common::Fixture) -> (common::TestReducer, task_engine::Recovery) {
    Reducer::replay(
        common::seed(),
        common::defaults(),
        ManualClock::at(1_000),
        task_engine::ids::SequentialIds::new(),
        fixture.reducer.journal(),
    )
    .expect("a committed generation replays")
}

fn envelope(reducer: &common::TestReducer, command: Command) -> CommandEnvelope {
    CommandEnvelope::new(
        IdempotencyKey::new(format!("recovered-{}", reducer.task().revision())),
        reducer.task().revision(),
        TraceId::new("recovered"),
        command,
    )
}

/// The whole of the defect, and the whole of the fix.
#[test]
fn a_pause_taken_mid_turn_settles_after_the_generation_holding_the_call_is_gone() {
    let fixture = paused_mid_turn();
    let (mut restored, recovery) = replayed(&fixture);

    assert_eq!(recovery.state, TaskState::Pausing);
    assert!(
        recovery.interrupted_model_call.is_some(),
        "the recovery names the call it gave up on",
    );
    assert!(
        !restored.model_turn_in_flight(),
        "a call nothing can deliver to is not in flight",
    );

    restored
        .apply(envelope(&restored, Command::PauseSettled))
        .expect("the settlement the browser sends on every start is accepted");
    assert_eq!(restored.task().state(), TaskState::Paused);
    assert_eq!(
        restored.allowed_task_controls(),
        [TaskControlKind::Resume, TaskControlKind::Stop],
        "and the person gets the task back",
    );
}

/// The stop half of the same wedge, because `CANCELLING` is guarded the same
/// way and offers just as little.
#[test]
fn a_stop_taken_mid_turn_also_settles() {
    let mut fixture = common::agent::running();
    let turn = common::agent::request_turn(&fixture);
    fixture.must_apply(turn);
    fixture.must_apply(Command::CancelTask);
    assert_eq!(fixture.state(), TaskState::Cancelling);

    let (mut restored, _) = replayed(&fixture);
    restored
        .apply(envelope(&restored, Command::CancelSettled))
        .expect("a stop settles across the generation that was cut off");
    assert_eq!(restored.task().state(), TaskState::Cancelled);
}

/// **The settlement the first replay admitted has to survive the second.**
///
/// This is the half that was missing, and the phone found it: the fix let a
/// live reducer accept `PauseSettled` for a task it had just given up a call
/// on, that acceptance was committed to the journal, and the next start
/// replayed the journal from the beginning — where the call is in flight
/// again, because the giving-up is not a command anybody sent. The settlement
/// was refused, the replay failed, and the core would not start at all:
/// `[taffy_core_initialization_refused] reason=task-restore`. A journal that
/// cannot be replayed is worse than the wedge it was meant to lift, because
/// it takes the whole profile with it.
#[test]
fn the_settlement_that_lifted_the_wedge_replays_for_ever_after() {
    let fixture = paused_mid_turn();
    let (mut restored, _) = replayed(&fixture);
    restored
        .apply(envelope(&restored, Command::PauseSettled))
        .expect("the settlement is accepted after the call is given up on");
    assert_eq!(restored.task().state(), TaskState::Paused);

    // Every generation after it, from the same journal.
    let mut generation = restored;
    for _ in 0..3 {
        let (next, recovery) = Reducer::replay(
            common::seed(),
            common::defaults(),
            ManualClock::at(1_000),
            task_engine::ids::SequentialIds::new(),
            generation.journal(),
        )
        .expect("the committed settlement replays");
        assert_eq!(recovery.state, TaskState::Paused);
        assert_eq!(next.task().revision(), generation.task().revision());
        assert_eq!(next.journal().entries(), generation.journal().entries());
        generation = next;
    }
    assert_eq!(
        generation.allowed_task_controls(),
        [TaskControlKind::Resume, TaskControlKind::Stop],
    );
}

/// And the stop that follows it, which is the sequence a person actually
/// performed on the phone: the task came back wedged, settled, and was stopped.
#[test]
fn a_stop_after_the_recovered_pause_replays_too() {
    let fixture = paused_mid_turn();
    let (mut restored, _) = replayed(&fixture);
    for command in [
        Command::PauseSettled,
        Command::CancelTask,
        Command::CancelSettled,
    ] {
        restored
            .apply(envelope(&restored, command))
            .expect("each step is accepted live");
    }
    assert_eq!(restored.task().state(), TaskState::Cancelled);

    let (next, recovery) = Reducer::replay(
        common::seed(),
        common::defaults(),
        ManualClock::at(1_000),
        task_engine::ids::SequentialIds::new(),
        restored.journal(),
    )
    .expect("a stopped task replays");
    assert_eq!(recovery.state, TaskState::Cancelled);
    assert_eq!(next.journal().entries(), restored.journal().entries());
}

/// A recorded turn is not touched, so the mark is about the interruption and
/// never about having replayed.
#[test]
fn a_turn_that_was_answered_before_the_pause_is_left_alone() {
    let (mut fixture, _) =
        common::agent::with_recorded_turn(task_engine::ModelStopReason::Complete, Vec::new());
    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::User,
    });

    let (_, recovery) = replayed(&fixture);
    assert_eq!(recovery.interrupted_model_call, None);
}

/// And a task with no turn at all replays with nothing to say about one.
#[test]
fn a_task_that_never_called_a_model_names_no_call() {
    let mut fixture = common::agent::running();
    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::User,
    });

    let (_, recovery) = replayed(&fixture);
    assert_eq!(recovery.interrupted_model_call, None);
}
