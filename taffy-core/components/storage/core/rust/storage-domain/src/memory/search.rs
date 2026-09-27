// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use crate::ids::{MemoryId, WorkspaceId};

use super::{
    MemoryError, MemoryRecord, MemoryScope, MemorySensitivity, MAX_MEMORY_QUERY_BYTES,
    MAX_MEMORY_QUERY_TERMS, MAX_MEMORY_SEARCH_RESULTS,
};

/// Which records a caller is permitted to consider.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MemorySearchAudience {
    /// Settings review: every active record, including sensitive ones.
    PersonReview,
    /// A regular task: global records and records scoped to this workspace;
    /// sensitive records are excluded.
    Task { workspace_id: Option<WorkspaceId> },
}

/// A validated bounded full-text query.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MemoryQuery {
    terms: Vec<String>,
    limit: usize,
}

impl MemoryQuery {
    pub fn new(query: &str, limit: u32) -> Result<Self, MemoryError> {
        let trimmed = query.trim();
        let Ok(limit) = usize::try_from(limit) else {
            return Err(MemoryError::InvalidQuery);
        };
        if trimmed.is_empty()
            || trimmed.len() > MAX_MEMORY_QUERY_BYTES
            || limit == 0
            || limit > MAX_MEMORY_SEARCH_RESULTS
            || trimmed
                .chars()
                .any(|character| character.is_control() && !character.is_whitespace())
        {
            return Err(MemoryError::InvalidQuery);
        }
        let terms = trimmed
            .split_whitespace()
            .map(str::to_lowercase)
            .collect::<Vec<_>>();
        if terms.is_empty()
            || terms.len() > MAX_MEMORY_QUERY_TERMS
            || terms.iter().any(|term| term.len() > MAX_MEMORY_QUERY_BYTES)
        {
            return Err(MemoryError::InvalidQuery);
        }
        Ok(Self { terms, limit })
    }

    pub fn terms(&self) -> &[String] {
        &self.terms
    }

    pub const fn limit(&self) -> usize {
        self.limit
    }
}

/// One deterministic match. The record is cloned only after selection and
/// truncation, so non-matches never incur a deep clone.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MemoryHit {
    pub record: MemoryRecord,
}

pub(super) fn matching_ids<'a>(
    records: impl Iterator<Item = &'a MemoryRecord>,
    query: &MemoryQuery,
) -> Vec<MemoryId> {
    let mut matches = records
        .filter(|record| matches_record(record, query))
        .collect::<Vec<_>>();
    matches.sort_by(|left, right| {
        right
            .updated_at_epoch_ms
            .cmp(&left.updated_at_epoch_ms)
            .then_with(|| left.memory_id.cmp(&right.memory_id))
    });
    matches.into_iter().map(|record| record.memory_id).collect()
}

pub(super) fn visible_to(
    record: &MemoryRecord,
    audience: MemorySearchAudience,
    now_epoch_ms: u64,
) -> bool {
    if !record.is_active_at(now_epoch_ms) {
        return false;
    }
    match audience {
        MemorySearchAudience::PersonReview => true,
        MemorySearchAudience::Task { workspace_id } => {
            if record.sensitivity == MemorySensitivity::Sensitive {
                return false;
            }
            match &record.scope {
                MemoryScope::AllTasks => true,
                MemoryScope::Workspace(scope) => workspace_id == Some(scope.workspace_id),
            }
        }
    }
}

fn matches_record(record: &MemoryRecord, query: &MemoryQuery) -> bool {
    let scope_name = match &record.scope {
        MemoryScope::AllTasks => "all tasks",
        MemoryScope::Workspace(workspace) => &workspace.display_name,
    };
    let source_name = match &record.source {
        super::MemorySource::UserEntered => "you added",
        super::MemorySource::AcceptedTaskSuggestion { workspace, .. } => workspace
            .as_ref()
            .map_or("accepted task suggestion", |value| &value.display_name),
    };
    if query.terms().iter().all(|term| term.is_ascii())
        && record.statement.is_ascii()
        && scope_name.is_ascii()
        && source_name.is_ascii()
    {
        return query.terms().iter().all(|term| {
            crate::search::contains_ascii_case_insensitive(&record.statement, term)
                || crate::search::contains_ascii_case_insensitive(scope_name, term)
                || crate::search::contains_ascii_case_insensitive(source_name, term)
        });
    }

    // Unicode case folding can expand a character, so preserve the original
    // lower-cased combined-document behavior outside the exact ASCII path.
    let mut searchable = String::with_capacity(
        record
            .statement
            .len()
            .saturating_add(scope_name.len())
            .saturating_add(source_name.len())
            .saturating_add(2),
    );
    push_lowercase(&mut searchable, &record.statement);
    push_lowercase(&mut searchable, scope_name);
    push_lowercase(&mut searchable, source_name);
    query.terms().iter().all(|term| searchable.contains(term))
}

fn push_lowercase(target: &mut String, value: &str) {
    if !target.is_empty() {
        target.push(' ');
    }
    target.extend(value.chars().flat_map(char::to_lowercase));
}
