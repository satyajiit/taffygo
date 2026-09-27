// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The append-only journal and the projections built from it.
//!
//! Events are appended with an expected aggregate revision, so two writers
//! racing to extend the same aggregate produce a conflict to reconcile rather
//! than a silently lost write. The uniqueness is a schema constraint, not a
//! convention: the database refuses the second writer.
//!
//! Events carry identifiers, decision facts, and redacted summaries. They do
//! not carry capability secrets, credentials, prohibited values, or raw model
//! reasoning, and nothing in this module gives a caller a place to put any.
//!
//! Deletion relabels rather than erases. An audit record that a source was used
//! is what makes the audit trustworthy; the content of that use is what the
//! user asked to remove. Journal projections are derived, so those are removed
//! outright and can be rebuilt from what remains.

use crate::backend::{Executor, Value};
use crate::clock::Timestamp;
use crate::error::StorageError;
use crate::ids::{EventId, SourceId, WorkspaceId};

/// The payload a relabeled event carries.
///
/// Fixed text, so a relabeled event is recognizable and carries nothing from
/// the record it used to describe.
pub const REDACTED_PAYLOAD: &str = "{\"redacted\":\"SOURCE_DELETED\"}";

/// The redaction class a relabeled event is filed under.
pub const REDACTION_CLASS_SOURCE_DELETED: &str = "SOURCE_DELETED";

/// One journal event.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DomainEvent {
    /// Identity.
    pub event_id: EventId,
    /// Which aggregate kind.
    pub aggregate_type: String,
    /// Which aggregate.
    pub aggregate_id: String,
    /// The revision this event takes the aggregate to.
    pub aggregate_revision: i64,
    /// What happened.
    pub event_type: String,
    /// When.
    pub occurred_at: Timestamp,
    /// Total order within the journal.
    pub monotonic_sequence: i64,
    /// Who acted.
    pub actor: String,
    /// The task, when there is one.
    pub task_id: Option<String>,
    /// The trace it belongs to.
    pub trace_id: String,
    /// The event that caused it.
    pub causation_event_id: Option<EventId>,
    /// How its payload is classified.
    pub redaction_class: String,
    /// The payload: identifiers, decision facts, redacted summaries.
    pub payload: String,
}

/// Appends an event.
///
/// Fails when the expected revision is already taken, which is the conflict the
/// caller has to reconcile rather than overwrite.
pub fn append(executor: &mut dyn Executor, event: &DomainEvent) -> Result<(), StorageError> {
    executor.execute(
        "INSERT INTO task_event (event_id, aggregate_type, aggregate_id, aggregate_revision, \
         event_type, schema_version, occurred_at_utc, monotonic_sequence, actor, task_id, \
         trace_id, causation_event_id, correlation_id, redaction_class, payload) \
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, NULL, ?13, ?14)",
        &[
            Value::text(event.event_id.to_text()),
            Value::text(event.aggregate_type.clone()),
            Value::text(event.aggregate_id.clone()),
            Value::Integer(event.aggregate_revision),
            Value::text(event.event_type.clone()),
            Value::Integer(crate::records::RECORD_SCHEMA_VERSION),
            Value::text(event.occurred_at.as_str()),
            Value::Integer(event.monotonic_sequence),
            Value::text(event.actor.clone()),
            Value::maybe_text(event.task_id.clone()),
            Value::text(event.trace_id.clone()),
            Value::maybe_text(event.causation_event_id.map(EventId::to_text)),
            Value::text(event.redaction_class.clone()),
            Value::text(event.payload.clone()),
        ],
    )?;
    Ok(())
}

/// Records that an event was about a source, so deletion can find it.
pub fn project_source_usage(
    executor: &mut dyn Executor,
    event_id: EventId,
    source_id: SourceId,
    workspace_id: Option<WorkspaceId>,
    occurred_at: &Timestamp,
    summary: &str,
) -> Result<(), StorageError> {
    executor.execute(
        "INSERT INTO journal_source_projection \
         (event_id, source_id, workspace_id, occurred_at_utc, summary) \
         VALUES (?1, ?2, ?3, ?4, ?5)",
        &[
            Value::text(event_id.to_text()),
            Value::text(source_id.to_text()),
            Value::maybe_text(workspace_id.map(WorkspaceId::to_text)),
            Value::text(occurred_at.as_str()),
            Value::text(summary),
        ],
    )?;
    Ok(())
}

/// How many events the journal holds.
pub fn event_count(executor: &mut dyn Executor) -> Result<i64, StorageError> {
    crate::backend::count(executor, "SELECT COUNT(*) FROM task_event", &[])
}

/// The payload of one event, for a test or an audit surface.
pub fn payload_of(
    executor: &mut dyn Executor,
    event_id: EventId,
) -> Result<Option<String>, StorageError> {
    let rows = executor.query(
        "SELECT payload, redaction_class FROM task_event WHERE event_id = ?1",
        &[Value::text(event_id.to_text())],
    )?;
    rows.first()
        .map(|row| row.text(0).map(str::to_owned))
        .transpose()
}
