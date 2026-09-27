// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::cell::RefCell;
use std::rc::Rc;

use core_api_types::{
    TaskTemplateId, WorkspaceExportFormat, WorkspaceExportView, WorkspacePhase, WorkspaceViewState,
};

use super::SUBSTITUTED_WORKSPACE;
use crate::ports::{
    WorkspaceDeleteRequest, WorkspaceDeletionCounts, WorkspaceDeletionPreview,
    WorkspaceExportError, WorkspaceListEntry, WorkspacePageFact, WorkspacePersistRequest,
    WorkspacePort, WorkspaceStoreError,
};
use crate::Sha256Port;
use taffy_storage::workspace::WorkspaceSnapshot;

/// A recording workspace plane that projects one fixed revision.
#[derive(Debug, Default)]
pub(crate) struct FakeWorkspaces {
    pub(crate) calls: Rc<RefCell<Vec<&'static str>>>,
}

impl FakeWorkspaces {
    fn record(&self, name: &'static str) {
        self.calls.borrow_mut().push(name);
    }
}

impl WorkspacePort for FakeWorkspaces {
    fn restore_encoded(&mut self, _snapshots: &[Vec<u8>]) -> Result<(), WorkspaceStoreError> {
        self.record("restore_encoded");
        Ok(())
    }

    fn restore_bound_records(
        &mut self,
        _records: &[(String, u64, Vec<u8>)],
    ) -> Result<(), WorkspaceStoreError> {
        self.record("restore_bound_records");
        Ok(())
    }

    fn list_workspaces(&self) -> Vec<WorkspaceListEntry> {
        self.record("list_workspaces");
        Vec::new()
    }

    fn retained_snapshot(&self, _workspace_id: &str) -> Option<&WorkspaceSnapshot> {
        self.record("retained_snapshot");
        None
    }

    fn reopen_workspace(
        &self,
        _workspace_id: &str,
        _expected_revision: u64,
    ) -> Result<WorkspaceSnapshot, WorkspaceStoreError> {
        self.record("reopen_workspace");
        Err(WorkspaceStoreError::UnknownWorkspace)
    }

    fn begin_creation(
        &mut self,
        operation_id: String,
        snapshot: WorkspaceSnapshot,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.record("begin_creation");
        Ok(WorkspacePersistRequest {
            operation_id,
            workspace_id: snapshot.workspace_id.to_text(),
            expected_revision: 0,
            resulting_revision: snapshot.revision,
            snapshot: Vec::new(),
        })
    }

    fn begin_page_ingestion(
        &mut self,
        _operation_id: String,
        _workspace_id: &str,
        _source_id: &str,
        _facts: Vec<WorkspacePageFact>,
        _updated_at_epoch_ms: u64,
    ) -> Result<Option<WorkspacePersistRequest>, WorkspaceStoreError> {
        self.record("begin_page_ingestion");
        Ok(None)
    }

    fn preflight_task_update(&self, _workspace_id: &str) -> Result<(), WorkspaceStoreError> {
        self.record("preflight_task_update");
        Ok(())
    }

    fn begin_task_update(
        &mut self,
        _operation_id: String,
        _workspace_id: &str,
        _phase: taffy_storage::workspace::WorkspacePhase,
        _page_facts: Option<(&str, Vec<WorkspacePageFact>)>,
        _observed_source: Option<taffy_storage::workspace::WorkspaceSource>,
        _updated_at_epoch_ms: u64,
    ) -> Result<Option<WorkspacePersistRequest>, WorkspaceStoreError> {
        self.record("begin_task_update");
        Ok(None)
    }

    fn begin_save(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        _updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.record("begin_save");
        Ok(WorkspacePersistRequest {
            operation_id,
            workspace_id: workspace_id.to_owned(),
            expected_revision,
            resulting_revision: expected_revision.saturating_add(1),
            snapshot: Vec::new(),
        })
    }

    fn begin_correction(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        _fact_id: &str,
        _value: String,
        _updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.record("begin_correction");
        Ok(WorkspacePersistRequest {
            operation_id,
            workspace_id: workspace_id.to_owned(),
            expected_revision,
            resulting_revision: expected_revision.saturating_add(1),
            snapshot: Vec::new(),
        })
    }

    fn begin_exclusion(
        &mut self,
        _operation_id: String,
        _workspace_id: &str,
        _expected_revision: u64,
        _source_id: &str,
        _updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.record("begin_exclusion");
        Err(WorkspaceStoreError::UnknownWorkspace)
    }

