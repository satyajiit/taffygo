// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Content-free deletion preview and exact current-state confirmation digest.

use crate::backend::{count, Executor, Value};
use crate::error::StorageError;
use crate::ids::WorkspaceId;
use crate::records;

use super::{WorkspaceDeletionCounts, WorkspaceDeletionPreview};

const CONFIRMATION_DOMAIN: &[u8] = b"\0taffy.storage.workspace-delete.v1\0";

pub trait WorkspaceConfirmationDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], StorageError>;
}

pub fn preview_workspace_deletion(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
    digest: &dyn WorkspaceConfirmationDigest,
) -> Result<WorkspaceDeletionPreview, StorageError> {
    let workspace = records::load_workspace(executor, workspace_id)?
        .filter(|workspace| workspace.deletion_state.as_str() == "ACTIVE")
        .ok_or(StorageError::WorkspaceNotFound)?;
    let expected_revision =
        u64::try_from(workspace.revision).map_err(|_| StorageError::Malformed {
            what: "a workspace revision",
            detail: workspace.revision.to_string(),
        })?;
    let counts = deletion_counts(executor, workspace_id)?;
    let binding = state_binding(executor, workspace_id, expected_revision, counts)?;
    let token = digest.sha256(&binding)?;
    Ok(WorkspaceDeletionPreview {
        workspace_id,
        expected_revision,
        counts,
        confirmation_token: lower_hex(&token),
    })
}

pub(super) fn deletion_counts(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
) -> Result<WorkspaceDeletionCounts, StorageError> {
    let key = Value::text(workspace_id.to_text());
    let sources = nonnegative(count(
        executor,
        "SELECT COUNT(*) FROM workspace_source WHERE workspace_id = ?1",
        std::slice::from_ref(&key),
    )?)?;
    let facts = nonnegative(count(
        executor,
        "SELECT COUNT(*) FROM fact WHERE workspace_id = ?1",
        std::slice::from_ref(&key),
    )?)?;
    let artifact_metadata = nonnegative(count(
        executor,
        "SELECT COUNT(*) FROM artifact WHERE workspace_id = ?1 OR task_id IN \
         (SELECT task_id FROM task WHERE workspace_id = ?1) OR artifact_id IN \
         (SELECT lineage.artifact_id FROM artifact_lineage lineage WHERE \
          (lineage.related_type='FACT' AND lineage.related_id IN \
           (SELECT fact_id FROM fact WHERE workspace_id=?1)) OR \
          (lineage.related_type='SOURCE' AND lineage.related_id IN \
           (SELECT owned.source_id FROM workspace_source owned WHERE owned.workspace_id=?1 \
            AND NOT EXISTS (SELECT 1 FROM workspace_source other WHERE \
            other.source_id=owned.source_id AND other.workspace_id<>?1))))",
        std::slice::from_ref(&key),
    )?)?;
    let derived_indexes = nonnegative(count(
        executor,
        scoped_index_count_sql(),
        std::slice::from_ref(&key),
    )?)?;
    let external_copies = executor
        .query(
            "SELECT COALESCE(SUM(exported_copies), 0) FROM artifact WHERE workspace_id = ?1 \
             OR task_id IN (SELECT task_id FROM task WHERE workspace_id = ?1) OR artifact_id IN \
             (SELECT lineage.artifact_id FROM artifact_lineage lineage WHERE \
              (lineage.related_type='FACT' AND lineage.related_id IN \
               (SELECT fact_id FROM fact WHERE workspace_id=?1)) OR \
              (lineage.related_type='SOURCE' AND lineage.related_id IN \
               (SELECT owned.source_id FROM workspace_source owned WHERE owned.workspace_id=?1 \
                AND NOT EXISTS (SELECT 1 FROM workspace_source other WHERE \
                other.source_id=owned.source_id AND other.workspace_id<>?1))))",
            std::slice::from_ref(&key),
        )?
        .first()
        .ok_or(StorageError::RowMissing {
            what: "workspace artifact copies",
        })?
        .integer(0)
        .and_then(nonnegative)?;
    Ok(WorkspaceDeletionCounts {
        sources,
        facts,
        artifact_metadata,
        derived_indexes,
        external_copies,
    })
}

