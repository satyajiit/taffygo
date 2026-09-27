// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_runtime::adapters::workspace::{ProductionWorkspaces, WorkspaceStore};
use core_runtime::{WorkspacePageFact, WorkspacePort, WorkspaceStoreError};
use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
use taffy_storage::workspace::{
    encode_snapshot, FactKind, WorkspaceFact, WorkspacePhase, WorkspaceSnapshot, WorkspaceSource,
    WorkspaceTemplate,
};

fn snapshot(workspace_byte: u8, revision: u64) -> WorkspaceSnapshot {
    let source_id = SourceId::from_bytes([2; 16]);
    WorkspaceSnapshot {
        workspace_id: WorkspaceId::from_bytes([workspace_byte; 16]),
        revision,
        display_name: "Evidence comparison".to_owned(),
        goal: "Compare retained evidence".to_owned(),
        phase: WorkspacePhase::Running,
        saved: true,
        last_updated_epoch_ms: 1_787_337_000_000,
        template: WorkspaceTemplate::CompareProducts,
        sources: vec![WorkspaceSource {
            source_id,
            title: "Independent evidence".to_owned(),
            host: "evidence.example".to_owned(),
            canonical_locator: None,
            read_at_epoch_ms: 1_787_336_900_000,
            excluded: false,
        }],
        facts: (revision > 1)
            .then(|| WorkspaceFact {
                fact_id: FactId::from_bytes([3; 16]),
                field: "warranty".to_owned(),
                value: "two years".to_owned(),
                kind: FactKind::FromPage,
                sources: vec![source_id],
                correction: None,
                has_conflict: false,
                media_provenance: None,
            })
            .into_iter()
            .collect(),
    }
}

fn restored_store(value: &WorkspaceSnapshot) -> WorkspaceStore {
    let mut store = WorkspaceStore::new();
    assert_eq!(
        store.restore_encoded(&[encode_snapshot(value).unwrap_or_default()]),
        Ok(())
    );
    store
}

#[test]
fn retained_temporary_snapshot_is_visible_only_after_exact_storage_ack() {
    let mut value = snapshot(11, 1);
    value.saved = false;
    let id = value.workspace_id.to_text();
    let source_id = value.sources.first().map_or_else(
        || unreachable!("fixture source"),
        |source| source.source_id.to_text(),
    );
    let mut port: Box<dyn WorkspacePort> = Box::new(ProductionWorkspaces::new());
    let creation = port
        .begin_creation("temporary-create".to_owned(), value)
        .unwrap_or_else(|_| unreachable!("temporary creation stages"));
    assert!(port.retained_snapshot(&id).is_none());
    assert_eq!(
        port.complete_persist(&creation.operation_id, creation.resulting_revision),
        Ok(())
    );
    assert!(port.list_workspaces().is_empty());
    assert_eq!(
        port.reopen_workspace(&id, 1),
        Err(WorkspaceStoreError::UnknownWorkspace)
    );
    assert_eq!(
        port.retained_snapshot(&id)
            .map(|snapshot| snapshot.facts.len()),
        Some(0)
    );
    let pending = port
        .begin_page_ingestion(
            "temporary-page".to_owned(),
            &id,
            &source_id,
            vec![WorkspacePageFact {
                fact_id: FactId::from_bytes([12; 16]).to_text(),
                field: "page".to_owned(),
                value: "A real retained page result".to_owned(),
                media_provenance: None,
            }],
            2_000,
        )
        .unwrap_or_else(|_| unreachable!("page write stages"))
        .unwrap_or_else(|| unreachable!("nonempty page writes"));
    assert_eq!(
        port.retained_snapshot(&id)
            .map(|snapshot| snapshot.facts.len()),
        Some(0)
    );
    assert!(port
        .complete_persist(&pending.operation_id, pending.resulting_revision + 1)
        .is_err());
    assert_eq!(
        port.retained_snapshot(&id)
            .map(|snapshot| snapshot.facts.len()),
        Some(0)
    );
    assert_eq!(
        port.complete_persist(&pending.operation_id, pending.resulting_revision),
        Ok(())
    );
    assert_eq!(
        port.retained_snapshot(&id)
            .map(|snapshot| snapshot.facts.len()),
        Some(1)
    );
}

