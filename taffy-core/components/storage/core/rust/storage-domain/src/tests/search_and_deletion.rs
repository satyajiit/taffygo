// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deletion, and the search index that proves it happened.
//!
//! The property under test is the one the deletion module exists for: after a
//! source is deleted, nothing derived from it is reachable by the search the
//! user believed they had emptied.

use super::support::*;

fn index_everything(database: &mut SqliteDatabase) {
    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    search::index(
        executor,
        &SearchDocument {
            document_id: DocumentId::from_bytes([0xa1; 16]),
            record: IndexedRecord::Observation(ObservationId::from_bytes([0x44; 16])),
            workspace_id: Some(workspace_id()),
            source_id: Some(source_id()),
            retention_class: "TASK_SESSION".to_owned(),
            title: "Annual report".to_owned(),
            body: "Reported emissions for the year were 412000 tonnes".to_owned(),
        },
        &at("2026-08-17T09:06:00Z"),
    )
    .unwrap();
    search::index(
        executor,
        &SearchDocument {
            document_id: DocumentId::from_bytes([0xa2; 16]),
            record: IndexedRecord::Fact(fact_id()),
            workspace_id: Some(workspace_id()),
            source_id: None,
            retention_class: "WORKSPACE_MANAGED".to_owned(),
            title: "acme-holdings reported-emissions".to_owned(),
            body: "412000 tonnes".to_owned(),
        },
        &at("2026-08-17T09:06:00Z"),
    )
    .unwrap();
    search::index(
        executor,
        &SearchDocument {
            document_id: DocumentId::from_bytes([0xa3; 16]),
            record: IndexedRecord::Observation(ObservationId::from_bytes([0x45; 16])),
            workspace_id: Some(workspace_id()),
            source_id: Some(other_source_id()),
            retention_class: "TASK_SESSION".to_owned(),
            title: "Second report".to_owned(),
            body: "An unrelated note about tonnes of paper".to_owned(),
        },
        &at("2026-08-17T09:06:00Z"),
    )
    .unwrap();
    transaction.commit().unwrap();
}

fn seed_journal_and_artifact(database: &mut SqliteDatabase) {
    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    let recorded = event(EventId::from_bytes([0x81; 16]), 1);
    journal::append(executor, &recorded).unwrap();
    journal::project_source_usage(
        executor,
        recorded.event_id,
        source_id(),
        Some(workspace_id()),
        &at("2026-08-17T09:05:00Z"),
        "read the emissions table",
    )
    .unwrap();
    executor
        .execute(
            "INSERT INTO task (task_id, browser_profile_id, kind, state, revision, user_goal, \
             created_by, assistant_snapshot, control_mode, source_scope, data_policy_snapshot, \
             provider_route_snapshot, budgets, created_at_utc, updated_at_utc, retention_class, \
             schema_version) VALUES ('task-0001', 'profile-default', 'RESEARCH', 'COMPLETED', 1, \
             'compare reported emissions', 'USER', '{}', 'ASSISTED', '{}', '{}', '{}', '{}', \
             ?1, ?1, 'TASK_SESSION', 1)",
            &[Value::text("2026-08-17T09:00:00Z")],
        )
        .unwrap();
    executor
        .execute(
            "INSERT INTO artifact (artifact_id, task_id, workspace_id, kind, state, lineage_state, \
             generation_method, created_at_utc, exported_copies, retention_class, schema_version) \
             VALUES (?1, 'task-0001', ?2, 'MARKDOWN', 'ACCEPTED', 'COMPLETE', 'SYNTHESIS', ?3, 1, \
             'WORKSPACE_MANAGED', 1)",
            &[
                Value::text(ArtifactId::from_bytes([0xb1; 16]).to_text()),
                Value::text(workspace_id().to_text()),
                Value::text("2026-08-17T09:10:00Z"),
            ],
        )
        .unwrap();
    executor
        .execute(
            "INSERT INTO artifact_lineage (artifact_id, related_type, related_id) \
             VALUES (?1, 'SOURCE', ?2)",
            &[
                Value::text(ArtifactId::from_bytes([0xb1; 16]).to_text()),
                Value::text(source_id().to_text()),
            ],
        )
        .unwrap();
    transaction.commit().unwrap();
}

fn request(fact_policy: DerivedFactPolicy) -> DeletionRequest {
    DeletionRequest {
        receipt_id: ReceiptId::from_bytes([0xc1; 16]),
        source_id: source_id(),
        fact_policy,
        artifact_policy: DerivedArtifactPolicy::MarkRemovedLineage,
        authority_revoked: true,
    }
}

