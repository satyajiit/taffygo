// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A finished task keeps its conversation: a follow-up reopens it (decision
//! 0137).

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::transition::RefusalReason;
use task_engine::{Command, EventKind, StateReason, TaskState};

use common::agent::{record_turn, request_turn, Digest};

fn completed() -> common::Fixture {
    let mut fixture = common::agent::running();
    fixture.must_apply(request_turn(&fixture));
    fixture.must_apply(record_turn(&fixture));
    fixture.must_apply(Command::ResultCandidateReady);
    fixture.must_apply(Command::CompleteResultValidated(common::complete_result()));
    assert_eq!(fixture.state(), TaskState::Completed);
    fixture
}

#[test]
fn a_follow_up_reopens_a_completed_task_and_asks_the_model_again() {
    let mut fixture = completed();
    let accepted = fixture
        .apply(Command::FollowUp)
        .unwrap_or_else(|refusal| panic!("{refusal:?}"));
    assert!(accepted
        .events
        .iter()
        .any(|event| event.kind == EventKind::FollowUpAsked));
    assert_eq!(fixture.state(), TaskState::Running);
    assert_eq!(
        fixture.reducer.task().state_reason(),
        Some(StateReason::FollowUpAsked)
    );
    // The recorded reply that finished the last question is gone, so the
    // agent table's first running row asks for a turn rather than offering
    // that reply as the answer again.
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
fn a_follow_up_reopens_a_partial_task_too() {
    let mut fixture = common::agent::running();
    fixture.must_apply(request_turn(&fixture));
    fixture.must_apply(record_turn(&fixture));
    fixture.must_apply(Command::ResultCandidateReady);
    fixture.must_apply(Command::PartialResultValidated(common::partial_result()));
    assert_eq!(fixture.state(), TaskState::Partial);
    fixture.must_apply(Command::FollowUp);
    assert_eq!(fixture.state(), TaskState::Running);
}

#[test]
fn a_task_still_working_refuses_a_follow_up_as_not_finished() {
    let mut fixture = common::agent::running();
    let refused = fixture
        .apply(Command::FollowUp)
        .err()
        .unwrap_or_else(|| panic!("a running task is not finished"));
    assert_eq!(refused.reason, RefusalReason::NotFinished);
}

#[test]
fn a_failed_or_cancelled_task_has_no_conversation_to_continue() {
    for state in [TaskState::Failed, TaskState::Cancelled] {
        let mut fixture = common::in_state(state, false);
        let refused = fixture
            .apply(Command::FollowUp)
            .err()
            .unwrap_or_else(|| panic!("{state:?} is terminal"));
        assert_eq!(refused.reason, RefusalReason::TaskIsTerminal, "{state:?}");
    }
}

#[test]
fn a_follow_up_after_completion_replays_to_running() {
    let mut fixture = completed();
    fixture.must_apply(Command::FollowUp);
    let (replayed, _) = task_engine::Reducer::replay(
        common::seed(),
        common::defaults(),
        task_engine::ManualClock::at(1_000),
        task_engine::SequentialIds::new(),
        fixture.reducer.journal(),
    )
    .unwrap_or_else(|error| panic!("{error:?}"));
    assert_eq!(replayed.task().state(), TaskState::Running);
    assert_eq!(
        replayed.task().state_reason(),
        Some(StateReason::FollowUpAsked)
    );
}
