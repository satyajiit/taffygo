// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The two registers the reducer keeps, and the bound each of them holds.
//!
//! Every assertion here is written against the exported ceiling rather than
//! against its value. A number that a test restates is a second copy of the
//! number, and the property being defended is "there is a ceiling and nothing
//! crosses it", not "the ceiling is five hundred and twelve".
//!
//! The two registers are bounded by opposite means, and the tests say which:
//!
//! - the **action register** never removes anything, because its consumers
//!   read records a proposal already ended and the identifiers are opaque, so
//!   there is no floor a discarded record could sit below. It refuses instead.
//! - the **receipt register** does remove, but only receipts a duplicate can no
//!   longer reach: `revision` only ever increases, and a command written
//!   against a revision the task has moved past meets `RevisionConflict`
//!   before `apply` consults a receipt. The test that matters most in this
//!   file is the one that drives a receipt out of the register and then
//!   presents its command again, byte for byte, and shows that it still does
//!   not run.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::command::{Command, CommandEnvelope, PauseCause};
use task_engine::task::TaskState;
use task_engine::{RefusalReason, TraceId, MAX_ACTIONS_PER_TASK, MAX_COMMAND_RECEIPTS};

/// Proposes actions until the register is at its ceiling, checking the bound
/// on every step rather than only at the end.
fn fill_action_register(fixture: &mut common::Fixture) {
    while fixture.reducer.action_count() < MAX_ACTIONS_PER_TASK {
        let filled = fixture.reducer.action_count();
        fixture.must_apply(Command::ProposeAction(Box::new(common::proposal(
            &format!("fill_key_{filled}"),
        ))));
        assert!(
            fixture.reducer.action_count() <= MAX_ACTIONS_PER_TASK,
            "the action register passed its ceiling at {filled}"
        );
        assert!(
            fixture.reducer.receipt_count() <= MAX_COMMAND_RECEIPTS,
            "the receipt register passed its ceiling at {filled}"
        );
    }
}

#[test]
fn the_action_register_stops_at_its_ceiling_and_says_so() {
    let mut fixture = common::running();
    fill_action_register(&mut fixture);
    assert_eq!(fixture.reducer.action_count(), MAX_ACTIONS_PER_TASK);

    let actions_before = fixture.reducer.action_count();
    let journal_before = fixture.reducer.journal().len();
    let revision_before = fixture.reducer.task().revision();
    let envelope = fixture.envelope(Command::ProposeAction(Box::new(common::proposal(
        "one_too_many",
    ))));
    let refusal = fixture.reducer.apply(envelope).err();

    // Visible, named, and closed: the proposal is refused for the reason it was
    // actually refused for, and nothing about the task moved.
    assert_eq!(
        refusal.map(|refusal| refusal.reason),
        Some(RefusalReason::ActionRegisterFull)
    );
    assert_eq!(fixture.reducer.action_count(), actions_before);
    assert_eq!(fixture.reducer.journal().len(), journal_before);
    assert_eq!(fixture.reducer.task().revision(), revision_before);
}

#[test]
fn a_full_action_register_still_knows_every_action_it_holds() {
    // The guard `ActionKnown` reads this register, so a bound implemented by
    // eviction would turn an action the task is still working on into an
    // unknown one. Nothing is evicted, so the first action a task ever
    // proposed is still there when the last one is refused.
    let mut fixture = common::running();
    let Some(first) = fixture.action_id.clone() else {
        unreachable!("the running fixture proposed an action")
    };
    fill_action_register(&mut fixture);

    assert!(fixture.reducer.action(&first).is_some());
    // And the guard admits it: a command naming it is not refused as unknown.
    let envelope = fixture.envelope(Command::RecordPolicyDecision {
        action_id: first,
        decision: Box::new(common::authorize()),
        dispatch_id: None,
    });
    assert!(fixture.reducer.apply(envelope).is_ok());
}

#[test]
fn a_replay_of_a_full_register_rebuilds_the_same_actions() {
    // The ceiling is checked before an identifier is minted, so a refused
    // proposal spends nothing and a replay of the journal mints the same
    // sequence it minted the first time. If the order were the other way
    // round, the identifier source would be one ahead of the journal here.
    let mut fixture = common::running();
    fill_action_register(&mut fixture);
    let envelope = fixture.envelope(Command::ProposeAction(Box::new(common::proposal(
        "refused_before_minting",
    ))));
    assert!(fixture.reducer.apply(envelope).is_err());

    let live: Vec<String> = fixture
        .reducer
        .actions()
        .map(|action| action.action_id().as_str().to_owned())
        .collect();
    let rebuilt = task_engine::Reducer::replay(
        common::seed(),
        common::defaults(),
        task_engine::ManualClock::at(1_000),
        task_engine::ids::SequentialIds::new(),
        fixture.reducer.journal(),
    );
    let Ok((rebuilt, _)) = rebuilt else {
        unreachable!("the journal a run produced replays")
    };
    let replayed: Vec<String> = rebuilt
        .actions()
        .map(|action| action.action_id().as_str().to_owned())
        .collect();
    assert_eq!(replayed, live);
    assert_eq!(rebuilt.action_count(), MAX_ACTIONS_PER_TASK);
}