pub(super) fn scoped_index_count_sql() -> &'static str {
    "SELECT COUNT(*) FROM search_document WHERE workspace_id = ?1 OR source_id IN \
     (SELECT owned.source_id FROM workspace_source AS owned WHERE owned.workspace_id = ?1 \
      AND NOT EXISTS (SELECT 1 FROM workspace_source AS other \
                      WHERE other.source_id = owned.source_id AND other.workspace_id <> ?1)) \
     OR (record_type = 'FACT' AND record_id IN \
         (SELECT fact_id FROM fact WHERE workspace_id = ?1)) \
     OR (record_type = 'ARTIFACT' AND record_id IN \
         (SELECT artifact_id FROM artifact WHERE workspace_id = ?1 OR task_id IN \
          (SELECT task_id FROM task WHERE workspace_id = ?1) OR artifact_id IN \
          (SELECT lineage.artifact_id FROM artifact_lineage lineage WHERE \
           lineage.related_type='FACT' AND lineage.related_id IN \
           (SELECT fact_id FROM fact WHERE workspace_id=?1))))"
}

pub(super) fn scoped_index_ids_sql() -> &'static str {
    "SELECT document_id FROM search_document WHERE workspace_id = ?1 OR source_id IN \
     (SELECT owned.source_id FROM workspace_source AS owned WHERE owned.workspace_id = ?1 \
      AND NOT EXISTS (SELECT 1 FROM workspace_source AS other \
                      WHERE other.source_id = owned.source_id AND other.workspace_id <> ?1)) \
     OR (record_type = 'FACT' AND record_id IN \
         (SELECT fact_id FROM fact WHERE workspace_id = ?1)) \
     OR (record_type = 'ARTIFACT' AND record_id IN \
         (SELECT artifact_id FROM artifact WHERE workspace_id = ?1 OR task_id IN \
          (SELECT task_id FROM task WHERE workspace_id = ?1) OR artifact_id IN \
          (SELECT lineage.artifact_id FROM artifact_lineage lineage WHERE \
           lineage.related_type='FACT' AND lineage.related_id IN \
           (SELECT fact_id FROM fact WHERE workspace_id=?1)))) ORDER BY document_id"
}

fn state_binding(
    executor: &mut dyn Executor,
    workspace_id: WorkspaceId,
    revision: u64,
    counts: WorkspaceDeletionCounts,
) -> Result<Vec<u8>, StorageError> {
    let key = Value::text(workspace_id.to_text());
    let mut output = Vec::from(CONFIRMATION_DOMAIN);
    append_value(&mut output, &key)?;
    append_value(
        &mut output,
        &Value::Integer(
            i64::try_from(revision).map_err(|_| StorageError::Malformed {
                what: "a workspace revision",
                detail: revision.to_string(),
            })?,
        ),
    )?;
    for value in [
        counts.sources,
        counts.facts,
        counts.artifact_metadata,
        counts.derived_indexes,
        counts.external_copies,
    ] {
        append_value(
            &mut output,
            &Value::Integer(i64::try_from(value).map_err(|_| StorageError::Malformed {
                what: "a workspace deletion count",
                detail: value.to_string(),
            })?),
        )?;
    }
    for &(sql, width) in EVIDENCE_BINDINGS.iter().chain(TASK_ARTIFACT_BINDINGS) {
        let rows = executor.query(sql, std::slice::from_ref(&key))?;
        append_rows(&mut output, &rows, width)?;
    }
    let index_rows = executor.query(scoped_index_ids_sql(), std::slice::from_ref(&key))?;
    append_rows(&mut output, &index_rows, 1)?;
    for row in &index_rows {
        let document_id = row.text(0)?;
        let content = executor.query(
            "SELECT document_id,title,body FROM search_index WHERE document_id=?1",
            &[Value::text(document_id)],
        )?;
        append_rows(&mut output, &content, 3)?;
    }
    Ok(output)
}

