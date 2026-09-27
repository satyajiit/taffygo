// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The append-only journal of commands and events (domain model sections 17
//! and 18).
//!
//! # Appending is the only operation
//!
//! [`TaskJournal`] has two mutating methods and both add. There is no update,
//! no delete, no truncate, and no accessor that hands out a mutable entry, so
//! append-only is a property of the API rather than a rule somebody has to
//! follow. Loading a prefix is [`TaskJournal::from_entries`], which builds a
//! new journal and validates it, rather than a method that shortens an existing
//! one.
//!
//! # The journal assigns the sequence
//!
//! Not the caller. A caller supplies what happened; the journal supplies where
//! it sits in the order, so two writers cannot claim the same position and a
//! gap is detectable rather than invisible.
//!
//! # Intent is durable before the effect
//!
//! A command is recorded before the events it causes, and the dispatch event of
//! an action is recorded before the reducer hands the caller the effect that
//! performs it. That ordering is what lets a replay tell "this was about to
//! happen" from "this happened".

use crate::command::{CommandEnvelope, CommandKind};
use crate::event::TaskEvent;
use crate::ids::IdempotencyKey;
use crate::task::TaskState;
use crate::time::{TraceId, UtcMillis};

/// A command as it was recorded.
#[derive(Clone, Debug, PartialEq)]
pub struct CommandRecord {
    /// Where it sits in the order.
    pub sequence: u64,
    /// What was asked for.
    pub envelope: CommandEnvelope,
    /// When it was recorded.
    pub recorded_at: UtcMillis,
}

/// An event as it was recorded.
#[derive(Clone, Debug, PartialEq)]
pub struct EventRecord {
    /// Where it sits in the order.
    pub sequence: u64,
    /// What happened.
    pub event: TaskEvent,
    /// The aggregate revision after it.
    pub revision: u64,
    /// The trace the causing command carried.
    pub trace_id: TraceId,
    /// The idempotency key of the causing command.
    pub causation_key: IdempotencyKey,
    /// When it was recorded.
    pub recorded_at: UtcMillis,
}

/// One entry.
#[derive(Clone, Debug, PartialEq)]
pub enum JournalEntry {
    /// Something was asked for.
    Command(CommandRecord),
    /// Something happened.
    Event(EventRecord),
}

impl JournalEntry {
    /// Where the entry sits in the order.
    pub const fn sequence(&self) -> u64 {
        match self {
            Self::Command(record) => record.sequence,
            Self::Event(record) => record.sequence,
        }
    }

    /// A short, compiled-in name for what kind of entry this is.
    pub const fn label(&self) -> &'static str {
        match self {
            Self::Command(_) => "command",
            Self::Event(_) => "event",
        }
    }

    /// The command record, when the entry is one.
    pub const fn as_command(&self) -> Option<&CommandRecord> {
        match self {
            Self::Command(record) => Some(record),
            Self::Event(_) => None,
        }
    }

    /// The event record, when the entry is one.
    pub const fn as_event(&self) -> Option<&EventRecord> {
        match self {
            Self::Event(record) => Some(record),
            Self::Command(_) => None,
        }
    }
}

/// Why a journal could not be loaded.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum JournalError {
    /// The sequence numbers are not one, two, three.
    SequenceNotContiguous {
        /// What the journal expected.
        expected: u64,
        /// What it found.
        found: u64,
    },
    /// An aggregate revision went backwards or repeated.
    RevisionNotMonotonic {
        /// What the journal expected next.
        expected: u64,
        /// What it found.
        found: u64,
    },
    /// The journal is empty, so there is no creation to rebuild from.
    Empty,
}

impl JournalError {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::SequenceNotContiguous { .. } => "sequence_not_contiguous",
            Self::RevisionNotMonotonic { .. } => "revision_not_monotonic",
            Self::Empty => "empty",
        }
    }
}

/// The append-only record of one task.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct TaskJournal {
    entries: Vec<JournalEntry>,
    next_sequence: u64,
    revision: u64,
}

impl TaskJournal {
    /// An empty journal. The first entry takes sequence one.
    pub const fn new() -> Self {
        Self {
            entries: Vec::new(),
            next_sequence: 1,
            revision: 0,
        }
    }

    /// Loads a journal from entries that were read back from storage.
    ///
    /// Validates the two things a reader cannot recover from: a gap or a repeat
    /// in the sequence, and a revision that did not move forward. Both mean the
    /// journal is not the one that was written, so this refuses rather than
    /// rebuilding a task from it.
    pub fn from_entries(entries: &[JournalEntry]) -> Result<Self, JournalError> {
        if entries.is_empty() {
            return Err(JournalError::Empty);
        }
        let mut journal = Self::new();
        for entry in entries {
            if entry.sequence() != journal.next_sequence {
                return Err(JournalError::SequenceNotContiguous {
                    expected: journal.next_sequence,
                    found: entry.sequence(),
                });
            }
            if let Some(event) = entry.as_event() {
                let expected = journal.revision.saturating_add(1);
                if event.revision != expected {
                    return Err(JournalError::RevisionNotMonotonic {
                        expected,
                        found: event.revision,
                    });
                }
                journal.revision = event.revision;
            }
            journal.entries.push(entry.clone());
            journal.next_sequence = journal.next_sequence.saturating_add(1);
        }
        Ok(journal)
    }

