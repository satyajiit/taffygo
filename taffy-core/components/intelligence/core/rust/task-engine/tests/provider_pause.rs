// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A provider limit or a lost network pauses the task rather than ending it,
//! and a resume asks the model again (decision 0136).

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::transition::RefusalReason;
use task_engine::{
    Command, PauseCause, StateReason, TaskControlKind, TaskState, TurnGap, TurnPhase,
};

use common::agent::{record_turn, request_turn, Digest};

/// A running task whose only turn ended in the gap the ladder stops on.
fn after_gap() -> common::Fixture {
    let mut fixture = common::agent::running();
    fixture.must_apply(request_turn(&fixture));
    let call_id = fixture.reducer.model_turn().map_or_else(
        || unreachable!("the turn was just requested"),
        |turn| turn.call_id().clone(),
    );
    fixture.must_apply(Command::RecordModelTurnGap {
        call_id,
        gap: TurnGap::Unavailable,
    });
    fixture
}

#[test]
fn a_provider_limit_pauses_the_task_with_a_resume_on_offer() {
    let mut fixture = after_gap();
    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::ProviderLimit,
    });
    assert_eq!(fixture.state(), TaskState::Pausing);
    assert_eq!(
        fixture.reducer.task().state_reason(),
        Some(StateReason::ProviderPaused)
    );
    fixture.must_apply(Command::PauseSettled);
    assert_eq!(fixture.state(), TaskState::Paused);
    let controls = fixture.reducer.allowed_task_controls();
    assert!(controls.contains(&TaskControlKind::Resume), "{controls:?}");
    assert!(controls.contains(&TaskControlKind::Stop), "{controls:?}");
}

#[test]
fn going_offline_pauses_under_the_same_state_reason() {
    let mut fixture = after_gap();
    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::Offline,
    });
    assert_eq!(
        fixture.reducer.task().state_reason(),
        Some(StateReason::ProviderPaused)
    );
}

#[test]
fn a_resume_drops_the_gap_and_asks_the_model_again() {
    let mut fixture = after_gap();
    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::ProviderLimit,
    });
    fixture.must_apply(Command::PauseSettled);
    assert!(
        fixture
            .reducer
            .model_turn()
            .is_some_and(|turn| matches!(turn.phase(), TurnPhase::Gap(_))),
        "the paused task still holds the turn that ended in the gap"
    );

    fixture.must_apply(Command::ResumeTask);
    assert_eq!(fixture.state(), TaskState::Queued);
    assert!(fixture.reducer.model_turn().is_none());
    fixture.must_apply(Command::ExecutorStarted);

    // Row 8 to 10 would have failed the task for the same gap again; with the
    // turn gone, the first running row asks for a fresh one.
    let next = fixture
        .reducer
        .next_agent_command(None, &Digest)
        .unwrap_or_else(|error| panic!("{error:?}"));
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "{next:?}"
    );
}

#[test]
fn a_user_pause_waits_for_the_turn_in_flight_and_a_resume_keeps_the_reply() {
    let mut fixture = common::agent::running();
    fixture.must_apply(request_turn(&fixture));
    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::User,
    });
    assert_eq!(fixture.state(), TaskState::Pausing);
    // The pause cannot settle over a call the provider is still answering.
    let refused = fixture.apply(Command::PauseSettled);
    assert!(
        matches!(refused, Err(ref refusal) if refusal.reason == RefusalReason::ModelTurnInFlight),
        "{refused:?}"
    );
    fixture.must_apply(record_turn(&fixture));
    fixture.must_apply(Command::PauseSettled);
    assert_eq!(fixture.state(), TaskState::Paused);

    fixture.must_apply(Command::ResumeTask);
    assert_eq!(fixture.state(), TaskState::Queued);
    // A resume drops a gap and nothing else: a reply that landed is kept for
    // the table to read.
    assert!(fixture
        .reducer
        .model_turn()
        .is_some_and(|turn| !matches!(turn.phase(), TurnPhase::Gap(_))));
}
