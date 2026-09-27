// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use core_api_types::WorkspaceExportFormat;
use core_runtime::adapters::workspace::WorkspaceStore;
use core_runtime::WorkspacePageFact;
use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
use taffy_storage::workspace::{
    encode_snapshot, FactKind, WorkspaceFact, WorkspaceMediaEvidenceKind, WorkspaceMediaFactKind,
    WorkspaceMediaKind, WorkspaceMediaProvenance, WorkspacePhase, WorkspaceSnapshot,
    WorkspaceSource, WorkspaceTemplate,
};

fn snapshot() -> WorkspaceSnapshot {
    let source_id = SourceId::from_bytes([2; 16]);
    WorkspaceSnapshot {
        workspace_id: WorkspaceId::from_bytes([1; 16]),
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

#[test]
fn media_coordinates_survive_scoped_ingestion_restart_and_export() {
    let value = snapshot();
    let workspace_id = value.workspace_id.to_text();
    let source_id = value
        .sources
        .first()
        .map(|source| source.source_id.to_text())
        .unwrap_or_default();
    let encoded = encode_snapshot(&value).unwrap_or_default();
    let mut store = WorkspaceStore::new();
    assert_eq!(store.restore_encoded(&[encoded]), Ok(()));

    let staged = store
        .begin_page_ingestion(
            "media-ingestion".to_owned(),
            &workspace_id,
            &source_id,
            vec![WorkspacePageFact {
                fact_id: FactId::from_bytes([8; 16]).to_text(),
                field: "PDF table row".to_owned(),
                value: "Revenue 42".to_owned(),
                media_provenance: Some(WorkspaceMediaProvenance {
                    media_kind: WorkspaceMediaKind::Pdf,
                    fact_kind: WorkspaceMediaFactKind::PdfTableRow,
                    evidence_kind: WorkspaceMediaEvidenceKind::TableHeuristic,
                    source_locator: "pdf:page/2".to_owned(),
                    source_start: 10,
                    source_end: 21,
                    page_index_plus_one: 2,
                    timestamp_start_ms: 0,
                    timestamp_end_ms: 0,
                    row_index_plus_one: 1,
                    confidence_ppm: 650_000,
                    truncated: false,
                }),
            }],
            1_787_337_100_000,
        )
        .unwrap_or_else(|_| unreachable!("bounded media fact stages"))
        .unwrap_or_else(|| unreachable!("nonempty media fact writes"));
    assert_eq!(store.complete_persist("media-ingestion", 4), Ok(()));
    let reopened = store
        .reopen_workspace(&workspace_id, 4)
        .unwrap_or_else(|_| unreachable!("committed workspace reopens"));
    assert_eq!(reopened.facts.len(), 2, "DOM and PDF scopes coexist");

    let mut restarted = WorkspaceStore::new();
    assert_eq!(
        restarted.restore_bound_records(&[(workspace_id.clone(), 4, staged.snapshot)]),
        Ok(())
    );
    let markdown = restarted
        .request_export(
            "media-export",
            &workspace_id,
            4,
            WorkspaceExportFormat::Markdown,
        )
        .map(|view| view.content)
        .unwrap_or_default();
    assert!(markdown.contains("Revenue 42"));
    assert!(markdown.contains("pdf:page/2"));
    assert!(markdown.contains("page 2"));
    assert!(markdown.contains("row 1"));
}
