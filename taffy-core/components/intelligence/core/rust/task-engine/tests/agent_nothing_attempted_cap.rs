// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Row 21j: turns that name calls and attempt none of them are bounded.
//!
//! A call refused on sight never reaches policy, the browser or a page. The
//! model is told which clause refused it and gets a fresh turn, and that turn
//! is paid for. Nothing counted them: `TurnDigest::every_call_refused_on_sight`
//! has said "this turn attempted nothing" since it was written, and its only
//! reader was a test. A phone measured the cost on 2026-09-19 — an errand that
//! had reached the page it wanted spent turn after turn this way, every reply
//! refused before it left the process (decision 0198).

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::{
    Command, ModelStopReason, ModelToolCall, TurnResidency, MAX_TURNS_ATTEMPTING_NOTHING,
};

use common::agent::{page, reply, Digest};
use common::errand::prepared_errand;

/// One turn whose single call names a tool this errand does not admit, so it
/// is refused on sight and nothing is proposed.
fn turn_attempting_nothing(fixture: &mut common::Fixture) -> TurnResidency {
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let (page, _) = page();
    let residency = TurnResidency::read(
        call_id,
        page,
        reply(
            ModelStopReason::ToolCall,
            vec![ModelToolCall::new("browser.form.submit", vec![])],
        ),
    )
    .unwrap_or_else(|| unreachable!("the fixture page is explicitly readable"));
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    assert!(
        dispositions
            .iter()
            .all(|disposition| !disposition.verdict.is_attemptable()),
        "the fixture must name a call this errand cannot attempt"
    );
    fixture.must_apply(Command::RecordModelTurn {
        call_id: residency.call_id().clone(),
        digest: Box::new(residency.digest(&dispositions)),
    });
    residency
}

#[test]
fn a_run_of_turns_that_attempt_nothing_ends_the_errand() {
    let mut fixture = prepared_errand(4);
    let mut residency = turn_attempting_nothing(&mut fixture);

    // Up to the bound the model is asked again: an on-sight refusal names the
    // clause, and naming it is only worth anything if the model gets to act on
    // what it was told.
    for turn in 1..MAX_TURNS_ATTEMPTING_NOTHING {
        let next = fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap_or_else(|error| panic!("{error:?}"));
        assert!(
            matches!(next, Some(Command::RequestModelTurn { .. })),
            "turn {turn} of {MAX_TURNS_ATTEMPTING_NOTHING} must be asked again, got {next:?}"
        );
        residency = turn_attempting_nothing(&mut fixture);
    }

    assert_eq!(
        fixture.reducer.turns_attempting_nothing(),
        MAX_TURNS_ATTEMPTING_NOTHING
    );
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap_or_else(|error| panic!("{error:?}"));
    assert!(
        matches!(next, Some(Command::FailTask { .. })),
        "past the bound the person gets an ending, not another paid turn: {next:?}"
    );
}

/// And it is a run, not a total. A turn that got a call as far as policy is
/// progress, whatever policy then said, and it starts the count again.
#[test]
fn a_turn_that_attempted_something_starts_the_count_again() {
    let mut fixture = prepared_errand(4);
    for _ in 0..MAX_TURNS_ATTEMPTING_NOTHING {
        turn_attempting_nothing(&mut fixture);
    }
    assert_eq!(
        fixture.reducer.turns_attempting_nothing(),
        MAX_TURNS_ATTEMPTING_NOTHING
    );

    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let (page, handle) = page();
    let residency = TurnResidency::read(
        call_id,
        page,
        reply(
            ModelStopReason::ToolCall,
            vec![common::agent::read_call(handle)],
        ),
    )
    .unwrap_or_else(|| unreachable!("the fixture page is explicitly readable"));
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    fixture.must_apply(Command::RecordModelTurn {
        call_id: residency.call_id().clone(),
        digest: Box::new(residency.digest(&dispositions)),
    });

    assert_eq!(
        fixture.reducer.turns_attempting_nothing(),
        0,
        "a call that was attempted clears the run"
    );
}
