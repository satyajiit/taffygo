// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::support::*;
use crate::backend::Executor;
use crate::workspace_lifecycle::{
    list_workspaces, preview_workspace_deletion, rename_workspace, reopen_workspace,
    run_workspace_deletion, WorkspaceConfirmationDigest, WorkspaceDeleteRequest,
};

struct TestDigest;

impl WorkspaceConfirmationDigest for TestDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], StorageError> {
        let mut output = [0_u8; 32];
        let width = output.len();
        for (block, chunk) in input.chunks(width).enumerate() {
            for (offset, (slot, byte)) in output.iter_mut().zip(chunk).enumerate() {
                let index = block.saturating_mul(width).saturating_add(offset);
                *slot = (*slot)
                    .wrapping_mul(33)
                    .wrapping_add(*byte)
                    .wrapping_add(u8::try_from(index % 251).unwrap_or(0));
            }
        }
        Ok(output)
    }
}

fn seed_lifecycle_dependents(database: &mut SqliteDatabase) {
    let mut transaction = database.begin().unwrap();
    let executor = transaction.as_mut();
    seed_task_records(executor);
    seed_artifact_records(executor);
    transaction.commit().unwrap();
}

fn seed_task_records(executor: &mut dyn Executor) {
    executor
        .execute(
            "INSERT INTO task (task_id,workspace_id,browser_profile_id,kind,state,revision,user_goal,\
             created_by,assistant_snapshot,control_mode,source_scope,data_policy_snapshot,\
             provider_route_snapshot,budgets,created_at_utc,updated_at_utc,retention_class,\
             schema_version) VALUES ('task-lifecycle',?1,'profile-default','RESEARCH','COMPLETED',1,\
             'immutable user goal','USER','{}','ASSISTED','{}','{}','{}','{}',?2,?2,\
             'TASK_SESSION',1)",
            &[
                Value::text(workspace_id().to_text()),
                Value::text("2026-08-17T09:00:00Z"),
            ],
        )
        .unwrap();
    let mut recorded = event(EventId::from_bytes([0x81; 16]), 1);
    recorded.task_id = Some("task-lifecycle".to_owned());
    recorded.aggregate_id = "task-lifecycle".to_owned();
    journal::append(executor, &recorded).unwrap();
    journal::project_source_usage(
        executor,
        recorded.event_id,
        source_id(),
        Some(workspace_id()),
        &at("2026-08-17T09:05:00Z"),
        "read retained evidence",
    )
    .unwrap();
    executor
        .execute(
            "INSERT INTO task_state_projection \
             (task_id,state,revision,updated_at_utc,last_event_id) VALUES \
             ('task-lifecycle','COMPLETED',1,?1,?2)",
            &[
                Value::text("2026-08-17T09:05:00Z"),
                Value::text(recorded.event_id.to_text()),
            ],
        )
        .unwrap();
    executor
        .execute(
            "INSERT INTO model_invocation (model_invocation_id,task_id,purpose,\
             provider_route_snapshot,model_identifier,context_manifest,redaction_policy_version,\
             request_digest,started_at_utc,result_code,retention_class,schema_version) VALUES \
             ('invocation-lifecycle','task-lifecycle','SYNTHESIS','{}','model','{}','v1','digest',\
              ?1,'OK','TASK_SESSION',1)",
            &[Value::text("2026-08-17T09:06:00Z")],
        )
        .unwrap();
}

