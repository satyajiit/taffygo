// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The redaction pipeline, all five layers, across the three crates that own
//! them (work package WP-M2-06).
//!
//! `policy-engine` decides what each of the four destinations may be told.
//! `audit-engine` decides again, from its own field policy, for the record that
//! is actually written. This suite runs one observation the whole way and
//! asserts the thing the milestone exit review cares about: **each layer is
//! strictly narrower than the one before it, and the last one does not trust
//! the one before it.**
//!
//! ```text
//! observation → local context → model projection → audit projection
//!                                                → audit record   (independent)
//!                                                → telemetry record (independent)
//! ```
//!
//! Independence is tested behaviourally rather than asserted: a value the
//! `policy-engine` masker is happy to carry is still dropped by `audit-engine`,
//! and a value `audit-engine` would carry is still withheld by the
//! classification. Two layers that agreed about everything would be one layer.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use audit_engine::{
    audit_record, telemetry_record, Actor, AggregateId, AggregateType, DerivedEventIds, EventDraft,
    EventPayload, EventType, FieldName, FieldValue, Journal, ManualClock, MemoryLog,
    RedactionClass, SchemaVersion, StreamId, TraceId,
};
use bip_types::identity::SemanticNodeId;
use bip_types::snapshot::{NodeState, SemanticRole};
use policy_engine::pipeline::project_all;
use policy_engine::redaction::{mask_secret_shaped, AuditProjection, InputType, InputTypeSignal};
use policy_engine::{FieldObservation, RedactionDestination};

/// One page node: an ordinary link, a name, some text, and a query string that
/// carries something nobody should keep.
fn ordinary_node() -> FieldObservation {
    let mut observation = FieldObservation::new(SemanticNodeId::new("n_link"), SemanticRole::Link);
    observation.name = Some("Your orders".to_owned());
    observation.description = Some("Opens the order history".to_owned());
    observation.text_runs = vec!["Order history".to_owned()];
    observation.destination_url =
        Some("https://example.test/orders?ref=Ab12Cd34Ef56Gh78Jk#top".to_owned());
    observation.states = vec![NodeState::Visible, NodeState::Enabled];
    observation
}

/// A credential control on the same page.
fn credential_node() -> FieldObservation {
    let mut observation =
        FieldObservation::new(SemanticNodeId::new("n_password"), SemanticRole::TextField);
    observation.name = Some("Password".to_owned());
    observation.signals.input_type = InputTypeSignal::Known(InputType::Password);
    observation.value = Some("SixteenCharsAndMore1".to_owned());
    observation
}

/// Builds the audit payload the broker would write, from the audit projection
/// and nothing else.
fn payload_from(projection: &AuditProjection) -> EventPayload {
    let mut payload = EventPayload::new()
        .with_identifier(FieldName::NodeId, projection.node_id.as_str())
        .with_count(FieldName::SourceCount, 1)
        .with_flag(FieldName::BudgetExceeded, false);
    if let Some(origin) = projection.destination_origin.as_deref() {
        payload = payload.with_url(FieldName::DestinationUrl, origin.to_owned());
    }
    for sensitivity in projection.sensitivity.members() {
        payload = payload.with_sensitivity(sensitivity);
    }
    payload
}

/// Records one event and returns its audit and telemetry serializations.
///
/// The redaction class is a parameter because it decides telemetry eligibility:
/// only an operational record is ever considered for the telemetry stream at
/// all, and that is the first gate rather than the field policy.
fn serialize_as(payload: EventPayload, class: RedactionClass) -> (String, Option<String>) {
    let mut journal = Journal::new(ManualClock::at(1_000), DerivedEventIds, MemoryLog::new());
    let event = journal
        .record(EventDraft {
            stream: StreamId::new(AggregateType::Task, AggregateId::new("task_1")),
            expected_revision: 0,
            event_type: EventType::ActionDispatchStarted,
            schema_version: SchemaVersion(1),
            actor: Actor::Policy,
            task_id: Some(AggregateId::new("task_1")),
            trace_id: TraceId::new("trace_1"),
            causation_event_id: None,
            correlation_id: None,
            redaction_class: class,
            payload,
        })
        .expect("the fixture append must succeed");
    let audit = serde_json::to_string(&audit_record(&event)).expect("the audit record serializes");
    let telemetry = telemetry_record(&event)
        .map(|record| serde_json::to_string(&record).expect("the telemetry record serializes"));
    (audit, telemetry)
}

/// The ordinary case: a decision record, which is never telemetry.
fn serialize(payload: EventPayload) -> (String, Option<String>) {
    serialize_as(payload, RedactionClass::Decision)
}

#[test]
fn one_observation_narrows_at_every_layer_from_the_page_to_the_record() {
    let observation = ordinary_node();
    let set = project_all(&observation);
    assert_eq!(set.verify_narrowing(), Ok(()));

    // Layer 1 to 4: the local observation keeps the page's own text, the model
    // gets it masked and without the query, audit gets an origin, telemetry
    // gets counts.
    let local = set.local().expect("the local projection is built");
    assert_eq!(
        local.destination_url.as_deref(),
        Some("https://example.test/orders?ref=Ab12Cd34Ef56Gh78Jk#top")
    );

    let model = set.model().expect("the model projection is built");
    assert_eq!(
        model.destination.as_deref(),
        Some("https://example.test/orders")
    );

    let audit_projection = set.audit().expect("the audit projection is built");
    assert_eq!(
        audit_projection.destination_origin.as_deref(),
        Some("https://example.test")
    );
    assert!(audit_projection.name_present);

    assert!(set
        .fragments(RedactionDestination::Telemetry)
        .next()
        .is_none());

    // Layer 5: the record that is actually written, decided again.
    let (audit, telemetry) = serialize(payload_from(audit_projection));
    for carried in ["Your orders", "Order history", "Opens the order history"] {
        assert!(
            !audit.contains(carried),
            "page text reached the audit record"
        );
    }
    assert!(
        audit.contains("https://example.test"),
        "the decision facts an audit record exists for must survive: {audit}"
    );
    // A decision record is not an operational one, so it is not eligible for
    // the telemetry stream at all. That is the first gate, before any field
    // policy runs.
    assert_eq!(telemetry, None);

    // The same facts written as an operational record do reach telemetry, and
    // the origin does not travel with them.
    let (_, telemetry) = serialize_as(payload_from(audit_projection), RedactionClass::Operational);
    let telemetry = telemetry.expect("an operational record is telemetry eligible");
    assert!(!telemetry.contains("example.test"));
}

