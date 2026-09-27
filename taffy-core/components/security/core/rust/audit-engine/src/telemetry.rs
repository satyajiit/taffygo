// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The operational-telemetry serializer (domain model section 22, data and
//! privacy section 14.2).
//!
//! Telemetry answers whether a component crashed, timed out, truncated,
//! rejected stale state, or exceeded a resource budget. It is a different
//! system from product audit, with different identifiers, consent, retention,
//! and destination, and it does not reuse the audit payload by convenience —
//! which is why this serializer starts from the envelope again rather than from
//! an [`crate::audit::AuditRecord`].
//!
//! # Content-free by construction
//!
//! [`TelemetryRecord`] holds `&'static str`, `u64`, and `bool`. There is no
//! field in it that can hold a `String`, so no URL, page title, prompt, model
//! output, file name, selected text, or opaque identifier can be carried
//! through it whatever a caller supplies and whatever a future field policy
//! says. The type is the control; the field policy is the second one.

use serde::Serialize;

use crate::envelope::EventEnvelope;
use crate::payload::FieldValue;
use crate::redaction::{kinds_agree, telemetry_disposition, FieldDisposition};

/// One value in a serialized telemetry record.
///
/// Every variant is a compiled-in name, a number, or a boolean.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case", tag = "kind", content = "value")]
pub enum TelemetryValue {
    /// A compiled-in enumerated name.
    Enumerated(&'static str),
    /// A count or bucket.
    Count(u64),
    /// A decision fact.
    Flag(bool),
}

/// One field of a serialized telemetry record.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
pub struct TelemetryField {
    /// The compiled-in field name.
    pub name: &'static str,
    /// The value.
    pub value: TelemetryValue,
}

/// One serialized telemetry record.
///
/// It carries no event identifier, no task identifier, no trace identifier, and
/// no wall-clock time. Durable local identifiers are not automatically
/// telemetry identifiers, and a timestamp precise enough to be useful is
/// precise enough to link records together.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct TelemetryRecord {
    /// What happened.
    pub event_type: &'static str,
    /// Who caused it.
    pub actor: &'static str,
    /// Which kind of aggregate, never which one.
    pub aggregate_type: &'static str,
    /// The schema version the event was written under.
    pub schema_version: u32,
    /// The fields that survived.
    pub fields: Vec<TelemetryField>,
    /// How many fields the redaction pass removed.
    pub dropped_field_count: u64,
}

/// Serializes one event as a telemetry record, when it is eligible at all.
///
/// `None` means the event is not operational. Product audit and telemetry are
/// separate systems, so an event written to answer "what did the assistant do
/// for me" is not repurposed to answer "is the component healthy" — the caller
/// records an operational event for that, with its own redaction class.
pub fn telemetry_record(event: &EventEnvelope) -> Option<TelemetryRecord> {
    if !event.redaction_class().is_telemetry_eligible() {
        return None;
    }

    let mut fields = Vec::new();
    let mut dropped = 0_u64;

    for field in event.payload().fields() {
        if !kinds_agree(field.name, &field.value) {
            dropped = dropped.saturating_add(1);
            continue;
        }
        let value = match (telemetry_disposition(field.name), &field.value) {
            (FieldDisposition::Emit, FieldValue::Enumerated(value)) => {
                Some(TelemetryValue::Enumerated(value))
            }
            (FieldDisposition::Emit, FieldValue::Count(value)) => {
                Some(TelemetryValue::Count(*value))
            }
            (FieldDisposition::Emit, FieldValue::Flag(value)) => Some(TelemetryValue::Flag(*value)),
            // Everything else, including every value that arrived as a string.
            _ => None,
        };

        match value {
            Some(value) => fields.push(TelemetryField {
                name: field.name.label(),
                value,
            }),
            None => dropped = dropped.saturating_add(1),
        }
    }

    Some(TelemetryRecord {
        event_type: event.event_type().wire(),
        actor: event.actor().label(),
        aggregate_type: event.stream().aggregate_type.label(),
        schema_version: event.schema_version().0,
        fields,
        dropped_field_count: dropped,
    })
}
