// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Row 12's bound: asks for values that come back with nothing are counted.
//!
//! The loop none of the other three counters can see. An ask reaches the
//! browser, so it is attempted and `MAX_TURNS_ATTEMPTING_NOTHING` resets on
//! it; it carries a tool call, so it is not an unproductive reply; and it
//! navigates nowhere, so it is not a fruitless arrival. Row 12 then sent the
//! turn straight back for a fresh one. A phone spent eleven paid turns that
//! way on the myAadhaar CAPTCHA on 2026-09-19 (decisions 0215 and 0216).

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use bip_types::identity::{SemanticNodeId, TabId};
use task_engine::{
    field_value_request_id_for_call, Command, FieldValueAskOutcome, ModelStopReason, ModelToolCall,
    SuppliedValueCount, TurnResidency, MAX_UNANSWERED_VALUE_ASKS,
};

use common::agent::{page, reply, Digest};
use common::errand::prepared_errand_admitting;

/// One whole ask: a turn whose single call reaches for the person, the request
/// that call produces, and what the browser came back with.
///
/// `supplied` is the count and `outcome` the instruction beside it, exactly as
/// the coordinator reports them.
fn ask_for_values(
    fixture: &mut common::Fixture,
    ordinal: u64,
    supplied: u32,
    outcome: FieldValueAskOutcome,
) -> TurnResidency {
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
            vec![ModelToolCall::new("user.request_values", vec![])],
        ),
    )
    .unwrap_or_else(|| unreachable!("the fixture page is explicitly readable"));
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    fixture.must_apply(Command::RecordModelTurn {
        call_id: residency.call_id().clone(),
        digest: Box::new(residency.digest(&dispositions)),
    });
    let request_id = field_value_request_id_for_call(ordinal, 0);
    fixture.must_apply(Command::RequestFieldValues {
        request_id: request_id.clone(),
        tab_id: TabId::new("tab_1"),
        node_id: SemanticNodeId::new("n-1"),
        companion_node_ids: task_engine::FieldNodeIds::none(),
    });
    fixture.must_apply(Command::SupplyFieldValues {
        request_id,
        supplied: SuppliedValueCount::new(supplied).expect("within the bound"),
        outcome: Some(outcome),
        field_node_ids: None,
    });
    residency
}

fn errand() -> common::Fixture {
    prepared_errand_admitting(4, "user.request_values")
}

/// Up to the bound the model is asked again — decision 0215 hands it a move
/// per outcome, and handing it one is only worth anything if it gets to act.
/// Past the bound the person gets an ending instead of another paid turn.
#[test]
fn a_run_of_asks_that_come_back_empty_ends_the_errand() {
    let mut fixture = errand();
    let mut residency =
        ask_for_values(&mut fixture, 0, 0, FieldValueAskOutcome::ChallengeOffScreen);

    for ask in 1..MAX_UNANSWERED_VALUE_ASKS {
        let next = fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap_or_else(|error| panic!("{error:?}"));
        assert!(
            matches!(next, Some(Command::RequestModelTurn { .. })),
            "ask {ask} of {MAX_UNANSWERED_VALUE_ASKS} must be asked again, got {next:?}"
        );
        residency = ask_for_values(
            &mut fixture,
            u64::from(ask),
            0,
            FieldValueAskOutcome::ChallengeOffScreen,
        );
    }

    assert_eq!(
        fixture.reducer.unanswered_value_asks(),
        MAX_UNANSWERED_VALUE_ASKS
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

/// A run, not a total. One value from the person is the ask working, whatever
/// came back empty before it: a form filled in over several steps must never
/// walk into a cap.
#[test]
fn one_value_from_the_person_starts_the_count_again() {
    let mut fixture = errand();
    for ask in 0..u64::from(MAX_UNANSWERED_VALUE_ASKS) {
        ask_for_values(&mut fixture, ask, 0, FieldValueAskOutcome::Dismissed);
    }
    assert_eq!(
        fixture.reducer.unanswered_value_asks(),
        MAX_UNANSWERED_VALUE_ASKS
    );

    let residency = ask_for_values(
        &mut fixture,
        u64::from(MAX_UNANSWERED_VALUE_ASKS),
        1,
        FieldValueAskOutcome::Answered,
    );
    assert_eq!(fixture.reducer.unanswered_value_asks(), 0);
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap_or_else(|error| panic!("{error:?}"));
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "an answered ask is the task working, so it carries on: {next:?}"
    );
}

/// The count is read off the number and not off the outcome, which is what
/// lets a replayed journal rebuild it: the outcome is `None` after a restart
/// by design, and "no value came back" is legible from the count alone.
#[test]
fn the_count_and_not_the_outcome_is_what_advances_the_run() {
    let mut fixture = errand();
    let request_id = field_value_request_id_for_call(0, 0);
    fixture.must_apply(Command::RequestFieldValues {
        request_id: request_id.clone(),
        tab_id: TabId::new("tab_1"),
        node_id: SemanticNodeId::new("n-1"),
        companion_node_ids: task_engine::FieldNodeIds::none(),
    });
    fixture.must_apply(Command::SupplyFieldValues {
        request_id,
        supplied: SuppliedValueCount::new(0).expect("zero is a count"),
        outcome: None,
        field_node_ids: None,
    });
    assert_eq!(
        fixture.reducer.unanswered_value_asks(),
        1,
        "a restored command carries no outcome and still advances the run"
    );
}
