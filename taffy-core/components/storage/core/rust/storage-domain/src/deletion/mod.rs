// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! End-to-end deletion of a source.
//!
//! Deleting a source is not a row delete. A source has derived dependents —
//! observations, provenance, facts, claims, conflicts, artifacts, index
//! entries, journal projections — and removing the source while any of them
//! survives leaves the material findable by exactly the search the user
//! believed they had emptied.
//!
//! So this is one operation, in one transaction, that verifies itself:
//!
//! 1. mark the source as deleting, which stops the ordinary lookups returning
//!    it, so nothing new is derived from it while the work runs;
//! 2. note the retained payload references the caller's blob store still owes,
//!    remove the provenance that cited the source, and then remove the
//!    observations it cited — that order, so no locator ever outlives the
//!    evidence it points at;
//! 3. decide about every fact that had cited the source — a fact left with no
//!    evidence goes, a fact with other evidence stays with its lineage
//!    narrowed;
//! 4. remove the index entries for the source and for every removed fact;
//! 5. relabel claims that lost their support, and either relabel or remove
//!    artifacts whose lineage included the source;
//! 6. remove the journal projections and relabel the events they pointed at —
//!    the record that a source was used is what makes an audit trustworthy, and
//!    the content of that use is what the user asked to remove;
//! 7. remove the memberships and the source row, leave a content-free
//!    tombstone, and verify.
//!
//! Verification is not optional and not advisory. If anything the deletion was
//! supposed to remove is still reachable, this returns an error, the
//! transaction is rolled back, and no receipt is written. A deletion that
//! cannot be verified is a deletion that did not happen.
//!
//! Two boundaries are reported rather than crossed. A copy the user exported to
//! another application is outside this authority and is counted, not claimed.
//! Cloud deletion is never implied by local success; the receipt says which
//! state applies, and it is always [`CloudDeletionState::NotApplicable`],
//! because workspace data is local and decision 0200 leaves no host of this
//! project's for it to have reached.
//!
//! # How this module is laid out
//!
//! | Module | Owns |
//! |---|---|
//! | [`policy`] | What a deletion is asked to do, and the receipt it returns |
//! | [`derived`] | The rules for facts, claims, and artifacts that cited the source |
//! | [`verify`] | The proof, and the receipt written only once it holds |
//!
//! The transaction itself — the seven ordered steps above — stays here, because
//! the order is the operation and splitting it would hide that.

mod derived;
mod policy;
mod verify;

pub use self::policy::{
    CloudDeletionState, DeletionReceipt, DeletionRequest, DerivedArtifactPolicy, DerivedFactPolicy,
};

use self::derived::{apply_artifact_policy, apply_fact_policy, relabel_claims};
use self::verify::{verify, write_receipt};
use crate::backend::{Connection, Executor, Value};
use crate::clock::Clock;
use crate::error::StorageError;
use crate::journal::{REDACTED_PAYLOAD, REDACTION_CLASS_SOURCE_DELETED};
use crate::search;

