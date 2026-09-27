// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The append-only log and the journal that writes to it (domain model section
//! 17.1).
//!
//! # Why appending is the only operation
//!
//! [`AppendOnlyLog`] has one mutating method. There is no update, no delete, no
//! truncate, and no accessor that hands out a mutable event, so "append-only"
//! is a property of the API rather than a rule somebody has to follow. A
//! storage-backed implementation inherits the same shape: it can only be asked
//! to append.
//!
//! Compaction is deliberately absent. Domain model section 17.1 allows a
//! signed or checksummed snapshot but forbids erasing audit records still
//! required by retention, and a compaction that cannot be asked for cannot be
//! asked for wrongly.
//!
//! # What the journal owns
//!
//! The caller supplies an [`EventDraft`]. The journal assigns the event
//! identifier, the wall-clock time, the per-stream sequence, and the aggregate
//! revision. A caller that could assign a sequence could claim to be rewriting
//! an event that already exists, so it cannot.
//!
//! Appends carry an expected revision. A disagreement is a
//! [`AppendError::RevisionConflict`], never a silent overwrite: two writers
//! that disagree are reconciled, not resolved by whoever arrived last.

use std::collections::BTreeMap;

use crate::clock::Clock;
use crate::envelope::{EventDraft, EventEnvelope, Sequence, StreamId};
use crate::ids::EventIdSource;

/// Why an append was refused.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum AppendError {
    /// The caller's expected revision is not the stream's current revision.
    RevisionConflict {
        /// What the caller expected.
        expected: u64,
        /// What the stream is actually at.
        actual: u64,
    },
    /// The stream's sequence counter is exhausted.
    SequenceExhausted,
    /// The identifier source is exhausted.
    IdSourceExhausted,
    /// The underlying log refused the write.
    LogRejected(&'static str),
}

/// A log that can only grow.
pub trait AppendOnlyLog {
    /// Appends one sealed event.
    ///
    /// The event already carries its sequence: the journal assigned it, and an
    /// implementation stores it rather than choosing one.
    fn append(&mut self, event: EventEnvelope) -> Result<(), AppendError>;

    /// The last sequence stored for a stream, or `None` when the stream is
    /// empty.
    fn last_sequence(&self, stream: &StreamId) -> Option<Sequence>;

    /// The last aggregate revision stored for a stream.
    fn last_revision(&self, stream: &StreamId) -> Option<u64>;

    /// Every event of one stream, in sequence order.
    fn read_stream(&self, stream: &StreamId) -> Vec<EventEnvelope>;

    /// Every event in the log, in the order it was appended.
    fn read_all(&self) -> Vec<EventEnvelope>;
}

/// An in-memory append-only log.
///
/// The incognito journal is one of these and is destroyed with its context
/// (domain model section 17.1). A durable implementation behind the same trait
/// is milestone M3 work.
#[derive(Clone, Debug, Default)]
pub struct MemoryLog {
    events: Vec<EventEnvelope>,
    tails: BTreeMap<StreamId, (Sequence, u64)>,
}

/// Maximum audit events retained by one in-memory (incognito) journal.
pub const MAX_MEMORY_LOG_EVENTS: usize = 16_384;

impl MemoryLog {
    /// An empty log.
    pub const fn new() -> Self {
        Self {
            events: Vec::new(),
            tails: BTreeMap::new(),
        }
    }

    /// Every event, in the order it was appended.
    ///
    /// A shared slice: a reader can walk the log and cannot edit it.
    pub fn events(&self) -> &[EventEnvelope] {
        &self.events
    }

    /// How many events the log holds.
    pub fn len(&self) -> usize {
        self.events.len()
    }

    /// Whether the log is empty.
    pub fn is_empty(&self) -> bool {
        self.events.is_empty()
    }
}

impl AppendOnlyLog for MemoryLog {
    fn append(&mut self, event: EventEnvelope) -> Result<(), AppendError> {
        if self.events.len() >= MAX_MEMORY_LOG_EVENTS {
            return Err(AppendError::LogRejected("memory log full"));
        }
        let stream = event.stream().clone();
        let sequence = event.monotonic_sequence();
        let revision = event.aggregate_revision();
        self.events.push(event);
        self.tails
            .entry(stream)
            .and_modify(|tail| {
                tail.0 = tail.0.max(sequence);
                tail.1 = tail.1.max(revision);
            })
            .or_insert((sequence, revision));
        Ok(())
    }

    fn last_sequence(&self, stream: &StreamId) -> Option<Sequence> {
        self.tails.get(stream).map(|tail| tail.0)
    }

