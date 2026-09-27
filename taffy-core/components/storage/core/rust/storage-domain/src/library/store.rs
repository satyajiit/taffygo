// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::BTreeMap;

use crate::ids::LibraryEntryId;

use super::search;
use super::validation::valid_operation_id;
use super::{
    LibraryEntry, LibraryError, LibraryHit, LibraryQuery, MAX_LIBRARY_ENTRIES,
    MAX_PENDING_LIBRARY_MUTATIONS,
};

/// A browser-owned durable change planned by portable logic.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LibraryMutation {
    Save(LibraryEntry),
    Remove {
        entry_id: LibraryEntryId,
        expected_entry_revision: u64,
        resulting_entry_revision: u64,
    },
}

/// Exact transaction facts sent to the browser storage adapter.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LibraryPersistRequest {
    pub operation_id: String,
    pub expected_library_revision: u64,
    pub resulting_library_revision: u64,
    pub mutation: LibraryMutation,
}

/// Planning can prove a requested save is already the exact durable state.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LibraryMutationPlan {
    AlreadyCurrent,
    Persist(Box<LibraryPersistRequest>),
}

#[derive(Clone, Debug)]
struct PendingMutation {
    request: LibraryPersistRequest,
}

/// Ordered resident projection of browser-owned Library storage.
#[derive(Clone, Debug, Default)]
pub struct LibraryStore {
    revision: u64,
    entries: BTreeMap<LibraryEntryId, LibraryEntry>,
    pending: BTreeMap<String, PendingMutation>,
}

impl LibraryStore {
    pub const fn new() -> Self {
        Self {
            revision: 0,
            entries: BTreeMap::new(),
            pending: BTreeMap::new(),
        }
    }

    pub const fn revision(&self) -> u64 {
        self.revision
    }

    pub fn restore(
        &mut self,
        revision: u64,
        entries: Vec<LibraryEntry>,
    ) -> Result<(), LibraryError> {
        if entries.len() > MAX_LIBRARY_ENTRIES
            || (revision == 0 && !entries.is_empty())
            || entries.iter().any(|entry| !entry.validate())
        {
            return Err(LibraryError::InvalidEntry);
        }
        let mut restored = BTreeMap::new();
        for entry in entries {
            if restored.insert(entry.entry_id, entry).is_some() {
                return Err(LibraryError::InvalidEntry);
            }
        }
        self.revision = revision;
        self.entries = restored;
        self.pending.clear();
        Ok(())
    }

    pub fn entries(&self) -> impl Iterator<Item = &LibraryEntry> {
        self.entries.values()
    }

    pub fn entry(&self, entry_id: LibraryEntryId) -> Option<&LibraryEntry> {
        self.entries.get(&entry_id)
    }

    pub fn search(&self, query: &LibraryQuery, now_epoch_ms: u64) -> Vec<LibraryHit> {
        search::search(self.entries.values(), query, now_epoch_ms)
    }

    pub fn begin_save(
        &mut self,
        operation_id: String,
        expected_library_revision: u64,
        expected_entry_revision: u64,
        entry: LibraryEntry,
    ) -> Result<LibraryMutationPlan, LibraryError> {
        self.preflight(&operation_id, expected_library_revision, entry.entry_id)?;
        let Some(resulting_entry_revision) = expected_entry_revision.checked_add(1) else {
            return Err(LibraryError::EntryRevisionConflict);
        };
        let Some(resulting_library_revision) = expected_library_revision.checked_add(1) else {
            return Err(LibraryError::LibraryRevisionConflict);
        };
        if !entry.validate() || entry.revision != resulting_entry_revision {
            return Err(LibraryError::InvalidEntry);
        }
        match self.entries.get(&entry.entry_id) {
            Some(current) if current.same_kept_fact(&entry) => {
                if current.revision != expected_entry_revision {
                    return Err(LibraryError::EntryRevisionConflict);
                }
                return Ok(LibraryMutationPlan::AlreadyCurrent);
            }
            Some(current) if current.revision != expected_entry_revision => {
                return Err(LibraryError::EntryRevisionConflict);
            }
            None if expected_entry_revision != 0 => {
                return Err(LibraryError::EntryRevisionConflict);
            }
            None if self.entries.len() >= MAX_LIBRARY_ENTRIES => {
                return Err(LibraryError::TooManyEntries);
            }
            Some(_) | None => {}
        }
        Ok(self.stage(LibraryPersistRequest {
            operation_id,
            expected_library_revision,
            resulting_library_revision,
            mutation: LibraryMutation::Save(entry),
        }))
    }

