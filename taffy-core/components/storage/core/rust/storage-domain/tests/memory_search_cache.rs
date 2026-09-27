// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::ids::{MemoryId, WorkspaceId};
use taffy_storage::memory::{
    MemoryMutationPlan, MemoryQuery, MemoryRecord, MemoryScope, MemorySearchAudience,
    MemorySensitivity, MemorySource, MemoryStore, MemoryWorkspace,
};

fn record(id: u8, updated: u64) -> MemoryRecord {
    MemoryRecord {
        memory_id: MemoryId::from_bytes([id; 16]),
        revision: 1,
        statement: "Prefer concise comparisons".to_owned(),
        source: MemorySource::UserEntered,
        scope: MemoryScope::AllTasks,
        sensitivity: MemorySensitivity::Standard,
        created_at_epoch_ms: 100,
        updated_at_epoch_ms: updated,
        reviewed_at_epoch_ms: None,
        expires_at_epoch_ms: None,
    }
}

fn query(limit: u32) -> MemoryQuery {
    MemoryQuery::new("prefer", limit).unwrap_or_else(|_| unreachable!("valid query"))
}

fn ids(store: &MemoryStore, audience: MemorySearchAudience, now: u64, limit: u32) -> Vec<MemoryId> {
    store
        .search(&query(limit), audience, now)
        .into_iter()
        .map(|hit| hit.record.memory_id)
        .collect()
}

#[test]
fn a_warm_review_search_cannot_supply_private_or_other_workspace_records_to_a_task() {
    let global = record(1, 200);
    let mut scoped = record(2, 300);
    let workspace_id = WorkspaceId::from_bytes([8; 16]);
    scoped.scope = MemoryScope::Workspace(MemoryWorkspace {
        workspace_id,
        display_name: "Shopping".to_owned(),
    });
    let mut sensitive = record(3, 400);
    sensitive.sensitivity = MemorySensitivity::Sensitive;
    let mut store = MemoryStore::new();
    store
        .restore(3, vec![global.clone(), scoped.clone(), sensitive.clone()])
        .unwrap_or_else(|_| unreachable!("valid records"));

    assert_eq!(
        ids(&store, MemorySearchAudience::PersonReview, 500, 1),
        vec![sensitive.memory_id]
    );
    assert_eq!(
        ids(
            &store,
            MemorySearchAudience::Task { workspace_id: None },
            500,
            8
        ),
        vec![global.memory_id]
    );
    assert_eq!(
        ids(
            &store,
            MemorySearchAudience::Task {
                workspace_id: Some(workspace_id)
            },
            500,
            8
        ),
        vec![scoped.memory_id, global.memory_id]
    );
    assert_eq!(
        ids(&store, MemorySearchAudience::PersonReview, 500, 8).len(),
        3
    );
}

#[test]
fn expiry_rechecks_all_candidates_before_applying_the_requested_limit() {
    let older = record(1, 200);
    let mut expiring = record(2, 300);
    expiring.expires_at_epoch_ms = Some(500);
    let mut store = MemoryStore::new();
    store
        .restore(2, vec![older.clone(), expiring.clone()])
        .unwrap_or_else(|_| unreachable!("valid records"));

    assert_eq!(
        ids(&store, MemorySearchAudience::PersonReview, 499, 1),
        vec![expiring.memory_id]
    );
    assert_eq!(
        ids(&store, MemorySearchAudience::PersonReview, 500, 1),
        vec![older.memory_id]
    );
    // A wall clock correction is also evaluated from current time, never from
    // when a search happened to populate the candidate cache.
    assert_eq!(
        ids(&store, MemorySearchAudience::PersonReview, 499, 1),
        vec![expiring.memory_id]
    );
}

#[test]
fn updates_and_deletes_invalidate_only_after_the_exact_durable_completion() {
    let original = record(1, 200);
    let mut store = MemoryStore::new();
    store
        .restore(1, vec![original.clone()])
        .unwrap_or_else(|_| unreachable!("valid record"));
    let audience = MemorySearchAudience::Task { workspace_id: None };
    assert_eq!(ids(&store, audience, 500, 8), vec![original.memory_id]);

    let mut updated = original.clone();
    updated.revision = 2;
    updated.updated_at_epoch_ms = 400;
    updated.statement = "Choose repairable phones".to_owned();
    let plan = store
        .begin_save("update".to_owned(), 1, 1, updated)
        .unwrap_or_else(|_| unreachable!("valid update"));
    assert!(matches!(plan, MemoryMutationPlan::Persist(_)));
    assert_eq!(ids(&store, audience, 500, 8), vec![original.memory_id]);
    assert!(store.complete("update", 3).is_err());
    assert_eq!(ids(&store, audience, 500, 8), vec![original.memory_id]);
    store
        .complete("update", 2)
        .unwrap_or_else(|_| unreachable!("matching update"));
    assert!(ids(&store, audience, 500, 8).is_empty());

    let repairable = MemoryQuery::new("repairable", 8).unwrap_or_else(|_| unreachable!("query"));
    assert_eq!(store.search(&repairable, audience, 500).len(), 1);
    store
        .begin_delete("delete".to_owned(), 2, original.memory_id, 2)
        .unwrap_or_else(|_| unreachable!("valid deletion"));
    assert_eq!(store.search(&repairable, audience, 500).len(), 1);
    store
        .complete("delete", 3)
        .unwrap_or_else(|_| unreachable!("matching deletion"));
    assert!(store.search(&repairable, audience, 500).is_empty());
}

#[test]
fn restore_and_new_saves_invalidate_cached_misses_and_rejected_writes_keep_the_live_view() {
    let original = record(1, 200);
    let mut store = MemoryStore::new();
    let audience = MemorySearchAudience::PersonReview;
    assert!(ids(&store, audience, 500, 8).is_empty());
    store
        .begin_save("new".to_owned(), 0, 0, original.clone())
        .unwrap_or_else(|_| unreachable!("valid record"));
    assert!(ids(&store, audience, 500, 8).is_empty());
    store
        .complete("new", 1)
        .unwrap_or_else(|_| unreachable!("matching save"));
    assert_eq!(ids(&store, audience, 500, 8), vec![original.memory_id]);

    store
        .begin_delete("rejected".to_owned(), 1, original.memory_id, 1)
        .unwrap_or_else(|_| unreachable!("valid deletion"));
    assert!(store.reject("rejected"));
    assert_eq!(ids(&store, audience, 500, 8), vec![original.memory_id]);
    let replacement = record(2, 200);
    store
        .restore(1, vec![replacement.clone()])
        .unwrap_or_else(|_| unreachable!("valid restore"));
    assert_eq!(ids(&store, audience, 500, 8), vec![replacement.memory_id]);
}