/// Deletes a source inside a transaction the caller owns.
///
/// On error the caller must roll back: partial deletion is the one outcome this
/// operation must never leave behind.
pub fn delete_source(
    executor: &mut dyn Executor,
    request: &DeletionRequest,
    clock: &dyn Clock,
) -> Result<DeletionReceipt, StorageError> {
    if !request.authority_revoked {
        return Err(StorageError::DeletionUnverified {
            detail: "running tasks were not stopped before deletion started".to_owned(),
        });
    }
    let key = Value::text(request.source_id.to_text());

    executor.execute(
        "UPDATE source SET deletion_state = 'DELETING' WHERE source_id = ?1",
        std::slice::from_ref(&key),
    )?;

    let blob_refs = collect_text(
        executor,
        "SELECT encrypted_payload_ref FROM observation \
         WHERE source_id = ?1 AND encrypted_payload_ref IS NOT NULL ORDER BY observation_id",
        std::slice::from_ref(&key),
    )?;
    // Identify facts for which this source is the last evidence in one indexed
    // query. The earlier implementation counted remaining provenance once per
    // cited fact, making source deletion an avoidable N+1 query path.
    let orphaned = collect_text(
        executor,
        "SELECT DISTINCT cited.fact_id FROM provenance_locator AS cited \
         WHERE cited.source_id = ?1 AND NOT EXISTS \
           (SELECT 1 FROM provenance_locator AS remaining \
            WHERE remaining.fact_id = cited.fact_id AND remaining.source_id <> ?1) \
         ORDER BY cited.fact_id",
        std::slice::from_ref(&key),
    )?;

    // Citations go before the evidence they cite. A locator that outlived its
    // observation would be a dangling reference into deleted material, which is
    // the shape of the bug this whole operation exists to prevent.
    let provenance_removed = executor.execute(
        "DELETE FROM provenance_locator WHERE source_id = ?1",
        std::slice::from_ref(&key),
    )?;
    let observations_removed = executor.execute(
        "DELETE FROM observation WHERE source_id = ?1",
        std::slice::from_ref(&key),
    )?;

    let mut index_entries_removed = search::remove_for_source(executor, request.source_id)?;
    for fact_id in &orphaned {
        index_entries_removed += search::remove_for_record(executor, "FACT", fact_id)?;
    }

    // Claims have to see the support rows before a hard-delete policy removes
    // them. Once the relationship is gone there is no honest way to discover
    // which claim became partial or unsupported.
    let (claims_relabeled, affected_claims) = relabel_claims(executor, &orphaned)?;
    let (facts_removed, facts_relabeled) = apply_fact_policy(executor, &orphaned, request)?;
    let (artifacts_removed, artifacts_relabeled, external_copies, affected_artifacts) =
        apply_artifact_policy(executor, &orphaned, request)?;

    let (journal_projections_removed, journal_events_relabeled) = relabel_journal(executor, &key)?;

    let memberships_removed = executor.execute(
        "DELETE FROM workspace_source WHERE source_id = ?1",
        std::slice::from_ref(&key),
    )?;
    executor.execute(
        "DELETE FROM source WHERE source_id = ?1",
        std::slice::from_ref(&key),
    )?;

    let deleted_at = clock.now_utc();
    executor.execute(
        "INSERT OR REPLACE INTO deletion_tombstone \
         (record_type, record_id, deleted_at_utc, revision, retention_class) \
         VALUES ('SOURCE', ?1, ?2, 1, 'DELETION_TOMBSTONE')",
        &[key.clone(), Value::text(deleted_at.as_str())],
    )?;

    verify(
        executor,
        request,
        &orphaned,
        &affected_claims,
        &affected_artifacts,
    )?;

    let receipt = DeletionReceipt {
        receipt_id: request.receipt_id,
        source_id: request.source_id,
        blob_refs_released: blob_refs,
        observations_removed,
        provenance_removed,
        facts_removed,
        facts_relabeled,
        claims_relabeled,
        artifacts_removed,
        artifacts_relabeled,
        index_entries_removed,
        journal_events_relabeled,
        journal_projections_removed,
        memberships_removed,
        external_copies,
        cloud_deletion: CloudDeletionState::NotApplicable,
        verified: true,
    };
    write_receipt(executor, &receipt, request, deleted_at.as_str())?;
    Ok(receipt)
}

/// Deletes a source in its own transaction, committing only if it verifies.
pub fn run_deletion(
    connection: &mut dyn Connection,
    request: &DeletionRequest,
    clock: &dyn Clock,
) -> Result<DeletionReceipt, StorageError> {
    let mut transaction = connection.begin()?;
    match delete_source(transaction.as_mut(), request, clock) {
        Ok(receipt) => {
            transaction.commit()?;
            Ok(receipt)
        }
        Err(error) => {
            transaction.rollback()?;
            Err(error)
        }
    }
}

/// Removes the source projections and redacts the events they pointed at.
fn relabel_journal(executor: &mut dyn Executor, key: &Value) -> Result<(u64, u64), StorageError> {
    let event_ids = collect_text(
        executor,
        "SELECT event_id FROM journal_source_projection WHERE source_id = ?1 ORDER BY event_id",
        std::slice::from_ref(key),
    )?;
    let projections_removed = executor.execute(
        "DELETE FROM journal_source_projection WHERE source_id = ?1",
        std::slice::from_ref(key),
    )?;
    let mut relabeled = 0;
    for event_id in &event_ids {
        relabeled += executor.execute(
            "UPDATE task_event SET payload = ?1, redaction_class = ?2 WHERE event_id = ?3",
            &[
                Value::text(REDACTED_PAYLOAD),
                Value::text(REDACTION_CLASS_SOURCE_DELETED),
                Value::text(event_id.clone()),
            ],
        )?;
    }
    Ok((projections_removed, relabeled))
}

/// Reads one text column out of a query, as owned strings.
///
/// Shared by the transaction here and the derived-material rules beside it,
/// so that "which rows did this step touch" is expressed the same way in both.
pub(super) fn collect_text(
    executor: &mut dyn Executor,
    sql: &str,
    params: &[Value],
) -> Result<Vec<String>, StorageError> {
    let rows = executor.query(sql, params)?;
    rows.iter()
        .map(|row| row.text(0).map(str::to_owned))
        .collect()
}