    pub fn begin_remove(
        &mut self,
        operation_id: String,
        expected_library_revision: u64,
        entry_id: LibraryEntryId,
        expected_entry_revision: u64,
    ) -> Result<LibraryPersistRequest, LibraryError> {
        self.preflight(&operation_id, expected_library_revision, entry_id)?;
        let entry = self
            .entries
            .get(&entry_id)
            .ok_or(LibraryError::EntryNotFound)?;
        if entry.revision != expected_entry_revision {
            return Err(LibraryError::EntryRevisionConflict);
        }
        let resulting_library_revision = expected_library_revision
            .checked_add(1)
            .ok_or(LibraryError::LibraryRevisionConflict)?;
        let resulting_entry_revision = expected_entry_revision
            .checked_add(1)
            .ok_or(LibraryError::EntryRevisionConflict)?;
        let request = LibraryPersistRequest {
            operation_id,
            expected_library_revision,
            resulting_library_revision,
            mutation: LibraryMutation::Remove {
                entry_id,
                expected_entry_revision,
                resulting_entry_revision,
            },
        };
        match self.stage(request) {
            LibraryMutationPlan::Persist(request) => Ok(*request),
            LibraryMutationPlan::AlreadyCurrent => Err(LibraryError::InvalidOperation),
        }
    }

    pub fn complete(
        &mut self,
        operation_id: &str,
        committed_library_revision: u64,
    ) -> Result<(), LibraryError> {
        let pending = self
            .pending
            .get(operation_id)
            .ok_or(LibraryError::WrongCompletion)?;
        if pending.request.resulting_library_revision != committed_library_revision {
            return Err(LibraryError::WrongCompletion);
        }
        let pending = self
            .pending
            .remove(operation_id)
            .ok_or(LibraryError::WrongCompletion)?;
        match pending.request.mutation {
            LibraryMutation::Save(entry) => {
                self.entries.insert(entry.entry_id, entry);
            }
            LibraryMutation::Remove { entry_id, .. } => {
                self.entries.remove(&entry_id);
            }
        }
        self.revision = committed_library_revision;
        Ok(())
    }

    pub fn reject(&mut self, operation_id: &str) -> bool {
        self.pending.remove(operation_id).is_some()
    }

    fn preflight(
        &self,
        operation_id: &str,
        expected_library_revision: u64,
        entry_id: LibraryEntryId,
    ) -> Result<(), LibraryError> {
        if !valid_operation_id(operation_id) {
            return Err(LibraryError::InvalidOperation);
        }
        if self.revision != expected_library_revision {
            return Err(LibraryError::LibraryRevisionConflict);
        }
        if self.pending.contains_key(operation_id)
            || self
                .pending
                .values()
                .any(|pending| match &pending.request.mutation {
                    LibraryMutation::Save(entry) => entry.entry_id == entry_id,
                    LibraryMutation::Remove {
                        entry_id: pending_id,
                        ..
                    } => *pending_id == entry_id,
                })
        {
            return Err(LibraryError::OperationAlreadyPending);
        }
        if self.pending.len() >= MAX_PENDING_LIBRARY_MUTATIONS {
            return Err(LibraryError::TooManyPendingMutations);
        }
        Ok(())
    }

    fn stage(&mut self, request: LibraryPersistRequest) -> LibraryMutationPlan {
        let operation_id = request.operation_id.clone();
        self.pending.insert(
            operation_id,
            PendingMutation {
                request: request.clone(),
            },
        );
        LibraryMutationPlan::Persist(Box::new(request))
    }
}
