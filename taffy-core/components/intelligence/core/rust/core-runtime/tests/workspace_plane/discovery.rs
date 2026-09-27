// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Exact metadata admission at the typed workspace port.

use super::*;

#[test]
fn new_task_page_facts_require_exact_public_metadata() {
    let value = snapshot(1);
    let mut new_source = value
        .sources
        .first()
        .unwrap_or_else(|| unreachable!("fixture source"))
        .clone();
    new_source.source_id = SourceId::from_bytes([9; 16]);
    new_source.read_at_epoch_ms = 0;
    let source_id = new_source.source_id.to_text();
    for mutation in 0..4 {
        let mut store = restored_store(&value);
        let mut supplied = new_source.clone();
        match mutation {
            1 => supplied.source_id = SourceId::from_bytes([8; 16]),
            2 => {
                supplied.canonical_locator =
                    Some("https://evidence.example/report?private=1".into());
            }
            3 => supplied.excluded = true,
            _ => {}
        }
        let result = store.begin_task_update(
            "new-page".into(),
            &value.workspace_id.to_text(),
            WorkspacePhase::Done,
            Some((
                &source_id,
                vec![WorkspacePageFact {
                    fact_id: FactId::from_bytes([7; 16]).to_text(),
                    field: "page".into(),
                    value: "verified page text".into(),
                    media_provenance: None,
                }],
            )),
            (mutation != 0).then_some(supplied),
            1_787_337_200_000,
        );
        assert!(result.is_err(), "mutation {mutation}");
        assert_eq!(
            store.list_workspaces().first().map(|row| row.revision),
            Some(value.revision)
        );
    }
}

#[test]
fn a_verified_empty_capture_can_record_its_source_without_inventing_facts() {
    let value = snapshot(1);
    let mut source = value
        .sources
        .first()
        .unwrap_or_else(|| unreachable!("fixture source"))
        .clone();
    source.source_id = SourceId::from_bytes([9; 16]);
    source.read_at_epoch_ms = 0;
    let id = source.source_id.to_text();
    let mut store = restored_store(&value);
    assert!(store
        .begin_task_update(
            "metadata-alone".into(),
            &value.workspace_id.to_text(),
            WorkspacePhase::Done,
            None,
            Some(source.clone()),
            1_787_337_200_000
        )
        .is_err());
    let pending = store
        .begin_task_update(
            "empty-capture".into(),
            &value.workspace_id.to_text(),
            WorkspacePhase::Done,
            Some((&id, Vec::new())),
            Some(source),
            1_787_337_200_000,
        )
        .unwrap_or_else(|_| unreachable!("exact empty capture is valid"))
        .unwrap_or_else(|| unreachable!("new observed source needs a revision"));
    let encoded = taffy_storage::workspace::decode_snapshot(&pending.snapshot)
        .unwrap_or_else(|_| unreachable!("pending snapshot is bounded"));
    assert_eq!(encoded.facts, value.facts);
    assert_eq!(encoded.sources.len(), value.sources.len() + 1);
    assert_eq!(
        store.list_workspaces().first().map(|row| row.revision),
        Some(value.revision)
    );
    assert_eq!(
        store.complete_persist("empty-capture", pending.resulting_revision),
        Ok(())
    );
    assert_eq!(
        store.list_workspaces().first().map(|row| row.revision),
        Some(pending.resulting_revision)
    );
}
