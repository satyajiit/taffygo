// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Ordered Library orchestration across workspace facts and the Library port.

mod task_search;

use bip_types::identity::{ActionId, TaskId};
use core_api_types::{LibraryExportView, WorkspaceExportFormat};
use core_service_types::LibraryEntryRecord;
use task_engine::action::{ActionIntent, LibraryIntent};
use task_engine::{ActionState, TaskLibrarySearchTranscriptOutcome};

use crate::ports::{
    LibraryRemoveRequest, LibrarySaveRequest, LibraryStoreError, WorkspaceStoreError,
};

pub(super) use self::task_search::PendingTaskLibrarySearchSettlement;
use super::CoreRuntime;

/// One authorized Library action after exact reducer/runtime validation.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum TaskLibraryExecution {
    /// A bounded search ran and its result is now in the Library projection.
    Search(TaskLibrarySearchTranscriptOutcome),
    /// The exact saved fact was already the current entry.
    AlreadyCurrent,
    /// Browser storage must atomically persist this entry and revision.
    Save(Box<LibrarySaveRequest>),
    /// Browser storage must atomically remove this entry and derived state.
    Remove(LibraryRemoveRequest),
}

/// Why a durable Library effect could not be executed as authored.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskLibraryExecutionError {
    UnknownTask,
    UnknownAction,
    WrongActionState,
    WrongIntent,
    MissingResidentOperand,
    Store(LibraryStoreError),
}

impl CoreRuntime {
    /// Replaces a metadata-only approved refresh request with the exact current
    /// resident manifest before task decoding. The browser never supplies or
    /// learns canonical locators through the UI contract.
    pub fn prepare_library_refresh_start(
        &self,
        start: &mut core_service_types::StartTaskCommand,
    ) -> Result<(), LibraryStoreError> {
        let Some(request) = start.library_refresh.as_mut() else {
            return Ok(());
        };
        if !request.sources.is_empty() {
            return Err(LibraryStoreError::InvalidRefreshPreview);
        }
        let workspace = self
            .components
            .workspaces
            .reopen_workspace(&request.collection_id, request.source_workspace_revision)
            .map_err(map_workspace_error)?;
        let preview = self.components.library.preview_refresh(
            &request.collection_id,
            request.library_revision,
            &workspace,
            request.source_workspace_revision,
            self.components.digest.as_ref(),
        )?;
        if preview.preview_id != request.preview_id
            || preview.collection_id.to_text() != request.collection_id
        {
            return Err(LibraryStoreError::InvalidRefreshPreview);
        }
        request.sources = preview
            .sources
            .into_iter()
            .map(|source| core_service_types::LibraryRefreshSource {
                source_id: source.source_id.to_text(),
                title: source.title,
                host: source.host,
                canonical_locator: source.canonical_locator,
                original_content_digest: source.original_content_digest,
            })
            .collect();
        Ok(())
    }

    /// Restores the complete browser-owned Library before commands are admitted.
    pub fn restore_library(
        &mut self,
        revision: u64,
        entries: Vec<LibraryEntryRecord>,
    ) -> Result<(), LibraryStoreError> {
        self.components.library.restore(revision, entries)
    }

    /// Executes one reducer-authorized Library tool against the profile
    /// aggregate. Writes are staged and remain invisible until the browser's
    /// exact-revision storage completion is delivered.
    pub fn begin_task_library_tool(
        &mut self,
        task_id: &TaskId,
        action_id: &ActionId,
        operation_id: String,
        now_utc_millis: u64,
    ) -> Result<TaskLibraryExecution, TaskLibraryExecutionError> {
        let facts = self
            .tasks
            .get(task_id.as_str())
            .ok_or(TaskLibraryExecutionError::UnknownTask)?
            .task
            .action_effect_facts(action_id)
            .ok_or(TaskLibraryExecutionError::UnknownAction)?;
        if facts.state != ActionState::Dispatching || facts.dispatch_id.is_none() {
            return Err(TaskLibraryExecutionError::WrongActionState);
        }
        let ActionIntent::Library(intent) = facts.proposal.intent().clone() else {
            return Err(TaskLibraryExecutionError::WrongIntent);
        };
        let library_revision = self.components.library.project_core_api().revision;
        match intent {
            LibraryIntent::Search { limit, .. } => {
                let query = self
                    .transient_library_query(task_id.as_str(), &facts.proposal)
                    .ok_or(TaskLibraryExecutionError::MissingResidentOperand)?
                    .to_owned();
                let outcome = self
                    .search_library(action_id.as_str(), &query, limit, now_utc_millis)
                    .map_err(TaskLibraryExecutionError::Store)?;
                Ok(TaskLibraryExecution::Search(outcome))
            }
            LibraryIntent::Save {
                workspace_id,
                workspace_revision,
                fact_id,
                entry_revision,
                ..
            } => self
                .begin_library_save(
                    operation_id,
                    &workspace_id,
                    workspace_revision,
                    &fact_id,
                    library_revision,
                    entry_revision,
                    now_utc_millis,
                )
                .map(|request| {
                    request.map_or(TaskLibraryExecution::AlreadyCurrent, |value| {
                        TaskLibraryExecution::Save(Box::new(value))
                    })
                })
                .map_err(TaskLibraryExecutionError::Store),
            LibraryIntent::Remove {
                entry_id,
                entry_revision,
                ..
            } => self
                .begin_library_remove(
                    operation_id,
                    &entry_id,
                    library_revision,
                    entry_revision,
                    now_utc_millis,
                )
                .map(TaskLibraryExecution::Remove)
                .map_err(TaskLibraryExecutionError::Store),
        }
    }

