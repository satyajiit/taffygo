// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical portable Library adapter.

mod conversion;
mod export;

use core_api_types::{
    LibraryAvailability, LibraryExportView, LibrarySearchHitView, LibrarySearchView,
    LibraryViewState, WorkspaceExportFormat,
};
use core_service_types::LibraryEntryRecord;
use taffy_storage::ids::{FactId, LibraryEntryId, WorkspaceId};
use taffy_storage::library::{
    entry_from_workspace, LibraryMutation, LibraryMutationPlan, LibraryQuery, LibraryRefreshDigest,
    LibraryRefreshPreview, LibraryRefreshResult, LibraryStore,
};
use taffy_storage::workspace::WorkspaceSnapshot;
use task_engine::TaskLibrarySearchTranscriptOutcome;

use crate::account::Sha256Port;
use crate::ports::{LibraryPort, LibraryRemoveRequest, LibrarySaveRequest, LibraryStoreError};

use self::conversion::{
    entry_from_wire, entry_to_view, entry_to_wire, map_error, stable_entry_id, transcript_entry,
    valid_identifier,
};
use self::export::render_export;

/// The one production Library aggregate for a profile generation.
#[derive(Clone, Debug)]
pub struct ProductionLibrary {
    store: LibraryStore,
    private_profile: bool,
    latest_search: Option<LibrarySearchView>,
    latest_export: Option<LibraryExportView>,
}

impl ProductionLibrary {
    pub const fn new(private_profile: bool) -> Self {
        Self {
            store: LibraryStore::new(),
            private_profile,
            latest_search: None,
            latest_export: None,
        }
    }

    fn require_available(&self) -> Result<(), LibraryStoreError> {
        if self.private_profile {
            Err(LibraryStoreError::PrivateProfile)
        } else {
            Ok(())
        }
    }

    fn clear_derived(&mut self) {
        self.latest_search = None;
        self.latest_export = None;
    }
}

impl LibraryPort for ProductionLibrary {
    fn restore(
        &mut self,
        revision: u64,
        entries: Vec<LibraryEntryRecord>,
    ) -> Result<(), LibraryStoreError> {
        if self.private_profile {
            return if revision == 0 && entries.is_empty() {
                self.clear_derived();
                Ok(())
            } else {
                Err(LibraryStoreError::PrivateProfile)
            };
        }
        let entries = entries
            .into_iter()
            .map(entry_from_wire)
            .collect::<Result<Vec<_>, _>>()?;
        self.store.restore(revision, entries).map_err(map_error)?;
        self.clear_derived();
        Ok(())
    }

    fn search(
        &mut self,
        request_id: &str,
        query: &str,
        limit: u32,
        requested_at_epoch_ms: u64,
    ) -> Result<TaskLibrarySearchTranscriptOutcome, LibraryStoreError> {
        self.require_available()?;
        if !valid_identifier(request_id) {
            return Err(LibraryStoreError::InvalidIdentifier);
        }
        let parsed = LibraryQuery::new(query, limit).map_err(map_error)?;
        let hits = self.store.search(&parsed, requested_at_epoch_ms);
        let transcript_entries = hits
            .iter()
            .map(|hit| transcript_entry(&hit.entry, hit.age_ms))
            .collect::<Result<Vec<_>, _>>()?;
        let search_hits = hits
            .into_iter()
            .map(|hit| LibrarySearchHitView {
                entry_id: hit.entry.entry_id.to_text(),
                age_ms: hit.age_ms,
            })
            .collect();
        self.latest_search = Some(LibrarySearchView {
            request_id: request_id.to_owned(),
            query: query.to_owned(),
            library_revision: self.store.revision(),
            hits: search_hits,
        });
        Ok(TaskLibrarySearchTranscriptOutcome::bounded(
            transcript_entries,
        ))
    }

    fn begin_save(
        &mut self,
        operation_id: String,
        workspace: &WorkspaceSnapshot,
        expected_workspace_revision: u64,
        fact_id: &str,
        expected_library_revision: u64,
        expected_entry_revision: u64,
        approved_at_epoch_ms: u64,
        digest: &dyn Sha256Port,
    ) -> Result<Option<LibrarySaveRequest>, LibraryStoreError> {
        self.require_available()?;
        let fact_id = FactId::parse(fact_id).map_err(|_| LibraryStoreError::InvalidIdentifier)?;
        let entry_id = stable_entry_id(workspace.workspace_id, fact_id, digest)?;
        let entry_revision = expected_entry_revision
            .checked_add(1)
            .ok_or(LibraryStoreError::StaleEntryRevision)?;
        let entry = entry_from_workspace(
            entry_id,
            entry_revision,
            workspace,
            expected_workspace_revision,
            fact_id,
            approved_at_epoch_ms,
        )
        .map_err(map_error)?;
        let plan = self
            .store
            .begin_save(
                operation_id,
                expected_library_revision,
                expected_entry_revision,
                entry,
            )
            .map_err(map_error)?;
        match plan {
            LibraryMutationPlan::AlreadyCurrent => Ok(None),
            LibraryMutationPlan::Persist(request) => {
                let LibraryMutation::Save(entry) = request.mutation else {
                    return Err(LibraryStoreError::InvalidEntry);
                };
                Ok(Some(LibrarySaveRequest {
                    operation_id: request.operation_id,
                    expected_library_revision: request.expected_library_revision,
                    resulting_library_revision: request.resulting_library_revision,
                    expected_entry_revision,
                    entry: entry_to_wire(&entry),
                }))
            }
        }
    }

