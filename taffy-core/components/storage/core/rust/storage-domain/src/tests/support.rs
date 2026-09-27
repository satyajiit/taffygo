// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The fixtures every storage test is written against.
//!
//! One builder per record type, one identifier per role, and a fixed clock.
//! They are here so that a test reads as the property it asserts rather than
//! as forty lines of setup, and so that two tests cannot disagree about what a
//! "sample workspace" is.

pub(super) use crate::backend::{count, Connection, Value};
pub(super) use crate::clock::{FixedClock, Timestamp};
pub(super) use crate::deferred::{CHROMIUM_OWNED_NAMES, DEFERRED_AGGREGATES};
pub(super) use crate::deletion::{
    run_deletion, CloudDeletionState, DeletionRequest, DerivedArtifactPolicy, DerivedFactPolicy,
};
pub(super) use crate::error::StorageError;
pub(super) use crate::ids::{
    ArtifactId, DocumentId, EventId, FactId, ObservationId, ProvenanceId, ReceiptId, SourceId,
    WorkspaceId,
};
pub(super) use crate::journal::{self, DomainEvent, REDACTED_PAYLOAD};
pub(super) use crate::migration::{self, MigrationPhase, MIGRATIONS};
pub(super) use crate::records::{
    self, DeletionState, Fact, FactClassification, FactStatus, MembershipState, Observation,
    Ownership, ProvenanceKind, ProvenanceLocator, Sensitivity, Source, SourceKind, Workspace,
    WorkspaceStatus,
};
pub(super) use crate::retention::{self, RetentionBound, RETENTION_CLASSES};
pub(super) use crate::search::{self, IndexedRecord, SearchDocument};
pub(super) use crate::sqlite::SqliteDatabase;

pub(super) fn at(text: &str) -> Timestamp {
    Timestamp::new(text).expect("a valid fixture timestamp")
}

pub(super) fn clock() -> FixedClock {
    FixedClock::new(at("2026-08-17T09:00:00Z"))
}

pub(super) fn empty_database() -> SqliteDatabase {
    SqliteDatabase::in_memory().expect("an in-memory database")
}

pub(super) fn migrated() -> SqliteDatabase {
    let mut database = empty_database();
    migration::migrate(&mut database, &clock()).expect("migrates to head");
    database
}

pub(super) fn workspace_id() -> WorkspaceId {
    WorkspaceId::from_bytes([0x11; 16])
}

pub(super) fn source_id() -> SourceId {
    SourceId::from_bytes([0x22; 16])
}

pub(super) fn other_source_id() -> SourceId {
    SourceId::from_bytes([0x23; 16])
}

pub(super) fn fact_id() -> FactId {
    FactId::from_bytes([0x33; 16])
}

pub(super) fn shared_fact_id() -> FactId {
    FactId::from_bytes([0x34; 16])
}

pub(super) fn sample_workspace() -> Workspace {
    Workspace {
        workspace_id: workspace_id(),
        browser_profile_id: "profile-default".to_owned(),
        owner_account_id: None,
        title: "Emissions review".to_owned(),
        status: WorkspaceStatus::Active,
        revision: 1,
        created_at: at("2026-08-17T09:00:00Z"),
        updated_at: at("2026-08-17T09:00:00Z"),
        retention_class: "WORKSPACE_MANAGED".to_owned(),
        sensitivity: Sensitivity::Public,
        deletion_state: DeletionState::Active,
    }
}

pub(super) fn sample_source(id: SourceId, locator: &str) -> Source {
    Source {
        source_id: id,
        kind: SourceKind::WebPage,
        canonical_locator: Some(locator.to_owned()),
        display_locator: locator.to_owned(),
        origin: Some("https://reports.example".to_owned()),
        title: Some("Annual report".to_owned()),
        first_seen_at: at("2026-08-17T09:00:00Z"),
        last_observed_at: Some(at("2026-08-17T09:05:00Z")),
        ownership: Ownership::External,
        sensitivity: Sensitivity::Public,
        retention_class: "WORKSPACE_MANAGED".to_owned(),
        deletion_state: DeletionState::Active,
    }
}

pub(super) fn sample_observation(id: ObservationId, source: SourceId) -> Observation {
    Observation {
        observation_id: id,
        source_id: source,
        task_id: None,
        captured_at: at("2026-08-17T09:05:00Z"),
        scope: "MAIN_CONTENT".to_owned(),
        truncation: "NONE".to_owned(),
        redaction_summary: "no sensitive values observed".to_owned(),
        provenance_root: "dom".to_owned(),
        retention_class: "TASK_SESSION".to_owned(),
        encrypted_payload_ref: Some("blob-0001".to_owned()),
    }
}

