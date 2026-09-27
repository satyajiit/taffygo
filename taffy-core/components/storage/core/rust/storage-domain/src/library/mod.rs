// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The portable Library aggregate.
//!
//! A Library entry is one fact a person explicitly kept from an exact saved
//! workspace revision. It is never a model-authored document and it is never
//! inferred from browsing. [`entry_from_workspace`] is the narrow promotion
//! seam: callers name one existing fact and this module copies only the fact,
//! its active cited sources, its correction, and its provenance timestamps.
//!
//! The browser owns the physical database and full-text index. This module
//! owns the values the browser may persist, optimistic revisions, bounded
//! deterministic matching, and replay-safe mutation planning.

mod refresh;
mod search;
mod store;
mod validation;

use crate::ids::{FactId, LibraryEntryId, SourceId, WorkspaceId};
use crate::workspace::{FactKind, WorkspaceSnapshot};

pub use self::refresh::{
    LibraryRefreshDigest, LibraryRefreshDisposition, LibraryRefreshPreview, LibraryRefreshResult,
    LibraryRefreshSource, LibraryRefreshSourceResult,
};
pub use self::search::{LibraryHit, LibraryQuery};
pub use self::store::{LibraryMutation, LibraryMutationPlan, LibraryPersistRequest, LibraryStore};

/// Maximum active entries restored into one profile core.
pub const MAX_LIBRARY_ENTRIES: usize = 1_024;
/// Maximum sources retained beside one kept fact.
pub const MAX_LIBRARY_SOURCES: usize = crate::workspace::MAX_FACT_SOURCES;
/// Maximum query bytes accepted from a person or model.
pub const MAX_LIBRARY_QUERY_BYTES: usize = 512;
/// Maximum terms in one full-text query.
pub const MAX_LIBRARY_QUERY_TERMS: usize = 16;
/// Maximum entries returned by one search.
pub const MAX_LIBRARY_SEARCH_RESULTS: usize = 32;
/// Maximum mutations waiting for a browser-owned transaction.
pub const MAX_PENDING_LIBRARY_MUTATIONS: usize = 64;
/// Maximum opaque operation identity length.
pub const MAX_LIBRARY_OPERATION_ID_BYTES: usize = 128;

/// Evidence for one kept fact.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LibrarySource {
    pub source_id: SourceId,
    pub title: String,
    pub host: String,
    pub observed_at_epoch_ms: u64,
}

/// One durable, source-cited fact the person chose to keep.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LibraryEntry {
    pub entry_id: LibraryEntryId,
    pub revision: u64,
    pub collection_id: WorkspaceId,
    pub collection_name: String,
    pub source_workspace_id: WorkspaceId,
    pub source_workspace_revision: u64,
    pub source_fact_id: FactId,
    pub field: String,
    pub original_value: String,
    pub correction: Option<String>,
    pub kind: FactKind,
    pub sources: Vec<LibrarySource>,
    pub captured_at_epoch_ms: u64,
    pub last_checked_epoch_ms: u64,
    pub has_conflict: bool,
}

impl LibraryEntry {
    /// The value shown and indexed. A person's correction remains visibly
    /// separate from the cited original in the record itself.
    pub fn display_value(&self) -> &str {
        self.correction.as_deref().unwrap_or(&self.original_value)
    }

    /// Whether every field satisfies the portable storage contract.
    pub fn validate(&self) -> bool {
        validation::valid_entry(self)
    }

    fn same_kept_fact(&self, other: &Self) -> bool {
        let mut left = self.clone();
        let mut right = other.clone();
        left.revision = 0;
        right.revision = 0;
        left == right
    }
}

/// Why a Library request was refused before any browser effect existed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LibraryError {
    InvalidIdentifier,
    InvalidOperation,
    InvalidQuery,
    InvalidEntry,
    WorkspaceNotSaved,
    WorkspaceRevisionConflict,
    FactNotFound,
    FactHasNoActiveCitation,
    EntryNotFound,
    LibraryRevisionConflict,
    EntryRevisionConflict,
    OperationAlreadyPending,
    TooManyEntries,
    TooManyPendingMutations,
    WrongCompletion,
    RefreshCollectionNotFound,
    SourceNotRefreshable,
    InvalidRefreshPreview,
    InvalidRefreshResult,
}

/// Copies one exact cited fact out of a saved workspace.
///
/// The call itself must be reached only from a person-approved command. This
/// function independently enforces the facts that can be checked from data:
/// exact workspace revision, saved state, existing fact, and at least one
/// active cited source. No title/body supplied by a model enters the entry.
pub fn entry_from_workspace(
    entry_id: LibraryEntryId,
    entry_revision: u64,
    workspace: &WorkspaceSnapshot,
    expected_workspace_revision: u64,
    fact_id: FactId,
    captured_at_epoch_ms: u64,
) -> Result<LibraryEntry, LibraryError> {
    if !workspace.saved {
        return Err(LibraryError::WorkspaceNotSaved);
    }
    if workspace.revision != expected_workspace_revision {
        return Err(LibraryError::WorkspaceRevisionConflict);
    }
    let fact = workspace.fact(fact_id).ok_or(LibraryError::FactNotFound)?;
    let mut sources = Vec::with_capacity(fact.sources.len());
    for source_id in &fact.sources {
        let Some(source) = workspace.source(*source_id) else {
            return Err(LibraryError::InvalidEntry);
        };
        if source.excluded {
            continue;
        }
        sources.push(LibrarySource {
            source_id: source.source_id,
            title: source.title.clone(),
            host: source.host.clone(),
            observed_at_epoch_ms: source.read_at_epoch_ms,
        });
    }
    if sources.is_empty() {
        return Err(LibraryError::FactHasNoActiveCitation);
    }
    let last_checked_epoch_ms = sources
        .iter()
        .map(|source| source.observed_at_epoch_ms)
        .max()
        .unwrap_or(0);
    let entry = LibraryEntry {
        entry_id,
        revision: entry_revision,
        collection_id: workspace.workspace_id,
        collection_name: workspace.display_name.clone(),
        source_workspace_id: workspace.workspace_id,
        source_workspace_revision: workspace.revision,
        source_fact_id: fact.fact_id,
        field: fact.field.clone(),
        original_value: fact.value.clone(),
        correction: fact.correction.clone(),
        kind: fact.kind,
        sources,
        captured_at_epoch_ms,
        last_checked_epoch_ms,
        has_conflict: fact.has_conflict,
    };
    entry
        .validate()
        .then_some(entry)
        .ok_or(LibraryError::InvalidEntry)
}
