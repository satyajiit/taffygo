// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical workspace adapter over the ordered in-memory store.
//!
//! The adapter owns no connection and no file. It holds the domain store and
//! presents it as one composition port, so a profile runtime can be built with
//! a substitute workspace plane without a build feature.

use core_api_types::{WorkspaceExportFormat, WorkspaceExportView, WorkspaceViewState};

use crate::ports::{
    WorkspaceDeleteRequest, WorkspaceDeletionPreview, WorkspaceExportError, WorkspaceListEntry,
    WorkspacePageFact, WorkspacePersistRequest, WorkspacePort, WorkspaceStoreError,
};
use crate::Sha256Port;
use taffy_storage::workspace::WorkspaceSnapshot;

use super::workspace_store::WorkspaceStore;

/// The one production workspace port for a profile generation.
#[derive(Debug, Default)]
pub struct ProductionWorkspaces {
    store: WorkspaceStore,
}

impl ProductionWorkspaces {
    /// An empty workspace plane.
    pub const fn new() -> Self {
        Self {
            store: WorkspaceStore::new(),
        }
    }
}

impl WorkspacePort for ProductionWorkspaces {
    fn restore_encoded(&mut self, snapshots: &[Vec<u8>]) -> Result<(), WorkspaceStoreError> {
        self.store.restore_encoded(snapshots)
    }

    fn restore_bound_records(
        &mut self,
        records: &[(String, u64, Vec<u8>)],
    ) -> Result<(), WorkspaceStoreError> {
        self.store.restore_bound_records(records)
    }

    fn list_workspaces(&self) -> Vec<WorkspaceListEntry> {
        self.store.list_workspaces()
    }

    fn retained_snapshot(&self, workspace_id: &str) -> Option<&WorkspaceSnapshot> {
        self.store.snapshot(workspace_id)
    }

    fn reopen_workspace(
        &self,
        workspace_id: &str,
        expected_revision: u64,
    ) -> Result<WorkspaceSnapshot, WorkspaceStoreError> {
        self.store.reopen_workspace(workspace_id, expected_revision)
    }

    fn begin_creation(
        &mut self,
        operation_id: String,
        snapshot: WorkspaceSnapshot,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.store.begin_creation(operation_id, snapshot)
    }

    fn begin_page_ingestion(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        source_id: &str,
        facts: Vec<WorkspacePageFact>,
        updated_at_epoch_ms: u64,
    ) -> Result<Option<WorkspacePersistRequest>, WorkspaceStoreError> {
        self.store.begin_page_ingestion(
            operation_id,
            workspace_id,
            source_id,
            facts,
            updated_at_epoch_ms,
        )
    }

    fn preflight_task_update(&self, workspace_id: &str) -> Result<(), WorkspaceStoreError> {
        self.store.preflight_task_update(workspace_id)
    }

    fn begin_task_update(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        phase: taffy_storage::workspace::WorkspacePhase,
        page_facts: Option<(&str, Vec<WorkspacePageFact>)>,
        observed_source: Option<taffy_storage::workspace::WorkspaceSource>,
        updated_at_epoch_ms: u64,
    ) -> Result<Option<WorkspacePersistRequest>, WorkspaceStoreError> {
        self.store.begin_task_update(
            operation_id,
            workspace_id,
            phase,
            page_facts,
            observed_source,
            updated_at_epoch_ms,
        )
    }

    fn begin_save(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.store.begin_save(
            operation_id,
            workspace_id,
            expected_revision,
            updated_at_epoch_ms,
        )
    }

    fn begin_correction(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        fact_id: &str,
        value: String,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.store.begin_correction(
            operation_id,
            workspace_id,
            expected_revision,
            fact_id,
            value,
            updated_at_epoch_ms,
        )
    }

    fn begin_exclusion(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        source_id: &str,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.store.begin_exclusion(
            operation_id,
            workspace_id,
            expected_revision,
            source_id,
            updated_at_epoch_ms,
        )
    }

    fn begin_rename(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        display_name: String,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.store.begin_rename(
            operation_id,
            workspace_id,
            expected_revision,
            display_name,
            updated_at_epoch_ms,
        )
    }

    fn preview_deletion(
        &self,
        workspace_id: &str,
        digest: &dyn Sha256Port,
    ) -> Result<WorkspaceDeletionPreview, WorkspaceStoreError> {
        self.store.preview_deletion(workspace_id, digest)
    }

    fn begin_deletion(
        &mut self,
        operation_id: String,
        preview: &WorkspaceDeletionPreview,
        digest: &dyn Sha256Port,
        now_utc_ms: u64,
    ) -> Result<WorkspaceDeleteRequest, WorkspaceStoreError> {
        self.store
            .begin_deletion(operation_id, preview, digest, now_utc_ms)
    }

    fn begin_discard(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        digest: &dyn Sha256Port,
        now_utc_ms: u64,
    ) -> Result<WorkspaceDeleteRequest, WorkspaceStoreError> {
        self.store.begin_discard(
            operation_id,
            workspace_id,
            expected_revision,
            digest,
            now_utc_ms,
        )
    }

    fn complete_persist(
        &mut self,
        operation_id: &str,
        committed_revision: u64,
    ) -> Result<(), WorkspaceStoreError> {
        self.store
            .complete_persist(operation_id, committed_revision)
    }

    fn reject_persist(&mut self, operation_id: &str) -> bool {
        self.store.reject_persist(operation_id)
    }

    fn complete_deletion(
        &mut self,
        operation_id: &str,
        committed_revision: u64,
    ) -> Result<(), WorkspaceStoreError> {
        self.store
            .complete_deletion(operation_id, committed_revision)
    }

    fn reject_deletion(&mut self, operation_id: &str) -> bool {
        self.store.reject_deletion(operation_id)
    }

    fn request_export(
        &mut self,
        request_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<WorkspaceExportView, WorkspaceExportError> {
        self.store
            .request_export(request_id, workspace_id, expected_revision, format)
    }

    fn prepare_artifact(
        &mut self,
        artifact_id: &str,
        workspace_id: &str,
        format: WorkspaceExportFormat,
    ) -> Result<u64, WorkspaceExportError> {
        self.store
            .prepare_artifact(artifact_id, workspace_id, format)
    }

    fn ensure_artifact(
        &mut self,
        artifact_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<(), WorkspaceExportError> {
        self.store
            .ensure_artifact(artifact_id, workspace_id, expected_revision, format)
    }

    fn publish_artifact(
        &mut self,
        request_id: &str,
        artifact_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<WorkspaceExportView, WorkspaceExportError> {
        self.store.publish_artifact(
            request_id,
            artifact_id,
            workspace_id,
            expected_revision,
            format,
        )
    }

    fn project_core_api(&self) -> Result<Vec<WorkspaceViewState>, WorkspaceStoreError> {
        self.store.project_core_api()
    }

    fn latest_export(&self) -> Option<WorkspaceExportView> {
        self.store.latest_export()
    }
}