const EVIDENCE_BINDINGS: &[(&str, usize)] = &[
    (
        "SELECT workspace_id,browser_profile_id,owner_account_id,title,status,revision,\
             created_at_utc,updated_at_utc,retention_class,sensitivity,deletion_state,\
             accepted_artifact_id,schema_version FROM workspace WHERE workspace_id = ?1",
        13,
    ),
    (
        "SELECT m.source_id,m.membership_state,m.added_by,m.added_at_utc,m.scope_receipt,\
             m.excluded_at_utc,m.removal_reason,s.kind,s.canonical_locator,s.display_locator,\
             s.origin,s.title,s.first_seen_at_utc,s.last_observed_at_utc,s.ownership,s.sensitivity,\
             s.retention_class,s.deletion_state,s.schema_version FROM workspace_source m JOIN \
             source s ON s.source_id=m.source_id WHERE m.workspace_id=?1 ORDER BY m.source_id",
        19,
    ),
    (
        "SELECT fact_id,subject_key,predicate,typed_value,unit,classification,\
             confidence_basis_points,observation_time_utc,validity_start_utc,validity_end_utc,\
             sensitivity,status,supersedes_fact_id,retention_class,schema_version FROM fact \
             WHERE workspace_id=?1 ORDER BY fact_id",
        15,
    ),
    (
        "SELECT provenance_id,fact_id,source_id,observation_id,source_kind,\
             location_descriptor,extraction_rule_version,transformation_chain,captured_at_utc \
             FROM provenance_locator WHERE fact_id IN \
             (SELECT fact_id FROM fact WHERE workspace_id=?1) ORDER BY provenance_id",
        9,
    ),
    (
        "SELECT conflict_id,subject_key,predicate,reason,resolution_state,resolution_fact_id,\
             resolution_rule,explanation,schema_version FROM conflict WHERE workspace_id=?1 \
             ORDER BY conflict_id",
        9,
    ),
    (
        "SELECT membership.conflict_id,membership.fact_id FROM conflict_fact membership \
             WHERE membership.conflict_id IN \
             (SELECT conflict_id FROM conflict WHERE workspace_id=?1) OR membership.fact_id IN \
             (SELECT fact_id FROM fact WHERE workspace_id=?1) \
             ORDER BY membership.conflict_id,membership.fact_id",
        2,
    ),
];

const TASK_ARTIFACT_BINDINGS: &[(&str, usize)] = &[
        (
            "SELECT task_id,state,execution_phase,state_reason,revision,user_goal,created_by,\
             assistant_snapshot,control_mode,source_scope,data_policy_snapshot,\
             provider_route_snapshot,budgets,created_at_utc,updated_at_utc,deadline_utc,\
             retention_class,schema_version FROM task WHERE workspace_id=?1 ORDER BY task_id",
            18,
        ),
        (
            "SELECT event_id,aggregate_type,aggregate_id,aggregate_revision,event_type,\
             schema_version,occurred_at_utc,monotonic_sequence,actor,task_id,trace_id,\
             causation_event_id,correlation_id,redaction_class,payload FROM task_event WHERE \
             task_id IN (SELECT task_id FROM task WHERE workspace_id=?1) ORDER BY event_id",
            15,
        ),
        (
            "SELECT event_id,source_id,workspace_id,occurred_at_utc,summary FROM \
             journal_source_projection WHERE workspace_id=?1 OR event_id IN \
             (SELECT event_id FROM task_event WHERE task_id IN \
              (SELECT task_id FROM task WHERE workspace_id=?1)) ORDER BY event_id,source_id",
            5,
        ),
        (
            "SELECT task_id,state,execution_phase,revision,updated_at_utc,last_event_id FROM \
             task_state_projection WHERE task_id IN \
             (SELECT task_id FROM task WHERE workspace_id=?1) ORDER BY task_id",
            6,
        ),
        (
            "SELECT model_invocation_id,task_id,purpose,provider_route_snapshot,model_identifier,\
             route_version,context_manifest,redaction_policy_version,request_digest,response_digest,\
             input_units,output_units,cost_class,started_at_utc,ended_at_utc,result_code,retry_of,\
             retention_class,schema_version FROM model_invocation WHERE task_id IN \
             (SELECT task_id FROM task WHERE workspace_id=?1) ORDER BY model_invocation_id",
            19,
        ),
        (
            "SELECT artifact_id,task_id,kind,state,lineage_state,generation_method,\
             validation_result,content_digest,encrypted_blob_ref,size_bytes,created_at_utc,\
             accepted_at_utc,exported_copies,retention_class,schema_version FROM artifact \
             WHERE workspace_id=?1 OR task_id IN \
             (SELECT task_id FROM task WHERE workspace_id=?1) OR artifact_id IN \
             (SELECT lineage.artifact_id FROM artifact_lineage lineage WHERE \
              (lineage.related_type='FACT' AND lineage.related_id IN \
               (SELECT fact_id FROM fact WHERE workspace_id=?1)) OR \
              (lineage.related_type='SOURCE' AND lineage.related_id IN \
               (SELECT owned.source_id FROM workspace_source owned WHERE owned.workspace_id=?1 \
                AND NOT EXISTS (SELECT 1 FROM workspace_source other WHERE \
                other.source_id=owned.source_id AND other.workspace_id<>?1)))) \
             ORDER BY artifact_id",
            15,
        ),
        (
            "SELECT artifact_id,related_type,related_id FROM artifact_lineage WHERE artifact_id IN \
             (SELECT artifact_id FROM artifact WHERE workspace_id=?1 OR task_id IN \
              (SELECT task_id FROM task WHERE workspace_id=?1)) ORDER BY artifact_id,related_type,related_id",
            3,
        ),
        (
            "SELECT claim_id,task_id,artifact_id,body,classification,validation_state,generated_by,\
             schema_version FROM claim WHERE task_id IN \
             (SELECT task_id FROM task WHERE workspace_id=?1) OR artifact_id IN \
             (SELECT artifact_id FROM artifact WHERE workspace_id=?1 OR task_id IN \
              (SELECT task_id FROM task WHERE workspace_id=?1)) ORDER BY claim_id",
            8,
        ),
        (
            "SELECT claim_id,fact_id FROM claim_support WHERE fact_id IN \
             (SELECT fact_id FROM fact WHERE workspace_id=?1) OR claim_id IN \
             (SELECT claim_id FROM claim WHERE task_id IN \
              (SELECT task_id FROM task WHERE workspace_id=?1)) ORDER BY claim_id,fact_id",
            2,
        ),
];

