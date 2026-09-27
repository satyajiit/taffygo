// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fail-closed deletion, retry, and search-input cases.

use super::*;

#[test]
fn deletion_refuses_to_start_before_running_work_is_stopped() {
    let mut database = migrated();
    seed_workspace(&mut database);
    let mut careless = request(DerivedFactPolicy::Tombstone);
    careless.authority_revoked = false;
    let error = run_deletion(&mut database, &careless, &clock()).unwrap_err();
    assert!(matches!(error, StorageError::DeletionUnverified { .. }));

    let mut transaction = database.begin().unwrap();
    assert!(records::load_source(transaction.as_mut(), source_id())
        .unwrap()
        .is_some());
    transaction.rollback().unwrap();
}

#[test]
fn a_deletion_that_cannot_verify_commits_nothing() {
    let mut database = migrated();
    seed_workspace(&mut database);
    index_everything(&mut database);

    database
        .raw_execute(
            "CREATE TRIGGER resurrect AFTER DELETE ON workspace_source BEGIN \
             INSERT INTO search_document (document_id, record_type, record_id, workspace_id, \
             source_id, retention_class, indexed_at_utc) \
             VALUES ('resurrected', 'OBSERVATION', 'x', NULL, OLD.source_id, 'TASK_SESSION', \
             '2026-08-17T09:00:00Z'); END",
        )
        .unwrap();

    let error = run_deletion(
        &mut database,
        &request(DerivedFactPolicy::Tombstone),
        &clock(),
    )
    .unwrap_err();
    assert!(matches!(error, StorageError::DeletionUnverified { .. }));

    database.raw_execute("DROP TRIGGER resurrect").unwrap();
    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    assert!(records::load_source(executor, source_id())
        .unwrap()
        .is_some());
    assert_eq!(search::search(executor, "emissions", 10).unwrap().len(), 2);
    assert_eq!(
        count(executor, "SELECT COUNT(*) FROM deletion_receipt", &[]).unwrap(),
        0
    );
    transaction.rollback().unwrap();
}

#[test]
fn a_fact_tombstone_that_restores_content_cannot_verify() {
    let mut database = migrated();
    seed_workspace(&mut database);
    index_everything(&mut database);

    database
        .raw_execute(
            "CREATE TRIGGER restore_fact_content AFTER UPDATE OF status ON fact \
             WHEN NEW.status = 'REMOVED' BEGIN \
             UPDATE fact SET typed_value = 'restored secret' WHERE fact_id = NEW.fact_id; END",
        )
        .unwrap();

    let error = run_deletion(
        &mut database,
        &request(DerivedFactPolicy::Tombstone),
        &clock(),
    )
    .unwrap_err();
    assert!(matches!(error, StorageError::DeletionUnverified { .. }));

    database
        .raw_execute("DROP TRIGGER restore_fact_content")
        .unwrap();
    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    assert!(records::load_source(executor, source_id())
        .unwrap()
        .is_some());
    assert_ne!(
        records::load_fact(executor, fact_id())
            .unwrap()
            .unwrap()
            .typed_value,
        "restored secret"
    );
    transaction.rollback().unwrap();
}

#[test]
fn a_claim_that_restores_its_old_state_cannot_verify() {
    let mut database = migrated();
    seed_workspace(&mut database);
    seed_journal_and_artifact(&mut database);

    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    executor
        .execute(
            "INSERT INTO claim (claim_id, task_id, artifact_id, body, classification, \
             validation_state, generated_by, schema_version) VALUES \
             ('claim-restored', 'task-0001', NULL, 'a claim', 'MODEL', 'VALIDATED', NULL, 1)",
            &[],
        )
        .unwrap();
    executor
        .execute(
            "INSERT INTO claim_support (claim_id, fact_id) VALUES ('claim-restored', ?1)",
            &[Value::text(fact_id().to_text())],
        )
        .unwrap();
    transaction.commit().unwrap();
    database
        .raw_execute(
            "CREATE TRIGGER restore_claim_state AFTER UPDATE OF validation_state ON claim \
             WHEN NEW.validation_state <> 'VALIDATED' BEGIN \
             UPDATE claim SET validation_state = 'VALIDATED' WHERE claim_id = NEW.claim_id; END",
        )
        .unwrap();

    let error = run_deletion(
        &mut database,
        &request(DerivedFactPolicy::Tombstone),
        &clock(),
    )
    .unwrap_err();
    assert!(matches!(error, StorageError::DeletionUnverified { .. }));

    database
        .raw_execute("DROP TRIGGER restore_claim_state")
        .unwrap();
    let mut transaction = database.begin().unwrap();
    assert!(records::load_source(transaction.as_mut(), source_id())
        .unwrap()
        .is_some());
    transaction.rollback().unwrap();
}