#[test]
fn the_query_string_the_local_observation_kept_reaches_nothing_narrower() {
    let observation = ordinary_node();
    let set = project_all(&observation);
    let secret_in_query = "Ab12Cd34Ef56Gh78Jk";

    for destination in [
        RedactionDestination::ModelProjection,
        RedactionDestination::Audit,
        RedactionDestination::Telemetry,
    ] {
        let rendered = format!("{:?}", set.projection(destination));
        assert!(
            !rendered.contains(secret_in_query),
            "the query string reached {}",
            destination.label()
        );
    }

    let audit_projection = set.audit().expect("the audit projection is built");
    let (audit, _) = serialize(payload_from(audit_projection));
    assert!(!audit.contains(secret_in_query));
    let (_, telemetry) = serialize_as(payload_from(audit_projection), RedactionClass::Operational);
    assert!(!telemetry.unwrap_or_default().contains(secret_in_query));
}

#[test]
fn a_credential_reaches_no_destination_and_no_record() {
    let observation = credential_node();
    let value = observation
        .value
        .clone()
        .expect("the fixture credential has a value");
    let set = project_all(&observation);
    assert!(set.classification().is_never_extract());

    for destination in RedactionDestination::ALL {
        let rendered = format!("{:?}", set.projection(*destination));
        assert!(
            !rendered.contains(&value),
            "a credential reached {}",
            destination.label()
        );
    }

    // What the model is told instead is a structural placeholder built from a
    // trusted local template: the field is there, and its value is not.
    let model = set.model().expect("the model projection is built");
    assert_eq!(model.value_placeholder, Some("password field present"));
    assert_eq!(model.value, None);

    let audit_projection = set.audit().expect("the audit projection is built");
    let (audit, _) = serialize(payload_from(audit_projection));
    assert!(!audit.contains(&value));
    let (_, telemetry) = serialize_as(payload_from(audit_projection), RedactionClass::Operational);
    assert!(!telemetry.unwrap_or_default().contains(&value));
    // The audit record still says a credential was involved, which is the fact
    // an audit reader needs.
    assert!(audit.contains("CREDENTIAL"), "{audit}");
}

#[test]
fn the_last_layer_does_not_trust_the_layer_before_it() {
    // A caller that hands the audit serializer the *local* observation's own
    // fields, wearing whatever audit field name looks closest. Every one of
    // them is dropped, because the field policy decides and the caller does not.
    let observation = ordinary_node();
    let set = project_all(&observation);
    let local = set.local().expect("the local projection is built");

    let payload = EventPayload::new()
        .with(
            FieldName::UserVisibleSummary,
            FieldValue::Text(local.name.clone().unwrap_or_default()),
        )
        .with(
            FieldName::PageTitle,
            FieldValue::Text(local.description.clone().unwrap_or_default()),
        )
        .with(
            FieldName::SelectedText,
            FieldValue::Text(local.text_runs.join(" ")),
        )
        .with_url(
            FieldName::DestinationUrl,
            local.destination_url.clone().unwrap_or_default(),
        )
        // Page text wearing a name that is otherwise emitted. The kinds
        // disagree, so it never reaches the record.
        .with(
            FieldName::ActionClass,
            FieldValue::Text("Your orders".to_owned()),
        );

    let (audit, _) = serialize(payload);
    for carried in [
        "Your orders",
        "Opens the order history",
        "Order history",
        "Ab12Cd34Ef56Gh78Jk",
    ] {
        assert!(
            !audit.contains(carried),
            "the audit serializer trusted its caller: {audit}"
        );
    }
    assert!(audit.contains("https://example.test"));
}

#[test]
fn the_two_layers_disagree_about_enough_to_be_two_layers() {
    // A value the masker is happy to carry, dropped by the audit field policy.
    let ordinary = "Order history";
    assert_eq!(mask_secret_shaped(ordinary).masked_spans, 0);
    let (audit, _) = serialize(
        EventPayload::new().with(FieldName::PageTitle, FieldValue::Text(ordinary.to_owned())),
    );
    assert!(!audit.contains(ordinary));

    // And the other way round: an opaque node identifier is a decision fact the
    // audit record keeps, while the value of the node it names never leaves the
    // classification. The two layers are answering different questions.
    let opaque = "n_7f3a2b";
    let (audit, _) = serialize(EventPayload::new().with_identifier(FieldName::NodeId, opaque));
    assert!(
        audit.contains(opaque),
        "an opaque node identifier is a decision fact: {audit}"
    );

    // The audit scan is not fooled by an identifier that names a secret, even
    // though the field policy would emit the field.
    let (audit, _) =
        serialize(EventPayload::new().with_identifier(FieldName::NodeId, "n_password_field_value"));
    assert!(!audit.contains("n_password_field_value"));

    let set = project_all(&credential_node());
    let model = set.model().expect("the model projection is built");
    assert_eq!(model.value, None, "the classification withheld the value");
}
