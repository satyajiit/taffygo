// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The explicit choice to remove a local artifact derived from a source.

use super::*;

#[test]
fn the_delete_policy_removes_an_artifact_its_lineage_and_its_index_entry() {
    let mut database = migrated();
    seed_workspace(&mut database);
    seed_journal_and_artifact(&mut database);
    let artifact_id = ArtifactId::from_bytes([0xb1; 16]);

    let mut transaction = database.begin().unwrap();
    search::index(
        transaction.as_mut(),
        &SearchDocument {
            document_id: DocumentId::from_bytes([0xa4; 16]),
            record: IndexedRecord::Artifact(artifact_id),
            workspace_id: Some(workspace_id()),
            source_id: None,
            retention_class: "WORKSPACE_MANAGED".to_owned(),
            title: "Accepted result".to_owned(),
            body: "Reported emissions".to_owned(),
        },
        &at("2026-08-17T09:12:00Z"),
    )
    .unwrap();
    transaction.commit().unwrap();

    let mut deletion = request(DerivedFactPolicy::Tombstone);
    deletion.artifact_policy = DerivedArtifactPolicy::Delete;
    let receipt = run_deletion(&mut database, &deletion, &clock()).expect("deletion verifies");
    assert_eq!(receipt.artifacts_removed, 1);
    assert_eq!(receipt.artifacts_relabeled, 0);
    assert_eq!(receipt.external_copies, 1);

    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    assert_eq!(
        count(
            executor,
            "SELECT COUNT(*) FROM artifact WHERE artifact_id = ?1",
            &[Value::text(artifact_id.to_text())]
        )
        .unwrap(),
        0
    );
    assert_eq!(
        count(
            executor,
            "SELECT COUNT(*) FROM artifact_lineage WHERE artifact_id = ?1",
            &[Value::text(artifact_id.to_text())]
        )
        .unwrap(),
        0
    );
    assert!(search::search(executor, "emissions", 10)
        .unwrap()
        .is_empty());
    transaction.rollback().unwrap();
}
