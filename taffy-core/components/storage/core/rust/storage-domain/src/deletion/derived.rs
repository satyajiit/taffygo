// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deciding about the material that was derived from the deleted source.
//!
//! Facts, claims, and artifacts each have their own rule, and each rule is the
//! same shape: something that had no support other than the deleted source is
//! removed, and something that still has other support survives with its
//! lineage narrowed rather than silently unchanged.
//!
//! Its own module because these are the decisions a reviewer needs to read
//! together — they are where "the material stops being findable" is either
//! true or not.

use std::collections::{BTreeMap, BTreeSet};

use super::policy::{DeletionRequest, DerivedArtifactPolicy, DerivedFactPolicy};
use crate::backend::{count, Executor, Value};
use crate::error::StorageError;
use crate::search;

use super::collect_text;
pub(super) fn apply_fact_policy(
    executor: &mut dyn Executor,
    orphaned: &[String],
    request: &DeletionRequest,
) -> Result<(u64, u64), StorageError> {
    let mut removed = 0;
    let mut relabeled = 0;
    for fact_id in orphaned {
        let key = Value::text(fact_id.clone());
        match request.fact_policy {
            DerivedFactPolicy::Tombstone => {
                relabeled += executor.execute(
                    "UPDATE fact SET status = 'REMOVED', typed_value = '', unit = NULL, \
                     confidence_basis_points = NULL WHERE fact_id = ?1",
                    &[key],
                )?;
            }
            DerivedFactPolicy::Delete => {
                executor.execute(
                    "DELETE FROM claim_support WHERE fact_id = ?1",
                    std::slice::from_ref(&key),
                )?;
                executor.execute(
                    "DELETE FROM conflict_fact WHERE fact_id = ?1",
                    std::slice::from_ref(&key),
                )?;
                removed += executor.execute("DELETE FROM fact WHERE fact_id = ?1", &[key])?;
            }
        }
    }
    Ok((removed, relabeled))
}

pub(super) fn relabel_claims(
    executor: &mut dyn Executor,
    orphaned: &[String],
) -> Result<(u64, Vec<String>), StorageError> {
    let mut affected = BTreeSet::new();
    for fact_id in orphaned {
        affected.extend(collect_text(
            executor,
            "SELECT claim_id FROM claim_support WHERE fact_id = ?1 ORDER BY claim_id",
            &[Value::text(fact_id.clone())],
        )?);
        executor.execute(
            "DELETE FROM claim_support WHERE fact_id = ?1",
            &[Value::text(fact_id.clone())],
        )?;
    }

    let affected: Vec<String> = affected.into_iter().collect();
    let mut relabeled = 0;
    for claim_id in &affected {
        let remaining = count(
            executor,
            "SELECT COUNT(*) FROM claim_support WHERE claim_id = ?1",
            &[Value::text(claim_id.clone())],
        )?;
        let state = if remaining == 0 {
            "UNSUPPORTED"
        } else {
            "PARTIAL"
        };
        relabeled += executor.execute(
            "UPDATE claim SET validation_state = ?1 WHERE claim_id = ?2",
            &[Value::text(state), Value::text(claim_id)],
        )?;
    }
    Ok((relabeled, affected))
}

pub(super) fn apply_artifact_policy(
    executor: &mut dyn Executor,
    orphaned: &[String],
    request: &DeletionRequest,
) -> Result<(u64, u64, u64, Vec<String>), StorageError> {
    let mut affected = BTreeMap::new();
    collect_affected_artifacts(
        executor,
        "SOURCE",
        &request.source_id.to_text(),
        &mut affected,
    )?;
    for fact_id in orphaned {
        collect_affected_artifacts(executor, "FACT", fact_id, &mut affected)?;
    }
    let external_copies = affected.values().try_fold(0_u64, |total, copies| {
        total.checked_add(*copies).ok_or(StorageError::Malformed {
            what: "an artifact export count",
            detail: "the affected export total exceeds u64".to_owned(),
        })
    })?;

    let mut removed = 0;
    let mut relabeled = 0;
    for artifact_id in affected.keys() {
        let key = Value::text(artifact_id.clone());
        match request.artifact_policy {
            DerivedArtifactPolicy::MarkRemovedLineage => {
                relabeled += executor.execute(
                    "UPDATE artifact SET lineage_state = 'SOURCE_REMOVED' WHERE artifact_id = ?1",
                    &[key],
                )?;
            }
            DerivedArtifactPolicy::Delete => {
                executor.execute(
                    "DELETE FROM artifact_lineage WHERE artifact_id = ?1",
                    std::slice::from_ref(&key),
                )?;
                search::remove_for_record(executor, "ARTIFACT", artifact_id)?;
                removed +=
                    executor.execute("DELETE FROM artifact WHERE artifact_id = ?1", &[key])?;
            }
        }
    }
    Ok((
        removed,
        relabeled,
        external_copies,
        affected.into_keys().collect(),
    ))
}

fn collect_affected_artifacts(
    executor: &mut dyn Executor,
    related_type: &str,
    related_id: &str,
    affected: &mut BTreeMap<String, u64>,
) -> Result<(), StorageError> {
    let rows = executor.query(
        "SELECT DISTINCT a.artifact_id, a.exported_copies \
         FROM artifact_lineage AS l JOIN artifact AS a ON a.artifact_id = l.artifact_id \
         WHERE l.related_type = ?1 AND l.related_id = ?2 ORDER BY a.artifact_id",
        &[Value::text(related_type), Value::text(related_id)],
    )?;
    for row in &rows {
        let artifact_id = row.text(0)?.to_owned();
        let raw_copies = row.integer(1)?;
        let copies = u64::try_from(raw_copies).map_err(|_| StorageError::Malformed {
            what: "an artifact export count",
            detail: raw_copies.to_string(),
        })?;
        affected.insert(artifact_id, copies);
    }
    Ok(())
}
