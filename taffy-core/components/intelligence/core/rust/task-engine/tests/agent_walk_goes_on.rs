// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The walk goes on past two things that used to end it.
//!
//! A reply that finished streaming after the person took over or stopped the
//! task is recorded before the task settles, and a call composed with no page
//! to act in is refused as a call rather than as the walk. Both are rows of the
//! agent decision table; they live here so that file stays under the line cap.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::agent::TurnResidency;
use task_engine::{CallVerdict, Command, ModelStopReason, NotAttempted};

use common::agent::{
    navigate_call, page, page_with_no_tab, read_call, reply, running, with_recorded_turn_on, Digest,
};

/// Row 3a: a reply that finished streaming after **Take over** or **Stop** is
/// recorded, and only then may the task settle. A phone showed the other
/// answer — nothing recorded it, `PauseSettled` was refused, and the task
/// said "Taffy is thinking…" for good (verification report, section 2.48).
#[test]
fn row_3a_a_reply_read_while_settling_is_recorded_and_then_the_task_settles() {
    for (interrupt, settle, settled) in [
        (
            Command::TakeOver,
            Command::PauseSettled,
            task_engine::TaskState::Paused,
        ),
        (
            Command::CancelTask,
            Command::CancelSettled,
            task_engine::TaskState::Cancelled,
        ),
    ] {
        let mut fixture = running();
        let call_id = fixture.reducer.next_model_call_id();
        fixture.must_apply(Command::RequestModelTurn {
            call_id: call_id.clone(),
        });
        fixture.must_apply(interrupt);
        // Nothing to record yet: the browser is still holding the call.
        assert_eq!(
            fixture.reducer.next_agent_command(None, &Digest).unwrap(),
            None
        );

        let (page, handle) = page();
        let residency = TurnResidency::read(
            call_id.clone(),
            page,
            reply(ModelStopReason::ToolCall, vec![read_call(handle)]),
        )
        .expect("a readable reply");
        let next = fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .unwrap();
        let Some(record @ Command::RecordModelTurn { .. }) = next else {
            panic!("expected the reply to be recorded, got {next:?}");
        };
        fixture.must_apply(record);
        // Recording proposes nothing: the call it carries waits for a resume.
        assert_eq!(
            fixture
                .reducer
                .next_agent_command(Some(&residency), &Digest)
                .unwrap(),
            None
        );
        fixture.must_apply(settle);
        assert_eq!(fixture.state(), settled);
    }
}

/// A turn composed from no page refuses its calls instead of ending the walk.
///
/// The tab of a projection taken from nothing is the empty identifier, and
/// every call that designates no node takes it into its canonical intent —
/// where the encoding refuses it. That refusal is an `AgentError`, and an
/// `AgentError` is the walk's refusal rather than the call's: the task then
/// has no next command for the life of the process, published as neither
/// running nor ended. The call is refused here instead, and the model is told
/// (decision 0178).
#[test]
fn a_call_with_no_page_to_act_in_is_refused_and_the_walk_goes_on() {
    let (fixture, residency) = with_recorded_turn_on(
        page_with_no_tab(),
        ModelStopReason::ToolCall,
        vec![navigate_call("https://official.test/")],
    );
    assert_eq!(
        fixture.reducer.turn_dispositions(&residency)[0].verdict,
        CallVerdict::NotAttempted(NotAttempted::PageUnknown)
    );
    assert!(
        fixture
            .reducer
            .next_agent_command(Some(&residency), &Digest)
            .is_ok(),
        "a refused call is not a refused walk"
    );
}
