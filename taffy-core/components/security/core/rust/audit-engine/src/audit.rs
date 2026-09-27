// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The product-audit serializer (domain model section 22).
//!
//! Product audit answers what the assistant did for the person using it: what
//! task ran, which sources and providers were used, what data class crossed a
//! boundary, which action was proposed, whether approval happened, and how the
//! result was verified. It follows task retention and it redacts values.
//!
//! The record shape mirrors the redacted action record of domain model section
//! 26, including its `content_values_retained` flag — which this serializer
//! sets rather than copies, and always sets to false, because there is no path
//! through it that carries a content value.

use serde::Serialize;

use crate::envelope::EventEnvelope;
use crate::payload::FieldValue;
use crate::redaction::{
    audit_disposition, kinds_agree, optional_identifier, origin_of, required_identifier,
    scrub_identifier, FieldDisposition, Scrubbed,
};

/// One value in a serialized audit record.
///
/// There is no free-text variant. A record cannot carry page text because the
/// type it would have to travel in does not exist.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case", tag = "kind", content = "value")]
pub enum AuditValue {
    /// An opaque domain identifier.
    Identifier(String),
    /// A compiled-in enumerated name.
    Enumerated(&'static str),
    /// A count or bucket.
    Count(u64),
    /// A decision fact.
    Flag(bool),
    /// A normalized origin. Never a path, a query, or a fragment.
    Origin(String),
}

/// One field of a serialized audit record.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct AuditField {
    /// The compiled-in field name.
    pub name: &'static str,
    /// The redacted value.
    pub value: AuditValue,
}

/// One serialized audit record.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct AuditRecord {
    /// The event identifier the journal minted.
    pub event_id: String,
    /// Which kind of aggregate.
    pub aggregate_type: &'static str,
    /// Which aggregate.
    pub aggregate_id: String,
    /// The revision this event produced.
    pub aggregate_revision: u64,
    /// What happened.
    pub event_type: &'static str,
    /// The schema version the event was written under.
    pub schema_version: u32,
    /// When it happened.
    pub occurred_at_utc: u64,
    /// Where it sits in its stream.
    pub monotonic_sequence: u64,
    /// Who caused it.
    pub actor: &'static str,
    /// The task, when the event belongs to one.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub task_id: Option<String>,
    /// The unit of work.
    pub trace_id: String,
    /// The event that caused this one.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub causation_event_id: Option<String>,
    /// The correlation across streams.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub correlation_id: Option<String>,
    /// How much this record was allowed to carry.
    pub redaction_class: &'static str,
    /// The decision facts that survived redaction.
    pub fields: Vec<AuditField>,
    /// How many fields the redaction pass removed.
    pub dropped_field_count: u64,
    /// Always false. No path through this serializer carries a content value.
    pub content_values_retained: bool,
}

/// Serializes one event as a product-audit record.
///
/// The payload is treated as untrusted input. Every field runs the four gates
/// of [`crate::redaction`], and a field that fails any of them is counted
/// rather than carried — the count is itself a decision fact, and a record that
/// silently held less than it looked like it held would be worse than one that
/// says how much it dropped.
pub fn audit_record(event: &EventEnvelope) -> AuditRecord {
    let mut fields = Vec::new();
    let mut dropped = 0_u64;

    for field in event.payload().fields() {
        if !kinds_agree(field.name, &field.value) {
            dropped = dropped.saturating_add(1);
            continue;
        }
        let value = match (audit_disposition(field.name), &field.value) {
            (FieldDisposition::Emit, FieldValue::Identifier(value)) => {
                match scrub_identifier(value) {
                    Scrubbed::Dropped => None,
                    other => other
                        .value()
                        .map(|value| AuditValue::Identifier(value.to_owned())),
                }
            }
            (FieldDisposition::Emit, FieldValue::Enumerated(value)) => {
                Some(AuditValue::Enumerated(value))
            }
            (FieldDisposition::Emit, FieldValue::Count(value)) => Some(AuditValue::Count(*value)),
            (FieldDisposition::Emit, FieldValue::Flag(value)) => Some(AuditValue::Flag(*value)),
            (FieldDisposition::OriginOnly, FieldValue::Url(value)) => {
                origin_of(value).map(AuditValue::Origin)
            }
            // A dropped field, and a disposition that does not match the
            // value's shape, both carry nothing. The gates are independent, so
            // neither one has to know what the other decided.
            (FieldDisposition::Drop | FieldDisposition::Emit | FieldDisposition::OriginOnly, _) => {
                None
            }
        };

        match value {
            Some(value) => fields.push(AuditField {
                name: field.name.label(),
                value,
            }),
            None => dropped = dropped.saturating_add(1),
        }
    }

    AuditRecord {
        event_id: required_identifier(event.event_id().as_str()),
        aggregate_type: event.stream().aggregate_type.label(),
        aggregate_id: required_identifier(event.stream().aggregate_id.as_str()),
        aggregate_revision: event.aggregate_revision(),
        event_type: event.event_type().wire(),
        schema_version: event.schema_version().0,
        occurred_at_utc: event.occurred_at_utc().0,
        monotonic_sequence: event.monotonic_sequence().0,
        actor: event.actor().label(),
        task_id: event
            .task_id()
            .and_then(|task| optional_identifier(task.as_str())),
        trace_id: required_identifier(event.trace_id().as_str()),
        causation_event_id: event
            .causation_event_id()
            .and_then(|id| optional_identifier(id.as_str())),
        correlation_id: event
            .correlation_id()
            .and_then(|id| optional_identifier(id.as_str())),
        redaction_class: event.redaction_class().label(),
        fields,
        dropped_field_count: dropped,
        content_values_retained: false,
    }
}
