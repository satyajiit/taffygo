// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Corrupt durable journals are refused even when their outer sequence is valid.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use task_engine::{JournalEntry, ManualClock, Reducer, SequentialIds, TaskJournal};

#[test]
fn a_journal_whose_recorded_state_disagrees_with_the_replay_is_refused() {
    let fixture = common::running();
    let entries = fixture.reducer.journal().entries().to_vec();
    // Drop the last command but keep the events it caused, which is exactly the
    // shape a corrupted or partially written journal has.
    let mut tampered = Vec::new();
    let mut dropped = false;
    for entry in entries {
        if !dropped && matches!(entry, JournalEntry::Command(_)) {
            dropped = true;
            continue;
        }
        tampered.push(entry);
    }
    // Re-number so the sequence check passes and the divergence check is what
    // actually fires.
    let renumbered: Vec<JournalEntry> = tampered
        .into_iter()
        .enumerate()
        .map(|(index, entry)| renumber(entry, u64::try_from(index).unwrap_or(0) + 1))
        .collect();
    let Ok(journal) = TaskJournal::from_entries(&renumbered) else {
        unreachable!("the renumbered journal loads")
    };
    let error = Reducer::replay(
        common::seed(),
        common::defaults(),
        ManualClock::at(1_000),
        SequentialIds::new(),
        &journal,
    )
    .err()
    .unwrap_or_else(|| unreachable!("a journal missing a command cannot replay"));
    assert!(matches!(
        error,
        task_engine::ReplayError::StateDiverged { .. }
            | task_engine::ReplayError::RevisionDiverged { .. }
            | task_engine::ReplayError::CommandRefused { .. }
    ));
}

fn renumber(entry: JournalEntry, sequence: u64) -> JournalEntry {
    match entry {
        JournalEntry::Command(mut record) => {
            record.sequence = sequence;
            JournalEntry::Command(record)
        }
        JournalEntry::Event(mut record) => {
            record.sequence = sequence;
            JournalEntry::Event(record)
        }
    }
}