    /// Runs one bounded deterministic search and publishes it in `CoreStatus`.
    pub fn search_library(
        &mut self,
        request_id: &str,
        query: &str,
        limit: u32,
        requested_at_epoch_ms: u64,
    ) -> Result<TaskLibrarySearchTranscriptOutcome, LibraryStoreError> {
        self.components
            .library
            .search(request_id, query, limit, requested_at_epoch_ms)
    }

    /// Stages explicit promotion of one cited fact from an exact saved workspace.
    #[allow(clippy::too_many_arguments)]
    pub fn begin_library_save(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_workspace_revision: u64,
        fact_id: &str,
        expected_library_revision: u64,
        expected_entry_revision: u64,
        approved_at_epoch_ms: u64,
    ) -> Result<Option<LibrarySaveRequest>, LibraryStoreError> {
        let workspace = self
            .components
            .workspaces
            .reopen_workspace(workspace_id, expected_workspace_revision)
            .map_err(map_workspace_error)?;
        let digest = self.components.digest.clone();
        self.components.library.begin_save(
            operation_id,
            &workspace,
            expected_workspace_revision,
            fact_id,
            expected_library_revision,
            expected_entry_revision,
            approved_at_epoch_ms,
            digest.as_ref(),
        )
    }

    /// Stages content-removing exact-revision deletion.
    pub fn begin_library_remove(
        &mut self,
        operation_id: String,
        entry_id: &str,
        expected_library_revision: u64,
        expected_entry_revision: u64,
        removed_at_epoch_ms: u64,
    ) -> Result<LibraryRemoveRequest, LibraryStoreError> {
        self.components.library.begin_remove(
            operation_id,
            entry_id,
            expected_library_revision,
            expected_entry_revision,
            removed_at_epoch_ms,
        )
    }

    /// Publishes one staged Library mutation after exact browser durability.
    pub fn complete_library_mutation(
        &mut self,
        operation_id: &str,
        committed_library_revision: u64,
    ) -> Result<(), LibraryStoreError> {
        self.components
            .library
            .complete(operation_id, committed_library_revision)
    }

    /// Rejects one staged mutation without changing visible state.
    pub fn reject_library_mutation(&mut self, operation_id: &str) -> bool {
        self.components.library.reject(operation_id)
    }

    /// Publishes one exact-revision deterministic Library export.
    pub fn request_library_export(
        &mut self,
        request_id: &str,
        expected_library_revision: u64,
        collection_id: Option<&str>,
        format: WorkspaceExportFormat,
    ) -> Result<LibraryExportView, LibraryStoreError> {
        self.components.library.request_export(
            request_id,
            expected_library_revision,
            collection_id,
            format,
        )
    }
}

const fn map_workspace_error(error: WorkspaceStoreError) -> LibraryStoreError {
    match error {
        WorkspaceStoreError::UnknownWorkspace => LibraryStoreError::UnknownWorkspace,
        WorkspaceStoreError::StaleRevision => LibraryStoreError::StaleWorkspaceRevision,
        WorkspaceStoreError::InvalidIdentifier => LibraryStoreError::InvalidIdentifier,
        _ => LibraryStoreError::InvalidEntry,
    }
}
