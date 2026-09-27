// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
use taffy_storage::workspace::{
    FactKind, WorkspaceFact, WorkspaceMediaEvidenceKind, WorkspaceMediaFactKind,
    WorkspaceMediaKind, WorkspaceMediaProvenance, WorkspaceMutation, WorkspacePageFactScope,
    WorkspacePhase, WorkspaceSnapshot, WorkspaceSource, WorkspaceTemplate,
};

fn fact(id: u8, source_id: SourceId, value: &str, media: bool) -> WorkspaceFact {
    WorkspaceFact {
        fact_id: FactId::from_bytes([id; 16]),
        field: if media { "PDF table row" } else { "page" }.to_owned(),
        value: value.to_owned(),
        kind: FactKind::FromPage,
        sources: vec![source_id],
        correction: None,
        has_conflict: false,
        media_provenance: media.then(|| WorkspaceMediaProvenance {
            media_kind: WorkspaceMediaKind::Pdf,
            fact_kind: WorkspaceMediaFactKind::PdfTableRow,
            evidence_kind: WorkspaceMediaEvidenceKind::TableHeuristic,
            source_locator: "pdf:page/2".to_owned(),
            source_start: 10,
            source_end: 20,
            page_index_plus_one: 2,
            timestamp_start_ms: 0,
            timestamp_end_ms: 0,
            row_index_plus_one: 1,
            confidence_ppm: 700_000,
            truncated: false,
        }),
    }
}

fn snapshot() -> WorkspaceSnapshot {
    let source_id = SourceId::from_bytes([2; 16]);
    WorkspaceSnapshot {
        workspace_id: WorkspaceId::from_bytes([1; 16]),
        revision: 3,
        display_name: "Evidence".to_owned(),
        goal: "Compare evidence".to_owned(),
        phase: WorkspacePhase::Running,
        saved: true,
        last_updated_epoch_ms: 40,
        template: WorkspaceTemplate::CompareProducts,
        sources: vec![WorkspaceSource {
            source_id,
            title: "Source".to_owned(),
            host: "evidence.example".to_owned(),
            canonical_locator: None,
            read_at_epoch_ms: 30,
            excluded: false,
        }],
        facts: vec![fact(3, source_id, "two years", false)],
    }
}

#[test]
fn media_upserts_replace_only_their_coordinate_scope() {
    let source_id = SourceId::from_bytes([2; 16]);
    let with_pdf = snapshot()
        .apply(
            3,
            50,
            WorkspaceMutation::ReplacePageFacts {
                source_id,
                facts: vec![fact(7, source_id, "Revenue 42", true)],
            },
        )
        .unwrap_or_else(|_| unreachable!("valid PDF facts coexist with DOM facts"));
    assert_eq!(with_pdf.facts.len(), 2);
    assert!(with_pdf
        .facts
        .iter()
        .any(|fact| fact.page_scope() == Some(WorkspacePageFactScope::Dom)));

    let replaced_pdf = with_pdf
        .apply(
            4,
            60,
            WorkspaceMutation::ReplacePageFacts {
                source_id,
                facts: vec![fact(8, source_id, "Revenue 43", true)],
            },
        )
        .unwrap_or_else(|_| unreachable!("same media scope replaces atomically"));
    assert_eq!(replaced_pdf.facts.len(), 2);
    assert!(replaced_pdf
        .facts
        .iter()
        .any(|fact| fact.value == "two years"));
    assert!(replaced_pdf
        .facts
        .iter()
        .any(|fact| fact.value == "Revenue 43"));
    assert!(!replaced_pdf
        .facts
        .iter()
        .any(|fact| fact.value == "Revenue 42"));

    let replaced_dom = replaced_pdf
        .apply(
            5,
            70,
            WorkspaceMutation::ReplacePageFacts {
                source_id,
                facts: vec![fact(9, source_id, "three years", false)],
            },
        )
        .unwrap_or_else(|_| unreachable!("DOM replacement retains PDF evidence"));
    assert_eq!(replaced_dom.facts.len(), 2);
    assert!(replaced_dom
        .facts
        .iter()
        .any(|fact| fact.value == "three years"));
    assert!(replaced_dom
        .facts
        .iter()
        .any(|fact| fact.value == "Revenue 43"));
}
