// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Proving the deletion happened, and recording that it did.
//!
//! Verification is not optional and not advisory. If anything the deletion was
//! supposed to remove is still reachable, [`verify`] returns an error, the
//! caller rolls the transaction back, and no receipt is written. A deletion
//! that cannot be verified is a deletion that did not happen — which is why
//! the check and the receipt live in one module and the receipt is written
//! only after the check passes.

use super::policy::{DeletionReceipt, DeletionRequest, DerivedArtifactPolicy, DerivedFactPolicy};
use crate::backend::{count, Executor, Value};
use crate::error::StorageError;
use crate::search;

pub(super) fn verify(
    executor: &mut dyn Executor,
    request: &DeletionRequest,
    orphaned: &[String],
    affected_claims: &[String],
    affected_artifacts: &[String],
) -> Result<(), StorageError> {
    let key = Value::text(request.source_id.to_text());
    let checks: [(&str, &str); 6] = [
        (
            "the source row",
            "SELECT COUNT(*) FROM source WHERE source_id = ?1",
        ),
        (
            "an observation",
            "SELECT COUNT(*) FROM observation WHERE source_id = ?1",
        ),
        (
            "a provenance locator",
            "SELECT COUNT(*) FROM provenance_locator WHERE source_id = ?1",
        ),
        (
            "an index entry",
            "SELECT COUNT(*) FROM search_document WHERE source_id = ?1",
        ),
        (
            "a workspace membership",
            "SELECT COUNT(*) FROM workspace_source WHERE source_id = ?1",
        ),
        (
            "a journal projection",
            "SELECT COUNT(*) FROM journal_source_projection WHERE source_id = ?1",
        ),
    ];
    for (what, sql) in checks {
        let remaining = count(executor, sql, std::slice::from_ref(&key))?;
        if remaining != 0 {
            return Err(StorageError::DeletionUnverified {
                detail: format!("{remaining} rows still hold {what}"),
            });
        }
    }
    for fact_id in orphaned {
        let remaining = count(
            executor,
            "SELECT COUNT(*) FROM search_document WHERE record_type = 'FACT' AND record_id = ?1",
            &[Value::text(fact_id.clone())],
        )?;
        if remaining != 0 {
            return Err(StorageError::DeletionUnverified {
                detail: format!("{remaining} index entries still describe fact {fact_id}"),
            });
        }
        verify_fact_policy(executor, fact_id, request.fact_policy)?;
    }
    for claim_id in affected_claims {
        verify_claim_state(executor, claim_id)?;
    }
    for artifact_id in affected_artifacts {
        verify_artifact_policy(executor, artifact_id, request.artifact_policy)?;
    }
    let orphans = search::orphaned_index_rows(executor)?;
    if orphans != 0 {
        return Err(StorageError::DeletionUnverified {
            detail: format!("{orphans} index rows have no entry behind them"),
        });
    }
    Ok(())
}

fn verify_fact_policy(
    executor: &mut dyn Executor,
    fact_id: &str,
    policy: DerivedFactPolicy,
) -> Result<(), StorageError> {
    let key = Value::text(fact_id);
    let (what, sql, expected) = match policy {
        DerivedFactPolicy::Tombstone => (
            "fact tombstone",
            "SELECT COUNT(*) FROM fact WHERE fact_id = ?1 AND status = 'REMOVED' \
             AND typed_value = '' AND unit IS NULL AND confidence_basis_points IS NULL",
            1,
        ),
        DerivedFactPolicy::Delete => (
            "deleted fact",
            "SELECT COUNT(*) FROM fact WHERE fact_id = ?1",
            0,
        ),
    };
    let actual = count(executor, sql, std::slice::from_ref(&key))?;
    if actual != expected {
        return Err(StorageError::DeletionUnverified {
            detail: format!("{what} {fact_id} did not reach its requested state"),
        });
    }
    if policy == DerivedFactPolicy::Delete {
        for (relationship, sql) in [
            (
                "claim support",
                "SELECT COUNT(*) FROM claim_support WHERE fact_id = ?1",
            ),
            (
                "conflict membership",
                "SELECT COUNT(*) FROM conflict_fact WHERE fact_id = ?1",
            ),
        ] {
            let remaining = count(executor, sql, std::slice::from_ref(&key))?;
            if remaining != 0 {
                return Err(StorageError::DeletionUnverified {
                    detail: format!("{remaining} {relationship} rows still name fact {fact_id}"),
                });
            }
        }
    }
    Ok(())
}