fn seed_artifact_records(executor: &mut dyn Executor) {
    let artifact_id = ArtifactId::from_bytes([0xb1; 16]);
    executor
        .execute(
            "INSERT INTO artifact (artifact_id,task_id,workspace_id,kind,state,lineage_state,\
             generation_method,content_digest,created_at_utc,exported_copies,retention_class,\
             schema_version) VALUES (?1,'task-lifecycle',?2,'MARKDOWN','ACCEPTED','COMPLETE',\
             'SYNTHESIS','artifact-digest',?3,2,'WORKSPACE_MANAGED',1)",
            &[
                Value::text(artifact_id.to_text()),
                Value::text(workspace_id().to_text()),
                Value::text("2026-08-17T09:10:00Z"),
            ],
        )
        .unwrap();
    executor
        .execute(
            "INSERT INTO artifact_lineage (artifact_id,related_type,related_id) \
             VALUES (?1,'FACT',?2)",
            &[
                Value::text(artifact_id.to_text()),
                Value::text(fact_id().to_text()),
            ],
        )
        .unwrap();
    for (document, record, source) in [
        (
            DocumentId::from_bytes([0xa1; 16]),
            IndexedRecord::Observation(observation_of(source_id())),
            Some(source_id()),
        ),
        (
            DocumentId::from_bytes([0xa2; 16]),
            IndexedRecord::Fact(fact_id()),
            None,
        ),
        (
            DocumentId::from_bytes([0xa3; 16]),
            IndexedRecord::Artifact(artifact_id),
            None,
        ),
    ] {
        search::index(
            executor,
            &SearchDocument {
                document_id: document,
                record,
                workspace_id: Some(workspace_id()),
                source_id: source,
                retention_class: "WORKSPACE_MANAGED".to_owned(),
                title: "indexed title".to_owned(),
                body: "indexed workspace content".to_owned(),
            },
            &at("2026-08-17T09:11:00Z"),
        )
        .unwrap();
    }
}

fn seeded() -> SqliteDatabase {
    let mut database = migrated();
    seed_workspace(&mut database);
    seed_lifecycle_dependents(&mut database);
    database
}

#[test]
fn list_reopen_and_rename_keep_the_goal_outside_the_display_name() {
    let mut database = seeded();
    let mut transaction = database.begin().unwrap();
    let listed = list_workspaces(transaction.as_mut(), "profile-default").unwrap();
    assert_eq!(listed.len(), 1);
    assert_eq!(listed[0].title, "Emissions review");
    assert_eq!(
        reopen_workspace(transaction.as_mut(), workspace_id(), 1),
        Ok(listed[0].clone())
    );
    transaction.rollback().unwrap();

    assert_eq!(
        rename_workspace(
            &mut database,
            workspace_id(),
            1,
            "Renamed evidence",
            &at("2026-08-17T09:12:00Z"),
        ),
        Ok(2)
    );
    let mut transaction = database.begin().unwrap();
    let reopened = reopen_workspace(transaction.as_mut(), workspace_id(), 2).unwrap();
    assert_eq!(reopened.title, "Renamed evidence");
    assert_eq!(
        count(
            transaction.as_mut(),
            "SELECT COUNT(*) FROM task WHERE workspace_id=?1 AND user_goal='immutable user goal'",
            &[Value::text(workspace_id().to_text())],
        )
        .unwrap(),
        1
    );
    transaction.rollback().unwrap();
}

#[test]
fn confirmed_delete_is_exact_atomic_verified_and_replay_identical() {
    let mut database = seeded();
    let preview = {
        let mut transaction = database.begin().unwrap();
        let value =
            preview_workspace_deletion(transaction.as_mut(), workspace_id(), &TestDigest).unwrap();
        transaction.rollback().unwrap();
        value
    };
    assert_eq!(preview.counts.sources, 2);
    assert_eq!(preview.counts.facts, 2);
    assert_eq!(preview.counts.artifact_metadata, 1);
    assert_eq!(preview.counts.derived_indexes, 3);
    assert_eq!(preview.counts.external_copies, 2);
    assert_eq!(preview.confirmation_token.len(), 64);
    assert!(!preview.confirmation_token.contains("workspace content"));
    let request = WorkspaceDeleteRequest {
        operation_id: "delete-workspace-1".to_owned(),
        preview,
    };
    let first = run_workspace_deletion(&mut database, &request, &TestDigest, &clock()).unwrap();
    let replay = run_workspace_deletion(&mut database, &request, &TestDigest, &clock()).unwrap();
    assert_eq!(replay, first);
    assert!(first.verified);

    let mut transaction = database.begin().unwrap();
    for table in [
        "workspace",
        "workspace_source",
        "fact",
        "task",
        "artifact",
        "artifact_lineage",
        "model_invocation",
        "search_document",
        "search_index",
    ] {
        assert_eq!(
            count(
                transaction.as_mut(),
                &format!("SELECT COUNT(*) FROM {table}"),
                &[]
            )
            .unwrap(),
            0,
            "{table} retained workspace-owned rows"
        );
    }
    assert_eq!(
        count(
            transaction.as_mut(),
            "SELECT COUNT(*) FROM workspace_deletion_receipt WHERE verified=1",
            &[],
        )
        .unwrap(),
        1
    );
    transaction.rollback().unwrap();
}

