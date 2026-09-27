// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The durable Library composition port.
//!
//! The browser owns physical rows and indexes. This port owns the complete
//! resident aggregate, exact revisions, explicit saved-fact promotion, and
//! deterministic retrieval and export.

use core_api_types::{LibraryExportView, LibraryViewState, WorkspaceExportFormat};
use core_service_types::LibraryEntryRecord;
use taffy_storage::library::{LibraryRefreshPreview, LibraryRefreshResult};
use taffy_storage::workspace::WorkspaceSnapshot;
use task_engine::TaskLibrarySearchTranscriptOutcome;

use crate::account::Sha256Port;

/// A staged entry upsert awaiting one browser-owned atomic transaction.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibrarySaveRequest {
    pub operation_id: String,
    pub expected_library_revision: u64,
    pub resulting_library_revision: u64,
    pub expected_entry_revision: u64,
    pub entry: LibraryEntryRecord,
}

/// A staged content-removing mutation awaiting browser durability.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRemoveRequest {
    pub operation_id: String,
    pub expected_library_revision: u64,
    pub resulting_library_revision: u64,
    pub entry_id: String,
    pub expected_entry_revision: u64,
    pub resulting_entry_revision: u64,
    pub removed_at_epoch_ms: u64,
}

/// Closed Library-plane failures.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LibraryStoreError {
    PrivateProfile,
    InvalidIdentifier,
    InvalidEntry,
    InvalidQuery,
    UnknownEntry,
    UnknownWorkspace,
    WorkspaceNotSaved,
    FactNotFound,
    UncitedFact,
    StaleLibraryRevision,
    StaleEntryRevision,
    StaleWorkspaceRevision,
    OperationAlreadyPending,
    TooManyEntries,
    TooManyPendingMutations,
    DigestUnavailable,
    WrongCompletion,
    ExportOverflow,
    RefreshCollectionNotFound,
    SourceNotRefreshable,
    InvalidRefreshPreview,
    InvalidRefreshResult,
}

/// One ordered, complete Library aggregate.
pub trait LibraryPort {
    /// Atomically restores the complete browser-owned state.
    fn restore(
        &mut self,
        revision: u64,
        entries: Vec<LibraryEntryRecord>,
    ) -> Result<(), LibraryStoreError>;

    /// Runs and publishes one bounded deterministic resident search.
    fn search(
        &mut self,
        request_id: &str,
        query: &str,
        limit: u32,
        requested_at_epoch_ms: u64,
    ) -> Result<TaskLibrarySearchTranscriptOutcome, LibraryStoreError>;

    /// Stages explicit promotion of one fact from one exact saved workspace.
    /// `None` means the same fact is already the exact current kept value.
    #[allow(clippy::too_many_arguments)]
    fn begin_save(
        &mut self,
        operation_id: String,
        workspace: &WorkspaceSnapshot,
        expected_workspace_revision: u64,
        fact_id: &str,
        expected_library_revision: u64,
        expected_entry_revision: u64,
        approved_at_epoch_ms: u64,
        digest: &dyn Sha256Port,
    ) -> Result<Option<LibrarySaveRequest>, LibraryStoreError>;

    /// Stages exact-revision deletion of one entry and its derived state.
    fn begin_remove(
        &mut self,
        operation_id: String,
        entry_id: &str,
        expected_library_revision: u64,
        expected_entry_revision: u64,
        removed_at_epoch_ms: u64,
    ) -> Result<LibraryRemoveRequest, LibraryStoreError>;

    /// Makes one staged mutation visible after exact browser durability.
    fn complete(
        &mut self,
        operation_id: &str,
        committed_library_revision: u64,
    ) -> Result<(), LibraryStoreError>;

    /// Drops one staged mutation after terminal refusal or ambiguity.
    fn reject(&mut self, operation_id: &str) -> bool;

    /// Publishes one bounded exact-revision deterministic export.
    fn request_export(
        &mut self,
        request_id: &str,
        expected_library_revision: u64,
        collection_id: Option<&str>,
        format: WorkspaceExportFormat,
    ) -> Result<LibraryExportView, LibraryStoreError>;

