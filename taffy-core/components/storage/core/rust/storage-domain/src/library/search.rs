// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{
    LibraryEntry, LibraryError, MAX_LIBRARY_QUERY_BYTES, MAX_LIBRARY_QUERY_TERMS,
    MAX_LIBRARY_SEARCH_RESULTS,
};

/// A validated bounded full-text query.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LibraryQuery {
    terms: Vec<String>,
    limit: usize,
}

impl LibraryQuery {
    pub fn new(query: &str, limit: u32) -> Result<Self, LibraryError> {
        let trimmed = query.trim();
        let Ok(limit) = usize::try_from(limit) else {
            return Err(LibraryError::InvalidQuery);
        };
        if trimmed.is_empty()
            || trimmed.len() > MAX_LIBRARY_QUERY_BYTES
            || limit == 0
            || limit > MAX_LIBRARY_SEARCH_RESULTS
            || trimmed
                .chars()
                .any(|character| character.is_control() && !character.is_whitespace())
        {
            return Err(LibraryError::InvalidQuery);
        }
        let terms = trimmed
            .split_whitespace()
            .map(str::to_lowercase)
            .collect::<Vec<_>>();
        if terms.is_empty()
            || terms.len() > MAX_LIBRARY_QUERY_TERMS
            || terms
                .iter()
                .any(|term| term.len() > MAX_LIBRARY_QUERY_BYTES)
        {
            return Err(LibraryError::InvalidQuery);
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

/// One deterministic match with freshness expressed as an exact age.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LibraryHit {
    pub entry: LibraryEntry,
    pub age_ms: u64,
}

pub(super) fn search<'a>(
    entries: impl Iterator<Item = &'a LibraryEntry>,
    query: &LibraryQuery,
    now_epoch_ms: u64,
) -> Vec<LibraryHit> {
    let mut matches = entries
        .filter(|entry| matches_entry(entry, query))
        .collect::<Vec<_>>();
    matches.sort_by(|left, right| {
        right
            .last_checked_epoch_ms
            .cmp(&left.last_checked_epoch_ms)
            .then_with(|| left.entry_id.cmp(&right.entry_id))
    });
    matches.truncate(query.limit());
    matches
        .into_iter()
        .map(|entry| LibraryHit {
            age_ms: now_epoch_ms.saturating_sub(entry.last_checked_epoch_ms),
            entry: entry.clone(),
        })
        .collect()
}

fn matches_entry(entry: &LibraryEntry, query: &LibraryQuery) -> bool {
    if query.terms().iter().all(|term| term.is_ascii()) && entry_is_ascii(entry) {
        return query.terms().iter().all(|term| {
            crate::search::contains_ascii_case_insensitive(&entry.collection_name, term)
                || crate::search::contains_ascii_case_insensitive(&entry.field, term)
                || crate::search::contains_ascii_case_insensitive(entry.display_value(), term)
                || entry.sources.iter().any(|source| {
                    crate::search::contains_ascii_case_insensitive(&source.title, term)
                        || crate::search::contains_ascii_case_insensitive(&source.host, term)
                })
        });
    }

    // Unicode case folding can expand a character, so preserve the original
    // lower-cased combined-document behavior outside the exact ASCII path.
    let source_bytes = entry.sources.iter().fold(0_usize, |total, source| {
        total
            .saturating_add(source.title.len())
            .saturating_add(source.host.len())
            .saturating_add(2)
    });
    let mut searchable = String::with_capacity(
        entry
            .field
            .len()
            .saturating_add(entry.display_value().len())
            .saturating_add(entry.collection_name.len())
            .saturating_add(source_bytes)
            .saturating_add(2),
    );
    push_lowercase(&mut searchable, &entry.collection_name);
    push_lowercase(&mut searchable, &entry.field);
    push_lowercase(&mut searchable, entry.display_value());
    for source in &entry.sources {
        push_lowercase(&mut searchable, &source.title);
        push_lowercase(&mut searchable, &source.host);
    }
    query.terms().iter().all(|term| searchable.contains(term))
}

fn entry_is_ascii(entry: &LibraryEntry) -> bool {
    entry.collection_name.is_ascii()
        && entry.field.is_ascii()
        && entry.display_value().is_ascii()
        && entry
            .sources
            .iter()
            .all(|source| source.title.is_ascii() && source.host.is_ascii())
}

fn push_lowercase(target: &mut String, value: &str) {
    if !target.is_empty() {
        target.push(' ');
    }
    target.extend(value.chars().flat_map(char::to_lowercase));
}
