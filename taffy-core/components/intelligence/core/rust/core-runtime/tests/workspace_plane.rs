// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_api_types::WorkspaceExportFormat;
use core_runtime::adapters::workspace::WorkspaceStore;
use core_runtime::{WorkspaceExportError, WorkspacePageFact, WorkspaceStoreError};
use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
use taffy_storage::workspace::{
    encode_snapshot, FactKind, WorkspaceFact, WorkspacePhase, WorkspaceSnapshot, WorkspaceSource,
    WorkspaceTemplate,
};

fn snapshot(workspace_byte: u8) -> WorkspaceSnapshot {
    let source_id = SourceId::from_bytes([2; 16]);
    WorkspaceSnapshot {
        workspace_id: WorkspaceId::from_bytes([workspace_byte; 16]),
        revision: 3,
        display_name: "Evidence comparison".to_owned(),
        goal: "Compare retained evidence".to_owned(),
        phase: WorkspacePhase::PartlyDone,
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
        facts: vec![WorkspaceFact {
            fact_id: FactId::from_bytes([3; 16]),
            field: "warranty".to_owned(),
            value: "two years".to_owned(),
            kind: FactKind::FromPage,
            sources: vec![source_id],
            correction: None,
            has_conflict: false,
            media_provenance: None,
        }],
    }
}

fn restored_store(value: &WorkspaceSnapshot) -> WorkspaceStore {
    let encoded = encode_snapshot(value).unwrap_or_default();
    let mut store = WorkspaceStore::new();
    assert_eq!(store.restore_encoded(&[encoded]), Ok(()));
    store
}

#[test]
fn empty_restore_is_truthful_and_profiles_do_not_share_state() {
    let empty = WorkspaceStore::new();
    assert_eq!(empty.project_core_api(), Ok(Vec::new()));

    let first = restored_store(&snapshot(1));
    let second = restored_store(&snapshot(4));
    let first_id = first
        .project_core_api()
        .ok()
        .and_then(|items| items.first().map(|item| item.workspace_id.clone()));
    let second_id = second
        .project_core_api()
        .ok()
        .and_then(|items| items.first().map(|item| item.workspace_id.clone()));
    assert_ne!(first_id, second_id);
}

#[test]
fn browser_restore_metadata_must_match_the_canonical_snapshot() {
    let value = snapshot(1);
    let encoded = encode_snapshot(&value).unwrap_or_default();
    let mut store = WorkspaceStore::new();
    assert_eq!(
        store.restore_bound_records(&[("wrong-workspace".to_owned(), 3, encoded.clone())]),
        Err(WorkspaceStoreError::RestoreBindingMismatch)
    );
    assert_eq!(
        store.restore_bound_records(&[(value.workspace_id.to_text(), 4, encoded.clone())]),
        Err(WorkspaceStoreError::RestoreBindingMismatch)
    );
    assert_eq!(
        store.restore_bound_records(&[(value.workspace_id.to_text(), 3, encoded)]),
        Ok(())
    );
}

#[test]
fn mutation_becomes_visible_only_after_exact_browser_commit() {
    let value = snapshot(1);
    let workspace_id = value.workspace_id.to_text();
    let fact_id = value
        .facts
        .first()
        .map(|fact| fact.fact_id.to_text())
        .unwrap_or_default();
    let mut store = restored_store(&value);
    let request = store.begin_correction(
        "operation-1".to_owned(),
        &workspace_id,
        3,
        &fact_id,
        "three years".to_owned(),
        1_787_337_100_000,
    );
    assert!(request.is_ok());
    assert_eq!(
        store
            .project_core_api()
            .ok()
            .and_then(|items| items.first().cloned())
            .and_then(|item| item.facts.first().cloned())
            .and_then(|fact| fact.correction),
        None
    );
    let resulting_revision = request.as_ref().map_or(0, |item| item.resulting_revision);
    assert_eq!(
        store.complete_persist("operation-1", resulting_revision + 1),
        Err(WorkspaceStoreError::WrongCompletion)
    );
    assert_eq!(
        store.complete_persist("operation-1", resulting_revision),
        Ok(())
    );
    assert_eq!(
        store
            .project_core_api()
            .ok()
            .and_then(|items| items.first().cloned())
            .and_then(|item| item.facts.first().cloned())
            .and_then(|fact| fact.correction),
        Some("three years".to_owned())
    );
    assert_eq!(
        store.begin_correction(
            "operation-2".to_owned(),
            &workspace_id,
            3,
            &fact_id,
            "four years".to_owned(),
            1_787_337_200_000,
        ),
        Err(WorkspaceStoreError::Mutation(
            taffy_storage::workspace::MutationError::StaleRevision
        ))
    );
}

