// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Append-only task and audit events with independent redaction.
//!
//! Authoritative specifications:
//! `docs/architecture/domain-model.md` sections 17, 19, and 22;
//! `docs/security/data-and-privacy.md` sections 7.2 and 14;
//! `docs/security/threat-model.md` invariant I-13. Owning milestone: M2 (page
//! intelligence), work package WP-M2-06.
//!
//! # What this crate is
//!
//! The record of what was decided, and the last layer of the redaction
//! pipeline. It assumes every layer before it failed: the payload a caller
//! hands it is untrusted input, its own field policy decides what may be
//! carried, and it shares no code with the projection serializers in
//! `policy-engine`. An independent layer that reuses the implementation it is
//! checking is not independent.
//!
//! It carries no authority. Whether an action may happen is `policy-engine`'s
//! decision; this crate writes down that the decision was made.
//!
//! # The two records
//!
//! Product audit and operational telemetry are separate systems with separate
//! identifiers, consent, retention, and destinations (domain model section 22).
//! They have separate serializers here, and the telemetry one starts from the
//! envelope again rather than from an audit record, so neither can drift into
//! reusing the other's payload by convenience.
//!
//! [`telemetry::TelemetryRecord`] is content-free by construction: every field
//! it holds is a compiled-in name, a number, or a boolean, so no URL, page
//! title, prompt, model output, file name, or opaque identifier can travel in
//! one whatever a caller supplies.
//!
//! # Properties this crate holds
//!
//! - **Appending is the only operation.** [`journal::AppendOnlyLog`] has one
//!   mutating method, and the journal — not the caller — assigns the event
//!   identifier, the wall-clock time, the sequence, and the revision.
//! - **Conflicts are visible.** An append carries an expected revision and is
//!   refused when it disagrees, rather than overwriting.
//! - **Gaps are detectable.** [`journal::find_sequence_gaps`] reports missing,
//!   repeated, and out-of-order events per stream, and [`projection::replay`]
//!   refuses to build a projection over a broken journal.
//! - **Replay reconstructs.** Folding the journal produces exactly what
//!   applying each event as it arrived produced.
//! - **Redaction does not trust the caller.** A field name with no policy, a
//!   value whose kind disagrees with its name, a URL beyond its origin, and a
//!   string that names itself a secret are all removed, and the count of what
//!   was removed is recorded.
//! - **No panics, no clocks, no randomness.** Time and identifiers arrive
//!   through [`clock::Clock`] and [`ids::EventIdSource`]; every function is
//!   total.
#![doc(html_no_source)]
#![cfg_attr(
    test,
    allow(
        clippy::unwrap_used,
        clippy::expect_used,
        clippy::panic,
        clippy::indexing_slicing
    )
)]

pub mod audit;
pub mod clock;
pub mod committed;
pub mod envelope;
pub mod ids;
pub mod journal;
pub mod payload;
pub mod projection;
pub mod redaction;
pub mod telemetry;

pub use crate::audit::{audit_record, AuditRecord};
pub use crate::clock::{Clock, ManualClock, UtcMillis};
pub use crate::committed::{project_committed, CommittedEvent, CommittedEventError};
pub use crate::envelope::{
    Actor, AggregateId, AggregateType, EventDraft, EventEnvelope, EventType, RedactionClass,
    SchemaVersion, Sequence, StreamId,
};
pub use crate::ids::{CorrelationId, DerivedEventIds, EventId, EventIdSource, TraceId};
pub use crate::journal::{
    find_sequence_gaps, AppendError, AppendOnlyLog, Journal, MemoryLog, SequenceGap,
    MAX_MEMORY_LOG_EVENTS,
};
pub use crate::payload::{EventPayload, FieldName, FieldValue, PayloadField, ValueKind};
pub use crate::projection::{replay, ReplayError, TaskProjection, TaskState};
pub use crate::telemetry::{telemetry_record, TelemetryRecord};
