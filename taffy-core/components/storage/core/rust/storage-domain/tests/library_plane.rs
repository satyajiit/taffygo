// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::ids::{FactId, LibraryEntryId, SourceId, WorkspaceId};
use taffy_storage::library::{
    entry_from_workspace, LibraryError, LibraryMutation, LibraryMutationPlan, LibraryQuery,
    LibraryStore,
};
use taffy_storage::workspace::{
    FactKind, WorkspaceFact, WorkspacePhase, WorkspaceSnapshot, WorkspaceSource, WorkspaceTemplate,
};

fn id<T>(byte: u8, wrap: impl FnOnce([u8; 16]) -> T) -> T {
    wrap([byte; 16])
}

fn workspace() -> WorkspaceSnapshot {
    let first_source = id(2, SourceId::from_bytes);
    let second_source = id(3, SourceId::from_bytes);
    WorkspaceSnapshot {
        workspace_id: id(1, WorkspaceId::from_bytes),
        revision: 7,
        display_name: "Television research".to_owned(),
        goal: "Compare large televisions".to_owned(),
        phase: WorkspacePhase::Done,
        saved: true,
        last_updated_epoch_ms: 900,
        template: WorkspaceTemplate::CompareProducts,
        sources: vec![
            WorkspaceSource {
                source_id: first_source,
                title: "Manufacturer specifications".to_owned(),
                host: "maker.example".to_owned(),
                canonical_locator: Some("https://maker.example/specifications".to_owned()),
                read_at_epoch_ms: 700,
                excluded: false,
            },
            WorkspaceSource {
                source_id: second_source,
                title: "Independent review".to_owned(),
                host: "review.example".to_owned(),
                canonical_locator: Some("https://review.example/television".to_owned()),
                read_at_epoch_ms: 800,
                excluded: false,
            },
        ],
        facts: vec![WorkspaceFact {
            fact_id: id(4, FactId::from_bytes),
            field: "warranty".to_owned(),
            value: "one year".to_owned(),
            kind: FactKind::Summarized,
            sources: vec![first_source, second_source],
            correction: Some("two years".to_owned()),
            has_conflict: true,
            media_provenance: None,
        }],
    }
}

fn kept_entry(
    entry_revision: u64,
    captured_at_epoch_ms: u64,
) -> taffy_storage::library::LibraryEntry {
    entry_from_workspace(
        id(5, LibraryEntryId::from_bytes),
        entry_revision,
        &workspace(),
        7,
        id(4, FactId::from_bytes),
        captured_at_epoch_ms,
    )
    .unwrap_or_else(|error| unreachable!("valid cited fact: {error:?}"))
}

#[test]
fn promotion_copies_only_the_exact_cited_fact_and_provenance() {
    let entry = kept_entry(1, 1_000);
    assert_eq!(entry.source_workspace_revision, 7);
    assert_eq!(entry.source_fact_id, id(4, FactId::from_bytes));
    assert_eq!(entry.original_value, "one year");
    assert_eq!(entry.correction.as_deref(), Some("two years"));
    assert_eq!(entry.display_value(), "two years");
    assert_eq!(entry.last_checked_epoch_ms, 800);
    assert!(entry.has_conflict);
    assert_eq!(entry.sources.len(), 2);
    assert!(entry.validate());
}

#[test]
fn unsaved_stale_and_uncited_workspace_facts_are_never_promoted() {
    let entry_id = id(5, LibraryEntryId::from_bytes);
    let fact_id = id(4, FactId::from_bytes);
    let mut snapshot = workspace();
    snapshot.saved = false;
    assert_eq!(
        entry_from_workspace(entry_id, 1, &snapshot, 7, fact_id, 1_000),
        Err(LibraryError::WorkspaceNotSaved)
    );

    let snapshot = workspace();
    assert_eq!(
        entry_from_workspace(entry_id, 1, &snapshot, 6, fact_id, 1_000),
        Err(LibraryError::WorkspaceRevisionConflict)
    );

    let mut snapshot = workspace();
    for source in &mut snapshot.sources {
        source.excluded = true;
    }
    assert_eq!(
        entry_from_workspace(entry_id, 1, &snapshot, 7, fact_id, 1_000),
        Err(LibraryError::FactHasNoActiveCitation)
    );
}

