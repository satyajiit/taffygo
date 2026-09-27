// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::cell::Cell;

use core_api_types::WorkspaceExportFormat;
use core_runtime::adapters::workspace::WorkspaceStore;
use core_runtime::{DigestError, Sha256Port, WorkspaceStoreError};
use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
use taffy_storage::workspace::{
    encode_snapshot, FactKind, WorkspaceFact, WorkspacePhase, WorkspaceSnapshot, WorkspaceSource,
    WorkspaceTemplate,
};

/// One fixed browser clock reading. Staging carries it so an unanswered
/// deletion can be forgotten; nothing in these cases is near the horizon.
const NOW_MS: u64 = 1_800_000_000_000;

struct TestDigest;

impl Sha256Port for TestDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], DigestError> {
        let mut output = [0_u8; 32];
        let width = output.len();
        for (block, chunk) in input.chunks(width).enumerate() {
            for (offset, (slot, byte)) in output.iter_mut().zip(chunk).enumerate() {
                let index = block.saturating_mul(width).saturating_add(offset);
                *slot = (*slot)
                    .wrapping_mul(31)
                    .wrapping_add(*byte)
                    .wrapping_add(u8::try_from(index % 251).unwrap_or(0));
            }
        }
        Ok(output)
    }
}

#[derive(Default)]
struct CountingDigest {
    calls: Cell<usize>,
}

impl Sha256Port for CountingDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], DigestError> {
        self.calls.set(self.calls.get().saturating_add(1));
        TestDigest.sha256(input)
    }
}

fn snapshot(id: u8, revision: u64, updated: u64) -> WorkspaceSnapshot {
    let source_id = SourceId::from_bytes([id.saturating_add(1); 16]);
    WorkspaceSnapshot {
        workspace_id: WorkspaceId::from_bytes([id; 16]),
        revision,
        display_name: format!("Workspace {id}"),
        goal: format!("Immutable goal with private detail {id}"),
        phase: WorkspacePhase::Running,
        saved: true,
        last_updated_epoch_ms: updated,
        template: WorkspaceTemplate::SummarizeEvidence,
        sources: vec![WorkspaceSource {
            source_id,
            title: "Cited page".to_owned(),
            host: "evidence.example".to_owned(),
            canonical_locator: None,
            read_at_epoch_ms: updated,
            excluded: false,
        }],
        facts: vec![WorkspaceFact {
            fact_id: FactId::from_bytes([id.saturating_add(2); 16]),
            field: "finding".to_owned(),
            value: "redacted retained fact".to_owned(),
            kind: FactKind::FromPage,
            sources: vec![source_id],
            correction: None,
            has_conflict: false,
            media_provenance: None,
        }],
    }
}

fn restored(values: &[WorkspaceSnapshot]) -> WorkspaceStore {
    let encoded = values
        .iter()
        .map(|value| encode_snapshot(value).unwrap_or_default())
        .collect::<Vec<_>>();
    let mut store = WorkspaceStore::new();
    assert_eq!(store.restore_encoded(&encoded), Ok(()));
    store
}

#[test]
fn persistent_list_and_reopen_are_deterministic_across_restart() {
    let older = snapshot(1, 3, 100);
    let newer = snapshot(4, 7, 200);
    let store = restored(&[older.clone(), newer.clone()]);
    let list = store.list_workspaces();
    assert_eq!(list.len(), 2);
    assert_eq!(
        list.first().map(|item| item.workspace_id.clone()),
        Some(newer.workspace_id.to_text())
    );
    assert_eq!(
        list.first().map(|item| item.display_name.as_str()),
        Some("Workspace 4")
    );
    assert_eq!(
        store.reopen_workspace(&older.workspace_id.to_text(), 3),
        Ok(older.clone())
    );
    assert_eq!(
        store.reopen_workspace(&older.workspace_id.to_text(), 2),
        Err(WorkspaceStoreError::StaleRevision)
    );

    let restarted = restored(&[older, newer]);
    assert_eq!(restarted.list_workspaces(), list);
}

#[test]
fn task_draft_stays_out_of_saved_lifecycle_until_exact_save_commits() {
    let mut draft = snapshot(1, 3, 100);
    draft.phase = WorkspacePhase::PartlyDone;
    draft.saved = false;
    let workspace_id = draft.workspace_id.to_text();
    let mut store = restored(&[draft]);

    assert!(store.list_workspaces().is_empty());
    assert_eq!(
        store.reopen_workspace(&workspace_id, 3),
        Err(WorkspaceStoreError::UnknownWorkspace)
    );
    let projected = store.project_core_api().unwrap_or_default();
    assert_eq!(projected.len(), 1);
    assert!(projected.first().is_some_and(|workspace| !workspace.saved));

    let request = store
        .begin_save("save-1".to_owned(), &workspace_id, 3, 200)
        .unwrap_or_else(|_| unreachable!("a terminal task draft can be saved"));
    assert!(store.list_workspaces().is_empty());
    assert_eq!(
        store.complete_persist("save-1", request.resulting_revision),
        Ok(())
    );
    let saved = store.list_workspaces();
    assert_eq!(saved.len(), 1);
    assert_eq!(saved.first().map(|workspace| workspace.revision), Some(4));
    assert!(store
        .project_core_api()
        .unwrap_or_default()
        .first()
        .is_some_and(|workspace| workspace.saved));
}