#[test]
fn full_text_finds_a_source_and_then_stops_finding_it() {
    let mut database = migrated();
    seed_workspace(&mut database);
    index_everything(&mut database);
    seed_journal_and_artifact(&mut database);

    let mut transaction = database.begin().unwrap();
    let before = search::search(transaction.as_mut(), "emissions", 10).unwrap();
    assert_eq!(before.len(), 2, "the observation and the fact both match");
    transaction.rollback().unwrap();

    let receipt = run_deletion(
        &mut database,
        &request(DerivedFactPolicy::Tombstone),
        &clock(),
    )
    .expect("the deletion verifies");
    assert!(receipt.verified);

    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    let after = search::search(executor, "emissions", 10).unwrap();
    assert!(
        after.is_empty(),
        "deleted material is still searchable: {after:?}"
    );
    // The other source is untouched.
    let unrelated = search::search(executor, "paper", 10).unwrap();
    assert_eq!(unrelated.len(), 1);
    assert_eq!(search::orphaned_index_rows(executor).unwrap(), 0);
    transaction.rollback().unwrap();
}

#[test]
fn deleting_a_source_removes_or_relabels_every_derived_record() {
    let mut database = migrated();
    seed_workspace(&mut database);
    index_everything(&mut database);
    seed_journal_and_artifact(&mut database);

    let receipt = run_deletion(
        &mut database,
        &request(DerivedFactPolicy::Tombstone),
        &clock(),
    )
    .expect("the deletion verifies");

    assert_eq!(receipt.observations_removed, 1);
    assert_eq!(receipt.provenance_removed, 2);
    assert_eq!(receipt.facts_relabeled, 1);
    assert_eq!(receipt.facts_removed, 0);
    assert_eq!(receipt.memberships_removed, 1);
    assert_eq!(receipt.journal_projections_removed, 1);
    assert_eq!(receipt.journal_events_relabeled, 1);
    assert_eq!(receipt.artifacts_relabeled, 1);
    assert_eq!(receipt.blob_refs_released, vec!["blob-0001".to_owned()]);
    // A copy the user exported is counted, never claimed as deleted.
    assert_eq!(receipt.external_copies, 1);
    assert_eq!(receipt.cloud_deletion, CloudDeletionState::NotApplicable);

    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();

    assert!(records::load_source(executor, source_id())
        .unwrap()
        .is_none());
    assert!(records::load_source(executor, other_source_id())
        .unwrap()
        .is_some());

    // The fact whose only evidence was this source keeps its identity and loses
    // its value; the fact with other evidence survives with narrower lineage.
    let removed = records::load_fact(executor, fact_id()).unwrap().unwrap();
    assert_eq!(removed.status, FactStatus::Removed);
    assert_eq!(removed.typed_value, "");
    let survivor = records::load_fact(executor, shared_fact_id())
        .unwrap()
        .unwrap();
    assert_eq!(survivor.status, FactStatus::Accepted);
    assert_eq!(
        records::fact_provenance(executor, shared_fact_id())
            .unwrap()
            .len(),
        1
    );

    // The journal keeps the record that a source was used and loses what it
    // said about it.
    assert_eq!(journal::event_count(executor).unwrap(), 1);
    assert_eq!(
        journal::payload_of(executor, EventId::from_bytes([0x81; 16])).unwrap(),
        Some(REDACTED_PAYLOAD.to_owned())
    );

    let lineage: Vec<String> = executor
        .query(
            "SELECT lineage_state FROM artifact WHERE artifact_id = ?1",
            &[Value::text(ArtifactId::from_bytes([0xb1; 16]).to_text())],
        )
        .unwrap()
        .iter()
        .map(|row| row.text(0).unwrap().to_owned())
        .collect();
    assert_eq!(lineage, vec!["SOURCE_REMOVED".to_owned()]);

    // A content-free tombstone and one receipt survive.
    assert_eq!(
        count(
            executor,
            "SELECT COUNT(*) FROM deletion_tombstone WHERE record_type = 'SOURCE'",
            &[]
        )
        .unwrap(),
        1
    );
    assert_eq!(
        count(executor, "SELECT COUNT(*) FROM deletion_receipt", &[]).unwrap(),
        1
    );
    transaction.rollback().unwrap();
}

