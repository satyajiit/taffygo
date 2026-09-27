// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Ordered workspace commands on the profile runtime.

use core_api_types::{WorkspaceExportFormat, WorkspaceExportView};

use super::CoreRuntime;
use crate::ports::{
    WorkspaceDeleteRequest, WorkspaceDeletionPreview, WorkspaceExportError, WorkspaceListEntry,
    WorkspacePersistRequest, WorkspacePort, WorkspaceStoreError,
};
use taffy_storage::workspace::WorkspaceSnapshot;
use task_engine::{ArtifactCustody, ArtifactId, ArtifactKind};

impl CoreRuntime {
    /// Resolves the one trusted owner of an accepted artifact's export bytes.
    pub fn task_artifact_custody(
        &self,
        task_id: &task_engine::deps::TaskId,
        artifact_id: &ArtifactId,
    ) -> Option<ArtifactCustody> {
        self.task(task_id)?.artifact_custody(artifact_id)
    }

    /// Restores the complete browser-owned workspace snapshot set atomically.
    pub fn restore_workspace_snapshots(
        &mut self,
        snapshots: &[Vec<u8>],
    ) -> Result<(), WorkspaceStoreError> {
        self.components.workspaces.restore_encoded(snapshots)
    }

    /// Restores browser-indexed snapshots and refuses mismatched record metadata.
    pub fn restore_workspace_records(
        &mut self,
        records: &[(String, u64, Vec<u8>)],
    ) -> Result<(), WorkspaceStoreError> {
        self.components.workspaces.restore_bound_records(records)
    }

    /// Lists durable workspaces without exposing fact or source content.
    pub fn list_workspaces(&self) -> Vec<WorkspaceListEntry> {
        self.components.workspaces.list_workspaces()
    }

    /// Reopens one exact revision from the durable workspace list.
    pub fn reopen_workspace(
        &self,
        workspace_id: &str,
        expected_revision: u64,
    ) -> Result<WorkspaceSnapshot, WorkspaceStoreError> {
        self.components
            .workspaces
            .reopen_workspace(workspace_id, expected_revision)
    }

    /// Begins one correction without publishing it before physical commit.
    pub fn begin_workspace_correction(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        fact_id: &str,
        value: String,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.components.workspaces.begin_correction(
            operation_id,
            workspace_id,
            expected_revision,
            fact_id,
            value,
            updated_at_epoch_ms,
        )
    }

    /// Begins the explicit retention choice for a completed or partial draft.
    pub fn begin_workspace_save(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.components.workspaces.begin_save(
            operation_id,
            workspace_id,
            expected_revision,
            updated_at_epoch_ms,
        )
    }

    /// Begins one source exclusion without publishing it before commit.
    pub fn begin_workspace_exclusion(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        source_id: &str,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.components.workspaces.begin_exclusion(
            operation_id,
            workspace_id,
            expected_revision,
            source_id,
            updated_at_epoch_ms,
        )
    }

    /// Stages a display-name change while preserving the immutable task goal.
    pub fn begin_workspace_rename(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        display_name: String,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.components.workspaces.begin_rename(
            operation_id,
            workspace_id,
            expected_revision,
            display_name,
            updated_at_epoch_ms,
        )
    }

    /// Builds the content-free challenge shown before local deletion.
    pub fn preview_workspace_deletion(
        &self,
        workspace_id: &str,
    ) -> Result<WorkspaceDeletionPreview, WorkspaceStoreError> {
        self.components
            .workspaces
            .preview_deletion(workspace_id, self.components.digest.as_ref())
    }

    /// Stages deletion only when the exact current challenge was confirmed.
    pub fn begin_workspace_deletion(
        &mut self,
        operation_id: String,
        preview: &WorkspaceDeletionPreview,
        now_utc_ms: u64,
    ) -> Result<WorkspaceDeleteRequest, WorkspaceStoreError> {
        let digest = self.components.digest.clone();
        self.components.workspaces.begin_deletion(
            operation_id,
            preview,
            digest.as_ref(),
            now_utc_ms,
        )
    }

