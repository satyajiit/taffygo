// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact conversion between Memory storage, generated contracts, and model context.

use core_api_types::{
    MemoryRecordView, MemoryScopeKind as ViewScopeKind, MemorySensitivity as ViewSensitivity,
    MemorySourceKind as ViewSourceKind, MemoryWorkspaceView,
};
use core_service_types::{
    MemoryRecord as WireRecord, MemoryScopeKind as WireScopeKind,
    MemorySensitivity as WireSensitivity, MemorySourceKind as WireSourceKind,
    MemoryWorkspaceRecord,
};
use taffy_storage::ids::{MemoryId, WorkspaceId};
use taffy_storage::memory::{
    MemoryError, MemoryRecord, MemoryScope, MemorySensitivity, MemorySource, MemoryWorkspace,
};
use task_engine::TaskMemorySearchEntry;

use crate::account::Sha256Port;
use crate::ports::{
    MemoryScopeInput, MemorySensitivityInput, MemoryStoreError, MemoryWorkspaceInput,
};

pub(super) fn stable_memory_id(
    operation_id: &str,
    digest: &dyn Sha256Port,
) -> Result<MemoryId, MemoryStoreError> {
    let mut input = b"taffy.memory.record.v1\0".to_vec();
    input.extend_from_slice(operation_id.as_bytes());
    let hash = digest
        .sha256(&input)
        .map_err(|_| MemoryStoreError::DigestUnavailable)?;
    let mut bytes = [0_u8; 16];
    bytes.copy_from_slice(hash.get(..16).ok_or(MemoryStoreError::DigestUnavailable)?);
    Ok(MemoryId::from_bytes(bytes))
}

pub(super) fn record_from_wire(value: WireRecord) -> Result<MemoryRecord, MemoryStoreError> {
    let source_workspace = value
        .source_workspace
        .map(workspace_from_wire)
        .transpose()?;
    let source = match (value.source_kind, value.source_task_id, source_workspace) {
        (WireSourceKind::UserEntered, None, None) => MemorySource::UserEntered,
        (WireSourceKind::AcceptedTaskSuggestion, Some(task_id), workspace) => {
            MemorySource::AcceptedTaskSuggestion { task_id, workspace }
        }
        _ => return Err(MemoryStoreError::InvalidRecord),
    };
    let scope_workspace = value.scope_workspace.map(workspace_from_wire).transpose()?;
    let scope = match (value.scope_kind, scope_workspace) {
        (WireScopeKind::AllTasks, None) => MemoryScope::AllTasks,
        (WireScopeKind::Workspace, Some(workspace)) => MemoryScope::Workspace(workspace),
        _ => return Err(MemoryStoreError::InvalidRecord),
    };
    Ok(MemoryRecord {
        memory_id: MemoryId::parse(&value.memory_id)
            .map_err(|_| MemoryStoreError::InvalidIdentifier)?,
        revision: value.revision,
        statement: value.statement,
        source,
        scope,
        sensitivity: match value.sensitivity {
            WireSensitivity::Standard => MemorySensitivity::Standard,
            WireSensitivity::Sensitive => MemorySensitivity::Sensitive,
        },
        created_at_epoch_ms: value.created_at_epoch_ms,
        updated_at_epoch_ms: value.updated_at_epoch_ms,
        reviewed_at_epoch_ms: nonzero(value.reviewed_at_epoch_ms),
        expires_at_epoch_ms: nonzero(value.expires_at_epoch_ms),
    })
}

pub(super) fn record_to_wire(value: &MemoryRecord) -> WireRecord {
    let (source_kind, source_task_id, source_workspace) = match &value.source {
        MemorySource::UserEntered => (WireSourceKind::UserEntered, None, None),
        MemorySource::AcceptedTaskSuggestion { task_id, workspace } => (
            WireSourceKind::AcceptedTaskSuggestion,
            Some(task_id.clone()),
            workspace.as_ref().map(workspace_to_wire),
        ),
    };
    let (scope_kind, scope_workspace) = match &value.scope {
        MemoryScope::AllTasks => (WireScopeKind::AllTasks, None),
        MemoryScope::Workspace(workspace) => {
            (WireScopeKind::Workspace, Some(workspace_to_wire(workspace)))
        }
    };
    WireRecord {
        memory_id: value.memory_id.to_text(),
        revision: value.revision,
        statement: value.statement.clone(),
        source_kind,
        source_task_id,
        source_workspace,
        scope_kind,
        scope_workspace,
        sensitivity: match value.sensitivity {
            MemorySensitivity::Standard => WireSensitivity::Standard,
            MemorySensitivity::Sensitive => WireSensitivity::Sensitive,
        },
        created_at_epoch_ms: value.created_at_epoch_ms,
        updated_at_epoch_ms: value.updated_at_epoch_ms,
        reviewed_at_epoch_ms: value.reviewed_at_epoch_ms.unwrap_or(0),
        expires_at_epoch_ms: value.expires_at_epoch_ms.unwrap_or(0),
    }
}