    fn begin_rename(
        &mut self,
        _operation_id: String,
        _workspace_id: &str,
        _expected_revision: u64,
        _display_name: String,
        _updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError> {
        self.record("begin_rename");
        Err(WorkspaceStoreError::UnknownWorkspace)
    }

    fn preview_deletion(
        &self,
        workspace_id: &str,
        _digest: &dyn Sha256Port,
    ) -> Result<WorkspaceDeletionPreview, WorkspaceStoreError> {
        self.record("preview_deletion");
        Ok(WorkspaceDeletionPreview {
            workspace_id: workspace_id.to_owned(),
            expected_revision: 9,
            counts: WorkspaceDeletionCounts::default(),
            confirmation_token: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                .to_owned(),
        })
    }

    fn begin_deletion(
        &mut self,
        _operation_id: String,
        _preview: &WorkspaceDeletionPreview,
        _digest: &dyn Sha256Port,
        _now_utc_ms: u64,
    ) -> Result<WorkspaceDeleteRequest, WorkspaceStoreError> {
        self.record("begin_deletion");
        Err(WorkspaceStoreError::UnknownWorkspace)
    }

    fn begin_discard(
        &mut self,
        _operation_id: String,
        _workspace_id: &str,
        _expected_revision: u64,
        _digest: &dyn Sha256Port,
        _now_utc_ms: u64,
    ) -> Result<WorkspaceDeleteRequest, WorkspaceStoreError> {
        self.record("begin_discard");
        Err(WorkspaceStoreError::NotDiscardable)
    }

    fn complete_persist(
        &mut self,
        _operation_id: &str,
        _committed_revision: u64,
    ) -> Result<(), WorkspaceStoreError> {
        self.record("complete_persist");
        Ok(())
    }

    fn reject_persist(&mut self, _operation_id: &str) -> bool {
        self.record("reject_persist");
        true
    }

    fn complete_deletion(
        &mut self,
        _operation_id: &str,
        _committed_revision: u64,
    ) -> Result<(), WorkspaceStoreError> {
        self.record("complete_deletion");
        Ok(())
    }

    fn reject_deletion(&mut self, _operation_id: &str) -> bool {
        self.record("reject_deletion");
        true
    }

    fn request_export(
        &mut self,
        _request_id: &str,
        _workspace_id: &str,
        _expected_revision: u64,
        _format: WorkspaceExportFormat,
    ) -> Result<WorkspaceExportView, WorkspaceExportError> {
        self.record("request_export");
        Err(WorkspaceExportError::NoExportableFacts)
    }

    fn prepare_artifact(
        &mut self,
        _artifact_id: &str,
        _workspace_id: &str,
        _format: WorkspaceExportFormat,
    ) -> Result<u64, WorkspaceExportError> {
        self.record("prepare_artifact");
        Ok(9)
    }

    fn ensure_artifact(
        &mut self,
        _artifact_id: &str,
        _workspace_id: &str,
        _expected_revision: u64,
        _format: WorkspaceExportFormat,
    ) -> Result<(), WorkspaceExportError> {
        self.record("ensure_artifact");
        Ok(())
    }

    fn publish_artifact(
        &mut self,
        _request_id: &str,
        _artifact_id: &str,
        _workspace_id: &str,
        _expected_revision: u64,
        _format: WorkspaceExportFormat,
    ) -> Result<WorkspaceExportView, WorkspaceExportError> {
        self.record("publish_artifact");
        Err(WorkspaceExportError::NoExportableFacts)
    }

    fn project_core_api(&self) -> Result<Vec<WorkspaceViewState>, WorkspaceStoreError> {
        self.record("project_core_api");
        Ok(vec![WorkspaceViewState {
            workspace_id: SUBSTITUTED_WORKSPACE.to_owned(),
            revision: 9,
            goal: "substituted goal".to_owned(),
            phase: WorkspacePhase::Running,
            last_updated_epoch_ms: 1,
            template_id: TaskTemplateId::CompareProducts,
            sources: Vec::new(),
            facts: Vec::new(),
            saved: true,
            display_name: "substituted workspace".to_owned(),
            deletion_preview: None,
        }])
    }

    fn latest_export(&self) -> Option<WorkspaceExportView> {
        self.record("latest_export");
        None
    }
}
