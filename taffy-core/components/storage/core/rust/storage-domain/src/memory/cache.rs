// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generation-resident candidate IDs for repeated Memory retrieval.
//!
//! This is not a result cache: every read still checks the current audience,
//! workspace and time against the live records. It retains no statement copy,
//! and every successful restore or durable mutation clears it in `MemoryStore`.

use std::cell::RefCell;
use std::collections::VecDeque;

use crate::ids::MemoryId;

use super::MemoryQuery;

const MAX_CACHED_QUERIES: usize = 8;

#[derive(Clone)]
struct Candidates {
    terms: Vec<String>,
    ids: Vec<MemoryId>,
}

#[derive(Clone, Default)]
pub(super) struct MemorySearchCache {
    entries: RefCell<VecDeque<Candidates>>,
}

impl core::fmt::Debug for MemorySearchCache {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("MemorySearchCache")
            .finish_non_exhaustive()
    }
}

impl MemorySearchCache {
    pub(super) const fn new() -> Self {
        Self {
            entries: RefCell::new(VecDeque::new()),
        }
    }

    pub(super) fn clear(&mut self) {
        self.entries.get_mut().clear();
    }

    pub(super) fn candidates(
        &self,
        query: &MemoryQuery,
        compute: impl FnOnce() -> Vec<MemoryId>,
    ) -> Vec<MemoryId> {
        // The core's ordered sequence does not contend. Still use the fallible
        // borrow: caching must never turn a valid read into a process panic.
        let Ok(mut entries) = self.entries.try_borrow_mut() else {
            return compute();
        };
        if let Some(index) = entries
            .iter()
            .position(|entry| entry.terms == query.terms())
        {
            if let Some(entry) = entries.remove(index) {
                let ids = entry.ids.clone();
                entries.push_back(entry);
                return ids;
            }
        }
        let ids = compute();
        if entries.len() >= MAX_CACHED_QUERIES {
            entries.pop_front();
        }
        entries.push_back(Candidates {
            terms: query.terms().to_vec(),
            ids: ids.clone(),
        });
        ids
    }
}

#[cfg(test)]
mod tests {
    use std::cell::Cell;

    use super::*;

    fn query(text: &str, limit: u32) -> MemoryQuery {
        MemoryQuery::new(text, limit).unwrap_or_else(|_| unreachable!("bounded query"))
    }

    #[test]
    fn a_warm_query_reuses_matching_ids_independently_of_the_requested_limit() {
        let cache = MemorySearchCache::new();
        let scans = Cell::new(0);
        for limit in [1, 2, 32] {
            let ids = cache.candidates(&query("PREFER", limit), || {
                scans.set(scans.get() + 1);
                vec![MemoryId::from_bytes([1; 16])]
            });
            assert_eq!(ids, vec![MemoryId::from_bytes([1; 16])]);
        }
        assert_eq!(scans.get(), 1);
    }

    #[test]
    fn eviction_retains_recent_queries_and_clear_forgets_query_terms() {
        let mut cache = MemorySearchCache::new();
        for index in 0..MAX_CACHED_QUERIES {
            cache.candidates(&query(&format!("query {index}"), 1), Vec::new);
        }
        cache.candidates(&query("query 0", 1), Vec::new);
        cache.candidates(&query("new query", 1), Vec::new);
        let scanned = Cell::new(false);
        cache.candidates(&query("query 0", 1), || {
            scanned.set(true);
            Vec::new()
        });
        assert!(!scanned.get());
        cache.candidates(&query("query 1", 1), || {
            scanned.set(true);
            Vec::new()
        });
        assert!(scanned.get());
        assert_eq!(cache.entries.borrow().len(), MAX_CACHED_QUERIES);
        cache.clear();
        assert!(cache.entries.borrow().is_empty());
    }
}
