// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Rows 10a, 17 and 18: a reply that could not be used is asked for again a
//! bounded number of times in a row, and then the task ends.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::agent::TurnResidency;
use task_engine::{Command, FailureReason, ModelStopReason, TurnGap, MAX_CONSECUTIVE_REASKS};

use common::agent::{page, reply, running, Digest};
use common::Fixture;

/// Asks for the next turn and records that its reply could not be read.
fn unreadable_turn(fixture: &mut Fixture) {
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    fixture.must_apply(Command::RecordModelTurnGap {
        call_id,
        gap: TurnGap::Unreadable,
    });
}

/// Asks for the next turn and records a reply of `stop` carrying no calls.
fn recorded_turn(fixture: &mut Fixture, stop: ModelStopReason) -> TurnResidency {
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let (page, _) = page();
    let residency = TurnResidency::read(call_id, page, reply(stop, Vec::new())).unwrap();
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    fixture.must_apply(Command::RecordModelTurn {
        call_id: residency.call_id().clone(),
        digest: Box::new(residency.digest(&dispositions)),
    });
    residency
}

fn next(fixture: &Fixture, residency: Option<&TurnResidency>) -> Option<Command> {
    fixture
        .reducer
        .next_agent_command(residency, &Digest)
        .unwrap()
}

fn ends_the_task(command: Option<&Command>) -> bool {
    command
        == Some(&Command::FailTask {
            reason: FailureReason::ProviderUnavailable,
        })
}

#[test]
fn a_reply_that_cannot_be_read_is_asked_for_three_times_and_no_more() {
    // The phone's failure, bounded: one call and two re-asks, then the task
    // ends rather than spending the rest of its budget on the same answer.
    let mut fixture = running();
    for _ in 0..MAX_CONSECUTIVE_REASKS {
        unreadable_turn(&mut fixture);
        assert!(
            matches!(next(&fixture, None), Some(Command::RequestModelTurn { .. })),
            "a re-ask is still allowed"
        );
    }
    unreadable_turn(&mut fixture);
    assert!(ends_the_task(next(&fixture, None).as_ref()));
    assert_eq!(
        fixture
            .reducer
            .task()
            .ledger()
            .spent(task_engine::BudgetKind::MaxModelRequests),
        u64::from(MAX_CONSECUTIVE_REASKS) + 1
    );
}

#[test]
fn a_reply_that_was_read_starts_the_count_again() {
    let mut fixture = running();
    for _ in 0..MAX_CONSECUTIVE_REASKS {
        unreadable_turn(&mut fixture);
    }
    // A reply the product could read. The turn after it is not a re-ask, so
    // the next unreadable reply is a first one and not a third.
    recorded_turn(&mut fixture, ModelStopReason::ProviderStop);
    unreadable_turn(&mut fixture);
    assert!(matches!(
        next(&fixture, None),
        Some(Command::RequestModelTurn { .. })
    ));
}

#[test]
fn a_reply_cut_off_at_its_allowance_counts_the_same_way() {
    let mut fixture = running();
    for _ in 0..MAX_CONSECUTIVE_REASKS {
        let residency = recorded_turn(&mut fixture, ModelStopReason::Length);
        assert!(matches!(
            next(&fixture, Some(&residency)),
            Some(Command::RequestModelTurn { .. })
        ));
    }
    let residency = recorded_turn(&mut fixture, ModelStopReason::Length);
    assert!(ends_the_task(next(&fixture, Some(&residency)).as_ref()));
}