    fn last_revision(&self, stream: &StreamId) -> Option<u64> {
        self.tails.get(stream).map(|tail| tail.1)
    }

    fn read_stream(&self, stream: &StreamId) -> Vec<EventEnvelope> {
        let mut selected: Vec<EventEnvelope> = self
            .events
            .iter()
            .filter(|event| event.stream() == stream)
            .cloned()
            .collect();
        selected.sort_by_key(EventEnvelope::monotonic_sequence);
        selected
    }

    fn read_all(&self) -> Vec<EventEnvelope> {
        self.events.clone()
    }
}

/// The writer: it seals drafts into events and appends them.
///
/// It owns the clock, the identifier source, and the log, so a caller cannot
/// reach past it to any of the three.
#[derive(Clone, Debug)]
pub struct Journal<C, I, L> {
    clock: C,
    ids: I,
    log: L,
    next_sequence: BTreeMap<StreamId, Sequence>,
}

impl<C: Clock, I: EventIdSource, L: AppendOnlyLog> Journal<C, I, L> {
    /// Builds a journal over a clock, an identifier source, and a log.
    pub fn new(clock: C, ids: I, log: L) -> Self {
        Self {
            clock,
            ids,
            log,
            next_sequence: BTreeMap::new(),
        }
    }

    /// The log, for reading.
    pub fn log(&self) -> &L {
        &self.log
    }

    /// The sequence the next append to a stream will use.
    pub fn next_sequence(&self, stream: &StreamId) -> Sequence {
        if let Some(next) = self.next_sequence.get(stream).copied() {
            return next;
        }
        match self.log.last_sequence(stream) {
            Some(last) => last.next().unwrap_or(last),
            None => Sequence::FIRST,
        }
    }

    /// The revision a stream is currently at.
    pub fn revision(&self, stream: &StreamId) -> u64 {
        self.log.last_revision(stream).unwrap_or(0)
    }

    /// Seals a draft and appends it.
    ///
    /// The returned envelope is a copy of what was written. Editing it changes
    /// nothing, because the log holds its own.
    pub fn record(&mut self, draft: EventDraft) -> Result<EventEnvelope, AppendError> {
        let stream = draft.stream.clone();
        let current_revision = self.revision(&stream);
        if draft.expected_revision != current_revision {
            return Err(AppendError::RevisionConflict {
                expected: draft.expected_revision,
                actual: current_revision,
            });
        }

        let sequence = self.next_sequence(&stream);
        let next = sequence.next().ok_or(AppendError::SequenceExhausted)?;
        let revision = current_revision
            .checked_add(1)
            .ok_or(AppendError::SequenceExhausted)?;
        let event_id = self
            .ids
            .next_event_id(&stream, sequence)
            .ok_or(AppendError::IdSourceExhausted)?;

        let event = EventEnvelope::seal(draft, event_id, self.clock.now_utc(), sequence, revision);
        self.log.append(event.clone())?;
        self.next_sequence.insert(stream, next);
        Ok(event)
    }
}

/// A break in a stream's sequence.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SequenceGap {
    /// Which stream.
    pub stream: StreamId,
    /// The sequence that should have come next.
    pub expected: Sequence,
    /// The sequence that was actually found.
    pub found: Sequence,
}

/// Finds every break in the sequences of a set of events.
///
/// A gap means an event is missing, repeated, or out of order, and any of the
/// three means the projection built from these events is not the projection the
/// journal describes. Detection is the point: a projection cannot be trusted
/// silently, so this returns what it found rather than repairing anything.
///
/// Events may arrive interleaved across streams; each stream is checked on its
/// own, because two aggregates are two independent histories.
pub fn find_sequence_gaps(events: &[EventEnvelope]) -> Vec<SequenceGap> {
    let mut expected: BTreeMap<StreamId, Sequence> = BTreeMap::new();
    let mut gaps = Vec::new();

    for event in events {
        let stream = event.stream().clone();
        let want = expected.get(&stream).copied().unwrap_or(Sequence::FIRST);
        let found = event.monotonic_sequence();
        if found != want {
            gaps.push(SequenceGap {
                stream: stream.clone(),
                expected: want,
                found,
            });
        }
        // Resume from whatever was actually found, so one gap reports once
        // instead of reporting on every event after it.
        match found.next() {
            Some(next) => {
                expected.insert(stream, next);
            }
            None => {
                expected.insert(stream, found);
            }
        }
    }

    gaps
}
