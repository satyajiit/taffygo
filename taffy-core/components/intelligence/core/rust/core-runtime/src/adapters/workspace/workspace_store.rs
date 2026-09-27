// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Ordered in-memory workspace state backed only by browser-owned persistence.

use std::cell::RefCell;
use std::collections::BTreeMap;

use core_api_types::{WorkspaceExportView, MAX_WORKSPACES};
use taffy_storage::ids::{FactId, SourceId, WorkspaceId};
use taffy_storage::workspace::{
    encode_snapshot, FactKind, WorkspaceFact, WorkspaceMutation, WorkspacePageFactReplacement,
    WorkspacePhase, WorkspaceSnapshot,
};

use crate::ports::{
    WorkspaceDeletionPreview, WorkspacePageFact, WorkspacePersistRequest, WorkspaceStoreError,
};

mod lifecycle;
mod projection;
mod restore;

const MAX_PENDING_MUTATIONS: usize = 64;
const MAX_RESTORE_BYTES: usize = 4_194_304;

/// Ordered in-memory workspace state whose durability the browser owns.
///
/// This is the domain half; [`ProductionWorkspaces`](super::ProductionWorkspaces)
/// is the adapter that installs it behind [`WorkspacePort`](crate::ports::WorkspacePort).
#[derive(Default)]
pub struct WorkspaceStore {
    snapshots: BTreeMap<String, WorkspaceSnapshot>,
    pending: BTreeMap<String, WorkspaceSnapshot>,
    pending_deletions: BTreeMap<String, lifecycle::PendingWorkspaceDeletion>,
    deletion_previews: RefCell<BTreeMap<String, WorkspaceDeletionPreview>>,
    prepared_export: Option<super::workspace_export::PreparedWorkspaceArtifact>,
    latest_export: Option<WorkspaceExportView>,
}

impl core::fmt::Debug for WorkspaceStore {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        let deletion_preview_count = self
            .deletion_previews
            .try_borrow()
            .map_or(0, |previews| previews.len());
        formatter
            .debug_struct("WorkspaceStore")
            .field("workspace_count", &self.snapshots.len())
            .field("pending_count", &self.pending.len())
            .field("pending_deletion_count", &self.pending_deletions.len())
            .field("deletion_previews", &deletion_preview_count)
            .field("has_prepared_export", &self.prepared_export.is_some())
            .field("has_latest_export", &self.latest_export.is_some())
            .finish()
    }
}

impl WorkspaceStore {
    pub const fn new() -> Self {
        Self {
            snapshots: BTreeMap::new(),
            pending: BTreeMap::new(),
            pending_deletions: BTreeMap::new(),
            deletion_previews: RefCell::new(BTreeMap::new()),
            prepared_export: None,
            latest_export: None,
        }
    }

    pub fn begin_creation(
        &mut self,
        operation_id: String,
        snapshot: WorkspaceSnapshot,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        let workspace_id = snapshot.workspace_id.to_text();
        if snapshot.revision != 1 || !snapshot.validate() {
            return Err(WorkspaceStoreError::InvalidSnapshot);
        }
        if self.snapshots.contains_key(&workspace_id) {
            return Err(WorkspaceStoreError::DuplicateWorkspace);
        }
        let pending_new = self
            .pending
            .values()
            .filter(|value| !self.snapshots.contains_key(&value.workspace_id.to_text()))
            .count();
        if self
            .snapshots
            .len()
            .saturating_add(pending_new)
            .saturating_add(1)
            > MAX_WORKSPACES
        {
            return Err(WorkspaceStoreError::TooManyWorkspaces);
        }
        self.stage_pending(operation_id, snapshot, 0)
    }

    pub fn begin_page_ingestion(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        source_id: &str,
        facts: Vec<WorkspacePageFact>,
        updated_at_epoch_ms: u64,
    ) -> Result<Option<WorkspacePersistRequest>, WorkspaceStoreError> {
        let phase = self
            .snapshot(workspace_id)
            .map(|snapshot| snapshot.phase)
            .ok_or(WorkspaceStoreError::UnknownWorkspace)?;
        self.begin_task_update(
            operation_id,
            workspace_id,
            phase,
            Some((source_id, facts)),
            None,
            updated_at_epoch_ms,
        )
    }

