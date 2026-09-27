// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Portable durable Memory aggregate.
//!
//! Memory is a bounded set of person-visible preference statements. Every
//! record names who added it, where it may affect future tasks, and the exact
//! revision a writer observed. Nothing in this module observes a page or
//! infers a preference: callers may construct a record only after a person
//! writes it or explicitly accepts one task suggestion.

mod cache;
mod search;
mod store;
mod validation;

use crate::ids::{MemoryId, WorkspaceId};

pub use self::search::{MemoryHit, MemoryQuery, MemorySearchAudience};
pub use self::store::{MemoryMutation, MemoryMutationPlan, MemoryPersistRequest, MemoryStore};

/// Maximum active records restored into one regular profile core.
pub const MAX_MEMORY_RECORDS: usize = 512;
/// Maximum UTF-8 bytes in one preference sentence.
pub const MAX_MEMORY_STATEMENT_BYTES: usize = 2_048;
/// Maximum UTF-8 bytes in a Memory search query.
pub const MAX_MEMORY_QUERY_BYTES: usize = 512;
/// Maximum terms in one Memory search query.
pub const MAX_MEMORY_QUERY_TERMS: usize = 16;
/// Maximum results returned by one Memory search.
pub const MAX_MEMORY_SEARCH_RESULTS: usize = 32;
/// Maximum staged changes waiting for browser-owned durability.
pub const MAX_PENDING_MEMORY_MUTATIONS: usize = 64;
/// Maximum opaque operation identity length.
pub const MAX_MEMORY_OPERATION_ID_BYTES: usize = 128;
/// Maximum durable task identity length retained for attribution.
pub const MAX_MEMORY_TASK_ID_BYTES: usize = 128;

/// Why this record exists. The accepted-suggestion variant is reached only
/// after the person approves the exact model proposal.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum MemorySource {
    UserEntered,
    AcceptedTaskSuggestion {
        task_id: String,
        workspace: Option<MemoryWorkspace>,
    },
}

/// Display-safe workspace attribution or scope.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MemoryWorkspace {
    pub workspace_id: WorkspaceId,
    pub display_name: String,
}

/// Where this statement may affect future work.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum MemoryScope {
    AllTasks,
    Workspace(MemoryWorkspace),
}

/// User-visible handling class. Sensitive Memory remains reviewable but is
/// never returned to a model by the ordinary task-search audience.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MemorySensitivity {
    Standard,
    Sensitive,
}

/// One durable, attributable preference the person can review and remove.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MemoryRecord {
    pub memory_id: MemoryId,
    pub revision: u64,
    pub statement: String,
    pub source: MemorySource,
    pub scope: MemoryScope,
    pub sensitivity: MemorySensitivity,
    pub created_at_epoch_ms: u64,
    pub updated_at_epoch_ms: u64,
    pub reviewed_at_epoch_ms: Option<u64>,
    pub expires_at_epoch_ms: Option<u64>,
}

impl MemoryRecord {
    /// Whether the preference may still affect work at this instant.
    pub fn is_active_at(&self, now_epoch_ms: u64) -> bool {
        self.expires_at_epoch_ms
            .is_none_or(|expiry| expiry > now_epoch_ms)
    }

    /// Whether every field satisfies the portable storage contract.
    pub fn validate(&self) -> bool {
        validation::valid_record(self)
    }

    fn same_content(&self, other: &Self) -> bool {
        self.memory_id == other.memory_id
            && self.statement == other.statement
            && self.source == other.source
            && self.scope == other.scope
            && self.sensitivity == other.sensitivity
            && self.created_at_epoch_ms == other.created_at_epoch_ms
            && self.expires_at_epoch_ms == other.expires_at_epoch_ms
    }
}

/// Why a Memory request was refused before any browser effect existed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MemoryError {
    InvalidIdentifier,
    InvalidOperation,
    InvalidQuery,
    InvalidRecord,
    RecordNotFound,
    MemoryRevisionConflict,
    RecordRevisionConflict,
    OperationAlreadyPending,
    TooManyRecords,
    TooManyPendingMutations,
    WrongCompletion,
}