    fn begin_remove(
        &mut self,
        operation_id: String,
        entry_id: &str,
        expected_library_revision: u64,
        expected_entry_revision: u64,
        removed_at_epoch_ms: u64,
    ) -> Result<LibraryRemoveRequest, LibraryStoreError> {
        self.require_available()?;
        let parsed =
            LibraryEntryId::parse(entry_id).map_err(|_| LibraryStoreError::InvalidIdentifier)?;
        let request = self
            .store
            .begin_remove(
                operation_id,
                expected_library_revision,
                parsed,
                expected_entry_revision,
            )
            .map_err(map_error)?;
        let LibraryMutation::Remove {
            entry_id,
            expected_entry_revision,
            resulting_entry_revision,
        } = request.mutation
        else {
            return Err(LibraryStoreError::InvalidEntry);
        };
        Ok(LibraryRemoveRequest {
            operation_id: request.operation_id,
            expected_library_revision: request.expected_library_revision,
            resulting_library_revision: request.resulting_library_revision,
            entry_id: entry_id.to_text(),
            expected_entry_revision,
            resulting_entry_revision,
            removed_at_epoch_ms,
        })
    }

    fn complete(
        &mut self,
        operation_id: &str,
        committed_library_revision: u64,
    ) -> Result<(), LibraryStoreError> {
        self.require_available()?;
        self.store
            .complete(operation_id, committed_library_revision)
            .map_err(map_error)?;
        self.clear_derived();
        Ok(())
    }

    fn reject(&mut self, operation_id: &str) -> bool {
        self.store.reject(operation_id)
    }

    fn request_export(
        &mut self,
        request_id: &str,
        expected_library_revision: u64,
        collection_id: Option<&str>,
        format: WorkspaceExportFormat,
    ) -> Result<LibraryExportView, LibraryStoreError> {
        self.require_available()?;
        if !valid_identifier(request_id)
            || collection_id.is_some_and(|value| WorkspaceId::parse(value).is_err())
        {
            return Err(LibraryStoreError::InvalidIdentifier);
        }
        if self.store.revision() != expected_library_revision {
            return Err(LibraryStoreError::StaleLibraryRevision);
        }
        let content = render_export(self.store.entries(), collection_id, format)?;
        let export = LibraryExportView {
            request_id: request_id.to_owned(),
            library_revision: expected_library_revision,
            collection_id: collection_id.map(str::to_owned),
            format,
            content,
        };
        self.latest_export = Some(export.clone());
        Ok(export)
    }

    fn preview_refresh(
        &self,
        collection_id: &str,
        expected_library_revision: u64,
        workspace: &WorkspaceSnapshot,
        expected_workspace_revision: u64,
        digest: &dyn Sha256Port,
    ) -> Result<LibraryRefreshPreview, LibraryStoreError> {
        self.require_available()?;
        let collection_id =
            WorkspaceId::parse(collection_id).map_err(|_| LibraryStoreError::InvalidIdentifier)?;
        self.store
            .preview_collection_refresh(
                collection_id,
                expected_library_revision,
                workspace,
                expected_workspace_revision,
                &RefreshDigest(digest),
            )
            .map_err(map_error)
    }

    fn compare_refresh(
        &self,
        preview: &LibraryRefreshPreview,
        original: &WorkspaceSnapshot,
        refreshed: &WorkspaceSnapshot,
        digest: &dyn Sha256Port,
    ) -> Result<LibraryRefreshResult, LibraryStoreError> {
        self.require_available()?;
        self.store
            .compare_collection_refresh(preview, original, refreshed, &RefreshDigest(digest))
            .map_err(map_error)
    }

    fn project_core_api(&self) -> LibraryViewState {
        LibraryViewState {
            availability: if self.private_profile {
                LibraryAvailability::PrivateProfile
            } else {
                LibraryAvailability::Available
            },
            revision: self.store.revision(),
            entries: if self.private_profile {
                Vec::new()
            } else {
                self.store.entries().map(entry_to_view).collect()
            },
            search: if self.private_profile {
                None
            } else {
                self.latest_search.clone()
            },
            refresh_previews: Vec::new(),
            refresh_results: Vec::new(),
        }
    }

    fn latest_export(&self) -> Option<LibraryExportView> {
        if self.private_profile {
            None
        } else {
            self.latest_export.clone()
        }
    }
}

struct RefreshDigest<'a>(&'a dyn Sha256Port);

impl LibraryRefreshDigest for RefreshDigest<'_> {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], taffy_storage::library::LibraryError> {
        self.0
            .sha256(input)
            .map_err(|_| taffy_storage::library::LibraryError::InvalidRefreshPreview)
    }
}