#[test]
fn creation_is_staged_validated_and_published_only_after_commit() {
    let value = snapshot(7, 1);
    let mut store = WorkspaceStore::new();
    let request = store
        .begin_creation("create-1".to_owned(), value.clone())
        .unwrap_or_else(|_| unreachable!("revision-one snapshot is valid"));
    assert_eq!(
        (request.expected_revision, request.resulting_revision),
        (0, 1)
    );
    assert_eq!(store.project_core_api(), Ok(Vec::new()));
    assert!(store.reject_persist("create-1"));

    let replay = store
        .begin_creation("create-2".to_owned(), value.clone())
        .unwrap_or_else(|_| unreachable!("replay stages identical bytes"));
    assert_eq!(request.snapshot, replay.snapshot);
    assert_eq!(store.complete_persist("create-2", 1), Ok(()));
    assert_eq!(store.project_core_api().map(|items| items.len()), Ok(1));
    assert_eq!(
        store.begin_creation("create-3".to_owned(), value),
        Err(WorkspaceStoreError::DuplicateWorkspace)
    );
    assert_eq!(
        WorkspaceStore::new().begin_creation("invalid".to_owned(), snapshot(8, 2)),
        Err(WorkspaceStoreError::InvalidSnapshot)
    );
}

#[test]
fn page_ingestion_is_exact_source_atomic_replay_stable_and_restartable() {
    let value = snapshot(1, 3);
    let workspace_id = value.workspace_id.to_text();
    let source_id = value.sources.first().map_or_else(
        || unreachable!("fixture has one source"),
        |source| source.source_id.to_text(),
    );
    let facts = vec![WorkspacePageFact {
        fact_id: FactId::from_bytes([9; 16]).to_text(),
        field: "page".to_owned(),
        value: "already redacted page evidence".to_owned(),
        media_provenance: None,
    }];
    let mut store = restored_store(&value);
    assert_eq!(
        store.begin_page_ingestion(
            "empty".to_owned(),
            &workspace_id,
            &source_id,
            Vec::new(),
            1_787_337_100_000,
        ),
        Ok(None)
    );
    let staged = store
        .begin_page_ingestion(
            "page-1".to_owned(),
            &workspace_id,
            &source_id,
            facts.clone(),
            1_787_337_100_000,
        )
        .unwrap_or_else(|_| unreachable!("page fact is valid"))
        .unwrap_or_else(|| unreachable!("nonempty fact stages a write"));
    assert_eq!(
        (staged.expected_revision, staged.resulting_revision),
        (3, 4)
    );
    assert_eq!(
        store
            .project_core_api()
            .ok()
            .and_then(|items| items.into_iter().next())
            .and_then(|item| item.facts.into_iter().next())
            .map(|fact| fact.value),
        Some("two years".to_owned())
    );
    assert_eq!(
        store.begin_page_ingestion(
            "page-concurrent".to_owned(),
            &workspace_id,
            &source_id,
            facts.clone(),
            1_787_337_100_000,
        ),
        Err(WorkspaceStoreError::OperationAlreadyPending)
    );
    assert!(store.reject_persist("page-1"));
    let replay = store
        .begin_page_ingestion(
            "page-2".to_owned(),
            &workspace_id,
            &source_id,
            facts,
            1_787_337_100_000,
        )
        .unwrap_or_else(|_| unreachable!("replay is valid"))
        .unwrap_or_else(|| unreachable!("replay stages a write"));
    assert_eq!(staged.snapshot, replay.snapshot);
    assert_eq!(store.complete_persist("page-2", 4), Ok(()));

    let mut restarted = WorkspaceStore::new();
    assert_eq!(
        restarted.restore_bound_records(&[(workspace_id, 4, replay.snapshot)]),
        Ok(())
    );
    let fact = restarted
        .project_core_api()
        .ok()
        .and_then(|items| items.into_iter().next())
        .and_then(|item| {
            if item.facts.len() == 1 {
                item.facts.into_iter().next()
            } else {
                None
            }
        })
        .unwrap_or_else(|| unreachable!("restart restores one fact"));
    assert_eq!(fact.value, "already redacted page evidence");
    assert_eq!(fact.sources, vec![source_id]);
}

#[test]
fn excluded_source_refuses_page_fact_persistence_without_staging() {
    let mut value = snapshot(1, 3);
    let workspace_id = value.workspace_id.to_text();
    let Some(source) = value.sources.first_mut() else {
        unreachable!("fixture has one source")
    };
    source.excluded = true;
    let source_id = source.source_id.to_text();
    let mut store = restored_store(&value);
    assert_eq!(
        store.begin_page_ingestion(
            "excluded".to_owned(),
            &workspace_id,
            &source_id,
            vec![WorkspacePageFact {
                fact_id: FactId::from_bytes([9; 16]).to_text(),
                field: "page".to_owned(),
                value: "must not persist".to_owned(),
                media_provenance: None,
            }],
            1_787_337_100_000,
        ),
        Ok(None)
    );
    assert_eq!(
        store
            .project_core_api()
            .ok()
            .and_then(|items| items.first().map(|item| item.revision)),
        Some(3)
    );
}
