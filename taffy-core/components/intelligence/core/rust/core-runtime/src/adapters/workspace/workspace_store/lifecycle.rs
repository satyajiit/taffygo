// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Persistent list/reopen, rename, and two-step workspace deletion.

use taffy_storage::ids::WorkspaceId;
use taffy_storage::workspace::{encode_snapshot, WorkspaceMutation, WorkspaceSnapshot};

use super::super::workspace_export::PreparedWorkspaceArtifact;
use super::{WorkspaceStore, MAX_PENDING_MUTATIONS};
use crate::account::Sha256Port;
use crate::ports::{
    WorkspaceDeleteRequest, WorkspaceDeletionCounts, WorkspaceDeletionPreview, WorkspaceListEntry,
    WorkspacePersistRequest, WorkspaceStoreError,
};

const DELETE_CONFIRMATION_DOMAIN: &[u8] = b"\0taffy.workspace-delete-confirmation.v1\0";

/// How long a staged deletion may sit unanswered before it is forgotten.
///
/// A staged deletion is latched: while it is here, every later save or discard
/// of that workspace answers `Duplicate`, which the surface reads as "the same
/// change is already running". A delivered failure clears the entry, but the
/// browser withdrawing an effect the core already accepted does not — so one
/// lost effect made that workspace permanently unchangeable, silently, for the
/// life of the profile.
///
/// Ten minutes because it has to exceed the operation deadline the browser
/// sets by enough that a slow but live operation is never swept out from under
/// itself; a person who confirms a discard and sees nothing happen would
/// rather be able to ask again than be told for ever that they already did.
const PENDING_DELETION_HORIZON_MS: u64 = 10 * 60 * 1_000;

#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct PendingWorkspaceDeletion {
    pub(super) workspace_id: String,
    expected_revision: u64,
    resulting_revision: u64,
    counts: WorkspaceDeletionCounts,
    confirmation_token: String,
    /// When the browser asked. Read only by [`WorkspaceStore::sweep_expired`].
    begun_at_utc_ms: u64,
}

impl WorkspaceStore {
    /// Lists committed workspaces in deterministic recent-first order.
    pub fn list_workspaces(&self) -> Vec<WorkspaceListEntry> {
        let mut entries = self
            .snapshots
            .values()
            .filter(|snapshot| snapshot.saved)
            .map(|snapshot| WorkspaceListEntry {
                workspace_id: snapshot.workspace_id.to_text(),
                revision: snapshot.revision,
                display_name: snapshot.display_name.clone(),
                phase: snapshot.phase,
                last_updated_epoch_ms: snapshot.last_updated_epoch_ms,
            })
            .collect::<Vec<_>>();
        entries.sort_by(|left, right| {
            right
                .last_updated_epoch_ms
                .cmp(&left.last_updated_epoch_ms)
                .then_with(|| left.workspace_id.cmp(&right.workspace_id))
        });
        entries
    }

    /// Reopens only the exact revision selected from the persistent list.
    pub fn reopen_workspace(
        &self,
        workspace_id: &str,
        expected_revision: u64,
    ) -> Result<WorkspaceSnapshot, WorkspaceStoreError> {
        let parsed =
            WorkspaceId::parse(workspace_id).map_err(|_| WorkspaceStoreError::InvalidIdentifier)?;
        let snapshot = self
            .snapshots
            .get(&parsed.to_text())
            .filter(|snapshot| snapshot.saved)
            .ok_or(WorkspaceStoreError::UnknownWorkspace)?;
        if snapshot.revision != expected_revision {
            return Err(WorkspaceStoreError::StaleRevision);
        }
        Ok(snapshot.clone())
    }

    pub fn begin_rename(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        display_name: String,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        if !self
            .snapshot(workspace_id)
            .is_some_and(|snapshot| snapshot.saved)
        {
            return Err(WorkspaceStoreError::UnknownWorkspace);
        }
        self.begin_mutation(
            operation_id,
            workspace_id,
            expected_revision,
            updated_at_epoch_ms,
            WorkspaceMutation::Rename { display_name },
        )
    }