fn verify_claim_state(executor: &mut dyn Executor, claim_id: &str) -> Result<(), StorageError> {
    let rows = executor.query(
        "SELECT c.validation_state, \
           (SELECT COUNT(*) FROM claim_support AS support WHERE support.claim_id = c.claim_id) \
         FROM claim AS c WHERE c.claim_id = ?1",
        &[Value::text(claim_id)],
    )?;
    let Some(row) = rows.first() else {
        return Err(StorageError::DeletionUnverified {
            detail: format!("affected claim {claim_id} disappeared instead of being relabeled"),
        });
    };
    let state = row.text(0)?;
    let remaining = row.integer(1)?;
    let expected = if remaining == 0 {
        "UNSUPPORTED"
    } else {
        "PARTIAL"
    };
    if state != expected {
        return Err(StorageError::DeletionUnverified {
            detail: format!(
                "claim {claim_id} is {state} after losing support, expected {expected}"
            ),
        });
    }
    Ok(())
}

fn verify_artifact_policy(
    executor: &mut dyn Executor,
    artifact_id: &str,
    policy: DerivedArtifactPolicy,
) -> Result<(), StorageError> {
    let key = Value::text(artifact_id);
    match policy {
        DerivedArtifactPolicy::MarkRemovedLineage => {
            let marked = count(
                executor,
                "SELECT COUNT(*) FROM artifact WHERE artifact_id = ?1 \
                 AND lineage_state = 'SOURCE_REMOVED'",
                std::slice::from_ref(&key),
            )?;
            if marked != 1 {
                return Err(StorageError::DeletionUnverified {
                    detail: format!(
                        "artifact {artifact_id} did not retain its removed-lineage mark"
                    ),
                });
            }
        }
        DerivedArtifactPolicy::Delete => {
            for (what, sql) in [
                (
                    "artifact row",
                    "SELECT COUNT(*) FROM artifact WHERE artifact_id = ?1",
                ),
                (
                    "artifact lineage row",
                    "SELECT COUNT(*) FROM artifact_lineage WHERE artifact_id = ?1",
                ),
                (
                    "artifact index entry",
                    "SELECT COUNT(*) FROM search_document \
                     WHERE record_type = 'ARTIFACT' AND record_id = ?1",
                ),
            ] {
                let remaining = count(executor, sql, std::slice::from_ref(&key))?;
                if remaining != 0 {
                    return Err(StorageError::DeletionUnverified {
                        detail: format!("{remaining} {what} values still name {artifact_id}"),
                    });
                }
            }
        }
    }
    Ok(())
}

pub(super) fn write_receipt(
    executor: &mut dyn Executor,
    receipt: &DeletionReceipt,
    request: &DeletionRequest,
    completed_at: &str,
) -> Result<(), StorageError> {
    let policy = match (request.fact_policy, request.artifact_policy) {
        (DerivedFactPolicy::Tombstone, DerivedArtifactPolicy::MarkRemovedLineage) => {
            "TOMBSTONE_AND_MARK"
        }
        (DerivedFactPolicy::Tombstone, DerivedArtifactPolicy::Delete) => "TOMBSTONE_AND_DELETE",
        (DerivedFactPolicy::Delete, DerivedArtifactPolicy::MarkRemovedLineage) => "DELETE_AND_MARK",
        (DerivedFactPolicy::Delete, DerivedArtifactPolicy::Delete) => "DELETE_AND_DELETE",
    };
    let counts = [
        receipt.observations_removed,
        receipt.provenance_removed,
        receipt.facts_removed,
        receipt.facts_relabeled,
        receipt.claims_relabeled,
        receipt.artifacts_removed,
        receipt.artifacts_relabeled,
        receipt.index_entries_removed,
        receipt.journal_events_relabeled,
        receipt.journal_projections_removed,
        receipt.memberships_removed,
        receipt.external_copies,
    ];
    let mut params = vec![
        Value::text(receipt.receipt_id.to_text()),
        Value::text("SOURCE"),
        Value::text(receipt.source_id.to_text()),
        Value::text(completed_at),
        Value::text(policy),
    ];
    for value in counts {
        params.push(Value::Integer(i64::try_from(value).unwrap_or(i64::MAX)));
    }
    params.push(Value::text(receipt.cloud_deletion.as_str()));
    params.push(Value::boolean(receipt.verified));
    executor.execute(
        "INSERT INTO deletion_receipt (receipt_id, subject_type, subject_id, completed_at_utc, \
         derived_policy, observations_removed, provenance_removed, facts_removed, \
         facts_relabeled, claims_relabeled, artifacts_removed, artifacts_relabeled, \
         index_entries_removed, journal_events_relabeled, journal_projections_removed, \
         memberships_removed, external_copies, cloud_deletion_state, verified) \
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19)",
        &params,
    )?;
    Ok(())
}
