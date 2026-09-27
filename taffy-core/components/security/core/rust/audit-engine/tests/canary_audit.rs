// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canaries fed straight into the audit API never reach serialized output.
//!
//! This crate is the last layer of the redaction pipeline, so the test hands it
//! exactly what a broken layer above would: raw page text in a summary field, a
//! prompt, a model output, a file name, a full URL with a token in its query, a
//! secret wearing an identifier's field, and page text smuggled through a field
//! name that is otherwise allowed to be emitted.
//!
//! The assertion is made on the serialized bytes, because that is what actually
//! reaches storage. A field the record holds but nobody thought to check still
//! appears in the JSON.

use audit_engine::payload::{FieldValue, ValueKind};
use audit_engine::{
    audit_record, telemetry_record, Actor, AggregateId, AggregateType, CorrelationId,
    DerivedEventIds, EventDraft, EventId, EventPayload, EventType, FieldName, Journal, ManualClock,
    MemoryLog, RedactionClass, SchemaVersion, StreamId, TraceId,
};

/// One secret, and where a caller managed to put it.
struct Canary {
    what: &'static str,
    value: &'static str,
}

const CANARIES: &[Canary] = &[
    Canary {
        what: "a summary composed from page text",
        value: "CanarySummaryPassword8143XZ",
    },
    Canary {
        what: "a page title",
        value: "CanaryTitle-account-token-77b1",
    },
    Canary {
        what: "a prompt",
        value: "CanaryPrompt-seed-phrase-abandon-ability",
    },
    Canary {
        what: "a model output",
        value: "CanaryOutput-cvv-731-4111111111111111",
    },
    Canary {
        what: "a file name",
        value: "CanaryFile-private-key.pem",
    },
    Canary {
        what: "selected text",
        value: "CanarySelection-otp-449182774213",
    },
    Canary {
        what: "a query string",
        value: "CanaryQuery-session-9f2ba71c4d8e6503",
    },
    Canary {
        what: "a secret wearing an identifier field",
        value: "CanaryId-password-Hunter2X9",
    },
    Canary {
        what: "page text smuggled through an enumerated field",
        value: "CanarySmuggled-bearer-eyJhbGciOi",
    },
    Canary {
        what: "a secret in the envelope's own task identifier",
        value: "CanaryTask-api-key-33bd",
    },
    Canary {
        what: "a secret in the envelope's aggregate identifier",
        value: "CanaryAggregate-password-41cd",
    },
    Canary {
        what: "a secret in the envelope's trace identifier",
        value: "CanaryTrace-session-token-98ed",
    },
    Canary {
        what: "a secret in the envelope's causation identifier",
        value: "CanaryCause-private-key-12ac",
    },
    Canary {
        what: "a secret in the envelope's correlation identifier",
        value: "CanaryCorrelation-api-key-76fe",
    },
];

fn canary(what: &str) -> &'static str {
    match CANARIES.iter().find(|canary| canary.what == what) {
        Some(canary) => canary.value,
        None => unreachable!("every canary this test names is in the corpus"),
    }
}

/// A payload that carries every canary, each through a different route.
fn hostile_payload() -> EventPayload {
    EventPayload::new()
        // Text fields: no policy emits one, whatever it holds.
        .with(
            FieldName::UserVisibleSummary,
            FieldValue::Text(canary("a summary composed from page text").to_owned()),
        )
        .with(
            FieldName::PageTitle,
            FieldValue::Text(canary("a page title").to_owned()),
        )
        .with(
            FieldName::PromptText,
            FieldValue::Text(canary("a prompt").to_owned()),
        )
        .with(
            FieldName::ModelOutput,
            FieldValue::Text(canary("a model output").to_owned()),
        )
        .with(
            FieldName::FileName,
            FieldValue::Text(canary("a file name").to_owned()),
        )
        .with(
            FieldName::SelectedText,
            FieldValue::Text(canary("selected text").to_owned()),
        )
        // A URL: only its origin survives, so the query goes whatever it holds.
        .with_url(
            FieldName::DestinationUrl,
            format!(
                "https://example.test/account?session={}",
                canary("a query string")
            ),
        )
        // An identifier that names itself a secret.
        .with_identifier(
            FieldName::CapabilityId,
            canary("a secret wearing an identifier field"),
        )
        // Page text wearing a field name that is allowed to be emitted. The
        // kinds disagree, so the field is dropped.
        .with(
            FieldName::ActionClass,
            FieldValue::Text(canary("page text smuggled through an enumerated field").to_owned()),
        )
        // Ordinary decision facts, which must survive.
        .with_enumerated(FieldName::ResultCode, "DENIED_BY_POLICY")
        .with_identifier(FieldName::ActionId, "act_7f3a2b")
        .with_count(FieldName::SourceCount, 3)
        .with_flag(FieldName::BudgetExceeded, false)
}

