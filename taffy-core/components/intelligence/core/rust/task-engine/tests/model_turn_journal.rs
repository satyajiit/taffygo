// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A model reply, a model-turn gap and a context eviction are each one
//! durable step.
//!
//! A commit is a revision step: the storage crate, the browser's journal and
//! the restore decoder all refuse a commit whose revision did not move. Each
//! of these commands changes what the task durably holds — the reply's
//! digest, the gap the next agent-table row reads, the boundary the transcript
//! builder drops to — so each has to journal an event, or the first live model
//! answer is the commit that takes the core down.

mod common;

use common::agent::{running, with_recorded_turn};
use task_engine::{Command, CommandKind, EventKind, JournalEntry, ModelStopReason, TurnGap};

fn last_event_kind(fixture: &common::Fixture) -> Option<(EventKind, CommandKind, u64)> {
    match fixture.reducer.journal().entries().last() {
        Some(JournalEntry::Event(record)) => {
            Some((record.event.kind, record.event.caused_by, record.revision))
        }
        _ => None,
    }
}

#[test]
fn a_recorded_reply_is_one_event_and_one_revision_step() {
    let (fixture, _) = with_recorded_turn(ModelStopReason::Complete, Vec::new());

    let revision = fixture.reducer.task().revision();
    assert_eq!(
        last_event_kind(&fixture),
        Some((
            EventKind::ModelTurnRecorded,
            CommandKind::RecordModelTurn,
            revision
        ))
    );
}

#[test]
fn a_recorded_gap_is_one_event_and_one_revision_step() {
    let mut fixture = running();
    let call_id = fixture.reducer.next_model_call_id();
    fixture.must_apply(Command::RequestModelTurn {
        call_id: call_id.clone(),
    });
    let before = fixture.reducer.task().revision();

    let accepted = fixture
        .apply(Command::RecordModelTurnGap {
            call_id,
            gap: TurnGap::Refused,
        })
        .unwrap_or_else(|refusal| unreachable!("the gap was refused: {refusal:?}"));

    assert_eq!(accepted.revision, before.saturating_add(1));
    assert!(accepted.effects.is_empty());
    assert_eq!(accepted.events.len(), 1);
    assert_eq!(
        last_event_kind(&fixture),
        Some((
            EventKind::ModelTurnGapRecorded,
            CommandKind::RecordModelTurnGap,
            accepted.revision
        ))
    );
}

#[test]
fn an_eviction_is_one_event_and_one_revision_step() {
    let (mut fixture, _) = with_recorded_turn(ModelStopReason::Complete, Vec::new());
    let before = fixture.reducer.task().revision();

    let accepted = fixture
        .apply(Command::RecordContextEviction { through_turn: 0 })
        .unwrap_or_else(|refusal| unreachable!("the eviction was refused: {refusal:?}"));

    assert_eq!(accepted.revision, before.saturating_add(1));
    assert!(accepted.effects.is_empty());
    assert_eq!(accepted.events.len(), 1);
    assert_eq!(
        last_event_kind(&fixture),
        Some((
            EventKind::ContextEvicted,
            CommandKind::RecordContextEviction,
            accepted.revision
        ))
    );
}