#[test]
fn task_phase_and_page_capture_stage_as_one_revision_and_serialize_with_edits() {
    let mut value = snapshot(1);
    value.phase = WorkspacePhase::Running;
    let workspace_id = value.workspace_id.to_text();
    let source_id = value
        .sources
        .first()
        .unwrap_or_else(|| unreachable!("fixture carries one source"))
        .source_id
        .to_text();
    let mut store = restored_store(&value);

    assert_eq!(store.preflight_task_update(&workspace_id), Ok(()));
    let pending = store
        .begin_task_update(
            "task-operation".to_owned(),
            &workspace_id,
            WorkspacePhase::Done,
            Some((
                &source_id,
                vec![WorkspacePageFact {
                    fact_id: FactId::from_bytes([8; 16]).to_text(),
                    field: "page".to_owned(),
                    value: "accepted task fact".to_owned(),
                    media_provenance: None,
                }],
            )),
            None,
            1_787_337_200_000,
        )
        .unwrap_or_else(|_| unreachable!("the task update is valid"))
        .unwrap_or_else(|| unreachable!("the phase and page both changed"));
    assert_eq!(pending.expected_revision, 3);
    assert_eq!(pending.resulting_revision, 4);
    assert_eq!(
        store.preflight_task_update(&workspace_id),
        Err(WorkspaceStoreError::OperationAlreadyPending)
    );
    assert_eq!(
        store
            .project_core_api()
            .unwrap_or_default()
            .first()
            .map(|workspace| workspace.phase),
        Some(core_api_types::WorkspacePhase::Running)
    );
    assert_eq!(
        store.complete_persist("task-operation", pending.resulting_revision),
        Ok(())
    );
    let projected = store.project_core_api().unwrap_or_default();
    let workspace = projected
        .first()
        .unwrap_or_else(|| unreachable!("committed workspace is published"));
    assert_eq!(workspace.phase, core_api_types::WorkspacePhase::Done);
    assert_eq!(workspace.revision, 4);
    assert_eq!(workspace.facts.len(), 1);
    assert_eq!(
        workspace.facts.first().map(|fact| fact.value.as_str()),
        Some("accepted task fact")
    );
    assert_eq!(
        store.begin_task_update(
            "task-noop".to_owned(),
            &workspace_id,
            WorkspacePhase::Done,
            None,
            None,
            1_787_337_300_000,
        ),
        Ok(None)
    );
}

#[test]
fn markdown_and_csv_are_stable_rust_artifacts() {
    let value = snapshot(1);
    let workspace_id = value.workspace_id.to_text();
    let mut store = restored_store(&value);
    let markdown = store
        .request_export(
            "export-1",
            &workspace_id,
            3,
            WorkspaceExportFormat::Markdown,
        )
        .map(|view| view.content)
        .unwrap_or_default();
    let again = store
        .request_export(
            "export-1",
            &workspace_id,
            3,
            WorkspaceExportFormat::Markdown,
        )
        .map(|view| view.content)
        .unwrap_or_default();
    let csv = store
        .request_export("export-2", &workspace_id, 3, WorkspaceExportFormat::Csv)
        .map(|view| view.content)
        .unwrap_or_default();
    let replayed = restored_store(&value)
        .request_export(
            "export-1",
            &workspace_id,
            3,
            WorkspaceExportFormat::Markdown,
        )
        .map(|view| view.content)
        .unwrap_or_default();
    assert_eq!(markdown, again);
    assert_eq!(markdown, replayed);
    assert!(markdown.contains("evidence.example"));
    assert!(!markdown.contains('?'));
    assert!(csv.starts_with("subject,detail,value"));
}

