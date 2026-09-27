// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The milestone M2 exit criterion, over the real corpus.
//!
//! Roadmap section 5: "seeded-secret fixtures produce zero plaintext-secret
//! model, context, log, crash, analytics, and audit records." The tokens are
//! not written here — they are read from `test-fixtures/web/manifest.json`,
//! which is the corpus's own authority for what a canary is and which class it
//! belongs to. A canary added to the corpus is a canary this suite starts
//! checking, and a canary that neither this suite can place nor
//! [`CLAIMED_ELSEWHERE`] names an owner for is a failure rather than a silent
//! gap — in both directions, since an excuse for a class the corpus no longer
//! declares fails too.
//!
//! Each token is driven through every projection and every serializer these
//! three crates own:
//!
//! | Crate | Driven through |
//! |---|---|
//! | `policy-engine` | The zone classifier, the value masker, the URL splitter, and all four destination projections |
//! | `audit-engine` | The event payload, the append-only journal, the audit serializer, the telemetry serializer, and the replayed projection |
//! | `task-engine` | The Markdown export and the comma-separated export |
//!
//! Two routes are exercised for each token, because a secret arrives two ways:
//! **through a field somebody can recognize**, where classification withholds
//! it, and **through a field nobody classified**, where the masker and the
//! field table are all that stand between it and a narrower destination.
//!
//! The assertions are made against serialized bytes and whole debug renderings
//! rather than against named fields, so a field added to a record and forgotten
//! about is still checked.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;

use std::path::PathBuf;

use audit_engine::{
    audit_record, telemetry_record, Actor, AggregateId, AggregateType, DerivedEventIds, EventDraft,
    EventPayload, EventType, FieldName, FieldValue, Journal, ManualClock, MemoryLog,
    RedactionClass, SchemaVersion, StreamId, TraceId, ValueKind,
};
use bip_types::identity::SemanticNodeId;
use bip_types::snapshot::{SemanticRole, Sensitivity};
use policy_engine::pipeline::project_all;
use policy_engine::redaction::{
    mask_secret_shaped, split_url, AutocompleteSignal, AutocompleteToken, InputType,
    InputTypeSignal,
};
use policy_engine::{FieldObservation, RedactionDestination};
use task_engine::artifact::{generate, ArtifactKind, CitedFact};
use task_engine::ids::ArtifactId;
use task_engine::ArtifactRequest;
use task_engine::{
    DeletionState, Fact, FactClassification, FactStatus, Ownership, ProvenanceId, ProvenanceKind,
    ProvenanceLocator, Sensitivity as ArtifactSensitivity, Source, SourceKind, Timestamp,
    WorkspaceId,
};

// ---------------------------------------------------------------------------
// The corpus manifest.
// ---------------------------------------------------------------------------

/// One canary as the corpus declares it.
#[derive(Clone, Debug, PartialEq, Eq)]
struct Canary {
    /// The token itself.
    token: String,
    /// The class the corpus says it belongs to.
    class: String,
    /// The fixtures that carry it.
    carried_by: Vec<String>,
}

/// Canary classes that are real, and that this suite is deliberately not the
/// one that drives them.
///
/// The corpus used to carry exactly one kind of canary — a secret a field can
/// hold — so "every canary this suite can place" and "every canary" were the
/// same set, and the module header's rule could be written as the flat claim it
/// still is. Decision 0090 added a second kind: a **page-authored file name**,
/// which is not a secret at all and is not withheld by anything. It is
/// attacker-authored text that the browsing plane legitimately displays and the
/// task plane must never hold, so what has to be proved about it is a crossing
/// rather than a classification, and proving it needs a real download the
/// browser started.
///
/// Placing it in a field here would assert something false in the direction
/// that matters: that field classification withholds a file name. It does not,
/// it should not, and a green test saying it did would be worse than no test.
///
/// So the rule is kept and made explicit instead of loosened. Every class is
/// either placed by this suite or named here with the suite that owns it, and a
/// class that is neither still stops the run — which is the property the module
/// header is really claiming.
const CLAIMED_ELSEWHERE: &[(&str, &str)] = &[(
    "page-authored-file-name",
    "taffy-core/browser/download_command_router_browsertest.cc — the canary is \
     served as a transfer's file name and must reach the browsing projection \
     and nothing the task plane can see (decision 0090)",
)];