fn draft(redaction_class: RedactionClass) -> EventDraft {
    EventDraft {
        stream: StreamId::new(
            AggregateType::Task,
            AggregateId::new(canary("a secret in the envelope's aggregate identifier")),
        ),
        expected_revision: 0,
        event_type: EventType::ActionProposed,
        schema_version: SchemaVersion(1),
        actor: Actor::Policy,
        task_id: Some(AggregateId::new(canary(
            "a secret in the envelope's own task identifier",
        ))),
        trace_id: TraceId::new(canary("a secret in the envelope's trace identifier")),
        causation_event_id: Some(EventId::new(canary(
            "a secret in the envelope's causation identifier",
        ))),
        correlation_id: Some(CorrelationId::new(canary(
            "a secret in the envelope's correlation identifier",
        ))),
        redaction_class,
        payload: hostile_payload(),
    }
}

fn recorded(redaction_class: RedactionClass) -> audit_engine::EventEnvelope {
    let mut journal = Journal::new(ManualClock::at(1_000), DerivedEventIds, MemoryLog::new());
    match journal.record(draft(redaction_class)) {
        Ok(event) => event,
        Err(error) => unreachable!("the fixture append must succeed: {error:?}"),
    }
}

fn assert_no_canary(serialized: &str, where_from: &str) {
    for canary in CANARIES {
        assert!(
            !serialized.contains(canary.value),
            "{} leaked into {where_from}: {serialized}",
            canary.what
        );
    }
}

#[test]
fn no_canary_survives_the_audit_serializer() {
    let event = recorded(RedactionClass::Decision);
    let record = audit_record(&event);
    let serialized = match serde_json::to_string(&record) {
        Ok(serialized) => serialized,
        Err(error) => unreachable!("an audit record must serialize: {error}"),
    };

    assert_no_canary(&serialized, "the audit record");

    // The record is not empty: the decision facts survived, so the test is not
    // passing because nothing was written.
    assert!(serialized.contains("DENIED_BY_POLICY"));
    assert!(serialized.contains("act_7f3a2b"));
    assert!(serialized.contains("https://example.test"));
    assert!(!record.content_values_retained);
    // Six text fields and one field whose kind disagreed with its name.
    assert_eq!(record.dropped_field_count, 7);
    // The identifier that named itself a secret is replaced rather than
    // dropped: the record still says a capability was involved.
    assert!(serialized.contains("[redacted]"));
}

#[test]
fn no_canary_survives_the_telemetry_serializer() {
    let event = recorded(RedactionClass::Operational);
    let Some(record) = telemetry_record(&event) else {
        unreachable!("an operational event is eligible for telemetry")
    };
    let serialized = match serde_json::to_string(&record) {
        Ok(serialized) => serialized,
        Err(error) => unreachable!("a telemetry record must serialize: {error}"),
    };

    assert_no_canary(&serialized, "the telemetry record");

    // Telemetry keeps the enumerated result and the counts and nothing else:
    // no origin, no identifier, no trace.
    assert!(serialized.contains("DENIED_BY_POLICY"));
    assert!(!serialized.contains("act_7f3a2b"));
    assert!(!serialized.contains("example.test"));
    assert!(!serialized.contains("trace_1"));
}

#[test]
fn an_audit_event_is_never_reused_as_a_telemetry_event() {
    // Product audit and operational telemetry are separate systems. An event
    // written to answer what the assistant did is not repurposed to answer
    // whether a component is healthy.
    for class in RedactionClass::ALL {
        let event = recorded(*class);
        assert_eq!(
            telemetry_record(&event).is_some(),
            class.is_telemetry_eligible(),
            "{}",
            class.label()
        );
    }
}

#[test]
fn every_text_shaped_field_is_refused_by_both_serializers() {
    // Walking the field vocabulary rather than a hand-written list: a text
    // field added later is covered the day it is added.
    for name in FieldName::ALL {
        if name.expected_kind() != ValueKind::Text {
            continue;
        }
        let payload = EventPayload::new().with(
            *name,
            FieldValue::Text("CanaryFieldWalk-password-9911".to_owned()),
        );
        let mut journal = Journal::new(ManualClock::at(1), DerivedEventIds, MemoryLog::new());
        let mut hostile = draft(RedactionClass::Operational);
        hostile.payload = payload;
        hostile.task_id = None;
        let Ok(event) = journal.record(hostile) else {
            unreachable!("the fixture append must succeed")
        };

        let audit = match serde_json::to_string(&audit_record(&event)) {
            Ok(serialized) => serialized,
            Err(error) => unreachable!("an audit record must serialize: {error}"),
        };
        assert!(
            !audit.contains("CanaryFieldWalk"),
            "{} reached the audit record",
            name.label()
        );

        let Some(record) = telemetry_record(&event) else {
            unreachable!("an operational event is eligible for telemetry")
        };
        let telemetry = match serde_json::to_string(&record) {
            Ok(serialized) => serialized,
            Err(error) => unreachable!("a telemetry record must serialize: {error}"),
        };
        assert!(
            !telemetry.contains("CanaryFieldWalk"),
            "{} reached the telemetry record",
            name.label()
        );
    }
}