    /// Records what was asked for. Returns the sequence it was given.
    pub fn append_command(&mut self, envelope: CommandEnvelope, recorded_at: UtcMillis) -> u64 {
        let sequence = self.next_sequence;
        self.next_sequence = self.next_sequence.saturating_add(1);
        self.entries.push(JournalEntry::Command(CommandRecord {
            sequence,
            envelope,
            recorded_at,
        }));
        sequence
    }

    /// Records what happened, and moves the aggregate revision on by one.
    /// Returns the new revision.
    pub fn append_event(
        &mut self,
        event: TaskEvent,
        trace_id: TraceId,
        causation_key: IdempotencyKey,
        recorded_at: UtcMillis,
    ) -> u64 {
        let sequence = self.next_sequence;
        self.next_sequence = self.next_sequence.saturating_add(1);
        self.revision = self.revision.saturating_add(1);
        self.entries.push(JournalEntry::Event(EventRecord {
            sequence,
            event,
            revision: self.revision,
            trace_id,
            causation_key,
            recorded_at,
        }));
        self.revision
    }

    /// Every entry, in order.
    pub fn entries(&self) -> &[JournalEntry] {
        &self.entries
    }

    /// The aggregate revision.
    pub const fn revision(&self) -> u64 {
        self.revision
    }

    /// How many entries there are.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether the journal is empty.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// The commands, in order.
    pub fn commands(&self) -> impl Iterator<Item = &CommandRecord> {
        self.entries.iter().filter_map(JournalEntry::as_command)
    }

    /// The events, in order.
    pub fn events(&self) -> impl DoubleEndedIterator<Item = &EventRecord> {
        self.entries.iter().filter_map(JournalEntry::as_event)
    }

    /// The state the last recorded transition reached.
    ///
    /// This is what a replay checks itself against: the reducer recomputes a
    /// state and the journal says what the state was.
    pub fn last_recorded_state(&self) -> Option<TaskState> {
        self.events()
            .filter_map(|record| record.event.to)
            .next_back()
    }

    /// How many times each command kind was recorded.
    ///
    /// The duplicate-action property is checked against this: a dispatch that
    /// happened once in the live run happens once in the replayed journal.
    pub fn command_counts(&self) -> std::collections::BTreeMap<CommandKind, usize> {
        let mut counts = std::collections::BTreeMap::new();
        for record in self.commands() {
            *counts.entry(record.envelope.kind()).or_insert(0) += 1;
        }
        counts
    }

    /// The idempotency keys of every dispatch that was recorded.
    pub fn dispatched_keys(&self) -> Vec<IdempotencyKey> {
        self.commands()
            .filter(|record| record.envelope.kind() == CommandKind::DispatchAction)
            .map(|record| record.envelope.idempotency_key.clone())
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::{JournalEntry, JournalError, TaskJournal};
    use crate::command::{Command, CommandEnvelope};
    use crate::event::{EventKind, TaskEvent};
    use crate::ids::IdempotencyKey;
    use crate::task::{StateReason, TaskState};
    use crate::time::{TraceId, UtcMillis};

    fn envelope(key: &str) -> CommandEnvelope {
        CommandEnvelope::new(
            IdempotencyKey::new(key),
            0,
            TraceId::new("trace_0"),
            Command::ExecutorStarted,
        )
    }

    fn event() -> TaskEvent {
        TaskEvent::transition(
            EventKind::TaskStarted,
            crate::command::CommandKind::ExecutorStarted,
            TaskState::Queued,
            TaskState::Running,
            StateReason::ExecutorStarted,
        )
    }

    fn journal() -> TaskJournal {
        let mut journal = TaskJournal::new();
        journal.append_command(envelope("key_0"), UtcMillis(1));
        journal.append_event(
            event(),
            TraceId::new("trace_0"),
            IdempotencyKey::new("key_0"),
            UtcMillis(1),
        );
        journal
    }

    #[test]
    fn the_journal_assigns_the_sequence_and_the_revision() {
        let journal = journal();
        assert_eq!(journal.len(), 2);
        assert_eq!(
            journal
                .entries()
                .iter()
                .map(JournalEntry::sequence)
                .collect::<Vec<u64>>(),
            vec![1, 2]
        );
        assert_eq!(journal.revision(), 1);
    }

    #[test]
    fn an_earlier_entry_is_never_changed_by_a_later_one() {
        let mut journal = journal();
        let before = journal.entries().to_vec();
        journal.append_command(envelope("key_1"), UtcMillis(2));
        let after = journal.entries().to_vec();
        assert_eq!(after.get(..before.len()), Some(before.as_slice()));
    }

    #[test]
    fn a_journal_with_a_gap_is_refused() {
        let journal = journal();
        let mut entries = journal.entries().to_vec();
        entries.remove(0);
        assert_eq!(
            TaskJournal::from_entries(&entries),
            Err(JournalError::SequenceNotContiguous {
                expected: 1,
                found: 2
            })
        );
    }

    #[test]
    fn an_empty_journal_rebuilds_nothing() {
        assert_eq!(TaskJournal::from_entries(&[]), Err(JournalError::Empty));
    }

    #[test]
    fn a_loaded_journal_equals_the_one_it_was_written_from() {
        let journal = journal();
        let loaded = TaskJournal::from_entries(journal.entries());
        assert_eq!(loaded.as_ref().map(TaskJournal::len), Ok(2));
        assert_eq!(loaded, Ok(journal));
    }

    #[test]
    fn the_last_recorded_transition_is_the_state_a_replay_checks_against() {
        assert_eq!(journal().last_recorded_state(), Some(TaskState::Running));
    }
}
