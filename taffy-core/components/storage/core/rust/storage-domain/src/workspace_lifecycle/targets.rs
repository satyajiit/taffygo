// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Discovery, removal, and absence verification for workspace-owned rows.

use crate::backend::{count, Executor, Value};
use crate::error::StorageError;
use crate::ids::WorkspaceId;
use crate::search;

use super::preview::scoped_index_ids_sql;
use super::WorkspaceDeletionCounts;

#[derive(Debug)]
pub(super) struct DeletionTargets {
    facts: Vec<String>,
    conflicts: Vec<String>,
    tasks: Vec<String>,
    artifacts: Vec<String>,
    claims: Vec<String>,
    events: Vec<String>,
    exclusive_sources: Vec<String>,
    index_documents: Vec<String>,
}

impl DeletionTargets {
    pub(super) fn collect(
        executor: &mut dyn Executor,
        workspace_id: WorkspaceId,
    ) -> Result<Self, StorageError> {
        let key = Value::text(workspace_id.to_text());
        Ok(Self {
            facts: text_column(
                executor,
                "SELECT fact_id FROM fact WHERE workspace_id=?1 ORDER BY fact_id",
                std::slice::from_ref(&key),
            )?,
            conflicts: text_column(
                executor,
                "SELECT conflict_id FROM conflict WHERE workspace_id=?1 ORDER BY conflict_id",
                std::slice::from_ref(&key),
            )?,
            tasks: text_column(
                executor,
                "SELECT task_id FROM task WHERE workspace_id=?1 ORDER BY task_id",
                std::slice::from_ref(&key),
            )?,
            artifacts: artifact_ids(executor, std::slice::from_ref(&key))?,
            claims: text_column(
                executor,
                "SELECT claim_id FROM claim WHERE task_id IN \
                 (SELECT task_id FROM task WHERE workspace_id=?1) OR artifact_id IN \
                 (SELECT artifact_id FROM artifact WHERE workspace_id=?1 OR task_id IN \
                  (SELECT task_id FROM task WHERE workspace_id=?1)) ORDER BY claim_id",
                std::slice::from_ref(&key),
            )?,
            events: text_column(
                executor,
                "SELECT event_id FROM task_event WHERE task_id IN \
                 (SELECT task_id FROM task WHERE workspace_id=?1) ORDER BY event_id",
                std::slice::from_ref(&key),
            )?,
            exclusive_sources: text_column(
                executor,
                "SELECT owned.source_id FROM workspace_source owned WHERE owned.workspace_id=?1 \
                 AND NOT EXISTS (SELECT 1 FROM workspace_source other WHERE \
                 other.source_id=owned.source_id AND other.workspace_id<>?1) \
                 ORDER BY owned.source_id",
                std::slice::from_ref(&key),
            )?,
            index_documents: text_column(
                executor,
                scoped_index_ids_sql(),
                std::slice::from_ref(&key),
            )?,
        })
    }
}

fn artifact_ids(executor: &mut dyn Executor, key: &[Value]) -> Result<Vec<String>, StorageError> {
    text_column(
        executor,
        "SELECT artifact_id FROM artifact WHERE workspace_id=?1 OR task_id IN \
         (SELECT task_id FROM task WHERE workspace_id=?1) OR artifact_id IN \
         (SELECT lineage.artifact_id FROM artifact_lineage lineage WHERE \
          (lineage.related_type='FACT' AND lineage.related_id IN \
           (SELECT fact_id FROM fact WHERE workspace_id=?1)) OR \
          (lineage.related_type='SOURCE' AND lineage.related_id IN \
           (SELECT owned.source_id FROM workspace_source owned WHERE owned.workspace_id=?1 \
            AND NOT EXISTS (SELECT 1 FROM workspace_source other WHERE \
            other.source_id=owned.source_id AND other.workspace_id<>?1)))) \
         ORDER BY artifact_id",
        key,
    )
}

pub(super) fn delete_dependents(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
    targets: &DeletionTargets,
    external_copies: u64,
) -> Result<WorkspaceDeletionCounts, StorageError> {
    delete_indexes(executor, &targets.index_documents)?;
    delete_by_ids(
        executor,
        "artifact_lineage",
        "artifact_id",
        &targets.artifacts,
    )?;
    let artifact_metadata = delete_by_ids(executor, "artifact", "artifact_id", &targets.artifacts)?;
    delete_by_ids(executor, "claim_support", "claim_id", &targets.claims)?;
    delete_by_ids(executor, "claim_support", "fact_id", &targets.facts)?;
    delete_by_ids(executor, "claim", "claim_id", &targets.claims)?;
    delete_by_ids(executor, "conflict_fact", "conflict_id", &targets.conflicts)?;
    delete_by_ids(executor, "conflict_fact", "fact_id", &targets.facts)?;
    delete_by_ids(executor, "conflict", "conflict_id", &targets.conflicts)?;
    delete_by_ids(executor, "provenance_locator", "fact_id", &targets.facts)?;
    delete_by_ids(
        executor,
        "provenance_locator",
        "source_id",
        &targets.exclusive_sources,
    )?;
    let facts = delete_by_ids(executor, "fact", "fact_id", &targets.facts)?;
    delete_task_rows(executor, workspace_id, targets)?;
    delete_by_ids(
        executor,
        "observation",
        "source_id",
        &targets.exclusive_sources,
    )?;
    let key = Value::text(workspace_id.to_text());
    let sources = executor.execute(
        "DELETE FROM workspace_source WHERE workspace_id=?1",
        std::slice::from_ref(&key),
    )?;
    delete_by_ids(executor, "source", "source_id", &targets.exclusive_sources)?;
    if executor.execute(
        "DELETE FROM workspace WHERE workspace_id=?1 AND deletion_state='DELETING'",
        std::slice::from_ref(&key),
    )? != 1
    {
        return Err(StorageError::DeletionUnverified {
            detail: "the workspace row survived deletion".to_owned(),
        });
    }
    Ok(WorkspaceDeletionCounts {
        sources,
        facts,
        artifact_metadata,
        derived_indexes: u64::try_from(targets.index_documents.len()).map_err(|_| {
            StorageError::Malformed {
                what: "workspace index count",
                detail: targets.index_documents.len().to_string(),
            }
        })?,
        external_copies,
    })
}