/// The corpus canaries this suite is the one that drives.
fn driven() -> Vec<Canary> {
    canaries()
        .into_iter()
        .filter(|canary| {
            !CLAIMED_ELSEWHERE
                .iter()
                .any(|(class, _)| canary.class == *class)
        })
        .collect()
}

/// Finds the repository-owned corpus without coupling it to crate placement.
fn manifest_path() -> PathBuf {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    manifest_dir
        .ancestors()
        .map(|ancestor| ancestor.join("test-fixtures/web/manifest.json"))
        .find(|candidate| candidate.is_file())
        .expect("the fixture corpus manifest must exist")
}

/// Reads the canaries the corpus declares.
fn canaries() -> Vec<Canary> {
    let text = std::fs::read_to_string(manifest_path()).expect("the manifest must be readable");
    let document: serde_json::Value =
        serde_json::from_str(&text).expect("the manifest must be well-formed");
    let declared = document
        .get("canaries")
        .and_then(serde_json::Value::as_array)
        .expect("the manifest declares its canaries");

    declared
        .iter()
        .map(|entry| Canary {
            token: entry
                .get("token")
                .and_then(serde_json::Value::as_str)
                .expect("every canary declares a token")
                .to_owned(),
            class: entry
                .get("class")
                .and_then(serde_json::Value::as_str)
                .expect("every canary declares a class")
                .to_owned(),
            carried_by: entry
                .get("carried_by")
                .and_then(serde_json::Value::as_array)
                .expect("every canary declares which fixtures carry it")
                .iter()
                .filter_map(|value| value.as_str().map(str::to_owned))
                .collect(),
        })
        .collect()
}

// ---------------------------------------------------------------------------
// Placing a token the way the corpus places it.
// ---------------------------------------------------------------------------

/// The control the corpus puts each class of secret in.
fn classified_observation(canary: &Canary) -> FieldObservation {
    let mut observation =
        FieldObservation::new(SemanticNodeId::new("n_secret"), SemanticRole::TextField);
    observation.value = Some(canary.token.clone());
    match canary.class.as_str() {
        "password" => {
            observation.name = Some("Password".to_owned());
            observation.signals.input_type = InputTypeSignal::Known(InputType::Password);
        }
        "one-time-code" => {
            observation.name = Some("Enter the code we sent you".to_owned());
            // The page insists it is ordinary. Classification joins, so it rises
            // anyway.
            observation.declared_sensitivity = Sensitivity::NotSensitive;
            observation.signals.autocomplete =
                AutocompleteSignal::Known(AutocompleteToken::OneTimeCode);
        }
        "card-security-code" => {
            observation.name = Some("Security code (CVC)".to_owned());
        }
        "session-token" => {
            observation.name = Some("Session token".to_owned());
        }
        "recovery-code" => {
            observation.name = Some("Recovery code".to_owned());
        }
        "seed-phrase" => {
            observation.name = Some("Wallet seed phrase".to_owned());
            observation.value = None;
            observation.text_runs = vec![canary.token.clone()];
        }
        "api-key" => {
            observation.name = Some("API key".to_owned());
        }
        other => {
            unreachable!(
                "the corpus declares a canary class this suite cannot place and no suite \
                 claims: {other}. Add a placement above, or name the suite that owns it \
                 in CLAIMED_ELSEWHERE."
            )
        }
    }
    observation
}

/// The same token where nothing declares it: pasted into a search box, quoted
/// in ordinary page text, and hidden in a link's query string.
fn unclassified_observations(canary: &Canary) -> Vec<(&'static str, FieldObservation)> {
    let mut placements = Vec::new();

    let mut search =
        FieldObservation::new(SemanticNodeId::new("n_search"), SemanticRole::SearchField);
    search.name = Some("Search".to_owned());
    search.signals.input_type = InputTypeSignal::Known(InputType::Search);
    search.value = Some(canary.token.clone());
    placements.push(("pasted into a search box", search));

    let mut prose = FieldObservation::new(SemanticNodeId::new("n_prose"), SemanticRole::Paragraph);
    prose.name = Some("Support ticket".to_owned());
    prose.text_runs = vec![format!(
        "Your temporary value is {} until Friday",
        canary.token
    )];
    placements.push(("quoted in ordinary page text", prose));

    let mut link = FieldObservation::new(SemanticNodeId::new("n_link"), SemanticRole::Link);
    link.name = Some("Continue".to_owned());
    link.destination_url = Some(format!(
        "https://example.test/continue?value={}",
        canary.token
    ));
    placements.push(("hidden in a link's query string", link));

    placements
}

