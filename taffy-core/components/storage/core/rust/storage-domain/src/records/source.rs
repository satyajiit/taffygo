// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Writing and reading a source, and a workspace's membership of it.
//!
//! [`readable_sources`] is the module's rule made executable: excluded and
//! removed memberships, and anything whose deletion has started, are left out.
//! A lookup is the wrong place to discover that a source is on its way out.

use super::row::{id, maybe_timestamp, timestamp};
use super::types::Source;
use super::vocabulary::{DeletionState, MembershipState, Ownership, Sensitivity, SourceKind};
use super::RECORD_SCHEMA_VERSION;
use crate::backend::{Executor, Row, Value};
use crate::clock::Timestamp;
use crate::error::StorageError;
use crate::ids::{SourceId, WorkspaceId};
/// Writes a source.
pub fn insert_source(executor: &mut dyn Executor, source: &Source) -> Result<(), StorageError> {
    executor.execute(
        "INSERT INTO source (source_id, kind, canonical_locator, display_locator, origin, title, \
         first_seen_at_utc, last_observed_at_utc, ownership, sensitivity, retention_class, \
         deletion_state, schema_version) \
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)",
        &[
            Value::text(source.source_id.to_text()),
            Value::text(source.kind.as_str()),
            Value::maybe_text(source.canonical_locator.clone()),
            Value::text(source.display_locator.clone()),
            Value::maybe_text(source.origin.clone()),
            Value::maybe_text(source.title.clone()),
            Value::text(source.first_seen_at.as_str()),
            Value::maybe_text(source.last_observed_at.as_ref().map(Timestamp::as_str)),
            Value::text(source.ownership.as_str()),
            Value::text(source.sensitivity.as_str()),
            Value::text(source.retention_class.clone()),
            Value::text(source.deletion_state.as_str()),
            Value::Integer(RECORD_SCHEMA_VERSION),
        ],
    )?;
    Ok(())
}

const SOURCE_COLUMNS: &str = "source_id, kind, canonical_locator, display_locator, origin, title, \
     first_seen_at_utc, last_observed_at_utc, ownership, sensitivity, retention_class, deletion_state";

fn read_source(row: &Row) -> Result<Source, StorageError> {
    Ok(Source {
        source_id: id(row, 0, |raw| SourceId::parse(raw).ok(), "a source id")?,
        kind: SourceKind::read(row, 1)?,
        canonical_locator: row.maybe_text(2)?.map(str::to_owned),
        display_locator: row.text(3)?.to_owned(),
        origin: row.maybe_text(4)?.map(str::to_owned),
        title: row.maybe_text(5)?.map(str::to_owned),
        first_seen_at: timestamp(row, 6)?,
        last_observed_at: maybe_timestamp(row, 7)?,
        ownership: Ownership::read(row, 8)?,
        sensitivity: Sensitivity::read(row, 9)?,
        retention_class: row.text(10)?.to_owned(),
        deletion_state: DeletionState::read(row, 11)?,
    })
}

/// Reads a source, whatever its deletion state.
pub fn load_source(
    executor: &mut dyn Executor,
    source_id: SourceId,
) -> Result<Option<Source>, StorageError> {
    let sql = format!("SELECT {SOURCE_COLUMNS} FROM source WHERE source_id = ?1");
    let rows = executor.query(&sql, &[Value::text(source_id.to_text())])?;
    rows.first().map(read_source).transpose()
}

/// Adds a source to a workspace.
///
/// Membership is explicit and recorded; visiting a page does not create it.
pub fn attach_source(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
    source_id: SourceId,
    state: MembershipState,
    added_by: &str,
    added_at: &Timestamp,
) -> Result<(), StorageError> {
    executor.execute(
        "INSERT INTO workspace_source \
         (workspace_id, source_id, membership_state, added_by, added_at_utc) \
         VALUES (?1, ?2, ?3, ?4, ?5)",
        &[
            Value::text(workspace_id.to_text()),
            Value::text(source_id.to_text()),
            Value::text(state.as_str()),
            Value::text(added_by),
            Value::text(added_at.as_str()),
        ],
    )?;
    Ok(())
}

/// The sources a workspace may still use.
///
/// Excluded and removed memberships, and anything whose deletion has started,
/// are left out: a lookup is the wrong place to discover that a source is on
/// its way out.
pub fn readable_sources(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
) -> Result<Vec<Source>, StorageError> {
    let sql = format!(
        "SELECT {} FROM source AS s \
         JOIN workspace_source AS m ON m.source_id = s.source_id \
         WHERE m.workspace_id = ?1 AND m.membership_state = 'INCLUDED' \
           AND s.deletion_state = 'ACTIVE' \
         ORDER BY s.source_id",
        SOURCE_COLUMNS
            .split(", ")
            .map(|column| format!("s.{column}"))
            .collect::<Vec<_>>()
            .join(", ")
    );
    let rows = executor.query(&sql, &[Value::text(workspace_id.to_text())])?;
    rows.iter().map(read_source).collect()
}
