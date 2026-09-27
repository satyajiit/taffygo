// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One verified transaction that removes a workspace and every local derivative.

use super::preview::preview_workspace_deletion;
use super::targets::{delete_dependents, verify_absent, DeletionTargets};
use super::{to_i64, WorkspaceConfirmationDigest, WorkspaceDeleteRequest, WorkspaceDeletionCounts};
use crate::backend::{Connection, Executor, Value};
use crate::clock::Clock;
use crate::error::StorageError;
use crate::ids::WorkspaceId;

const MAX_OPERATION_ID_BYTES: usize = 256;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceDeletionReceipt {
    pub operation_id: String,
    pub workspace_id: WorkspaceId,
    pub expected_revision: u64,
    pub resulting_revision: u64,
    pub counts: WorkspaceDeletionCounts,
    pub completed_at_utc: String,
    pub verified: bool,
}

pub fn run_workspace_deletion(
    connection: &mut dyn Connection,
    request: &WorkspaceDeleteRequest,
    digest: &dyn WorkspaceConfirmationDigest,
    clock: &dyn Clock,
) -> Result<WorkspaceDeletionReceipt, StorageError> {
    validate_request(request)?;
    let mut transaction = connection.begin()?;
    if let Some(receipt) = replay_receipt(transaction.as_mut(), request)? {
        transaction.rollback()?;
        return Ok(receipt);
    }
    let current =
        preview_workspace_deletion(transaction.as_mut(), request.preview.workspace_id, digest)?;
    if current != request.preview {
        return Err(StorageError::InvalidWorkspaceConfirmation);
    }
    let resulting_revision =
        current
            .expected_revision
            .checked_add(1)
            .ok_or(StorageError::Malformed {
                what: "a resulting workspace revision",
                detail: "revision overflow".to_owned(),
            })?;
    let targets = DeletionTargets::collect(transaction.as_mut(), current.workspace_id)?;
    let updated = transaction.execute(
        "UPDATE workspace SET deletion_state='DELETING' WHERE workspace_id=?1 \
         AND revision=?2 AND deletion_state='ACTIVE'",
        &[
            Value::text(current.workspace_id.to_text()),
            Value::Integer(to_i64(
                current.expected_revision,
                "an expected workspace revision",
            )?),
        ],
    )?;
    if updated != 1 {
        return Err(StorageError::WorkspaceRevisionConflict {
            expected: current.expected_revision,
            actual: current.expected_revision,
        });
    }

    let removed = delete_dependents(
        transaction.as_mut(),
        current.workspace_id,
        &targets,
        current.counts.external_copies,
    )?;
    if removed != current.counts {
        return Err(StorageError::DeletionUnverified {
            detail: "the rows removed do not match the confirmed preview".to_owned(),
        });
    }
    verify_absent(transaction.as_mut(), current.workspace_id, &targets)?;
    let completed_at = clock.now_utc();
    transaction.execute(
        "INSERT INTO deletion_tombstone \
         (record_type,record_id,deleted_at_utc,revision,retention_class) \
         VALUES ('WORKSPACE',?1,?2,?3,'DELETION_TOMBSTONE')",
        &[
            Value::text(current.workspace_id.to_text()),
            Value::text(completed_at.as_str()),
            Value::Integer(to_i64(
                resulting_revision,
                "a resulting workspace revision",
            )?),
        ],
    )?;
    let receipt = WorkspaceDeletionReceipt {
        operation_id: request.operation_id.clone(),
        workspace_id: current.workspace_id,
        expected_revision: current.expected_revision,
        resulting_revision,
        counts: current.counts,
        completed_at_utc: completed_at.as_str().to_owned(),
        verified: true,
    };
    write_receipt(transaction.as_mut(), &receipt, &current.confirmation_token)?;
    transaction.commit()?;
    Ok(receipt)
}