#[test]
fn a_receipt_the_revision_floor_retired_never_admits_its_command_again() {
    // The exactly-once property, across a retirement. One command is applied,
    // its receipt is driven out of the register by the revision floor, and the
    // identical envelope is then presented again. It must not run a second
    // time, and the register that answered it before must not be what decides
    // that.
    let mut fixture = common::running();
    let key = fixture.next_key();
    let written_against = fixture.reducer.task().revision();
    let envelope = CommandEnvelope::new(
        key,
        written_against,
        TraceId::new("trace_test"),
        Command::ProposeAction(Box::new(common::proposal("exactly_once"))),
    );
    assert!(fixture.reducer.apply(envelope.clone()).is_ok());
    let actions_after_first = fixture.reducer.action_count();

    // Enough further commands that the register has been swept at least once,
    // which drops every receipt below the floor — this one included.
    for filled in 0..=MAX_COMMAND_RECEIPTS {
        fixture.must_apply(Command::ProposeAction(Box::new(common::proposal(
            &format!("sweep_key_{filled}"),
        ))));
        assert!(fixture.reducer.receipt_count() <= MAX_COMMAND_RECEIPTS);
    }

    let actions_before = fixture.reducer.action_count();
    let journal_before = fixture.reducer.journal().len();
    let revision_before = fixture.reducer.task().revision();
    let state_before = fixture.reducer.task().state();

    let replayed = fixture.reducer.apply(envelope);

    // Refused, not re-applied. The revision the command was written against is
    // no longer the task's revision, and that check runs before anything is
    // written — which is exactly why the receipt was safe to drop.
    assert_eq!(
        replayed.err().map(|refusal| refusal.reason),
        Some(RefusalReason::RevisionConflict)
    );
    assert_eq!(fixture.reducer.action_count(), actions_before);
    assert_eq!(fixture.reducer.journal().len(), journal_before);
    assert_eq!(fixture.reducer.task().revision(), revision_before);
    assert_eq!(fixture.reducer.task().state(), state_before);
    // And the one action it did mint is still exactly one action.
    assert!(actions_before > actions_after_first);
}

#[test]
fn a_reachable_receipt_still_answers_its_duplicate() {
    // The other half of the same property: a receipt is only dropped when the
    // register is at its ceiling, so an ordinary retry inside the duplicate
    // window still gets the original answer rather than a refusal.
    let mut fixture = common::running();
    let key = fixture.next_key();
    let revision = fixture.reducer.task().revision();
    let envelope = CommandEnvelope::new(
        key,
        revision,
        TraceId::new("trace_test"),
        Command::ProposeAction(Box::new(common::proposal("retried"))),
    );
    let Ok(first) = fixture.reducer.apply(envelope.clone()) else {
        unreachable!("a well-formed proposal is accepted")
    };
    let actions_after_first = fixture.reducer.action_count();

    let Ok(second) = fixture.reducer.apply(envelope) else {
        unreachable!("a retry inside the duplicate window is answered")
    };
    assert!(second.duplicate);
    assert!(second.effects.is_empty());
    assert_eq!(second.revision, first.revision);
    assert_eq!(fixture.reducer.action_count(), actions_after_first);
}

#[test]
fn the_dispatched_key_set_is_bounded_by_the_action_register() {
    // One key per action that reached dispatch, taken from that action's own
    // proposal, so re-dispatching an action re-inserts the key it already
    // holds rather than adding one. Bounding the actions bounds this too,
    // which is why it carries no ceiling of its own.
    let mut fixture = common::running();
    let Some(action_id) = fixture.action_id.clone() else {
        unreachable!("the running fixture proposed an action")
    };
    for attempt in 0..8_u32 {
        fixture.must_apply(Command::DispatchAction {
            action_id: action_id.clone(),
            dispatch_id: bip_types::identity::DispatchId::new(format!("dispatch_{attempt}")),
        });
        assert_eq!(fixture.reducer.dispatched_keys().len(), 1);
        // Back to authorized, so the next attempt is admissible at all.
        fixture.must_apply(Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(common::authorize()),
            dispatch_id: None,
        });
    }
    assert!(fixture.reducer.dispatched_keys().len() <= fixture.reducer.action_count());
}

#[test]
fn a_storm_of_commands_that_do_nothing_never_wedges_the_task() {
    // The failure mode the second floor exists for. Pausing a task that is
    // already settling moves nothing, journals nothing, and returns nothing,
    // so its receipt sits at a revision that never advances — which the
    // revision floor cannot reach. Enough of them under distinct keys would
    // fill the register, and the one command that would have moved the task on
    // would be the one refused.
    let mut fixture = common::in_state(TaskState::Pausing, false);
    let held_at = fixture.reducer.task().revision();
    for _ in 0..=MAX_COMMAND_RECEIPTS {
        fixture.must_apply(Command::PauseTask {
            cause: PauseCause::User,
        });
        assert!(fixture.reducer.receipt_count() <= MAX_COMMAND_RECEIPTS);
    }
    // Nothing moved, which is what made every one of those receipts unreachable
    // by the revision floor.
    assert_eq!(fixture.reducer.task().revision(), held_at);
    assert_eq!(fixture.reducer.task().state(), TaskState::Pausing);

    // And the settlement still lands.
    let envelope = fixture.envelope(Command::PauseSettled);
    assert!(fixture.reducer.apply(envelope).is_ok());
    assert_eq!(fixture.reducer.task().state(), TaskState::Paused);
}
