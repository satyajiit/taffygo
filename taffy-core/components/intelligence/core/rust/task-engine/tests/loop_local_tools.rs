// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Loop-local tools on the decision-table walk: settlement without a command.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::{
    loop_tool_result, CallVerdict, Command, EffectiveToolSet, LoopOutcome, Milestone,
    ModelStopReason, NotAttempted,
};

use common::agent::{
    activate_tool_call, page, read_call, search_tools_call, spawn_run_call, with_recorded_turn,
    Digest,
};

#[test]
fn an_unsettled_loop_call_waits_rather_than_proposing() {
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![search_tools_call("document")],
    );
    assert!(fixture.reducer.pending_loop_call(&residency).is_some());
    assert_eq!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap(),
        None
    );
}

#[test]
fn a_settled_search_lets_the_walk_continue() {
    let (_, handle) = page();
    let (fixture, mut residency) = with_recorded_turn(
        ModelStopReason::ToolCall,
        vec![search_tools_call("document"), read_call(handle)],
    );
    let (sequence, call, entry) = fixture
        .reducer
        .pending_loop_call(&residency)
        .expect("the search is waiting");
    let (outcome, result) =
        loop_tool_result(entry, call, &EffectiveToolSet::for_task(Milestone::M3, &[]));
    assert_eq!(outcome, LoopOutcome::Searched { hits: 0 });
    assert!(residency.settle_loop_with_result(sequence, outcome, result));
    assert!(fixture.reducer.pending_loop_call(&residency).is_none());

    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    let Some(Command::ProposeAction(proposal)) = next else {
        panic!("expected the read after a settled search, got {next:?}");
    };
    assert_eq!(proposal.tool_name(), "browser.dom.read");
}

#[test]
fn a_settled_loop_only_turn_asks_again() {
    let (fixture, mut residency) =
        with_recorded_turn(ModelStopReason::ToolCall, vec![search_tools_call("pdf")]);
    let (sequence, call, entry) = fixture
        .reducer
        .pending_loop_call(&residency)
        .expect("the search is waiting");
    let (outcome, result) =
        loop_tool_result(entry, call, &EffectiveToolSet::for_task(Milestone::M3, &[]));
    assert!(residency.settle_loop_with_result(sequence, outcome, result));
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "a settled loop-only reply must start a fresh turn, got {next:?}"
    );
}

#[test]
fn a_truncated_reply_refuses_loop_tools_too() {
    let (fixture, residency) = with_recorded_turn(
        ModelStopReason::Length,
        vec![
            search_tools_call("document"),
            activate_tool_call("page.pdf.inspect"),
            spawn_run_call("narrow the comparison"),
        ],
    );
    let dispositions = fixture.reducer.turn_dispositions(&residency);
    assert_eq!(dispositions.len(), 3);
    for disposition in &dispositions {
        assert_eq!(
            disposition.verdict,
            CallVerdict::NotAttempted(NotAttempted::TruncatedArguments)
        );
    }
    assert!(fixture.reducer.pending_loop_call(&residency).is_none());
    let next = fixture
        .reducer
        .next_agent_command(Some(&residency), &Digest)
        .unwrap();
    assert!(
        matches!(next, Some(Command::RequestModelTurn { .. })),
        "a truncated reply must dispatch nothing, got {next:?}"
    );
}