fn append_rows(
    output: &mut Vec<u8>,
    rows: &[crate::backend::Row],
    width: usize,
) -> Result<(), StorageError> {
    output.extend_from_slice(
        &u64::try_from(rows.len())
            .map_err(|_| StorageError::Malformed {
                what: "workspace state rows",
                detail: rows.len().to_string(),
            })?
            .to_be_bytes(),
    );
    for row in rows {
        if row.len() != width {
            return Err(StorageError::Malformed {
                what: "workspace state row width",
                detail: row.len().to_string(),
            });
        }
        for column in 0..width {
            append_value(output, row.value(column)?)?;
        }
    }
    Ok(())
}

fn append_value(output: &mut Vec<u8>, value: &Value) -> Result<(), StorageError> {
    match value {
        Value::Null => output.push(0),
        Value::Integer(inner) => {
            output.push(1);
            output.extend_from_slice(&inner.to_be_bytes());
        }
        Value::Text(inner) => append_bytes(output, 2, inner.as_bytes())?,
        Value::Blob(inner) => append_bytes(output, 3, inner)?,
    }
    Ok(())
}

fn append_bytes(output: &mut Vec<u8>, tag: u8, value: &[u8]) -> Result<(), StorageError> {
    output.push(tag);
    output.extend_from_slice(
        &u64::try_from(value.len())
            .map_err(|_| StorageError::Malformed {
                what: "workspace state bytes",
                detail: value.len().to_string(),
            })?
            .to_be_bytes(),
    );
    output.extend_from_slice(value);
    Ok(())
}

fn nonnegative(value: i64) -> Result<u64, StorageError> {
    u64::try_from(value).map_err(|_| StorageError::Malformed {
        what: "a workspace deletion count",
        detail: value.to_string(),
    })
}

fn lower_hex(value: &[u8; 32]) -> String {
    let mut output = String::with_capacity(64);
    for byte in value {
        output.push(char::from_digit(u32::from(byte >> 4), 16).unwrap_or('0'));
        output.push(char::from_digit(u32::from(byte & 0x0f), 16).unwrap_or('0'));
    }
    output
}