fn validate_request(request: &WorkspaceDeleteRequest) -> Result<(), StorageError> {
    if request.operation_id.is_empty()
        || request.operation_id.len() > MAX_OPERATION_ID_BYTES
        || request.preview.confirmation_token.len() != 64
        || !request
            .preview
            .confirmation_token
            .bytes()
            .all(|byte| byte.is_ascii_digit() || matches!(byte, b'a'..=b'f'))
    {
        return Err(StorageError::InvalidWorkspaceConfirmation);
    }
    Ok(())
}

fn replay_receipt(
    executor: &mut dyn Executor,
    request: &WorkspaceDeleteRequest,
) -> Result<Option<WorkspaceDeletionReceipt>, StorageError> {
    let rows = executor.query(
        "SELECT workspace_id,expected_revision,resulting_revision,confirmation_token,\
         source_count,fact_count,artifact_count,index_count,external_copies,\
         completed_at_utc,verified FROM workspace_deletion_receipt WHERE operation_id=?1",
        &[Value::text(&request.operation_id)],
    )?;
    let Some(row) = rows.first() else {
        return Ok(None);
    };
    let workspace_id = WorkspaceId::parse(row.text(0)?).map_err(|_| StorageError::Malformed {
        what: "a workspace deletion receipt id",
        detail: row.text(0).unwrap_or_default().to_owned(),
    })?;
    let receipt = WorkspaceDeletionReceipt {
        operation_id: request.operation_id.clone(),
        workspace_id,
        expected_revision: nonnegative(row.integer(1)?)?,
        resulting_revision: nonnegative(row.integer(2)?)?,
        counts: WorkspaceDeletionCounts {
            sources: nonnegative(row.integer(4)?)?,
            facts: nonnegative(row.integer(5)?)?,
            artifact_metadata: nonnegative(row.integer(6)?)?,
            derived_indexes: nonnegative(row.integer(7)?)?,
            external_copies: nonnegative(row.integer(8)?)?,
        },
        completed_at_utc: row.text(9)?.to_owned(),
        verified: row.boolean(10)?,
    };
    let identical = receipt.workspace_id == request.preview.workspace_id
        && receipt.expected_revision == request.preview.expected_revision
        && receipt.counts == request.preview.counts
        && row.text(3)? == request.preview.confirmation_token
        && receipt.verified;
    identical
        .then_some(Some(receipt))
        .ok_or(StorageError::InvalidWorkspaceConfirmation)
}

fn write_receipt(
    executor: &mut dyn Executor,
    receipt: &WorkspaceDeletionReceipt,
    confirmation_token: &str,
) -> Result<(), StorageError> {
    executor.execute(
        "INSERT INTO workspace_deletion_receipt \
         (operation_id,workspace_id,expected_revision,resulting_revision,confirmation_token,\
          source_count,fact_count,artifact_count,index_count,external_copies,completed_at_utc,verified) \
         VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,1)",
        &[
            Value::text(&receipt.operation_id),
            Value::text(receipt.workspace_id.to_text()),
            Value::Integer(to_i64(receipt.expected_revision, "an expected revision")?),
            Value::Integer(to_i64(receipt.resulting_revision, "a resulting revision")?),
            Value::text(confirmation_token),
            Value::Integer(to_i64(receipt.counts.sources, "a source count")?),
            Value::Integer(to_i64(receipt.counts.facts, "a fact count")?),
            Value::Integer(to_i64(
                receipt.counts.artifact_metadata,
                "an artifact count",
            )?),
            Value::Integer(to_i64(receipt.counts.derived_indexes, "an index count")?),
            Value::Integer(to_i64(receipt.counts.external_copies, "an export count")?),
            Value::text(&receipt.completed_at_utc),
        ],
    )?;
    Ok(())
}

fn nonnegative(value: i64) -> Result<u64, StorageError> {
    u64::try_from(value).map_err(|_| StorageError::Malformed {
        what: "a workspace deletion receipt count",
        detail: value.to_string(),
    })
}
