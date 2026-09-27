// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::BTreeMap;

use crate::ids::MemoryId;

use super::cache::MemorySearchCache;
use super::search;
use super::validation::valid_operation_id;
use super::{
    MemoryError, MemoryHit, MemoryQuery, MemoryRecord, MemorySearchAudience, MAX_MEMORY_RECORDS,
    MAX_PENDING_MEMORY_MUTATIONS,
};

/// A browser-owned durable change planned by portable logic.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum MemoryMutation {
    Save(MemoryRecord),
    Delete {
        memory_id: MemoryId,
        expected_record_revision: u64,
        resulting_record_revision: u64,
    },
}

/// Exact transaction facts sent to the browser storage adapter.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MemoryPersistRequest {
    pub operation_id: String,
    pub expected_memory_revision: u64,
    pub resulting_memory_revision: u64,
    pub mutation: MemoryMutation,
}

/// Planning can prove a requested save is already exact durable state.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum MemoryMutationPlan {
    AlreadyCurrent,
    Persist(Box<MemoryPersistRequest>),
}

#[derive(Clone, Debug)]
struct PendingMutation {
    request: MemoryPersistRequest,
}

/// Ordered resident projection of browser-owned Memory storage.
#[derive(Clone, Debug, Default)]
pub struct MemoryStore {
    revision: u64,
    records: BTreeMap<MemoryId, MemoryRecord>,
    pending: BTreeMap<String, PendingMutation>,
    search_cache: MemorySearchCache,
}

impl MemoryStore {
    pub const fn new() -> Self {
        Self {
            revision: 0,
            records: BTreeMap::new(),
            pending: BTreeMap::new(),
            search_cache: MemorySearchCache::new(),
        }
    }

    pub const fn revision(&self) -> u64 {
        self.revision
    }

    pub fn restore(
        &mut self,
        revision: u64,
        records: Vec<MemoryRecord>,
    ) -> Result<(), MemoryError> {
        if records.len() > MAX_MEMORY_RECORDS
            || (revision == 0 && !records.is_empty())
            || records.iter().any(|record| !record.validate())
        {
            return Err(MemoryError::InvalidRecord);
        }
        let mut restored = BTreeMap::new();
        for record in records {
            if restored.insert(record.memory_id, record).is_some() {
                return Err(MemoryError::InvalidRecord);
            }
        }
        self.revision = revision;
        self.records = restored;
        self.pending.clear();
        self.search_cache.clear();
        Ok(())
    }

    pub fn records(&self) -> impl Iterator<Item = &MemoryRecord> {
        self.records.values()
    }

    pub fn record(&self, memory_id: MemoryId) -> Option<&MemoryRecord> {
        self.records.get(&memory_id)
    }

    pub fn search(
        &self,
        query: &MemoryQuery,
        audience: MemorySearchAudience,
        now_epoch_ms: u64,
    ) -> Vec<MemoryHit> {
        self.search_cache
            .candidates(query, || search::matching_ids(self.records.values(), query))
            .into_iter()
            .filter_map(|id| self.records.get(&id))
            // Visibility is never cached. A settings search cannot seed a
            // model result, and expiry or a different workspace is checked
            // even when no durable Memory revision has changed.
            .filter(|record| search::visible_to(record, audience, now_epoch_ms))
            .take(query.limit())
            .map(|record| MemoryHit {
                record: record.clone(),
            })
            .collect()
    }

    pub fn begin_save(
        &mut self,
        operation_id: String,
        expected_memory_revision: u64,
        expected_record_revision: u64,
        record: MemoryRecord,
    ) -> Result<MemoryMutationPlan, MemoryError> {
        self.preflight(&operation_id, expected_memory_revision, record.memory_id)?;
        let resulting_record_revision = expected_record_revision
            .checked_add(1)
            .ok_or(MemoryError::RecordRevisionConflict)?;
        let resulting_memory_revision = expected_memory_revision
            .checked_add(1)
            .ok_or(MemoryError::MemoryRevisionConflict)?;
        if !record.validate() || record.revision != resulting_record_revision {
            return Err(MemoryError::InvalidRecord);
        }
        match self.records.get(&record.memory_id) {
            Some(current) if current.revision != expected_record_revision => {
                return Err(MemoryError::RecordRevisionConflict);
            }
            Some(current)
                if current.source != record.source
                    || current.created_at_epoch_ms != record.created_at_epoch_ms
                    || record.updated_at_epoch_ms < current.updated_at_epoch_ms =>
            {
                return Err(MemoryError::InvalidRecord);
            }
            Some(current) if current.same_content(&record) => {
                return Ok(MemoryMutationPlan::AlreadyCurrent);
            }
            None if expected_record_revision != 0 => {
                return Err(MemoryError::RecordRevisionConflict);
            }
            None if self.records.len() >= MAX_MEMORY_RECORDS => {
                return Err(MemoryError::TooManyRecords);
            }
            Some(_) | None => {}
        }
        Ok(self.stage(MemoryPersistRequest {
            operation_id,
            expected_memory_revision,
            resulting_memory_revision,
            mutation: MemoryMutation::Save(record),
        }))
    }