    pub fn preview_deletion(
        &self,
        workspace_id: &str,
        digest: &dyn Sha256Port,
    ) -> Result<WorkspaceDeletionPreview, WorkspaceStoreError> {
        let parsed =
            WorkspaceId::parse(workspace_id).map_err(|_| WorkspaceStoreError::InvalidIdentifier)?;
        let canonical_id = parsed.to_text();
        let snapshot = self
            .snapshots
            .get(&canonical_id)
            .filter(|snapshot| snapshot.saved)
            .ok_or(WorkspaceStoreError::UnknownWorkspace)?;
        if self.has_pending_workspace(&canonical_id) {
            return Err(WorkspaceStoreError::OperationAlreadyPending);
        }
        if let Ok(cache) = self.deletion_previews.try_borrow() {
            if let Some(preview) = cache.get(&canonical_id) {
                return Ok(preview.clone());
            }
        }
        let preview = self.preview_for(snapshot, digest)?;
        if let Ok(mut cache) = self.deletion_previews.try_borrow_mut() {
            cache.insert(canonical_id, preview.clone());
        }
        Ok(preview)
    }

    pub fn begin_deletion(
        &mut self,
        operation_id: String,
        preview: &WorkspaceDeletionPreview,
        digest: &dyn Sha256Port,
        now_utc_ms: u64,
    ) -> Result<WorkspaceDeleteRequest, WorkspaceStoreError> {
        // The runtime has no timer, and the only moment the answer matters is
        // the moment something asks.
        self.sweep_expired(now_utc_ms);
        if let Some(pending) = self.pending_deletions.get(&operation_id) {
            return pending
                .matches(preview)
                .then(|| pending.request(operation_id))
                .ok_or(WorkspaceStoreError::OperationAlreadyPending);
        }
        if self.pending.contains_key(&operation_id) {
            return Err(WorkspaceStoreError::OperationAlreadyPending);
        }
        if self
            .pending
            .len()
            .saturating_add(self.pending_deletions.len())
            >= MAX_PENDING_MUTATIONS
        {
            return Err(WorkspaceStoreError::TooManyPendingMutations);
        }
        let parsed = WorkspaceId::parse(&preview.workspace_id)
            .map_err(|_| WorkspaceStoreError::InvalidIdentifier)?;
        let workspace_id = parsed.to_text();
        if self.has_pending_workspace(&workspace_id) {
            return Err(WorkspaceStoreError::OperationAlreadyPending);
        }
        let snapshot = self
            .snapshots
            .get(&workspace_id)
            .ok_or(WorkspaceStoreError::UnknownWorkspace)?;
        let current = self
            .deletion_previews
            .try_borrow()
            .ok()
            .and_then(|cache| cache.get(&workspace_id).cloned())
            .map_or_else(|| self.preview_for(snapshot, digest), Ok)?;
        if &current != preview {
            return Err(WorkspaceStoreError::InvalidConfirmation);
        }
        let resulting_revision = preview
            .expected_revision
            .checked_add(1)
            .ok_or(WorkspaceStoreError::RevisionOverflow)?;
        let pending = PendingWorkspaceDeletion {
            workspace_id,
            expected_revision: preview.expected_revision,
            resulting_revision,
            counts: preview.counts,
            confirmation_token: preview.confirmation_token.clone(),
            begun_at_utc_ms: now_utc_ms,
        };
        let request = pending.request(operation_id.clone());
        self.pending_deletions.insert(operation_id, pending);
        Ok(request)
    }

