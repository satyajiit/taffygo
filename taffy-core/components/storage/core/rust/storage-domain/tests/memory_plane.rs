// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::ids::{MemoryId, WorkspaceId};
use taffy_storage::memory::{
    MemoryError, MemoryMutation, MemoryMutationPlan, MemoryQuery, MemoryRecord, MemoryScope,
    MemorySearchAudience, MemorySensitivity, MemorySource, MemoryStore, MemoryWorkspace,
};

fn id<T>(byte: u8, wrap: impl FnOnce([u8; 16]) -> T) -> T {
    wrap([byte; 16])
}

fn workspace() -> MemoryWorkspace {
    MemoryWorkspace {
        workspace_id: id(9, WorkspaceId::from_bytes),
        display_name: "Travel planning".to_owned(),
    }
}

fn record(memory_byte: u8, revision: u64, updated_at_epoch_ms: u64) -> MemoryRecord {
    MemoryRecord {
        memory_id: id(memory_byte, MemoryId::from_bytes),
        revision,
        statement: "Prefer nonstop flights".to_owned(),
        source: MemorySource::AcceptedTaskSuggestion {
            task_id: "task_42".to_owned(),
            workspace: Some(workspace()),
        },
        scope: MemoryScope::AllTasks,
        sensitivity: MemorySensitivity::Standard,
        created_at_epoch_ms: 1_000,
        updated_at_epoch_ms,
        reviewed_at_epoch_ms: Some(updated_at_epoch_ms),
        expires_at_epoch_ms: None,
    }
}

#[test]
fn create_update_delete_are_exact_revision_staged_transactions() {
    let mut store = MemoryStore::new();
    let initial = record(1, 1, 1_000);
    let plan = store
        .begin_save("memory-create".to_owned(), 0, 0, initial.clone())
        .unwrap_or_else(|error| unreachable!("valid create: {error:?}"));
    let MemoryMutationPlan::Persist(create) = plan else {
        unreachable!("new Memory needs persistence")
    };
    assert_eq!(create.expected_memory_revision, 0);
    assert_eq!(create.resulting_memory_revision, 1);
    assert!(matches!(create.mutation, MemoryMutation::Save(_)));
    store
        .complete("memory-create", 1)
        .unwrap_or_else(|error| unreachable!("matching completion: {error:?}"));

    let mut changed = record(1, 2, 1_500);
    changed.statement = "Prefer direct flights".to_owned();
    let update = store
        .begin_save("memory-update".to_owned(), 1, 1, changed.clone())
        .unwrap_or_else(|error| unreachable!("valid update: {error:?}"));
    assert!(matches!(update, MemoryMutationPlan::Persist(_)));
    store
        .complete("memory-update", 2)
        .unwrap_or_else(|error| unreachable!("matching update: {error:?}"));
    assert_eq!(store.record(changed.memory_id), Some(&changed));

    assert_eq!(
        store.begin_delete("memory-stale".to_owned(), 1, changed.memory_id, 2),
        Err(MemoryError::MemoryRevisionConflict)
    );
    let deletion = store
        .begin_delete("memory-delete".to_owned(), 2, changed.memory_id, 2)
        .unwrap_or_else(|error| unreachable!("valid delete: {error:?}"));
    assert!(matches!(deletion.mutation, MemoryMutation::Delete { .. }));
    store
        .complete("memory-delete", 3)
        .unwrap_or_else(|error| unreachable!("matching delete: {error:?}"));
    assert!(store.record(changed.memory_id).is_none());
}

#[test]
fn immutable_attribution_cannot_be_rewritten() {
    let mut store = MemoryStore::new();
    let initial = record(1, 1, 1_000);
    let _ = store
        .begin_save("create".to_owned(), 0, 0, initial.clone())
        .unwrap_or_else(|error| unreachable!("valid create: {error:?}"));
    store
        .complete("create", 1)
        .unwrap_or_else(|error| unreachable!("matching completion: {error:?}"));

    let mut rewritten = record(1, 2, 1_500);
    rewritten.source = MemorySource::UserEntered;
    assert_eq!(
        store.begin_save("rewrite".to_owned(), 1, 1, rewritten),
        Err(MemoryError::InvalidRecord)
    );
}

#[test]
fn task_retrieval_is_scoped_bounded_and_excludes_sensitive_or_expired_records() {
    let mut global = record(1, 1, 4_000);
    global.statement = "Prefer nonstop flights".to_owned();
    let mut scoped = record(2, 1, 5_000);
    scoped.statement = "Prefer aisle seats".to_owned();
    scoped.scope = MemoryScope::Workspace(workspace());
    let mut sensitive = record(3, 1, 6_000);
    sensitive.statement = "Use my private dietary note".to_owned();
    sensitive.sensitivity = MemorySensitivity::Sensitive;
    let mut expired = record(4, 1, 7_000);
    expired.statement = "Old flight preference".to_owned();
    expired.expires_at_epoch_ms = Some(8_000);

    let mut store = MemoryStore::new();
    store
        .restore(1, vec![global, scoped, sensitive, expired])
        .unwrap_or_else(|error| unreachable!("valid restore: {error:?}"));

    // Upper-case input exercises the allocation-free ASCII path while the
    // stored projection remains in its original case.
    let query = MemoryQuery::new("PREFER", 8)
        .unwrap_or_else(|error| unreachable!("valid query: {error:?}"));
    let task_hits = store.search(
        &query,
        MemorySearchAudience::Task {
            workspace_id: Some(workspace().workspace_id),
        },
        9_000,
    );
    assert_eq!(task_hits.len(), 2);
    assert_eq!(
        task_hits.first().map(|hit| hit.record.memory_id),
        Some(id(2, MemoryId::from_bytes))
    );

    let review = MemoryQuery::new("private", 8)
        .unwrap_or_else(|error| unreachable!("valid query: {error:?}"));
    assert_eq!(
        store
            .search(&review, MemorySearchAudience::PersonReview, 9_000)
            .len(),
        1
    );
}

#[test]
fn search_preserves_unicode_case_matching_outside_the_ascii_fast_path() {
    let mut remembered = record(1, 1, 4_000);
    remembered.statement = "Remember the Café reservation".to_owned();
    let mut store = MemoryStore::new();
    store
        .restore(1, vec![remembered])
        .unwrap_or_else(|error| unreachable!("valid restore: {error:?}"));
    let query = MemoryQuery::new("CAFÉ", 8)
        .unwrap_or_else(|error| unreachable!("valid Unicode query: {error:?}"));

    assert_eq!(
        store
            .search(&query, MemorySearchAudience::PersonReview, 5_000)
            .len(),
        1
    );
}

#[test]
fn accepted_suggestions_require_visible_review_attribution() {
    let mut invalid = record(1, 1, 1_000);
    invalid.reviewed_at_epoch_ms = None;
    assert!(!invalid.validate());

    let mut invalid_expiry = record(2, 1, 1_000);
    invalid_expiry.expires_at_epoch_ms = Some(1_000);
    assert!(!invalid_expiry.validate());
}
