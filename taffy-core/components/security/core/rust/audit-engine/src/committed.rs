// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Projection of task-journal-owned event facts into audit envelopes.

use crate::{EventDraft, EventEnvelope, EventId, Sequence, UtcMillis};

/// Immutable task-journal facts used by the independent audit projection.
#[derive(Clone, Debug, PartialEq)]
pub struct CommittedEvent {
    /// Draft payload and closed event type selected by the projection adapter.
    pub draft: EventDraft,
    /// Deterministic identifier derived from the committed task event.
    pub event_id: EventId,
    /// Browser-supplied UTC time recorded on the task event.
    pub occurred_at_utc: UtcMillis,
    /// Event-only position in this task audit stream.
    pub sequence: Sequence,
    /// Exact task aggregate revision produced by the event.
    pub aggregate_revision: u64,
}

/// Why committed facts could not form an immutable audit envelope.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CommittedEventError {
    /// Event, stream, or trace identity was empty.
    EmptyIdentity,
    /// Sequences and aggregate revisions start at one.
    ZeroPosition,
}

/// Validates journal-owned facts and performs independent last-redaction input
/// projection without allocating a second physical audit writer.
pub fn project_committed(value: CommittedEvent) -> Result<EventEnvelope, CommittedEventError> {
    if value.event_id.as_str().is_empty()
        || value.draft.stream.aggregate_id.as_str().is_empty()
        || value.draft.trace_id.as_str().is_empty()
    {
        return Err(CommittedEventError::EmptyIdentity);
    }
    if value.sequence.0 == 0 || value.aggregate_revision == 0 {
        return Err(CommittedEventError::ZeroPosition);
    }
    Ok(EventEnvelope::seal(
        value.draft,
        value.event_id,
        value.occurred_at_utc,
        value.sequence,
        value.aggregate_revision,
    ))
}