pub(super) fn sample_fact(id: FactId) -> Fact {
    Fact {
        fact_id: id,
        workspace_id: workspace_id(),
        subject_key: "acme-holdings".to_owned(),
        predicate: "reported-emissions".to_owned(),
        typed_value: "412000".to_owned(),
        unit: Some("tonnes".to_owned()),
        classification: FactClassification::Extracted,
        confidence_basis_points: Some(9_000),
        observation_time: at("2026-08-17T09:05:00Z"),
        sensitivity: Sensitivity::Public,
        status: FactStatus::Accepted,
        supersedes_fact_id: None,
        retention_class: "WORKSPACE_MANAGED".to_owned(),
    }
}

pub(super) fn observation_of(source: SourceId) -> ObservationId {
    if source == source_id() {
        ObservationId::from_bytes([0x44; 16])
    } else {
        ObservationId::from_bytes([0x45; 16])
    }
}

pub(super) fn locator(id: u8, fact: FactId, source: SourceId) -> ProvenanceLocator {
    ProvenanceLocator {
        provenance_id: ProvenanceId::from_bytes([id; 16]),
        fact_id: fact,
        source_id: source,
        observation_id: Some(observation_of(source)),
        kind: ProvenanceKind::Dom,
        location_descriptor: Some("Sustainability > table 3 > row 2".to_owned()),
        extraction_rule_version: Some("table-1".to_owned()),
        transformation_chain: "parse-number".to_owned(),
        captured_at: at("2026-08-17T09:05:00Z"),
    }
}

/// A workspace with two sources, three facts and their evidence.
///
/// Used by both the round-trip tests and the deletion tests: deletion is only
/// meaningful against material that was really written.
pub(super) fn seed_workspace(database: &mut SqliteDatabase) {
    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    records::insert_workspace(executor, &sample_workspace()).unwrap();
    records::insert_source(
        executor,
        &sample_source(source_id(), "https://reports.example/a"),
    )
    .unwrap();
    records::insert_source(
        executor,
        &sample_source(other_source_id(), "https://reports.example/b"),
    )
    .unwrap();
    records::attach_source(
        executor,
        workspace_id(),
        source_id(),
        MembershipState::Included,
        "USER",
        &at("2026-08-17T09:01:00Z"),
    )
    .unwrap();
    records::attach_source(
        executor,
        workspace_id(),
        other_source_id(),
        MembershipState::Included,
        "USER",
        &at("2026-08-17T09:01:00Z"),
    )
    .unwrap();
    records::insert_observation(
        executor,
        &sample_observation(ObservationId::from_bytes([0x44; 16]), source_id()),
    )
    .unwrap();
    records::insert_observation(
        executor,
        &sample_observation(ObservationId::from_bytes([0x45; 16]), other_source_id()),
    )
    .unwrap();
    records::insert_fact(
        executor,
        &sample_fact(fact_id()),
        &[locator(0x55, fact_id(), source_id())],
    )
    .unwrap();
    // A second fact cites both sources, so deleting one narrows its lineage
    // instead of removing it.
    records::insert_fact(
        executor,
        &sample_fact(shared_fact_id()),
        &[
            locator(0x56, shared_fact_id(), source_id()),
            locator(0x57, shared_fact_id(), other_source_id()),
        ],
    )
    .unwrap();
    transaction.commit().unwrap();
}

/// One journal event at a given revision.
pub(super) fn event(id: EventId, revision: i64) -> DomainEvent {
    DomainEvent {
        event_id: id,
        aggregate_type: "TASK".to_owned(),
        aggregate_id: "task-0001".to_owned(),
        aggregate_revision: revision,
        event_type: "ObservationCaptured".to_owned(),
        occurred_at: at("2026-08-17T09:05:00Z"),
        monotonic_sequence: revision,
        actor: "TASK_RUNTIME".to_owned(),
        task_id: Some("task-0001".to_owned()),
        trace_id: "trace-0001".to_owned(),
        causation_event_id: None,
        redaction_class: "TASK_AUDIT".to_owned(),
        payload: "{\"source\":\"a\",\"summary\":\"read the emissions table\"}".to_owned(),
    }
}