#[test]
fn save_and_remove_are_exact_revision_staged_transactions() {
    let mut store = LibraryStore::new();
    let entry = kept_entry(1, 1_000);
    let plan = store
        .begin_save("keep-1".to_owned(), 0, 0, entry.clone())
        .unwrap_or_else(|error| unreachable!("valid save: {error:?}"));
    let LibraryMutationPlan::Persist(save) = plan else {
        unreachable!("new entry needs persistence")
    };
    assert_eq!(save.expected_library_revision, 0);
    assert_eq!(save.resulting_library_revision, 1);
    assert!(matches!(save.mutation, LibraryMutation::Save(_)));
    assert_eq!(
        store.begin_remove("remove-too-early".to_owned(), 0, entry.entry_id, 1),
        Err(LibraryError::OperationAlreadyPending)
    );
    store
        .complete("keep-1", 1)
        .unwrap_or_else(|error| unreachable!("matching completion: {error:?}"));
    assert_eq!(store.revision(), 1);

    let idempotent_plan = store
        .begin_save("keep-again".to_owned(), 1, 1, kept_entry(2, 1_000))
        .unwrap_or_else(|error| unreachable!("same fact is idempotent: {error:?}"));
    assert_eq!(idempotent_plan, LibraryMutationPlan::AlreadyCurrent);

    assert_eq!(
        store.begin_remove("remove-stale".to_owned(), 0, entry.entry_id, 1),
        Err(LibraryError::LibraryRevisionConflict)
    );
    let remove = store
        .begin_remove("remove-1".to_owned(), 1, entry.entry_id, 1)
        .unwrap_or_else(|error| unreachable!("current remove: {error:?}"));
    assert_eq!(remove.resulting_library_revision, 2);
    assert!(matches!(remove.mutation, LibraryMutation::Remove { .. }));
    store
        .complete("remove-1", 2)
        .unwrap_or_else(|error| unreachable!("matching remove: {error:?}"));
    assert_eq!(store.revision(), 2);
    assert!(store.entry(entry.entry_id).is_none());
}

#[test]
fn search_is_bounded_deterministic_and_carries_exact_freshness() {
    let mut store = LibraryStore::new();
    store
        .restore(1, vec![kept_entry(1, 1_000)])
        .unwrap_or_else(|error| unreachable!("valid restore: {error:?}"));
    // Upper-case input exercises the allocation-free ASCII path while the
    // stored projection remains in its original case.
    let query = LibraryQuery::new("WARRANTY REVIEW.EXAMPLE", 8)
        .unwrap_or_else(|error| unreachable!("valid query: {error:?}"));
    let hits = store.search(&query, 2_000);
    assert_eq!(hits.len(), 1);
    assert_eq!(hits.first().map(|hit| hit.age_ms), Some(1_200));
    assert!(hits.first().is_some_and(|hit| hit.entry.has_conflict));

    let no_match = LibraryQuery::new("unrelated", 8)
        .unwrap_or_else(|error| unreachable!("valid query: {error:?}"));
    assert!(store.search(&no_match, 2_000).is_empty());
    assert_eq!(LibraryQuery::new("", 8), Err(LibraryError::InvalidQuery));
    assert_eq!(
        LibraryQuery::new("warranty", 0),
        Err(LibraryError::InvalidQuery)
    );
}

#[test]
fn search_preserves_unicode_case_matching_outside_the_ascii_fast_path() {
    let mut entry = kept_entry(1, 1_000);
    entry.collection_name = "Café research".to_owned();
    let mut store = LibraryStore::new();
    store
        .restore(1, vec![entry])
        .unwrap_or_else(|error| unreachable!("valid restore: {error:?}"));
    let query = LibraryQuery::new("CAFÉ", 8)
        .unwrap_or_else(|error| unreachable!("valid Unicode query: {error:?}"));

    assert_eq!(store.search(&query, 2_000).len(), 1);
}