    pub fn begin_discard(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        digest: &dyn Sha256Port,
        now_utc_ms: u64,
    ) -> Result<WorkspaceDeleteRequest, WorkspaceStoreError> {
        self.sweep_expired(now_utc_ms);
        let parsed =
            WorkspaceId::parse(workspace_id).map_err(|_| WorkspaceStoreError::InvalidIdentifier)?;
        let canonical_id = parsed.to_text();
        let snapshot = self
            .snapshots
            .get(&canonical_id)
            .ok_or(WorkspaceStoreError::UnknownWorkspace)?;
        if snapshot.saved
            || !matches!(
                snapshot.phase,
                taffy_storage::workspace::WorkspacePhase::Done
                    | taffy_storage::workspace::WorkspacePhase::PartlyDone
                    | taffy_storage::workspace::WorkspacePhase::Stopped
                    | taffy_storage::workspace::WorkspacePhase::Failed
            )
        {
            return Err(WorkspaceStoreError::NotDiscardable);
        }
        if snapshot.revision != expected_revision {
            return Err(WorkspaceStoreError::StaleRevision);
        }
        if self.has_pending_workspace(&canonical_id) {
            return Err(WorkspaceStoreError::OperationAlreadyPending);
        }
        let preview = self.preview_for(snapshot, digest)?;
        self.begin_deletion(operation_id, &preview, digest, now_utc_ms)
    }

    pub fn complete_deletion(
        &mut self,
        operation_id: &str,
        committed_revision: u64,
    ) -> Result<(), WorkspaceStoreError> {
        let pending = self
            .pending_deletions
            .get(operation_id)
            .ok_or(WorkspaceStoreError::WrongCompletion)?;
        if pending.resulting_revision != committed_revision
            || self
                .snapshots
                .get(&pending.workspace_id)
                .is_none_or(|snapshot| snapshot.revision != pending.expected_revision)
        {
            return Err(WorkspaceStoreError::WrongCompletion);
        }
        let workspace_id = pending.workspace_id.clone();
        self.pending_deletions.remove(operation_id);
        self.snapshots.remove(&workspace_id);
        self.deletion_previews.get_mut().clear();
        self.prepared_export = None;
        self.latest_export = None;
        Ok(())
    }

    pub fn reject_deletion(&mut self, operation_id: &str) -> bool {
        self.pending_deletions.remove(operation_id).is_some()
    }

    fn preview_for(
        &self,
        snapshot: &WorkspaceSnapshot,
        digest: &dyn Sha256Port,
    ) -> Result<WorkspaceDeletionPreview, WorkspaceStoreError> {
        let workspace_id = snapshot.workspace_id.to_text();
        let counts = self.deletion_counts(&workspace_id, snapshot)?;
        let input = self.deletion_binding(&workspace_id, snapshot, counts)?;
        let token = digest
            .sha256(&input)
            .map_err(|_| WorkspaceStoreError::DigestUnavailable)?;
        Ok(WorkspaceDeletionPreview {
            workspace_id,
            expected_revision: snapshot.revision,
            counts,
            confirmation_token: lower_hex(&token),
        })
    }

    fn deletion_counts(
        &self,
        workspace_id: &str,
        snapshot: &WorkspaceSnapshot,
    ) -> Result<WorkspaceDeletionCounts, WorkspaceStoreError> {
        let artifact_metadata = usize::from(
            self.prepared_export
                .as_ref()
                .is_some_and(|artifact| artifact.belongs_to(workspace_id)),
        ) + usize::from(
            self.latest_export
                .as_ref()
                .is_some_and(|artifact| artifact.workspace_id == workspace_id),
        );
        Ok(WorkspaceDeletionCounts {
            sources: u32::try_from(snapshot.sources.len())
                .map_err(|_| WorkspaceStoreError::ProjectionOverflow)?,
            facts: u32::try_from(snapshot.facts.len())
                .map_err(|_| WorkspaceStoreError::ProjectionOverflow)?,
            artifact_metadata: u32::try_from(artifact_metadata)
                .map_err(|_| WorkspaceStoreError::ProjectionOverflow)?,
            // The shipping workspace plane has no independent index table yet.
            derived_indexes: 0,
        })
    }