#[test]
fn an_artifact_that_loses_its_lineage_mark_cannot_verify() {
    let mut database = migrated();
    seed_workspace(&mut database);
    seed_journal_and_artifact(&mut database);
    database
        .raw_execute(
            "CREATE TRIGGER restore_artifact_lineage AFTER UPDATE OF lineage_state ON artifact \
             WHEN NEW.lineage_state = 'SOURCE_REMOVED' BEGIN \
             UPDATE artifact SET lineage_state = 'COMPLETE' \
             WHERE artifact_id = NEW.artifact_id; END",
        )
        .unwrap();

    let error = run_deletion(
        &mut database,
        &request(DerivedFactPolicy::Tombstone),
        &clock(),
    )
    .unwrap_err();
    assert!(matches!(error, StorageError::DeletionUnverified { .. }));

    database
        .raw_execute("DROP TRIGGER restore_artifact_lineage")
        .unwrap();
    let mut transaction = database.begin().unwrap();
    assert!(records::load_source(transaction.as_mut(), source_id())
        .unwrap()
        .is_some());
    transaction.rollback().unwrap();
}

#[test]
fn an_artifact_delete_that_the_backend_ignores_cannot_verify() {
    let mut database = migrated();
    seed_workspace(&mut database);
    seed_journal_and_artifact(&mut database);
    database
        .raw_execute(
            "CREATE TRIGGER preserve_artifact BEFORE DELETE ON artifact BEGIN \
             SELECT RAISE(IGNORE); END",
        )
        .unwrap();

    let mut deletion = request(DerivedFactPolicy::Tombstone);
    deletion.artifact_policy = DerivedArtifactPolicy::Delete;
    let error = run_deletion(&mut database, &deletion, &clock()).unwrap_err();
    assert!(matches!(error, StorageError::DeletionUnverified { .. }));

    database
        .raw_execute("DROP TRIGGER preserve_artifact")
        .unwrap();
    let mut transaction = database.begin().unwrap();
    assert!(records::load_source(transaction.as_mut(), source_id())
        .unwrap()
        .is_some());
    transaction.rollback().unwrap();
}

#[test]
fn a_source_being_deleted_stops_being_readable_first() {
    let mut database = migrated();
    seed_workspace(&mut database);
    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    executor
        .execute(
            "UPDATE source SET deletion_state = 'DELETING' WHERE source_id = ?1",
            &[Value::text(source_id().to_text())],
        )
        .unwrap();
    let readable = records::readable_sources(executor, workspace_id()).unwrap();
    assert_eq!(readable.len(), 1);
    assert_eq!(
        readable.first().map(|source| source.source_id),
        Some(other_source_id())
    );
    transaction.rollback().unwrap();
}

#[test]
fn a_search_term_is_text_and_not_an_expression() {
    let mut database = migrated();
    seed_workspace(&mut database);
    index_everything(&mut database);
    let mut transaction = database.begin().unwrap();
    let hits = search::search(transaction.as_mut(), "emissions OR paper", 10).unwrap();
    assert!(hits.is_empty());
    transaction.rollback().unwrap();
}

#[test]
fn deleting_the_same_source_twice_removes_nothing_the_second_time() {
    let mut database = migrated();
    seed_workspace(&mut database);
    index_everything(&mut database);
    seed_journal_and_artifact(&mut database);
    run_deletion(
        &mut database,
        &request(DerivedFactPolicy::Tombstone),
        &clock(),
    )
    .unwrap();

    let mut retry = request(DerivedFactPolicy::Tombstone);
    retry.receipt_id = ReceiptId::from_bytes([0xc2; 16]);
    let second = run_deletion(&mut database, &retry, &clock()).expect("verifies");
    assert!(second.verified);
    assert_eq!(second.observations_removed, 0);
    assert_eq!(second.provenance_removed, 0);
    assert_eq!(second.facts_relabeled, 0);
    assert_eq!(second.memberships_removed, 0);

    let mut transaction = database.begin().unwrap();
    assert_eq!(
        count(
            transaction.as_mut(),
            "SELECT COUNT(*) FROM deletion_tombstone WHERE record_type = 'SOURCE'",
            &[]
        )
        .unwrap(),
        1,
        "the tombstone stays a single record of one deletion"
    );
    transaction.rollback().unwrap();
}
