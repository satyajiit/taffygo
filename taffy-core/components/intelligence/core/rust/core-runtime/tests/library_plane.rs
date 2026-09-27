// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_api_types::{LibraryAvailability, WorkspaceExportFormat};
use core_runtime::{DigestError, LibraryPort, LibraryStoreError, ProductionLibrary, Sha256Port};
use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
use taffy_storage::workspace::{
    FactKind, WorkspaceFact, WorkspacePhase, WorkspaceSnapshot, WorkspaceSource, WorkspaceTemplate,
};

struct TestDigest;

impl Sha256Port for TestDigest {
    fn sha256(&self, _input: &[u8]) -> Result<[u8; 32], DigestError> {
        Ok([9; 32])
    }
}

fn workspace() -> WorkspaceSnapshot {
    let first_source = SourceId::from_bytes([2; 16]);
    let second_source = SourceId::from_bytes([3; 16]);
    WorkspaceSnapshot {
        workspace_id: WorkspaceId::from_bytes([1; 16]),
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
                canonical_locator: None,
                read_at_epoch_ms: 700,
                excluded: false,
            },
            WorkspaceSource {
                source_id: second_source,
                title: "Independent review".to_owned(),
                host: "review.example".to_owned(),
                canonical_locator: None,
                read_at_epoch_ms: 800,
                excluded: false,
            },
        ],
        facts: vec![WorkspaceFact {
            fact_id: FactId::from_bytes([4; 16]),
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

#[test]
fn exact_promotion_search_export_and_removal_publish_only_after_commit() {
    let workspace = workspace();
    let fact_id = workspace
        .facts
        .first()
        .map(|fact| fact.fact_id.to_text())
        .unwrap_or_default();
    let mut library = ProductionLibrary::new(false);
    let save = library
        .begin_save(
            "keep-1".to_owned(),
            &workspace,
            7,
            &fact_id,
            0,
            0,
            1_000,
            &TestDigest,
        )
        .unwrap_or_else(|error| unreachable!("valid exact promotion: {error:?}"))
        .unwrap_or_else(|| unreachable!("a new fact needs one durable write"));
    assert!(library.project_core_api().entries.is_empty());
    assert_eq!(
        library.complete("keep-1", 2),
        Err(LibraryStoreError::WrongCompletion)
    );
    assert_eq!(library.complete("keep-1", 1), Ok(()));

    let projected = library.project_core_api();
    assert_eq!(projected.availability, LibraryAvailability::Available);
    assert_eq!(projected.revision, 1);
    let entry = projected
        .entries
        .first()
        .unwrap_or_else(|| unreachable!("the committed entry is published"));
    assert_eq!(entry.entry_id, save.entry.entry_id);
    assert_eq!(entry.source_workspace_revision, 7);
    assert_eq!(entry.source_fact_id, fact_id);
    assert_eq!(entry.original_value, "one year");
    assert_eq!(entry.correction.as_deref(), Some("two years"));
    assert_eq!(entry.sources.len(), 2);
    assert_eq!(entry.captured_at_epoch_ms, 1_000);
    assert_eq!(entry.last_checked_epoch_ms, 800);
    assert!(entry.has_conflict);

    let transcript = library
        .search("search-1", "WARRANTY review.example", 8, 2_000)
        .unwrap_or_else(|error| unreachable!("valid deterministic search: {error:?}"));
    let transcript_text = transcript.result_pieces().join("\n");
    assert!(transcript_text.contains("warranty: two years"));
    assert!(transcript_text.contains("Independent review (review.example)"));
    assert!(transcript_text.contains("Last checked 1200 ms ago"));
    let search = library
        .project_core_api()
        .search
        .unwrap_or_else(|| unreachable!("the explicit search is published"));
    assert_eq!(search.library_revision, 1);
    assert_eq!(search.hits.len(), 1);
    assert_eq!(search.hits.first().map(|hit| hit.age_ms), Some(1_200));

    let export = library
        .request_export(
            "export-1",
            1,
            Some(&workspace.workspace_id.to_text()),
            WorkspaceExportFormat::Markdown,
        )
        .unwrap_or_else(|error| unreachable!("current collection export: {error:?}"));
    assert!(export.content.contains("two years"));
    assert!(export.content.contains("review.example"));

    let remove = library
        .begin_remove(
            "remove-1".to_owned(),
            &save.entry.entry_id,
            1,
            save.entry.revision,
            3_000,
        )
        .unwrap_or_else(|error| unreachable!("current exact removal: {error:?}"));
    assert_eq!(remove.resulting_library_revision, 2);
    assert_eq!(library.project_core_api().entries.len(), 1);
    assert_eq!(library.complete("remove-1", 2), Ok(()));
    assert!(library.project_core_api().entries.is_empty());
    assert!(library.project_core_api().search.is_none());
    assert!(library.latest_export().is_none());
}

#[test]
fn private_profiles_restore_and_publish_no_library_content() {
    let workspace = workspace();
    let fact_id = workspace
        .facts
        .first()
        .map(|fact| fact.fact_id.to_text())
        .unwrap_or_default();
    let mut regular = ProductionLibrary::new(false);
    let saved = regular
        .begin_save(
            "regular-save".to_owned(),
            &workspace,
            7,
            &fact_id,
            0,
            0,
            1_000,
            &TestDigest,
        )
        .unwrap_or_else(|error| unreachable!("valid fixture: {error:?}"))
        .unwrap_or_else(|| unreachable!("fixture is new"));

    let mut private = ProductionLibrary::new(true);
    assert_eq!(private.restore(0, Vec::new()), Ok(()));
    assert_eq!(
        private.restore(1, vec![saved.entry]),
        Err(LibraryStoreError::PrivateProfile)
    );
    assert_eq!(
        private.search("private-search", "warranty", 8, 2_000),
        Err(LibraryStoreError::PrivateProfile)
    );
    assert_eq!(
        private.request_export("private-export", 0, None, WorkspaceExportFormat::Markdown),
        Err(LibraryStoreError::PrivateProfile)
    );
    let projected = private.project_core_api();
    assert_eq!(projected.availability, LibraryAvailability::PrivateProfile);
    assert_eq!(projected.revision, 0);
    assert!(projected.entries.is_empty());
    assert!(projected.search.is_none());
    assert!(private.latest_export().is_none());
}