    fn deletion_binding(
        &self,
        workspace_id: &str,
        snapshot: &WorkspaceSnapshot,
        counts: WorkspaceDeletionCounts,
    ) -> Result<Vec<u8>, WorkspaceStoreError> {
        let encoded =
            encode_snapshot(snapshot).map_err(|_| WorkspaceStoreError::InvalidSnapshot)?;
        let prepared = self
            .prepared_export
            .as_ref()
            .filter(|artifact| artifact.belongs_to(workspace_id))
            .map_or_else(Vec::new, PreparedWorkspaceArtifact::confirmation_bytes);
        let latest = self
            .latest_export
            .as_ref()
            .filter(|artifact| artifact.workspace_id == workspace_id)
            .map_or_else(Vec::new, latest_confirmation_bytes);
        let revision = snapshot.revision.to_be_bytes();
        let count_bytes = [
            counts.sources,
            counts.facts,
            counts.artifact_metadata,
            counts.derived_indexes,
        ]
        .into_iter()
        .flat_map(u32::to_be_bytes)
        .collect::<Vec<_>>();
        Ok(framed(
            DELETE_CONFIRMATION_DOMAIN,
            &[
                workspace_id.as_bytes(),
                &revision,
                &count_bytes,
                &encoded,
                &prepared,
                &latest,
            ],
        ))
    }

    /// Forgets staged deletions older than [`PENDING_DELETION_HORIZON_MS`].
    ///
    /// `saturating_sub` so a clock that went backwards reads as not-expired: a
    /// sweep that fires on a bad clock would discard a live operation, which is
    /// a worse failure than the one this exists to end.
    fn sweep_expired(&mut self, now_utc_ms: u64) {
        let horizon = now_utc_ms.saturating_sub(PENDING_DELETION_HORIZON_MS);
        if horizon == 0 {
            return;
        }
        self.pending_deletions
            .retain(|_, pending| pending.begun_at_utc_ms > horizon);
    }

    fn has_pending_workspace(&self, workspace_id: &str) -> bool {
        self.pending
            .values()
            .any(|snapshot| snapshot.workspace_id.to_text() == workspace_id)
            || self
                .pending_deletions
                .values()
                .any(|pending| pending.workspace_id == workspace_id)
    }
}

impl PendingWorkspaceDeletion {
    fn matches(&self, preview: &WorkspaceDeletionPreview) -> bool {
        self.workspace_id == preview.workspace_id
            && self.expected_revision == preview.expected_revision
            && self.counts == preview.counts
            && self.confirmation_token == preview.confirmation_token
    }

    fn request(&self, operation_id: String) -> WorkspaceDeleteRequest {
        WorkspaceDeleteRequest {
            operation_id,
            workspace_id: self.workspace_id.clone(),
            expected_revision: self.expected_revision,
            resulting_revision: self.resulting_revision,
            counts: self.counts,
            confirmation_token: self.confirmation_token.clone(),
        }
    }
}

fn latest_confirmation_bytes(value: &core_api_types::WorkspaceExportView) -> Vec<u8> {
    let revision = value.revision.to_be_bytes();
    let format = [match value.format {
        core_api_types::WorkspaceExportFormat::Markdown => 0,
        core_api_types::WorkspaceExportFormat::Csv => 1,
    }];
    framed(
        b"latest-export",
        &[
            value.request_id.as_bytes(),
            value.workspace_id.as_bytes(),
            &revision,
            &format,
            value.content.as_bytes(),
        ],
    )
}

fn framed(domain: &[u8], values: &[&[u8]]) -> Vec<u8> {
    let mut bytes = Vec::new();
    bytes.extend_from_slice(domain);
    for value in values {
        bytes.extend_from_slice(&u64::try_from(value.len()).unwrap_or(u64::MAX).to_be_bytes());
        bytes.extend_from_slice(value);
    }
    bytes
}

fn lower_hex(value: &[u8; 32]) -> String {
    let mut output = String::with_capacity(64);
    for byte in value {
        output.push(char::from_digit(u32::from(byte >> 4), 16).unwrap_or('0'));
        output.push(char::from_digit(u32::from(byte & 0x0f), 16).unwrap_or('0'));
    }
    output
}