pub(super) fn record_to_view(value: &MemoryRecord) -> MemoryRecordView {
    let (source_kind, source_task_id, source_workspace) = match &value.source {
        MemorySource::UserEntered => (ViewSourceKind::YouWrote, None, None),
        MemorySource::AcceptedTaskSuggestion { task_id, workspace } => (
            ViewSourceKind::TaffySuggested,
            Some(task_id.clone()),
            workspace.as_ref().map(workspace_to_view),
        ),
    };
    let (scope_kind, scope_workspace) = match &value.scope {
        MemoryScope::AllTasks => (ViewScopeKind::AllTasks, None),
        MemoryScope::Workspace(workspace) => {
            (ViewScopeKind::Workspace, Some(workspace_to_view(workspace)))
        }
    };
    MemoryRecordView {
        memory_id: value.memory_id.to_text(),
        revision: value.revision,
        statement: value.statement.clone(),
        source_kind,
        source_task_id,
        source_workspace,
        scope_kind,
        scope_workspace,
        sensitivity: match value.sensitivity {
            MemorySensitivity::Standard => ViewSensitivity::Standard,
            MemorySensitivity::Sensitive => ViewSensitivity::Sensitive,
        },
        created_at_epoch_ms: value.created_at_epoch_ms,
        updated_at_epoch_ms: value.updated_at_epoch_ms,
        reviewed_at_epoch_ms: value.reviewed_at_epoch_ms.unwrap_or(0),
        expires_at_epoch_ms: value.expires_at_epoch_ms.unwrap_or(0),
    }
}

pub(super) fn scope_from_input(value: MemoryScopeInput) -> Result<MemoryScope, MemoryStoreError> {
    match value {
        MemoryScopeInput::AllTasks => Ok(MemoryScope::AllTasks),
        MemoryScopeInput::Workspace(workspace) => {
            workspace_from_input(workspace).map(MemoryScope::Workspace)
        }
    }
}

pub(super) const fn sensitivity_from_input(value: MemorySensitivityInput) -> MemorySensitivity {
    match value {
        MemorySensitivityInput::Standard => MemorySensitivity::Standard,
        MemorySensitivityInput::Sensitive => MemorySensitivity::Sensitive,
    }
}

pub(super) fn workspace_from_input(
    value: MemoryWorkspaceInput,
) -> Result<MemoryWorkspace, MemoryStoreError> {
    Ok(MemoryWorkspace {
        workspace_id: WorkspaceId::parse(&value.workspace_id)
            .map_err(|_| MemoryStoreError::InvalidIdentifier)?,
        display_name: value.display_name,
    })
}

pub(super) fn transcript_entry(value: &MemoryRecord) -> Option<TaskMemorySearchEntry> {
    let source = match &value.source {
        MemorySource::UserEntered => "You wrote this".to_owned(),
        MemorySource::AcceptedTaskSuggestion { workspace, .. } => workspace.as_ref().map_or_else(
            || "You approved a Taffy suggestion".to_owned(),
            |workspace| {
                format!(
                    "You approved a Taffy suggestion in {}",
                    workspace.display_name
                )
            },
        ),
    };
    let scope = match &value.scope {
        MemoryScope::AllTasks => "all tasks".to_owned(),
        MemoryScope::Workspace(workspace) => format!("workspace {}", workspace.display_name),
    };
    TaskMemorySearchEntry::new(
        value.memory_id.to_text(),
        value.statement.clone(),
        source,
        scope,
    )
}

pub(super) const fn map_error(error: MemoryError) -> MemoryStoreError {
    match error {
        MemoryError::InvalidIdentifier | MemoryError::InvalidOperation => {
            MemoryStoreError::InvalidIdentifier
        }
        MemoryError::InvalidQuery => MemoryStoreError::InvalidQuery,
        MemoryError::InvalidRecord => MemoryStoreError::InvalidRecord,
        MemoryError::RecordNotFound => MemoryStoreError::UnknownRecord,
        MemoryError::MemoryRevisionConflict => MemoryStoreError::StaleMemoryRevision,
        MemoryError::RecordRevisionConflict => MemoryStoreError::StaleRecordRevision,
        MemoryError::OperationAlreadyPending => MemoryStoreError::OperationAlreadyPending,
        MemoryError::TooManyRecords => MemoryStoreError::TooManyRecords,
        MemoryError::TooManyPendingMutations => MemoryStoreError::TooManyPendingMutations,
        MemoryError::WrongCompletion => MemoryStoreError::WrongCompletion,
    }
}

const fn nonzero(value: u64) -> Option<u64> {
    if value == 0 {
        None
    } else {
        Some(value)
    }
}

fn workspace_from_wire(value: MemoryWorkspaceRecord) -> Result<MemoryWorkspace, MemoryStoreError> {
    Ok(MemoryWorkspace {
        workspace_id: WorkspaceId::parse(&value.workspace_id)
            .map_err(|_| MemoryStoreError::InvalidIdentifier)?,
        display_name: value.display_name,
    })
}

fn workspace_to_wire(value: &MemoryWorkspace) -> MemoryWorkspaceRecord {
    MemoryWorkspaceRecord {
        workspace_id: value.workspace_id.to_text(),
        display_name: value.display_name.clone(),
    }
}

fn workspace_to_view(value: &MemoryWorkspace) -> MemoryWorkspaceView {
    MemoryWorkspaceView {
        workspace_id: value.workspace_id.to_text(),
        display_name: value.display_name.clone(),
    }
}
