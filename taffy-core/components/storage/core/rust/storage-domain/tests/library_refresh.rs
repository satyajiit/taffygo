// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::ids::{FactId, LibraryEntryId, SourceId, WorkspaceId};
use taffy_storage::library::{
    entry_from_workspace, LibraryError, LibraryRefreshDigest, LibraryRefreshDisposition,
    LibraryStore,
};
use taffy_storage::workspace::{
    FactKind, WorkspaceFact, WorkspacePhase, WorkspaceSnapshot, WorkspaceSource, WorkspaceTemplate,
};

struct Digest;

impl LibraryRefreshDigest for Digest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], LibraryError> {
        let mut out = [0_u8; 32];
        for (index, byte) in input.iter().enumerate() {
            let slot = index % out.len();
            if let Some(value) = out.get_mut(slot) {
                *value = value
                    .wrapping_mul(31)
                    .wrapping_add(*byte)
                    .wrapping_add(u8::try_from(index % 251).unwrap_or_default());
            }
        }
        Ok(out)
    }
}

fn source(byte: u8, read_at_epoch_ms: u64) -> WorkspaceSource {
    WorkspaceSource {
        source_id: SourceId::from_bytes([byte; 16]),
        title: format!("Page {byte}"),
        host: format!("source-{byte}.example"),
        canonical_locator: Some(format!("https://source-{byte}.example/article")),
        read_at_epoch_ms,
        excluded: false,
    }
}

fn page_fact(byte: u8, source: SourceId, value: &str) -> WorkspaceFact {
    WorkspaceFact {
        fact_id: FactId::from_bytes([byte; 16]),
        field: "page".to_owned(),
        value: value.to_owned(),
        kind: FactKind::FromPage,
        sources: vec![source],
        correction: None,
        has_conflict: false,
        media_provenance: None,
    }
}

fn original_workspace() -> WorkspaceSnapshot {
    let first = source(2, 20);
    let second = source(3, 30);
    let third = source(4, 40);
    let first_id = first.source_id;
    let second_id = second.source_id;
    let third_id = third.source_id;
    let sources = vec![first, second, third];
    WorkspaceSnapshot {
        workspace_id: WorkspaceId::from_bytes([1; 16]),
        revision: 7,
        display_name: "Saved evidence".to_owned(),
        goal: "Keep exact page evidence".to_owned(),
        phase: WorkspacePhase::Done,
        saved: true,
        last_updated_epoch_ms: 40,
        template: WorkspaceTemplate::SummarizeEvidence,
        facts: vec![
            page_fact(5, first_id, "same"),
            page_fact(6, second_id, "old"),
            page_fact(7, third_id, "gone"),
            WorkspaceFact {
                fact_id: FactId::from_bytes([8; 16]),
                field: "summary".to_owned(),
                value: "kept original".to_owned(),
                kind: FactKind::Summarized,
                sources: sources.iter().map(|source| source.source_id).collect(),
                correction: None,
                has_conflict: false,
                media_provenance: None,
            },
        ],
        sources,
    }
}

fn store(workspace: &WorkspaceSnapshot) -> LibraryStore {
    let entry = entry_from_workspace(
        LibraryEntryId::from_bytes([9; 16]),
        1,
        workspace,
        workspace.revision,
        FactId::from_bytes([8; 16]),
        50,
    )
    .unwrap_or_else(|error| unreachable!("valid saved entry: {error:?}"));
    let mut store = LibraryStore::new();
    store
        .restore(1, vec![entry])
        .unwrap_or_else(|error| unreachable!("valid Library: {error:?}"));
    store
}

#[test]
fn preview_is_exact_content_free_work_before_any_refresh() {
    let workspace = original_workspace();
    let preview = store(&workspace)
        .preview_collection_refresh(workspace.workspace_id, 1, &workspace, 7, &Digest)
        .unwrap_or_else(|error| unreachable!("refreshable collection: {error:?}"));
    assert_eq!(preview.preview_id.len(), 64);
    assert_eq!(preview.sources.len(), 3);
    assert_eq!(preview.estimated_navigation_count(), 3);
    assert_eq!(preview.estimated_observation_count(), 3);
    assert_eq!(preview.estimated_work_units(), 6);
    assert_eq!(preview.collection_name, "Saved evidence");
}

#[test]
fn comparison_preserves_original_and_distinguishes_all_three_outcomes() {
    let original = original_workspace();
    let store = store(&original);
    let preview = store
        .preview_collection_refresh(original.workspace_id, 1, &original, 7, &Digest)
        .unwrap_or_else(|error| unreachable!("refreshable collection: {error:?}"));
    let mut refreshed = original.clone();
    refreshed.workspace_id = WorkspaceId::from_bytes([10; 16]);
    refreshed.revision = 4;
    refreshed.saved = false;
    for (source, read_at_epoch_ms) in refreshed.sources.iter_mut().zip([100, 100, 0]) {
        source.read_at_epoch_ms = read_at_epoch_ms;
    }
    let mut refreshed_source_ids = refreshed.sources.iter().map(|source| source.source_id);
    let first = refreshed_source_ids
        .next()
        .unwrap_or_else(|| unreachable!("fixture has three sources"));
    let second = refreshed_source_ids
        .next()
        .unwrap_or_else(|| unreachable!("fixture has three sources"));
    refreshed.facts = vec![page_fact(11, first, "same"), page_fact(12, second, "new")];
    let result = store
        .compare_collection_refresh(&preview, &original, &refreshed, &Digest)
        .unwrap_or_else(|error| unreachable!("valid terminal comparison: {error:?}"));
    assert_eq!(
        result
            .sources
            .iter()
            .map(|source| source.disposition)
            .collect::<Vec<_>>(),
        vec![
            LibraryRefreshDisposition::Unchanged,
            LibraryRefreshDisposition::Changed,
            LibraryRefreshDisposition::Missing,
        ]
    );
    assert!(original
        .facts
        .iter()
        .any(|fact| fact.kind == FactKind::Summarized && fact.value == "kept original"));
    assert_ne!(result.refreshed_workspace_id, result.collection_id);
}

#[test]
fn deletion_revision_drift_and_unsafe_locator_withdraw_refresh() {
    let mut workspace = original_workspace();
    let populated = store(&workspace);
    let preview = populated
        .preview_collection_refresh(workspace.workspace_id, 1, &workspace, 7, &Digest)
        .unwrap_or_else(|error| unreachable!("refreshable collection: {error:?}"));
    assert_eq!(
        populated.preview_collection_refresh(workspace.workspace_id, 2, &workspace, 7, &Digest),
        Err(LibraryError::LibraryRevisionConflict)
    );
    if let Some(source) = workspace.sources.first_mut() {
        source.canonical_locator = None;
    }
    assert_eq!(
        populated.preview_collection_refresh(workspace.workspace_id, 1, &workspace, 7, &Digest),
        Err(LibraryError::SourceNotRefreshable)
    );
    let deleted = LibraryStore::new();
    assert_eq!(
        deleted.compare_collection_refresh(&preview, &original_workspace(), &workspace, &Digest),
        Err(LibraryError::LibraryRevisionConflict)
    );
}