#[test]
fn stale_or_mutated_confirmation_is_refused_and_faults_roll_back() {
    let mut database = seeded();
    let preview = {
        let mut transaction = database.begin().unwrap();
        let value =
            preview_workspace_deletion(transaction.as_mut(), workspace_id(), &TestDigest).unwrap();
        transaction.rollback().unwrap();
        value
    };
    database
        .raw_execute("UPDATE artifact SET state='STALE' WHERE artifact_id IS NOT NULL")
        .unwrap();
    let request = WorkspaceDeleteRequest {
        operation_id: "delete-mutated".to_owned(),
        preview,
    };
    assert_eq!(
        run_workspace_deletion(&mut database, &request, &TestDigest, &clock()),
        Err(StorageError::InvalidWorkspaceConfirmation)
    );

    database
        .raw_execute(
            "CREATE TRIGGER preserve_artifact BEFORE DELETE ON artifact BEGIN \
             SELECT RAISE(IGNORE); END",
        )
        .unwrap();
    let current = {
        let mut transaction = database.begin().unwrap();
        let value =
            preview_workspace_deletion(transaction.as_mut(), workspace_id(), &TestDigest).unwrap();
        transaction.rollback().unwrap();
        value
    };
    let faulted = WorkspaceDeleteRequest {
        operation_id: "delete-faulted".to_owned(),
        preview: current,
    };
    let result = run_workspace_deletion(&mut database, &faulted, &TestDigest, &clock());
    assert!(result.is_err(), "fault injection unexpectedly committed");
    database
        .raw_execute("DROP TRIGGER preserve_artifact")
        .unwrap();
    let mut transaction = database.begin().unwrap();
    assert!(
        records::load_workspace(transaction.as_mut(), workspace_id())
            .unwrap()
            .is_some()
    );
    assert_eq!(
        count(
            transaction.as_mut(),
            "SELECT COUNT(*) FROM workspace_deletion_receipt",
            &[],
        )
        .unwrap(),
        0
    );
    transaction.rollback().unwrap();
}

#[test]
fn deleting_one_workspace_preserves_a_source_shared_with_another() {
    let mut database = seeded();
    let other_workspace_id = WorkspaceId::from_bytes([0x12; 16]);
    let mut other_workspace = sample_workspace();
    other_workspace.workspace_id = other_workspace_id;
    other_workspace.title = "Other evidence".to_owned();
    {
        let mut transaction = database.begin().unwrap();
        records::insert_workspace(transaction.as_mut(), &other_workspace).unwrap();
        records::attach_source(
            transaction.as_mut(),
            other_workspace_id,
            source_id(),
            MembershipState::Included,
            "USER",
            &at("2026-08-17T09:12:00Z"),
        )
        .unwrap();
        transaction.commit().unwrap();
    }
    let preview = {
        let mut transaction = database.begin().unwrap();
        let value =
            preview_workspace_deletion(transaction.as_mut(), workspace_id(), &TestDigest).unwrap();
        transaction.rollback().unwrap();
        value
    };
    run_workspace_deletion(
        &mut database,
        &WorkspaceDeleteRequest {
            operation_id: "delete-shared-source-workspace".to_owned(),
            preview,
        },
        &TestDigest,
        &clock(),
    )
    .unwrap();

    let mut transaction = database.begin().unwrap();
    assert_eq!(
        count(
            transaction.as_mut(),
            "SELECT COUNT(*) FROM source WHERE source_id=?1",
            &[Value::text(source_id().to_text())],
        )
        .unwrap(),
        1
    );
    assert_eq!(
        count(
            transaction.as_mut(),
            "SELECT COUNT(*) FROM workspace_source WHERE workspace_id=?1 AND source_id=?2",
            &[
                Value::text(other_workspace_id.to_text()),
                Value::text(source_id().to_text()),
            ],
        )
        .unwrap(),
        1
    );
    assert_eq!(
        count(
            transaction.as_mut(),
            "SELECT COUNT(*) FROM observation WHERE source_id=?1",
            &[Value::text(source_id().to_text())],
        )
        .unwrap(),
        1
    );
    transaction.rollback().unwrap();
}