#[test]
fn preparing_does_not_publish_and_only_an_explicit_current_request_exports() {
    let value = snapshot(1);
    let workspace_id = value.workspace_id.to_text();
    let fact_id = value
        .facts
        .first()
        .map(|fact| fact.fact_id.to_text())
        .unwrap_or_default();
    let mut store = restored_store(&value);

    assert_eq!(
        store.prepare_artifact("ready-1", &workspace_id, WorkspaceExportFormat::Markdown),
        Ok(3)
    );
    assert_eq!(store.latest_export(), None);

    let pending = store
        .begin_correction(
            "operation-prepare-invalidation".to_owned(),
            &workspace_id,
            3,
            &fact_id,
            "three years".to_owned(),
            1_787_337_100_000,
        )
        .unwrap_or_else(|_| unreachable!("the correction is valid"));
    assert_eq!(
        store.complete_persist("operation-prepare-invalidation", pending.resulting_revision,),
        Ok(())
    );
    assert_eq!(store.latest_export(), None);
    assert_eq!(
        store.publish_artifact(
            "export-stale",
            "ready-1",
            &workspace_id,
            3,
            WorkspaceExportFormat::Markdown,
        ),
        Err(WorkspaceExportError::StaleRevision)
    );
    assert_eq!(store.latest_export(), None);

    assert_eq!(
        store.prepare_artifact(
            "ready-current",
            &workspace_id,
            WorkspaceExportFormat::Markdown,
        ),
        Ok(4)
    );
    assert_eq!(store.latest_export(), None);
    let current = store
        .publish_artifact(
            "export-current",
            "ready-current",
            &workspace_id,
            4,
            WorkspaceExportFormat::Markdown,
        )
        .unwrap_or_else(|_| unreachable!("the current accepted facts render"));
    assert_eq!(current.request_id, "export-current");
    assert_eq!(current.revision, 4);
    assert!(current.content.contains("three years"));
    assert_eq!(store.latest_export(), Some(current));
}

#[test]
fn an_excluded_only_source_does_not_leave_an_uncited_export() {
    let mut value = snapshot(1);
    if let Some(source) = value.sources.first_mut() {
        source.excluded = true;
    }
    let workspace_id = value.workspace_id.to_text();
    let mut store = restored_store(&value);
    assert_eq!(
        store.prepare_export(
            "ready-uncited",
            &workspace_id,
            3,
            WorkspaceExportFormat::Csv,
        ),
        Err(WorkspaceExportError::NoExportableFacts)
    );
    assert_eq!(store.latest_export(), None);
}

#[test]
fn an_oversized_workspace_artifact_is_refused_before_publication() {
    let mut value = snapshot(1);
    let source_id = value
        .sources
        .first()
        .map_or_else(|| SourceId::from_bytes([2; 16]), |source| source.source_id);
    value.facts = (0_u8..8)
        .map(|offset| WorkspaceFact {
            fact_id: FactId::from_bytes([offset.saturating_add(3); 16]),
            field: format!("field-{offset}"),
            value: "x".repeat(16_384),
            kind: FactKind::FromPage,
            sources: vec![source_id],
            correction: None,
            has_conflict: false,
            media_provenance: None,
        })
        .collect();
    let workspace_id = value.workspace_id.to_text();
    let mut store = restored_store(&value);
    assert_eq!(
        store.prepare_export(
            "ready-too-large",
            &workspace_id,
            3,
            WorkspaceExportFormat::Markdown,
        ),
        Err(WorkspaceExportError::RenderRefused)
    );
    assert_eq!(store.latest_export(), None);
}

#[path = "workspace_plane/discovery.rs"]
mod discovery;
