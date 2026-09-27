// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Writing and reading a workspace.

use super::row::{id, timestamp};
use super::types::Workspace;
use super::vocabulary::{DeletionState, Sensitivity, WorkspaceStatus};
use super::RECORD_SCHEMA_VERSION;
use crate::backend::{Executor, Row, Value};
use crate::error::StorageError;
use crate::ids::WorkspaceId;
/// Writes a workspace.
pub fn insert_workspace(
    executor: &mut dyn Executor,
    workspace: &Workspace,
) -> Result<(), StorageError> {
    executor.execute(
        "INSERT INTO workspace (workspace_id, browser_profile_id, owner_account_id, title, \
         status, revision, created_at_utc, updated_at_utc, retention_class, sensitivity, \
         deletion_state, accepted_artifact_id, schema_version) \
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, NULL, ?12)",
        &[
            Value::text(workspace.workspace_id.to_text()),
            Value::text(workspace.browser_profile_id.clone()),
            Value::maybe_text(workspace.owner_account_id.clone()),
            Value::text(workspace.title.clone()),
            Value::text(workspace.status.as_str()),
            Value::Integer(workspace.revision),
            Value::text(workspace.created_at.as_str()),
            Value::text(workspace.updated_at.as_str()),
            Value::text(workspace.retention_class.clone()),
            Value::text(workspace.sensitivity.as_str()),
            Value::text(workspace.deletion_state.as_str()),
            Value::Integer(RECORD_SCHEMA_VERSION),
        ],
    )?;
    Ok(())
}

const WORKSPACE_COLUMNS: &str = "workspace_id, browser_profile_id, owner_account_id, title, \
     status, revision, created_at_utc, updated_at_utc, retention_class, sensitivity, deletion_state";

fn read_workspace(row: &Row) -> Result<Workspace, StorageError> {
    Ok(Workspace {
        workspace_id: id(row, 0, |raw| WorkspaceId::parse(raw).ok(), "a workspace id")?,
        browser_profile_id: row.text(1)?.to_owned(),
        owner_account_id: row.maybe_text(2)?.map(str::to_owned),
        title: row.text(3)?.to_owned(),
        status: WorkspaceStatus::read(row, 4)?,
        revision: row.integer(5)?,
        created_at: timestamp(row, 6)?,
        updated_at: timestamp(row, 7)?,
        retention_class: row.text(8)?.to_owned(),
        sensitivity: Sensitivity::read(row, 9)?,
        deletion_state: DeletionState::read(row, 10)?,
    })
}

/// Reads a workspace.
pub fn load_workspace(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
) -> Result<Option<Workspace>, StorageError> {
    let sql = format!("SELECT {WORKSPACE_COLUMNS} FROM workspace WHERE workspace_id = ?1");
    let rows = executor.query(&sql, &[Value::text(workspace_id.to_text())])?;
    rows.first().map(read_workspace).transpose()
}