#[test]
fn the_delete_policy_removes_the_orphaned_fact_outright() {
    let mut database = migrated();
    seed_workspace(&mut database);
    index_everything(&mut database);
    seed_journal_and_artifact(&mut database);

    let mut transaction = database.begin().unwrap();
    transaction
        .execute(
            "INSERT INTO claim (claim_id, task_id, artifact_id, body, classification, \
             validation_state, generated_by, schema_version) VALUES \
             ('claim-delete', 'task-0001', NULL, 'the supported claim', 'MODEL', \
              'VALIDATED', NULL, 1)",
            &[],
        )
        .unwrap();
    transaction
        .execute(
            "INSERT INTO claim_support (claim_id, fact_id) VALUES ('claim-delete', ?1)",
            &[Value::text(fact_id().to_text())],
        )
        .unwrap();
    transaction.commit().unwrap();

    let receipt = run_deletion(&mut database, &request(DerivedFactPolicy::Delete), &clock())
        .expect("the deletion verifies");
    assert_eq!(receipt.facts_removed, 1);
    assert_eq!(receipt.facts_relabeled, 0);
    assert_eq!(
        receipt.claims_relabeled, 1,
        "a claim must be relabeled before its last support row is deleted"
    );

    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    assert!(records::load_fact(executor, fact_id()).unwrap().is_none());
    let states = executor
        .query(
            "SELECT validation_state FROM claim WHERE claim_id = 'claim-delete'",
            &[],
        )
        .unwrap();
    assert_eq!(
        states.first().map(|row| row.text(0).unwrap()),
        Some("UNSUPPORTED")
    );
    transaction.rollback().unwrap();
}

#[test]
fn a_claim_losing_two_supports_is_counted_once() {
    let mut database = migrated();
    seed_workspace(&mut database);
    seed_journal_and_artifact(&mut database);
    let second_fact = FactId::from_bytes([0x35; 16]);

    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    records::insert_fact(
        executor,
        &sample_fact(second_fact),
        &[locator(0x58, second_fact, source_id())],
    )
    .unwrap();
    executor
        .execute(
            "INSERT INTO claim (claim_id, task_id, artifact_id, body, classification, \
             validation_state, generated_by, schema_version) VALUES \
             ('claim-two-supports', 'task-0001', NULL, 'the supported claim', 'MODEL', \
              'VALIDATED', NULL, 1)",
            &[],
        )
        .unwrap();
    executor
        .execute(
            "INSERT INTO claim_support (claim_id, fact_id) VALUES \
             ('claim-two-supports', ?1), ('claim-two-supports', ?2)",
            &[
                Value::text(fact_id().to_text()),
                Value::text(second_fact.to_text()),
            ],
        )
        .unwrap();
    transaction.commit().unwrap();

    let receipt = run_deletion(&mut database, &request(DerivedFactPolicy::Delete), &clock())
        .expect("the deletion verifies");
    assert_eq!(
        receipt.claims_relabeled, 1,
        "a receipt counts changed claims, not removed support rows"
    );

    let mut transaction = database.begin().unwrap();
    let states = transaction
        .query(
            "SELECT validation_state FROM claim WHERE claim_id = 'claim-two-supports'",
            &[],
        )
        .unwrap();
    assert_eq!(
        states.first().map(|row| row.text(0).unwrap()),
        Some("UNSUPPORTED")
    );
    transaction.rollback().unwrap();
}

#[test]
fn a_deletion_receipt_counts_only_exports_derived_from_the_deleted_source() {
    let mut database = migrated();
    seed_workspace(&mut database);
    seed_journal_and_artifact(&mut database);

    let mut transaction = database.begin().unwrap();
    transaction
        .execute(
            "INSERT INTO artifact (artifact_id, task_id, workspace_id, kind, state, \
             lineage_state, generation_method, created_at_utc, exported_copies, \
             retention_class, schema_version) VALUES (?1, 'task-0001', ?2, 'MARKDOWN', \
             'ACCEPTED', 'COMPLETE', 'SYNTHESIS', ?3, 7, 'WORKSPACE_MANAGED', 1)",
            &[
                Value::text(ArtifactId::from_bytes([0xb2; 16]).to_text()),
                Value::text(workspace_id().to_text()),
                Value::text("2026-08-17T09:11:00Z"),
            ],
        )
        .unwrap();
    transaction.commit().unwrap();

    let receipt = run_deletion(
        &mut database,
        &request(DerivedFactPolicy::Tombstone),
        &clock(),
    )
    .expect("the deletion verifies");
    assert_eq!(
        receipt.external_copies, 1,
        "an unrelated artifact's seven exports do not belong to this receipt"
    );
}

#[path = "search_and_deletion/artifact_policy.rs"]
mod artifact_policy;
#[path = "search_and_deletion/failure_cases.rs"]
mod failure_cases;