fn delete_indexes(
    executor: &mut dyn Executor,
    document_ids: &[String],
) -> Result<(), StorageError> {
    for document_id in document_ids {
        executor.execute(
            "DELETE FROM search_index WHERE document_id=?1",
            &[Value::text(document_id)],
        )?;
        executor.execute(
            "DELETE FROM search_document WHERE document_id=?1",
            &[Value::text(document_id)],
        )?;
    }
    Ok(())
}

fn delete_task_rows(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
    targets: &DeletionTargets,
) -> Result<(), StorageError> {
    delete_by_ids(
        executor,
        "journal_source_projection",
        "event_id",
        &targets.events,
    )?;
    executor.execute(
        "DELETE FROM journal_source_projection WHERE workspace_id=?1",
        &[Value::text(workspace_id.to_text())],
    )?;
    delete_by_ids(
        executor,
        "journal_source_projection",
        "source_id",
        &targets.exclusive_sources,
    )?;
    delete_by_ids(executor, "task_state_projection", "task_id", &targets.tasks)?;
    delete_by_ids(executor, "task_event", "event_id", &targets.events)?;
    delete_by_ids(executor, "model_invocation", "task_id", &targets.tasks)?;
    delete_by_ids(executor, "task", "task_id", &targets.tasks)?;
    Ok(())
}

pub(super) fn verify_absent(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
    targets: &DeletionTargets,
) -> Result<(), StorageError> {
    let key = Value::text(workspace_id.to_text());
    for (what, sql) in [
        (
            "workspace rows",
            "SELECT COUNT(*) FROM workspace WHERE workspace_id=?1",
        ),
        (
            "workspace memberships",
            "SELECT COUNT(*) FROM workspace_source WHERE workspace_id=?1",
        ),
        (
            "workspace facts",
            "SELECT COUNT(*) FROM fact WHERE workspace_id=?1",
        ),
        (
            "workspace tasks",
            "SELECT COUNT(*) FROM task WHERE workspace_id=?1",
        ),
        (
            "workspace artifacts",
            "SELECT COUNT(*) FROM artifact WHERE workspace_id=?1",
        ),
        (
            "workspace conflicts",
            "SELECT COUNT(*) FROM conflict WHERE workspace_id=?1",
        ),
        (
            "workspace index entries",
            "SELECT COUNT(*) FROM search_document WHERE workspace_id=?1",
        ),
    ] {
        if count(executor, sql, std::slice::from_ref(&key))? != 0 {
            return Err(StorageError::DeletionUnverified {
                detail: format!("{what} remain"),
            });
        }
    }
    verify_ids_absent(executor, "fact", "fact_id", &targets.facts)?;
    verify_ids_absent(executor, "artifact", "artifact_id", &targets.artifacts)?;
    verify_ids_absent(executor, "task", "task_id", &targets.tasks)?;
    verify_ids_absent(executor, "source", "source_id", &targets.exclusive_sources)?;
    verify_ids_absent(
        executor,
        "search_document",
        "document_id",
        &targets.index_documents,
    )?;
    if search::orphaned_index_rows(executor)? != 0 {
        return Err(StorageError::DeletionUnverified {
            detail: "derived index rows became orphaned".to_owned(),
        });
    }
    Ok(())
}

fn delete_by_ids(
    executor: &mut dyn Executor,
    table: &'static str,
    column: &'static str,
    ids: &[String],
) -> Result<u64, StorageError> {
    let sql = format!("DELETE FROM {table} WHERE {column}=?1");
    let mut removed = 0_u64;
    for id in ids {
        removed = removed
            .checked_add(executor.execute(&sql, &[Value::text(id)])?)
            .ok_or(StorageError::Malformed {
                what: "workspace rows removed",
                detail: "count overflow".to_owned(),
            })?;
    }
    Ok(removed)
}

fn verify_ids_absent(
    executor: &mut dyn Executor,
    table: &'static str,
    column: &'static str,
    ids: &[String],
) -> Result<(), StorageError> {
    let sql = format!("SELECT COUNT(*) FROM {table} WHERE {column}=?1");
    for id in ids {
        if count(executor, &sql, &[Value::text(id)])? != 0 {
            return Err(StorageError::DeletionUnverified {
                detail: format!("{table}.{column} still names {id}"),
            });
        }
    }
    Ok(())
}

fn text_column(
    executor: &mut dyn Executor,
    sql: &str,
    params: &[Value],
) -> Result<Vec<String>, StorageError> {
    executor
        .query(sql, params)?
        .iter()
        .map(|row| row.text(0).map(str::to_owned))
        .collect()
}