#[test]
fn rename_preserves_goal_and_invalidates_an_earlier_delete_preview() {
    let value = snapshot(1, 3, 100);
    let workspace_id = value.workspace_id.to_text();
    let goal = value.goal.clone();
    let mut store = restored(&[value]);
    let preview = store
        .preview_deletion(&workspace_id, &TestDigest)
        .unwrap_or_else(|_| unreachable!("fixture can be previewed"));
    let rename = store
        .begin_rename(
            "rename-1".to_owned(),
            &workspace_id,
            3,
            "A clearer name".to_owned(),
            200,
        )
        .unwrap_or_else(|_| unreachable!("valid rename stages"));
    assert_eq!(
        store
            .list_workspaces()
            .first()
            .map(|entry| entry.display_name.as_str()),
        Some("Workspace 1")
    );
    assert_eq!(
        store.complete_persist("rename-1", rename.resulting_revision),
        Ok(())
    );
    let reopened = store
        .reopen_workspace(&workspace_id, 4)
        .unwrap_or_else(|_| unreachable!("renamed revision reopens"));
    assert_eq!(reopened.display_name, "A clearer name");
    assert_eq!(reopened.goal, goal);
    assert_eq!(
        store.begin_deletion("delete-stale".to_owned(), &preview, &TestDigest, NOW_MS),
        Err(WorkspaceStoreError::InvalidConfirmation)
    );
}

#[test]
fn deletion_is_content_free_staged_idempotent_and_published_only_after_commit() {
    let value = snapshot(1, 3, 100);
    let workspace_id = value.workspace_id.to_text();
    let mut store = restored(&[value]);
    store
        .prepare_artifact("artifact-1", &workspace_id, WorkspaceExportFormat::Markdown)
        .unwrap_or_else(|_| unreachable!("cited fact renders"));
    let preview = store
        .preview_deletion(&workspace_id, &TestDigest)
        .unwrap_or_else(|_| unreachable!("fixture can be previewed"));
    assert_eq!(preview.counts.sources, 1);
    assert_eq!(preview.counts.facts, 1);
    assert_eq!(preview.counts.artifact_metadata, 1);
    assert_eq!(preview.counts.derived_indexes, 0);
    assert_eq!(preview.confirmation_token.len(), 64);
    assert!(!preview.confirmation_token.contains("private"));

    let request = store
        .begin_deletion("delete-1".to_owned(), &preview, &TestDigest, NOW_MS)
        .unwrap_or_else(|_| unreachable!("exact confirmation stages"));
    assert_eq!(
        store.begin_deletion("delete-1".to_owned(), &preview, &TestDigest, NOW_MS),
        Ok(request.clone())
    );
    assert_eq!(store.list_workspaces().len(), 1);
    assert_eq!(
        store.complete_deletion("delete-1", request.resulting_revision + 1),
        Err(WorkspaceStoreError::WrongCompletion)
    );
    assert_eq!(store.list_workspaces().len(), 1);
    assert_eq!(
        store.complete_deletion("delete-1", request.resulting_revision),
        Ok(())
    );
    assert!(store.list_workspaces().is_empty());
    assert_eq!(store.latest_export(), None);
}

#[test]
fn metadata_change_or_refusal_cannot_commit_an_old_confirmation() {
    let value = snapshot(1, 3, 100);
    let workspace_id = value.workspace_id.to_text();
    let mut store = restored(&[value]);
    let preview = store
        .preview_deletion(&workspace_id, &TestDigest)
        .unwrap_or_else(|_| unreachable!("fixture can be previewed"));
    store
        .prepare_artifact("artifact-1", &workspace_id, WorkspaceExportFormat::Csv)
        .unwrap_or_else(|_| unreachable!("cited fact renders"));
    assert_eq!(
        store.begin_deletion("delete-old".to_owned(), &preview, &TestDigest, NOW_MS),
        Err(WorkspaceStoreError::InvalidConfirmation)
    );

    let current = store
        .preview_deletion(&workspace_id, &TestDigest)
        .unwrap_or_else(|_| unreachable!("current state can be previewed"));
    store
        .begin_deletion("delete-refused".to_owned(), &current, &TestDigest, NOW_MS)
        .unwrap_or_else(|_| unreachable!("exact confirmation stages"));
    assert!(store.reject_deletion("delete-refused"));
    assert_eq!(store.list_workspaces().len(), 1);
    assert_eq!(
        store
            .reopen_workspace(&workspace_id, 3)
            .map(|item| item.goal),
        Ok("Immutable goal with private detail 1".to_owned())
    );
}

