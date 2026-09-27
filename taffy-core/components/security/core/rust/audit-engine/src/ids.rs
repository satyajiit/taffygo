// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Event identifiers and their deterministic source.
//!
//! An event identifier is minted by the journal, never by the caller. That is
//! part of what makes the log append-only: a caller that could choose an
//! identifier could claim to be rewriting an event that already exists.
//!
//! The identifier is derived from the stream and the sequence, so a replay of
//! the same events produces the same identifiers and two journals built from
//! the same input compare equal. The shipping wiring may substitute an
//! unguessable source (domain model section 3); the trait is the seam.

use core::fmt;

use crate::envelope::{Sequence, StreamId};

/// Opaque event identifier.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct EventId(String);

impl EventId {
    /// Wraps an identifier minted by an [`EventIdSource`].
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for EventId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Opaque trace identifier, correlating the events of one unit of work.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct TraceId(String);

impl TraceId {
    /// Wraps a trace identifier.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for TraceId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Opaque correlation identifier, joining events across streams.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct CorrelationId(String);

impl CorrelationId {
    /// Wraps a correlation identifier.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for CorrelationId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// A source of event identifiers.
///
/// It sees the stream and the sequence so a derived implementation can be
/// deterministic, and it may ignore both.
pub trait EventIdSource {
    /// Mints the identifier for the event about to be appended.
    ///
    /// `None` refuses the append. An identifier that could repeat would let one
    /// event impersonate another, so refusing is the only safe answer.
    fn next_event_id(&mut self, stream: &StreamId, sequence: Sequence) -> Option<EventId>;
}

/// An identifier derived from the stream and the sequence.
///
/// Deterministic and therefore predictable, which is exactly what a replay test
/// needs and exactly what the product does not want: the shipping wiring
/// injects an unguessable source instead (domain model section 3).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DerivedEventIds;

impl EventIdSource for DerivedEventIds {
    fn next_event_id(&mut self, stream: &StreamId, sequence: Sequence) -> Option<EventId> {
        Some(EventId::new(format!(
            "evt_{}_{}_{}",
            stream.aggregate_type.label(),
            stream.aggregate_id.as_str(),
            sequence.0
        )))
    }
}