    /// Stages explicit cleanup only for one exact terminal unsaved workspace.
    pub fn begin_workspace_discard(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        now_utc_ms: u64,
    ) -> Result<WorkspaceDeleteRequest, WorkspaceStoreError> {
        let digest = self.components.digest.clone();
        self.components.workspaces.begin_discard(
            operation_id,
            workspace_id,
            expected_revision,
            digest.as_ref(),
            now_utc_ms,
        )
    }

    /// Makes one pending workspace revision visible after exact browser commit.
    pub fn complete_workspace_persist(
        &mut self,
        operation_id: &str,
        committed_revision: u64,
    ) -> Result<(), WorkspaceStoreError> {
        self.components
            .workspaces
            .complete_persist(operation_id, committed_revision)
    }

    /// Drops one pending workspace revision after a terminal refusal.
    pub fn reject_workspace_persist(&mut self, operation_id: &str) -> bool {
        self.components.workspaces.reject_persist(operation_id)
    }

    /// Publishes workspace absence only after the exact browser transaction commits.
    pub fn complete_workspace_deletion(
        &mut self,
        operation_id: &str,
        committed_revision: u64,
    ) -> Result<(), WorkspaceStoreError> {
        self.components
            .workspaces
            .complete_deletion(operation_id, committed_revision)
    }

    /// Rolls back a staged deletion after refusal, failure, or ambiguous completion.
    pub fn reject_workspace_deletion(&mut self, operation_id: &str) -> bool {
        self.components.workspaces.reject_deletion(operation_id)
    }

