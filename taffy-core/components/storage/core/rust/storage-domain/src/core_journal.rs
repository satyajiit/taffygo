// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Pure invariants for browser-owned Core Service transaction batches.
//!
//! This module owns no codec and performs no I/O. The Core Service contract
//! owns the encoded DTO; the browser owns the versioned SQL journal and is its
//! only physical writer. These checks keep both adapters on one append-only
//! shape without making this crate a storage service.

/// Maximum records of one category accepted in a single atomic batch.
pub const MAX_BATCH_ITEMS: usize = 4_096;

/// Content-free shape checked before encoding a durable intent.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CommitShape {
    pub expected_revision: u64,
    pub resulting_revision: u64,
    pub has_seed: bool,
    pub journal_entry_count: usize,
    pub event_count: usize,
    pub effect_intent_count: usize,
    pub audit_record_count: usize,
}

/// Why a transaction could not be a canonical append.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CommitShapeError {
    RevisionDidNotAdvance,
    SeedAtWrongRevision,
    MissingJournalEntries,
    AuditEventMismatch,
    CollectionLimit,
}

/// Validates the portable append-only transaction shape.
pub const fn validate_commit_shape(shape: CommitShape) -> Result<(), CommitShapeError> {
    if shape.resulting_revision <= shape.expected_revision {
        return Err(CommitShapeError::RevisionDidNotAdvance);
    }
    if shape.has_seed != (shape.expected_revision == 0) {
        return Err(CommitShapeError::SeedAtWrongRevision);
    }
    if shape.journal_entry_count == 0 || shape.event_count == 0 {
        return Err(CommitShapeError::MissingJournalEntries);
    }
    if shape.audit_record_count != shape.event_count {
        return Err(CommitShapeError::AuditEventMismatch);
    }
    if shape.journal_entry_count > MAX_BATCH_ITEMS
        || shape.effect_intent_count > MAX_BATCH_ITEMS
        || shape.audit_record_count > MAX_BATCH_ITEMS
    {
        return Err(CommitShapeError::CollectionLimit);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{validate_commit_shape, CommitShape, CommitShapeError};

    #[test]
    fn creation_requires_a_seed_and_one_audit_record_per_event() {
        let valid = CommitShape {
            expected_revision: 0,
            resulting_revision: 1,
            has_seed: true,
            journal_entry_count: 2,
            event_count: 1,
            effect_intent_count: 0,
            audit_record_count: 1,
        };
        assert_eq!(validate_commit_shape(valid), Ok(()));
        assert_eq!(
            validate_commit_shape(CommitShape {
                has_seed: false,
                ..valid
            }),
            Err(CommitShapeError::SeedAtWrongRevision)
        );
    }
}