    /// Refuses a task transition before its reducer mutates when another
    /// revision of the same workspace is already in flight.
    pub fn preflight_task_update(&self, workspace_id: &str) -> Result<(), WorkspaceStoreError> {
        let parsed =
            WorkspaceId::parse(workspace_id).map_err(|_| WorkspaceStoreError::InvalidIdentifier)?;
        let canonical = parsed.to_text();
        if !self.snapshots.contains_key(&canonical) {
            return Err(WorkspaceStoreError::UnknownWorkspace);
        }
        if self
            .pending
            .values()
            .any(|pending| pending.workspace_id == parsed)
            || self
                .pending_deletions
                .values()
                .any(|pending| pending.workspace_id == canonical)
        {
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
        Ok(())
    }

    /// Stages the task's post-transition phase and optional page facts as one
    /// workspace revision. No write is produced when neither changed.
    pub fn begin_task_update(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        phase: WorkspacePhase,
        page_facts: Option<(&str, Vec<WorkspacePageFact>)>,
        observed_source: Option<taffy_storage::workspace::WorkspaceSource>,
        updated_at_epoch_ms: u64,
    ) -> Result<Option<WorkspacePersistRequest>, WorkspaceStoreError> {
        let workspace_id =
            WorkspaceId::parse(workspace_id).map_err(|_| WorkspaceStoreError::InvalidIdentifier)?;
        let current = self
            .snapshots
            .get(&workspace_id.to_text())
            .ok_or(WorkspaceStoreError::UnknownWorkspace)?;
        if let Some(source) = observed_source.as_ref() {
            let exact_capture = page_facts
                .as_ref()
                .is_some_and(|(id, _)| SourceId::parse(id).ok() == Some(source.source_id));
            if !exact_capture
                || source.excluded
                || source.read_at_epoch_ms != 0
                || current.source(source.source_id).is_some_and(|held| {
                    held.host != source.host || held.canonical_locator != source.canonical_locator
                })
            {
                return Err(WorkspaceStoreError::InvalidSnapshot);
            }
        }
        let page_facts = page_facts
            .map(|(source_id, facts)| {
                let source_id = SourceId::parse(source_id)
                    .map_err(|_| WorkspaceStoreError::InvalidIdentifier)?;
                if facts.is_empty() {
                    return Ok(None);
                }
                let source = current
                    .source(source_id)
                    .or_else(|| {
                        observed_source
                            .as_ref()
                            .filter(|source| source.source_id == source_id)
                    })
                    .ok_or(WorkspaceStoreError::Mutation(
                        taffy_storage::workspace::MutationError::UnknownSource,
                    ))?;
                if source.excluded {
                    return Ok(None);
                }
                let scope = facts
                    .first()
                    .map(WorkspacePageFact::scope)
                    .ok_or(WorkspaceStoreError::InvalidSnapshot)?;
                if facts.iter().any(|fact| fact.scope() != scope) {
                    return Err(WorkspaceStoreError::InvalidSnapshot);
                }
                let facts = facts
                    .into_iter()
                    .map(|fact| {
                        Ok(WorkspaceFact {
                            fact_id: FactId::parse(&fact.fact_id)
                                .map_err(|_| WorkspaceStoreError::InvalidIdentifier)?,
                            field: fact.field,
                            value: fact.value,
                            kind: FactKind::FromPage,
                            sources: vec![source_id],
                            correction: None,
                            has_conflict: false,
                            media_provenance: fact.media_provenance,
                        })
                    })
                    .collect::<Result<Vec<_>, WorkspaceStoreError>>()?;
                Ok(Some(WorkspacePageFactReplacement {
                    source_id,
                    scope,
                    facts,
                }))
            })
            .transpose()?
            .flatten();
        let source = observed_source.filter(|source| current.source(source.source_id).is_none());
        if current.phase == phase && page_facts.is_none() && source.is_none() {
            return Ok(None);
        }
        let expected_revision = current.revision;
        let next = current
            .apply(
                expected_revision,
                updated_at_epoch_ms,
                WorkspaceMutation::SyncTask {
                    phase,
                    page_facts,
                    source,
                },
            )
            .map_err(WorkspaceStoreError::Mutation)?;
        self.stage_pending(operation_id, next, expected_revision)
            .map(Some)
    }

    pub fn begin_correction(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        fact_id: &str,
        value: String,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        let fact_id = FactId::parse(fact_id).map_err(|_| WorkspaceStoreError::InvalidIdentifier)?;
        self.begin_mutation(
            operation_id,
            workspace_id,
            expected_revision,
            updated_at_epoch_ms,
            WorkspaceMutation::CorrectFact { fact_id, value },
        )
    }

    pub fn begin_save(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.begin_mutation(
            operation_id,
            workspace_id,
            expected_revision,
            updated_at_epoch_ms,
            WorkspaceMutation::Save,
        )
    }

    pub fn begin_exclusion(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        source_id: &str,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        let source_id =
            SourceId::parse(source_id).map_err(|_| WorkspaceStoreError::InvalidIdentifier)?;
        self.begin_mutation(
            operation_id,
            workspace_id,
            expected_revision,
            updated_at_epoch_ms,
            WorkspaceMutation::ExcludeSource { source_id },
        )
    }

    fn begin_mutation(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        updated_at_epoch_ms: u64,
        mutation: WorkspaceMutation,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        let parsed =
            WorkspaceId::parse(workspace_id).map_err(|_| WorkspaceStoreError::InvalidIdentifier)?;
        let current = self
            .snapshots
            .get(&parsed.to_text())
            .ok_or(WorkspaceStoreError::UnknownWorkspace)?;
        let next = current
            .apply(expected_revision, updated_at_epoch_ms, mutation)
            .map_err(WorkspaceStoreError::Mutation)?;
        self.stage_pending(operation_id, next, expected_revision)
    }

    fn stage_pending(
        &mut self,
        operation_id: String,
        next: WorkspaceSnapshot,
        expected_revision: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        if self.pending.contains_key(&operation_id)
            || self.pending_deletions.contains_key(&operation_id)
            || self
                .pending
                .values()
                .any(|pending| pending.workspace_id == next.workspace_id)
            || self
                .pending_deletions
                .values()
                .any(|pending| pending.workspace_id == next.workspace_id.to_text())
        {
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
        let snapshot = encode_snapshot(&next).map_err(|_| WorkspaceStoreError::InvalidSnapshot)?;
        let workspace_id = next.workspace_id.to_text();
        let request = WorkspacePersistRequest {
            operation_id: operation_id.clone(),
            workspace_id,
            expected_revision,
            resulting_revision: next.revision,
            snapshot,
        };
        self.pending.insert(operation_id, next);
        Ok(request)
    }

    pub fn complete_persist(
        &mut self,
        operation_id: &str,
        committed_revision: u64,
    ) -> Result<(), WorkspaceStoreError> {
        let pending = self
            .pending
            .get(operation_id)
            .ok_or(WorkspaceStoreError::WrongCompletion)?;
        if pending.revision != committed_revision {
            return Err(WorkspaceStoreError::WrongCompletion);
        }
        let pending = self
            .pending
            .remove(operation_id)
            .ok_or(WorkspaceStoreError::WrongCompletion)?;
        self.snapshots
            .insert(pending.workspace_id.to_text(), pending);
        self.deletion_previews.get_mut().clear();
        self.prepared_export = None;
        self.latest_export = None;
        Ok(())
    }

    pub fn reject_persist(&mut self, operation_id: &str) -> bool {
        self.pending.remove(operation_id).is_some()
    }

    pub fn latest_export(&self) -> Option<WorkspaceExportView> {
        self.latest_export.clone()
    }

    pub(crate) fn snapshot(&self, workspace_id: &str) -> Option<&WorkspaceSnapshot> {
        WorkspaceId::parse(workspace_id)
            .ok()
            .map(WorkspaceId::to_text)
            .filter(|id| {
                !self
                    .pending_deletions
                    .values()
                    .any(|pending| pending.workspace_id.as_str() == id.as_str())
            })
            .and_then(|id| self.snapshots.get(&id))
    }

    pub(crate) fn set_latest_export(&mut self, value: WorkspaceExportView) {
        self.latest_export = Some(value);
        self.deletion_previews.get_mut().clear();
    }

    pub(crate) fn prepared_export(
        &self,
    ) -> Option<&super::workspace_export::PreparedWorkspaceArtifact> {
        self.prepared_export.as_ref()
    }

    pub(crate) fn set_prepared_export(
        &mut self,
        value: super::workspace_export::PreparedWorkspaceArtifact,
    ) {
        self.prepared_export = Some(value);
        self.deletion_previews.get_mut().clear();
    }
}