    pub fn begin_delete(
        &mut self,
        operation_id: String,
        expected_memory_revision: u64,
        memory_id: MemoryId,
        expected_record_revision: u64,
    ) -> Result<MemoryPersistRequest, MemoryError> {
        self.preflight(&operation_id, expected_memory_revision, memory_id)?;
        let record = self
            .records
            .get(&memory_id)
            .ok_or(MemoryError::RecordNotFound)?;
        if record.revision != expected_record_revision {
            return Err(MemoryError::RecordRevisionConflict);
        }
        let resulting_memory_revision = expected_memory_revision
            .checked_add(1)
            .ok_or(MemoryError::MemoryRevisionConflict)?;
        let resulting_record_revision = expected_record_revision
            .checked_add(1)
            .ok_or(MemoryError::RecordRevisionConflict)?;
        let request = MemoryPersistRequest {
            operation_id,
            expected_memory_revision,
            resulting_memory_revision,
            mutation: MemoryMutation::Delete {
                memory_id,
                expected_record_revision,
                resulting_record_revision,
            },
        };
        match self.stage(request) {
            MemoryMutationPlan::Persist(request) => Ok(*request),
            MemoryMutationPlan::AlreadyCurrent => Err(MemoryError::InvalidOperation),
        }
    }

    pub fn complete(
        &mut self,
        operation_id: &str,
        committed_memory_revision: u64,
    ) -> Result<(), MemoryError> {
        let pending = self
            .pending
            .get(operation_id)
            .ok_or(MemoryError::WrongCompletion)?;
        if pending.request.resulting_memory_revision != committed_memory_revision {
            return Err(MemoryError::WrongCompletion);
        }
        let pending = self
            .pending
            .remove(operation_id)
            .ok_or(MemoryError::WrongCompletion)?;
        match pending.request.mutation {
            MemoryMutation::Save(record) => {
                self.records.insert(record.memory_id, record);
            }
            MemoryMutation::Delete { memory_id, .. } => {
                self.records.remove(&memory_id);
            }
        }
        self.revision = committed_memory_revision;
        // Scrub both candidate IDs and query terms on any committed change,
        // including deletion and scope/sensitivity changes. Pending or rejected
        // writes leave the same committed view and may keep their cache.
        self.search_cache.clear();
        Ok(())
    }

    pub fn reject(&mut self, operation_id: &str) -> bool {
        self.pending.remove(operation_id).is_some()
    }

    fn preflight(
        &self,
        operation_id: &str,
        expected_memory_revision: u64,
        memory_id: MemoryId,
    ) -> Result<(), MemoryError> {
        if !valid_operation_id(operation_id) {
            return Err(MemoryError::InvalidOperation);
        }
        if self.revision != expected_memory_revision {
            return Err(MemoryError::MemoryRevisionConflict);
        }
        if self.pending.contains_key(operation_id)
            || self
                .pending
                .values()
                .any(|pending| match &pending.request.mutation {
                    MemoryMutation::Save(record) => record.memory_id == memory_id,
                    MemoryMutation::Delete {
                        memory_id: pending_id,
                        ..
                    } => *pending_id == memory_id,
                })
        {
            return Err(MemoryError::OperationAlreadyPending);
        }
        if self.pending.len() >= MAX_PENDING_MEMORY_MUTATIONS {
            return Err(MemoryError::TooManyPendingMutations);
        }
        Ok(())
    }

    fn stage(&mut self, request: MemoryPersistRequest) -> MemoryMutationPlan {
        let operation_id = request.operation_id.clone();
        self.pending.insert(
            operation_id,
            PendingMutation {
                request: request.clone(),
            },
        );
        MemoryMutationPlan::Persist(Box::new(request))
    }
}