    /// Computes an exact pre-egress refresh plan from resident state.
    fn preview_refresh(
        &self,
        collection_id: &str,
        expected_library_revision: u64,
        workspace: &WorkspaceSnapshot,
        expected_workspace_revision: u64,
        digest: &dyn Sha256Port,
    ) -> Result<LibraryRefreshPreview, LibraryStoreError>;

    /// Compares one terminal refresh workspace with the still-current
    /// approved preview without mutating or recreating the original.
    fn compare_refresh(
        &self,
        preview: &LibraryRefreshPreview,
        original: &WorkspaceSnapshot,
        refreshed: &WorkspaceSnapshot,
        digest: &dyn Sha256Port,
    ) -> Result<LibraryRefreshResult, LibraryStoreError>;

    /// Complete generated state for the platform surface.
    fn project_core_api(&self) -> LibraryViewState;

    /// Latest export, cleared by every committed mutation.
    fn latest_export(&self) -> Option<LibraryExportView>;
}

impl<T> LibraryPort for Box<T>
where
    T: LibraryPort + ?Sized,
{
    fn restore(
        &mut self,
        revision: u64,
        entries: Vec<LibraryEntryRecord>,
    ) -> Result<(), LibraryStoreError> {
        self.as_mut().restore(revision, entries)
    }

    fn search(
        &mut self,
        request_id: &str,
        query: &str,
        limit: u32,
        requested_at_epoch_ms: u64,
    ) -> Result<TaskLibrarySearchTranscriptOutcome, LibraryStoreError> {
        self.as_mut()
            .search(request_id, query, limit, requested_at_epoch_ms)
    }

    fn begin_save(
        &mut self,
        operation_id: String,
        workspace: &WorkspaceSnapshot,
        expected_workspace_revision: u64,
        fact_id: &str,
        expected_library_revision: u64,
        expected_entry_revision: u64,
        approved_at_epoch_ms: u64,
        digest: &dyn Sha256Port,
    ) -> Result<Option<LibrarySaveRequest>, LibraryStoreError> {
        self.as_mut().begin_save(
            operation_id,
            workspace,
            expected_workspace_revision,
            fact_id,
            expected_library_revision,
            expected_entry_revision,
            approved_at_epoch_ms,
            digest,
        )
    }

    fn begin_remove(
        &mut self,
        operation_id: String,
        entry_id: &str,
        expected_library_revision: u64,
        expected_entry_revision: u64,
        removed_at_epoch_ms: u64,
    ) -> Result<LibraryRemoveRequest, LibraryStoreError> {
        self.as_mut().begin_remove(
            operation_id,
            entry_id,
            expected_library_revision,
            expected_entry_revision,
            removed_at_epoch_ms,
        )
    }

    fn complete(
        &mut self,
        operation_id: &str,
        committed_library_revision: u64,
    ) -> Result<(), LibraryStoreError> {
        self.as_mut()
            .complete(operation_id, committed_library_revision)
    }

    fn reject(&mut self, operation_id: &str) -> bool {
        self.as_mut().reject(operation_id)
    }

    fn request_export(
        &mut self,
        request_id: &str,
        expected_library_revision: u64,
        collection_id: Option<&str>,
        format: WorkspaceExportFormat,
    ) -> Result<LibraryExportView, LibraryStoreError> {
        self.as_mut()
            .request_export(request_id, expected_library_revision, collection_id, format)
    }

    fn project_core_api(&self) -> LibraryViewState {
        self.as_ref().project_core_api()
    }

    fn preview_refresh(
        &self,
        collection_id: &str,
        expected_library_revision: u64,
        workspace: &WorkspaceSnapshot,
        expected_workspace_revision: u64,
        digest: &dyn Sha256Port,
    ) -> Result<LibraryRefreshPreview, LibraryStoreError> {
        self.as_ref().preview_refresh(
            collection_id,
            expected_library_revision,
            workspace,
            expected_workspace_revision,
            digest,
        )
    }

    fn compare_refresh(
        &self,
        preview: &LibraryRefreshPreview,
        original: &WorkspaceSnapshot,
        refreshed: &WorkspaceSnapshot,
        digest: &dyn Sha256Port,
    ) -> Result<LibraryRefreshResult, LibraryStoreError> {
        self.as_ref()
            .compare_refresh(preview, original, refreshed, digest)
    }

    fn latest_export(&self) -> Option<LibraryExportView> {
        self.as_ref().latest_export()
    }
}