/// The destinations nothing page-derived may reach with a secret intact.
const NARROW: &[RedactionDestination] = &[
    RedactionDestination::ModelProjection,
    RedactionDestination::Audit,
    RedactionDestination::Telemetry,
];

/// Asserts the token appears nowhere in `rendered`.
#[track_caller]
fn assert_absent(rendered: &str, token: &str, where_from: &str) {
    assert!(
        !rendered.contains(token),
        "a seeded secret reached {where_from}: {rendered}"
    );
}

// ---------------------------------------------------------------------------
// policy-engine: classification, masking, and the four destinations.
// ---------------------------------------------------------------------------

#[test]
fn the_corpus_declares_the_canaries_this_suite_drives() {
    let all = canaries();
    assert!(
        all.len() >= 10,
        "the corpus declares {} canaries",
        all.len()
    );
    for canary in &all {
        assert!(canary.token.starts_with("TAFFYGO-CANARY-"));
        assert!(
            !canary.carried_by.is_empty(),
            "{} names no fixture",
            canary.token
        );
    }

    for canary in &driven() {
        // Placing it is what proves the suite covers it: a class that is
        // neither placeable here nor claimed by a named suite stops the run
        // rather than being skipped.
        let observation = classified_observation(canary);
        assert!(
            observation.value.is_some() || !observation.text_runs.is_empty(),
            "{} was placed in no field",
            canary.token
        );
    }

    // The other half of the same rule, and the half that rots quietly: a class
    // excused here has to still be in the corpus. Without this, deleting a
    // canary leaves an excuse standing for a class nothing declares any more,
    // and the next class given that name inherits the excuse.
    for (class, owner) in CLAIMED_ELSEWHERE {
        assert!(
            all.iter().any(|canary| canary.class == *class),
            "no canary of class {class} is declared, but it is excused to {owner}"
        );
    }
}

#[test]
fn a_classified_secret_reaches_no_destination_at_all() {
    // The corpus places every canary in a field its class makes recognizable,
    // so the classifier withholds it everywhere — including the in-process
    // local observation, which is the "context" the exit criterion names.
    for canary in driven() {
        let observation = classified_observation(&canary);
        let set = project_all(&observation);
        assert_eq!(set.verify_narrowing(), Ok(()), "{}", canary.token);
        assert!(
            set.classification().is_never_extract(),
            "{} was not recognized as a never-extract class",
            canary.token
        );
        for destination in RedactionDestination::ALL {
            let rendered = format!("{:?}", set.projection(*destination));
            assert_absent(
                &rendered,
                &canary.token,
                &format!("the {} projection", destination.label()),
            );
        }
    }
}

#[test]
fn an_unclassified_secret_reaches_no_narrow_destination() {
    for canary in driven() {
        for (placement, observation) in unclassified_observations(&canary) {
            let set = project_all(&observation);
            assert_eq!(set.verify_narrowing(), Ok(()), "{placement}");
            for destination in NARROW {
                let rendered = format!("{:?}", set.projection(*destination));
                assert_absent(
                    &rendered,
                    &canary.token,
                    &format!("the {} projection ({placement})", destination.label()),
                );
                for fragment in set.fragments(*destination) {
                    assert_absent(
                        fragment,
                        &canary.token,
                        &format!("a {} fragment ({placement})", destination.label()),
                    );
                }
            }
        }
    }
}

#[test]
fn every_canary_is_secret_shaped_to_the_value_masker() {
    // The masker is the defence for a secret nobody classified. If a token in
    // the corpus were invisible to it, the previous test would be passing on
    // the field table alone.
    for canary in driven() {
        let masked = mask_secret_shaped(&canary.token);
        assert!(
            masked.masked_spans >= 1,
            "{} is invisible to the value masker",
            canary.token
        );
        assert_absent(&masked.text, &canary.token, "the masked text");
    }
}

