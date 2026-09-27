// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a deletion is asked to do, and what it reports having done.
//!
//! Three closed enumerations and two records. They are one module because they
//! are the *contract* of a deletion — the caller's choices about derived
//! material, and the receipt that says what actually happened — while the
//! transaction that carries them out is everything else in this area.
//!
//! One boundary is reported rather than crossed: cloud deletion is never
//! implied by local success. The receipt says which state applies, and it is
//! always [`CloudDeletionState::NotApplicable`], because workspace data is
//! local and decision 0200 leaves no host of this project's for it to have
//! reached.

use crate::ids::{ReceiptId, SourceId};
/// What to do with a fact whose last evidence was the deleted source.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DerivedFactPolicy {
    /// Keep the record, remove the value, mark it removed. The workspace can
    /// still explain why a result changed.
    Tombstone,
    /// Remove the record.
    Delete,
}

/// What to do with an artifact whose lineage included the deleted source.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DerivedArtifactPolicy {
    /// Keep the artifact and mark that part of its lineage is gone.
    MarkRemovedLineage,
    /// Remove the local artifact.
    Delete,
}

/// Whether anything is owed to a remote copy.
///
/// One state, because there is one answer. This enumeration carried a `Queued`
/// variant for a remote deletion awaiting confirmation, and nothing ever
/// constructed it: decision 0200 leaves no TaffyGo-operated destination for a
/// workspace record to have reached, so there is no remote copy and nothing is
/// owed to one. The type stays rather than collapsing into the receipt,
/// because a receipt that simply omitted the question would read as an
/// oversight instead of an answer.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CloudDeletionState {
    /// Nothing was ever sent, and there is nowhere it could have been sent to.
    NotApplicable,
}

impl CloudDeletionState {
    /// The stored spelling.
    pub fn as_str(self) -> &'static str {
        match self {
            Self::NotApplicable => "NOT_APPLICABLE",
        }
    }
}

/// One deletion.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DeletionRequest {
    /// Identity of the receipt to write.
    pub receipt_id: ReceiptId,
    /// What to delete.
    pub source_id: SourceId,
    /// What to do with orphaned facts.
    pub fact_policy: DerivedFactPolicy,
    /// What to do with artifacts that used it.
    pub artifact_policy: DerivedArtifactPolicy,
    /// Whether running tasks have been stopped and their authority revoked.
    ///
    /// The task machine does that, not this crate. Deletion refuses to start
    /// until the caller says it happened, so the ordering is explicit instead of
    /// assumed.
    pub authority_revoked: bool,
}

/// What a deletion did.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DeletionReceipt {
    /// Identity.
    pub receipt_id: ReceiptId,
    /// What was deleted.
    pub source_id: SourceId,
    /// Payload references the caller's blob store still has to release.
    pub blob_refs_released: Vec<String>,
    /// Observations removed.
    pub observations_removed: u64,
    /// Provenance locators removed.
    pub provenance_removed: u64,
    /// Facts removed outright.
    pub facts_removed: u64,
    /// Facts kept with their value removed.
    pub facts_relabeled: u64,
    /// Claims whose support state changed.
    pub claims_relabeled: u64,
    /// Artifacts removed.
    pub artifacts_removed: u64,
    /// Artifacts kept with their lineage marked.
    pub artifacts_relabeled: u64,
    /// Index entries removed.
    pub index_entries_removed: u64,
    /// Journal events relabeled.
    pub journal_events_relabeled: u64,
    /// Journal projections removed.
    pub journal_projections_removed: u64,
    /// Workspace memberships removed.
    pub memberships_removed: u64,
    /// Copies the user exported elsewhere, which are outside this authority.
    pub external_copies: u64,
    /// What is owed to a remote copy.
    pub cloud_deletion: CloudDeletionState,
    /// Whether verification passed. Always true in a returned receipt.
    pub verified: bool,
}
