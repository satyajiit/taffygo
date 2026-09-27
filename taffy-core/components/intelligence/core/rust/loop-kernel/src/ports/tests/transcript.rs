// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The transcript a turn is composed from: the conversation the durable turn
//! calls become, and what its opening says when the turns before it changed
//! nothing, before the run of them ends the task (decision 0233).

use crate::context::opening::STALLED_TURNS_NUDGE;
use crate::context::RecordedCall;
use task_engine::{
    ModelStopReason, RenderShape, TurnDigest, TurnUsage, TURNS_WITHOUT_PROGRESS_NUDGE,
};

use super::*;

#[test]
fn a_task_with_no_turns_carries_its_goal_and_no_conversation() {
    let reducer = running_reducer();
    let facts = reducer.model_turn_facts();
    assert_eq!(facts.transcript.goal(), seed().user_goal);
    assert!(facts.transcript.exchanges().is_empty());
    assert_eq!(facts.transcript.elided(), 0);
}

#[test]
fn the_durable_turn_calls_become_the_conversation_and_nothing_else_does() {
    let mut reducer = running_reducer();
    propose_read(&mut reducer, "turn-0-call-0", true);
    propose_read(&mut reducer, "turn-0-call-1", true);
    propose_read(&mut reducer, "turn-1-call-0", false);
    // A key from the reviewed workflow's own minting. It is an action of this
    // task and it is not a turn, so it must not appear as one.
    propose_read(&mut reducer, "source-table-src-step-1", true);

    let transcript = reducer.model_turn_facts().transcript;
    let shape: Vec<(u64, Vec<(&str, &str)>)> = transcript
        .exchanges()
        .iter()
        .map(|exchange| {
            (
                exchange.ordinal(),
                exchange
                    .calls()
                    .iter()
                    .map(|call| (call.call_id(), call.tool()))
                    .collect(),
            )
        })
        .collect();
    assert_eq!(
        shape,
        vec![
            (
                0,
                vec![
                    ("turn-0-call-0", "browser.dom.read"),
                    ("turn-0-call-1", "browser.dom.read"),
                ]
            ),
            (1, vec![("turn-1-call-0", "browser.dom.read")]),
        ]
    );
    // The outcome each call reached, read from the durable record rather than
    // assumed: two settled refusals and one still proposed.
    let outcomes: Vec<ActionState> = transcript
        .exchanges()
        .iter()
        .flat_map(|exchange| exchange.calls().iter().map(RecordedCall::outcome))
        .collect();
    assert_eq!(
        outcomes,
        vec![
            ActionState::Failed,
            ActionState::Failed,
            ActionState::Proposed
        ]
    );
}

/// A reply that named one call. Nothing the call did is applied, so the turn
/// changed nothing.
fn named_one_call() -> TurnDigest {
    TurnDigest {
        stop: ModelStopReason::ToolCall,
        overflow: None,
        usage: TurnUsage::default(),
        answer_segments: 0,
        tool_calls: 1,
        refused_tool_calls: 0,
        render: RenderShape::empty([0_u8; 32]),
    }
}

/// The opening of the turn that follows `unchanged` recorded turns in a row
/// that changed nothing, read the way the runtime reads it: with that turn
/// already requested.
fn opening_after(unchanged: u8) -> Vec<String> {
    let mut reducer = running_reducer();
    for turn in 0..=unchanged {
        let call_id = reducer.next_model_call_id();
        apply(
            &mut reducer,
            Command::RequestModelTurn {
                call_id: call_id.clone(),
            },
            &format!("request-{turn}"),
        );
        if turn < unchanged {
            apply(
                &mut reducer,
                Command::RecordModelTurn {
                    call_id,
                    digest: Box::new(named_one_call()),
                },
                &format!("record-{turn}"),
            );
        }
    }
    assert_eq!(reducer.turns_without_progress(), unchanged);
    reducer.model_turn_facts().transcript.preface().to_vec()
}

#[test]
fn a_run_of_turns_that_changed_nothing_is_said_before_it_ends_the_task() {
    let told = opening_after(TURNS_WITHOUT_PROGRESS_NUDGE);
    assert!(
        told.iter().any(|line| line == STALLED_TURNS_NUDGE),
        "the run was not said: {told:?}"
    );
    let not_yet = opening_after(TURNS_WITHOUT_PROGRESS_NUDGE - 1);
    assert!(!not_yet.iter().any(|line| line == STALLED_TURNS_NUDGE));
}
