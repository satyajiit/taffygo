// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deep local-workspace lifecycle: list/reopen, exact rename, and verified delete.

mod delete;
mod preview;
mod targets;

pub use delete::{run_workspace_deletion, WorkspaceDeletionReceipt};
pub use preview::{preview_workspace_deletion, WorkspaceConfirmationDigest};

use crate::backend::{Connection, Executor, Value};
use crate::clock::Timestamp;
use crate::error::StorageError;
use crate::ids::WorkspaceId;
use crate::records::{self, Workspace};

pub const MAX_WORKSPACE_DISPLAY_NAME_BYTES: usize = 256;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct WorkspaceDeletionCounts {
    pub sources: u64,
    pub facts: u64,
    pub artifact_metadata: u64,
    pub derived_indexes: u64,
    pub external_copies: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceDeletionPreview {
    pub workspace_id: WorkspaceId,
    pub expected_revision: u64,
    pub counts: WorkspaceDeletionCounts,
    pub confirmation_token: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceDeleteRequest {
    pub operation_id: String,
    pub preview: WorkspaceDeletionPreview,
}

pub fn list_workspaces(
    executor: &mut dyn Executor,
    browser_profile_id: &str,
) -> Result<Vec<Workspace>, StorageError> {
    let rows = executor.query(
        "SELECT workspace_id FROM workspace WHERE browser_profile_id = ?1 \
         AND deletion_state = 'ACTIVE' ORDER BY updated_at_utc DESC, workspace_id",
        &[Value::text(browser_profile_id)],
    )?;
    let mut workspaces = Vec::with_capacity(rows.len());
    for row in rows {
        let id = WorkspaceId::parse(row.text(0)?).map_err(|_| StorageError::Malformed {
            what: "a workspace id",
            detail: row.text(0).unwrap_or_default().to_owned(),
        })?;
        let workspace = records::load_workspace(executor, id)?
            .filter(|workspace| workspace.deletion_state.as_str() == "ACTIVE")
            .ok_or(StorageError::WorkspaceNotFound)?;
        workspaces.push(workspace);
    }
    Ok(workspaces)
}

pub fn reopen_workspace(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
    expected_revision: u64,
) -> Result<Workspace, StorageError> {
    let workspace = records::load_workspace(executor, workspace_id)?
        .filter(|workspace| workspace.deletion_state.as_str() == "ACTIVE")
        .ok_or(StorageError::WorkspaceNotFound)?;
    let actual = u64::try_from(workspace.revision).map_err(|_| StorageError::Malformed {
        what: "a workspace revision",
        detail: workspace.revision.to_string(),
    })?;
    if actual != expected_revision {
        return Err(StorageError::WorkspaceRevisionConflict {
            expected: expected_revision,
            actual,
        });
    }
    Ok(workspace)
}

pub fn rename_workspace(
    connection: &mut dyn Connection,
    workspace_id: WorkspaceId,
    expected_revision: u64,
    display_name: &str,
    updated_at: &Timestamp,
) -> Result<u64, StorageError> {
    if !valid_display_name(display_name) {
        return Err(StorageError::Malformed {
            what: "a workspace display name",
            detail: "expected trimmed, printable, bounded text".to_owned(),
        });
    }
    let mut transaction = connection.begin()?;
    let workspace = reopen_workspace(transaction.as_mut(), workspace_id, expected_revision)?;
    if workspace.title == display_name {
        transaction.rollback()?;
        return Ok(expected_revision);
    }
    let resulting_revision = expected_revision
        .checked_add(1)
        .ok_or(StorageError::Malformed {
            what: "a workspace revision",
            detail: "revision overflow".to_owned(),
        })?;
    let updated = transaction.execute(
        "UPDATE workspace SET title = ?1, revision = ?2, updated_at_utc = ?3 \
         WHERE workspace_id = ?4 AND revision = ?5 AND deletion_state = 'ACTIVE'",
        &[
            Value::text(display_name),
            Value::Integer(to_i64(
                resulting_revision,
                "a resulting workspace revision",
            )?),
            Value::text(updated_at.as_str()),
            Value::text(workspace_id.to_text()),
            Value::Integer(to_i64(expected_revision, "an expected workspace revision")?),
        ],
    )?;
    if updated != 1 {
        return Err(StorageError::WorkspaceRevisionConflict {
            expected: expected_revision,
            actual: u64::try_from(workspace.revision).unwrap_or(0),
        });
    }
    transaction.commit()?;
    Ok(resulting_revision)
}

pub(super) fn to_i64(value: u64, what: &'static str) -> Result<i64, StorageError> {
    i64::try_from(value).map_err(|_| StorageError::Malformed {
        what,
        detail: value.to_string(),
    })
}

fn valid_display_name(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= MAX_WORKSPACE_DISPLAY_NAME_BYTES
        && value.trim() == value
        && !value.chars().any(char::is_control)
}