#[test]
fn unchanged_delete_preview_hashes_the_workspace_only_once() {
    let value = snapshot(1, 3, 100);
    let workspace_id = value.workspace_id.to_text();
    let mut store = restored(&[value]);
    let digest = CountingDigest::default();

    let first = store
        .preview_deletion(&workspace_id, &digest)
        .unwrap_or_else(|_| unreachable!("fixture can be previewed"));
    let repeated = store
        .preview_deletion(&workspace_id, &digest)
        .unwrap_or_else(|_| unreachable!("unchanged preview can be reused"));

    assert_eq!(repeated, first);
    assert_eq!(digest.calls.get(), 1);

    store
        .prepare_artifact("artifact-1", &workspace_id, WorkspaceExportFormat::Csv)
        .unwrap_or_else(|_| unreachable!("cited fact renders"));
    let changed = store
        .preview_deletion(&workspace_id, &digest)
        .unwrap_or_else(|_| unreachable!("changed metadata can be previewed"));

    assert_ne!(changed, first);
    assert_eq!(digest.calls.get(), 2);
}

#[test]
fn only_an_exact_terminal_task_draft_can_be_discarded() {
    let mut draft = snapshot(1, 3, 100);
    draft.saved = false;
    draft.phase = WorkspacePhase::PartlyDone;
    let workspace_id = draft.workspace_id.to_text();
    let mut store = restored(&[draft]);

    assert_eq!(
        store.begin_discard(
            "discard-stale".to_owned(),
            &workspace_id,
            2,
            &TestDigest,
            NOW_MS
        ),
        Err(WorkspaceStoreError::StaleRevision)
    );
    let request = store
        .begin_discard(
            "discard-1".to_owned(),
            &workspace_id,
            3,
            &TestDigest,
            NOW_MS,
        )
        .unwrap_or_else(|_| unreachable!("an exact terminal draft can be discarded"));
    assert_eq!(request.expected_revision, 3);
    assert_eq!(request.resulting_revision, 4);
    assert!(store.list_workspaces().is_empty());
    assert_eq!(
        store.complete_deletion("discard-1", request.resulting_revision),
        Ok(())
    );
    assert!(store.project_core_api().unwrap_or_default().is_empty());

    let mut running = snapshot(2, 5, 200);
    running.saved = false;
    let running_id = running.workspace_id.to_text();
    let mut store = restored(&[running]);
    assert_eq!(
        store.begin_discard(
            "discard-running".to_owned(),
            &running_id,
            5,
            &TestDigest,
            NOW_MS
        ),
        Err(WorkspaceStoreError::NotDiscardable)
    );

    let saved = snapshot(3, 7, 300);
    let saved_id = saved.workspace_id.to_text();
    let mut store = restored(&[saved]);
    assert_eq!(
        store.begin_discard(
            "discard-saved".to_owned(),
            &saved_id,
            7,
            &TestDigest,
            NOW_MS
        ),
        Err(WorkspaceStoreError::NotDiscardable)
    );
}

#[test]
fn a_staged_deletion_nobody_answered_stops_latching_the_workspace() {
    let mut draft = snapshot(1, 3, 100);
    draft.saved = false;
    draft.phase = WorkspacePhase::PartlyDone;
    let workspace_id = draft.workspace_id.to_text();
    let mut store = restored(&[draft]);

    store
        .begin_discard(
            "discard-lost".to_owned(),
            &workspace_id,
            3,
            &TestDigest,
            NOW_MS,
        )
        .unwrap_or_else(|_| unreachable!("an exact terminal draft can be discarded"));
    // The effect never came back. Every later change to this workspace used to
    // answer `Duplicate` — read on the screen as "the same change is already
    // running" — silently and for the rest of the profile's life.
    assert_eq!(
        store.begin_discard(
            "discard-again".to_owned(),
            &workspace_id,
            3,
            &TestDigest,
            NOW_MS + 1_000
        ),
        Err(WorkspaceStoreError::OperationAlreadyPending)
    );

    let request = store
        .begin_discard(
            "discard-later".to_owned(),
            &workspace_id,
            3,
            &TestDigest,
            NOW_MS + 11 * 60 * 1_000,
        )
        .unwrap_or_else(|_| unreachable!("a forgotten staging no longer latches"));
    assert_eq!(request.resulting_revision, 4);
}

#[test]
fn a_clock_that_went_backwards_never_sweeps_a_live_staging() {
    let mut draft = snapshot(1, 3, 100);
    draft.saved = false;
    draft.phase = WorkspacePhase::PartlyDone;
    let workspace_id = draft.workspace_id.to_text();
    let mut store = restored(&[draft]);

    store
        .begin_discard(
            "discard-live".to_owned(),
            &workspace_id,
            3,
            &TestDigest,
            NOW_MS,
        )
        .unwrap_or_else(|_| unreachable!("an exact terminal draft can be discarded"));
    // Sweeping a live operation is a worse failure than the one the sweep
    // exists to end, so a reading from before the staging reads as no time
    // having passed at all.
    assert_eq!(
        store.begin_discard(
            "discard-backwards".to_owned(),
            &workspace_id,
            3,
            &TestDigest,
            NOW_MS - 60 * 60 * 1_000
        ),
        Err(WorkspaceStoreError::OperationAlreadyPending)
    );
}