#[test]
fn a_url_carrying_a_canary_reduces_to_an_origin_that_does_not() {
    for canary in driven() {
        let url = format!("https://example.test/reset?value={}#tail", canary.token);
        let parts = split_url(&url).expect("the fixture URL must split");
        assert_absent(
            &parts.origin.display(),
            &canary.token,
            "a normalized origin",
        );
        assert_absent(
            parts.path.as_deref().unwrap_or_default(),
            &canary.token,
            "a normalized path",
        );
    }
}

// ---------------------------------------------------------------------------
// audit-engine: the payload, the journal, and both serializers.
// ---------------------------------------------------------------------------

/// A value of the kind a field name declares, carrying the token.
fn hostile_value(name: FieldName, token: &str) -> FieldValue {
    match name.expected_kind() {
        ValueKind::Identifier => FieldValue::Identifier(token.to_owned()),
        // An enumerated field declares a compiled-in name, so the token can only
        // reach it wearing the wrong kind — which is the smuggling route the
        // kind-agreement gate exists for.
        ValueKind::Text | ValueKind::Enumerated => FieldValue::Text(token.to_owned()),
        ValueKind::Url => FieldValue::Url(format!("https://example.test/x?value={token}")),
        ValueKind::Count => FieldValue::Count(u64::try_from(token.len()).unwrap_or(0)),
        ValueKind::Flag => FieldValue::Flag(true),
    }
}

/// Records one event and returns the audit and telemetry serializations.
fn serialize_event(payload: EventPayload, task_id: &str) -> (String, Option<String>) {
    let mut journal = Journal::new(ManualClock::at(1_000), DerivedEventIds, MemoryLog::new());
    let event = journal
        .record(EventDraft {
            stream: StreamId::new(AggregateType::Task, AggregateId::new("task_1")),
            expected_revision: 0,
            event_type: EventType::ActionProposed,
            schema_version: SchemaVersion(1),
            actor: Actor::Policy,
            task_id: Some(AggregateId::new(task_id)),
            trace_id: TraceId::new("trace_1"),
            causation_event_id: None,
            correlation_id: None,
            redaction_class: RedactionClass::Operational,
            payload,
        })
        .expect("the fixture append must succeed");

    let audit = serde_json::to_string(&audit_record(&event)).expect("the audit record serializes");
    let telemetry = telemetry_record(&event)
        .map(|record| serde_json::to_string(&record).expect("the telemetry record serializes"));
    (audit, telemetry)
}

#[test]
fn no_canary_survives_a_payload_field_of_any_kind() {
    // Every field name, with the token wearing the kind that name declares, and
    // the token again as the envelope's own task identifier. This is the
    // hostile-caller route: the audit crate assumes every layer above it failed.
    for canary in driven() {
        for name in FieldName::ALL {
            let payload = EventPayload::new().with(*name, hostile_value(*name, &canary.token));
            let (audit, telemetry) = serialize_event(payload, &canary.token);
            assert_absent(
                &audit,
                &canary.token,
                &format!("an audit record through {}", name.label()),
            );
            if let Some(telemetry) = telemetry {
                assert_absent(
                    &telemetry,
                    &canary.token,
                    &format!("a telemetry record through {}", name.label()),
                );
            }
        }
    }
}