    /// Renders one immutable workspace revision through the Rust artifact renderer.
    pub fn request_workspace_export(
        &mut self,
        request_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<WorkspaceExportView, WorkspaceExportError> {
        self.components.workspaces.request_export(
            request_id,
            workspace_id,
            expected_revision,
            format,
        )
    }

    /// Prepares the current workspace revision without publishing bytes.
    pub fn prepare_task_artifact(
        &mut self,
        task_id: &task_engine::deps::TaskId,
        artifact_id: &str,
        format: WorkspaceExportFormat,
    ) -> Result<u64, WorkspaceExportError> {
        let workspace_id = self
            .task(task_id)
            .and_then(crate::ports::TaskEnginePort::workspace_id)
            .map(|workspace_id| (*workspace_id).to_text())
            .ok_or(WorkspaceExportError::Store(
                WorkspaceStoreError::UnknownWorkspace,
            ))?;
        self.components
            .workspaces
            .prepare_artifact(artifact_id, &workspace_id, format)
    }

    /// Generates and validates one rich artifact against the current task
    /// workspace, discards the bytes, and returns the exact revision that can
    /// reproduce them after durable acceptance.
    pub fn prepare_rich_task_artifact(
        &self,
        task_id: &task_engine::deps::TaskId,
        kind: ArtifactKind,
    ) -> Result<u64, WorkspaceExportError> {
        let snapshot = self.current_task_workspace(task_id)?;
        let format = crate::adapters::workspace::rich_artifact::format_for_artifact(kind)
            .ok_or(WorkspaceExportError::RenderRefused)?;
        crate::adapters::workspace::rich_artifact::generate_workspace_file(
            &snapshot,
            snapshot.revision,
            format,
        )?;
        Ok(snapshot.revision)
    }

    /// Revalidates one exact durable rich-artifact revision without retaining
    /// or publishing its bytes.
    pub fn ensure_rich_task_artifact(
        &self,
        task_id: &task_engine::deps::TaskId,
        workspace_revision: u64,
        kind: ArtifactKind,
    ) -> Result<(), WorkspaceExportError> {
        self.generate_rich_task_artifact(task_id, workspace_revision, kind)
            .map(|_| ())
    }

    /// Regenerates one exact accepted rich artifact for transient browser
    /// custody. The returned bytes never enter reducer or snapshot state.
    pub fn export_rich_task_artifact(
        &self,
        task_id: &task_engine::deps::TaskId,
        workspace_revision: u64,
        kind: ArtifactKind,
    ) -> Result<Vec<u8>, WorkspaceExportError> {
        self.generate_rich_task_artifact(task_id, workspace_revision, kind)
            .map(file_engine::GeneratedFile::into_bytes)
    }

    /// Ensures replay can reproduce one already committed artifact exactly.
    pub fn ensure_task_artifact(
        &mut self,
        task_id: &task_engine::deps::TaskId,
        artifact_id: &str,
        workspace_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<(), WorkspaceExportError> {
        let workspace_id = self
            .task(task_id)
            .and_then(crate::ports::TaskEnginePort::workspace_id)
            .map(|workspace_id| (*workspace_id).to_text())
            .ok_or(WorkspaceExportError::Store(
                WorkspaceStoreError::UnknownWorkspace,
            ))?;
        self.components.workspaces.ensure_artifact(
            artifact_id,
            &workspace_id,
            workspace_revision,
            format,
        )
    }

    /// Publishes an accepted artifact only when its exact evidence revision is current.
    pub fn publish_task_artifact(
        &mut self,
        task_id: &task_engine::deps::TaskId,
        request_id: &str,
        artifact_id: &str,
        workspace_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<WorkspaceExportView, WorkspaceExportError> {
        let workspace_id = self
            .task(task_id)
            .and_then(crate::ports::TaskEnginePort::workspace_id)
            .map(|workspace_id| (*workspace_id).to_text())
            .ok_or(WorkspaceExportError::Store(
                WorkspaceStoreError::UnknownWorkspace,
            ))?;
        self.components.workspaces.publish_artifact(
            request_id,
            artifact_id,
            &workspace_id,
            workspace_revision,
            format,
        )
    }

    fn current_task_workspace(
        &self,
        task_id: &task_engine::deps::TaskId,
    ) -> Result<WorkspaceSnapshot, WorkspaceExportError> {
        let workspace_id = self.task_workspace_id(task_id)?;
        let revision = self
            .components
            .workspaces
            .list_workspaces()
            .into_iter()
            .find(|entry| entry.workspace_id == workspace_id)
            .map(|entry| entry.revision)
            .ok_or(WorkspaceExportError::Store(
                WorkspaceStoreError::UnknownWorkspace,
            ))?;
        self.components
            .workspaces
            .reopen_workspace(&workspace_id, revision)
            .map_err(WorkspaceExportError::Store)
    }

    fn generate_rich_task_artifact(
        &self,
        task_id: &task_engine::deps::TaskId,
        workspace_revision: u64,
        kind: ArtifactKind,
    ) -> Result<file_engine::GeneratedFile, WorkspaceExportError> {
        let workspace_id = self.task_workspace_id(task_id)?;
        let snapshot = self
            .components
            .workspaces
            .reopen_workspace(&workspace_id, workspace_revision)
            .map_err(WorkspaceExportError::Store)?;
        let format = crate::adapters::workspace::rich_artifact::format_for_artifact(kind)
            .ok_or(WorkspaceExportError::RenderRefused)?;
        crate::adapters::workspace::rich_artifact::generate_workspace_file(
            &snapshot,
            workspace_revision,
            format,
        )
    }

    fn task_workspace_id(
        &self,
        task_id: &task_engine::deps::TaskId,
    ) -> Result<String, WorkspaceExportError> {
        self.task(task_id)
            .and_then(crate::ports::TaskEnginePort::workspace_id)
            .map(|workspace_id| (*workspace_id).to_text())
            .ok_or(WorkspaceExportError::Store(
                WorkspaceStoreError::UnknownWorkspace,
            ))
    }
}