#[test]
fn no_canary_survives_the_journal_or_the_projection_replayed_from_it() {
    for canary in driven() {
        let mut journal = Journal::new(ManualClock::at(1_000), DerivedEventIds, MemoryLog::new());
        let payload = EventPayload::new()
            .with_identifier(FieldName::CapabilityId, canary.token.clone())
            .with_url(
                FieldName::DestinationUrl,
                format!("https://example.test/x?value={}", canary.token),
            )
            .with_enumerated(FieldName::ResultCode, "DENIED_BY_POLICY");
        journal
            .record(EventDraft {
                stream: StreamId::new(AggregateType::Task, AggregateId::new("task_1")),
                expected_revision: 0,
                event_type: EventType::ActionProposed,
                schema_version: SchemaVersion(1),
                actor: Actor::Policy,
                task_id: Some(AggregateId::new("task_1")),
                trace_id: TraceId::new("trace_1"),
                causation_event_id: None,
                correlation_id: None,
                redaction_class: RedactionClass::Decision,
                payload,
            })
            .expect("the fixture append must succeed");

        // The journal holds the event as the caller handed it over, which is
        // exactly why the serializers redact rather than the log. What must not
        // carry a secret is anything derived from it.
        let events = journal.log().events();
        let projection = audit_engine::replay(events).expect("the journal replays");
        assert_absent(
            &format!("{projection:?}"),
            &canary.token,
            "a replayed task projection",
        );
        for event in events {
            let record = audit_record(event);
            assert_absent(
                &serde_json::to_string(&record).expect("the audit record serializes"),
                &canary.token,
                "an audit record built from the journal",
            );
        }
    }
}

// ---------------------------------------------------------------------------
// task-engine: the two exports.
// ---------------------------------------------------------------------------

fn stamp(text: &str) -> Timestamp {
    Timestamp::new(text).expect("the fixture timestamp is well formed")
}

fn source() -> Source {
    Source {
        source_id: common::source_id(1),
        kind: SourceKind::WebPage,
        canonical_locator: None,
        display_locator: "example.test/account".to_owned(),
        origin: Some("https://example.test".to_owned()),
        title: Some("Account settings".to_owned()),
        first_seen_at: stamp("2026-08-17T09:00:00Z"),
        last_observed_at: Some(stamp("2026-08-17T09:05:00Z")),
        ownership: Ownership::External,
        sensitivity: ArtifactSensitivity::Public,
        retention_class: "workspace".to_owned(),
        deletion_state: DeletionState::Active,
    }
}

/// A fact whose value is whatever the model projection was willing to carry.
fn fact_from(value: &str) -> CitedFact {
    let mut bytes = [0_u8; 16];
    bytes[15] = 3;
    CitedFact {
        fact: Fact {
            fact_id: common::fact_id(3),
            workspace_id: WorkspaceId::from_bytes(bytes),
            subject_key: "Account".to_owned(),
            predicate: "recovery hint".to_owned(),
            typed_value: value.to_owned(),
            unit: None,
            classification: FactClassification::Extracted,
            confidence_basis_points: Some(9_000),
            observation_time: stamp("2026-08-17T09:05:00Z"),
            sensitivity: ArtifactSensitivity::Public,
            status: FactStatus::Accepted,
            supersedes_fact_id: None,
            retention_class: "workspace".to_owned(),
        },
        provenance: vec![ProvenanceLocator {
            provenance_id: {
                let mut bytes = [0_u8; 16];
                bytes[15] = 3;
                ProvenanceId::from_bytes(bytes)
            },
            fact_id: common::fact_id(3),
            source_id: common::source_id(1),
            observation_id: None,
            kind: ProvenanceKind::Dom,
            location_descriptor: Some("Account panel".to_owned()),
            extraction_rule_version: Some("rule_1".to_owned()),
            transformation_chain: "normalize".to_owned(),
            captured_at: stamp("2026-08-17T09:05:00Z"),
        }],
    }
}

#[test]
fn no_canary_reaches_an_export_built_from_what_the_model_was_told() {
    // The export is built from facts, and facts come from what left the local
    // observation. Feeding each export the narrowest thing a canary could have
    // become proves the whole chain rather than the last link of it.
    for canary in driven() {
        for (placement, observation) in unclassified_observations(&canary) {
            let set = project_all(&observation);
            let carried: String = set
                .fragments(RedactionDestination::ModelProjection)
                .collect::<Vec<_>>()
                .join(" ");
            let request = ArtifactRequest {
                artifact_id: ArtifactId::new("artifact_0"),
                title: "Account review".to_owned(),
                schema_version: 1,
                sources: vec![source()],
                facts: vec![fact_from(&carried)],
            };
            for kind in [ArtifactKind::Markdown, ArtifactKind::Csv] {
                let artifact = generate(&request, kind).expect("the fixture artifact generates");
                assert_absent(
                    &artifact.content,
                    &canary.token,
                    &format!("a {} export ({placement})", kind.extension()),
                );
            }
        }
    }
}
