// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact conversion between storage, generated contracts, and resident model context.

use core_api_types::{LibraryEntryView, LibrarySourceView, WorkspaceFactKind};
use core_service_types::{LibraryEntryRecord, LibraryFactKind, LibrarySourceRecord};
use taffy_storage::ids::{FactId, LibraryEntryId, SourceId, WorkspaceId};
use taffy_storage::library::{LibraryEntry, LibraryError};
use taffy_storage::workspace::FactKind;
use task_engine::{TaskLibraryCitation, TaskLibrarySearchEntry};

use crate::account::Sha256Port;
use crate::ports::LibraryStoreError;

pub(super) fn stable_entry_id(
    workspace_id: WorkspaceId,
    fact_id: FactId,
    digest: &dyn Sha256Port,
) -> Result<LibraryEntryId, LibraryStoreError> {
    let mut input = b"taffy.library.saved-fact.v1\0".to_vec();
    input.extend_from_slice(workspace_id.to_text().as_bytes());
    input.push(0);
    input.extend_from_slice(fact_id.to_text().as_bytes());
    let hash = digest
        .sha256(&input)
        .map_err(|_| LibraryStoreError::DigestUnavailable)?;
    let mut bytes = [0_u8; 16];
    bytes.copy_from_slice(hash.get(..16).ok_or(LibraryStoreError::DigestUnavailable)?);
    Ok(LibraryEntryId::from_bytes(bytes))
}

pub(super) fn entry_from_wire(
    value: LibraryEntryRecord,
) -> Result<LibraryEntry, LibraryStoreError> {
    let sources = value
        .sources
        .into_iter()
        .map(|source| {
            Ok(taffy_storage::library::LibrarySource {
                source_id: SourceId::parse(&source.source_id)
                    .map_err(|_| LibraryStoreError::InvalidIdentifier)?,
                title: source.title,
                host: source.host,
                observed_at_epoch_ms: source.observed_at_epoch_ms,
            })
        })
        .collect::<Result<Vec<_>, LibraryStoreError>>()?;
    Ok(LibraryEntry {
        entry_id: LibraryEntryId::parse(&value.entry_id)
            .map_err(|_| LibraryStoreError::InvalidIdentifier)?,
        revision: value.revision,
        collection_id: WorkspaceId::parse(&value.collection_id)
            .map_err(|_| LibraryStoreError::InvalidIdentifier)?,
        collection_name: value.collection_name,
        source_workspace_id: WorkspaceId::parse(&value.source_workspace_id)
            .map_err(|_| LibraryStoreError::InvalidIdentifier)?,
        source_workspace_revision: value.source_workspace_revision,
        source_fact_id: FactId::parse(&value.source_fact_id)
            .map_err(|_| LibraryStoreError::InvalidIdentifier)?,
        field: value.field,
        original_value: value.original_value,
        correction: value.correction,
        kind: fact_kind_from_wire(value.kind),
        sources,
        captured_at_epoch_ms: value.captured_at_epoch_ms,
        last_checked_epoch_ms: value.last_checked_epoch_ms,
        has_conflict: value.has_conflict,
    })
}

pub(super) fn entry_to_wire(value: &LibraryEntry) -> LibraryEntryRecord {
    LibraryEntryRecord {
        entry_id: value.entry_id.to_text(),
        revision: value.revision,
        collection_id: value.collection_id.to_text(),
        collection_name: value.collection_name.clone(),
        source_workspace_id: value.source_workspace_id.to_text(),
        source_workspace_revision: value.source_workspace_revision,
        source_fact_id: value.source_fact_id.to_text(),
        field: value.field.clone(),
        original_value: value.original_value.clone(),
        correction: value.correction.clone(),
        kind: fact_kind_to_wire(value.kind),
        sources: value
            .sources
            .iter()
            .map(|source| LibrarySourceRecord {
                source_id: source.source_id.to_text(),
                title: source.title.clone(),
                host: source.host.clone(),
                observed_at_epoch_ms: source.observed_at_epoch_ms,
            })
            .collect(),
        captured_at_epoch_ms: value.captured_at_epoch_ms,
        last_checked_epoch_ms: value.last_checked_epoch_ms,
        has_conflict: value.has_conflict,
    }
}

pub(super) fn transcript_entry(
    value: &LibraryEntry,
    age_ms: u64,
) -> Result<TaskLibrarySearchEntry, LibraryStoreError> {
    let citations = value
        .sources
        .iter()
        .map(|source| TaskLibraryCitation::new(source.title.clone(), source.host.clone()))
        .collect::<Option<Vec<_>>>()
        .ok_or(LibraryStoreError::InvalidEntry)?;
    TaskLibrarySearchEntry::new(
        value.entry_id.to_text(),
        value.collection_name.clone(),
        value.field.clone(),
        value.display_value().to_owned(),
        citations,
        age_ms,
        value.has_conflict,
    )
    .ok_or(LibraryStoreError::InvalidEntry)
}

pub(super) fn entry_to_view(value: &LibraryEntry) -> LibraryEntryView {
    LibraryEntryView {
        entry_id: value.entry_id.to_text(),
        revision: value.revision,
        collection_id: value.collection_id.to_text(),
        collection_name: value.collection_name.clone(),
        source_workspace_id: value.source_workspace_id.to_text(),
        source_workspace_revision: value.source_workspace_revision,
        source_fact_id: value.source_fact_id.to_text(),
        field: value.field.clone(),
        original_value: value.original_value.clone(),
        correction: value.correction.clone(),
        kind: fact_kind_to_view(value.kind),
        sources: value
            .sources
            .iter()
            .map(|source| LibrarySourceView {
                source_id: source.source_id.to_text(),
                title: source.title.clone(),
                host: source.host.clone(),
                observed_at_epoch_ms: source.observed_at_epoch_ms,
            })
            .collect(),
        captured_at_epoch_ms: value.captured_at_epoch_ms,
        last_checked_epoch_ms: value.last_checked_epoch_ms,
        has_conflict: value.has_conflict,
    }
}

const fn fact_kind_from_wire(value: LibraryFactKind) -> FactKind {
    match value {
        LibraryFactKind::FromPage => FactKind::FromPage,
        LibraryFactKind::Summarized => FactKind::Summarized,
        LibraryFactKind::TaffyInference => FactKind::TaffyInference,
        LibraryFactKind::UserEntered => FactKind::UserEntered,
    }
}

const fn fact_kind_to_wire(value: FactKind) -> LibraryFactKind {
    match value {
        FactKind::FromPage => LibraryFactKind::FromPage,
        FactKind::Summarized => LibraryFactKind::Summarized,
        FactKind::TaffyInference => LibraryFactKind::TaffyInference,
        FactKind::UserEntered => LibraryFactKind::UserEntered,
    }
}

const fn fact_kind_to_view(value: FactKind) -> WorkspaceFactKind {
    match value {
        FactKind::FromPage => WorkspaceFactKind::FromPage,
        FactKind::Summarized => WorkspaceFactKind::Summarized,
        FactKind::TaffyInference => WorkspaceFactKind::TaffyInference,
        FactKind::UserEntered => WorkspaceFactKind::UserEntered,
    }
}

pub(super) fn map_error(error: LibraryError) -> LibraryStoreError {
    match error {
        LibraryError::InvalidIdentifier | LibraryError::InvalidOperation => {
            LibraryStoreError::InvalidIdentifier
        }
        LibraryError::InvalidQuery => LibraryStoreError::InvalidQuery,
        LibraryError::InvalidEntry => LibraryStoreError::InvalidEntry,
        LibraryError::WorkspaceNotSaved => LibraryStoreError::WorkspaceNotSaved,
        LibraryError::WorkspaceRevisionConflict => LibraryStoreError::StaleWorkspaceRevision,
        LibraryError::FactNotFound => LibraryStoreError::FactNotFound,
        LibraryError::FactHasNoActiveCitation => LibraryStoreError::UncitedFact,
        LibraryError::EntryNotFound => LibraryStoreError::UnknownEntry,
        LibraryError::LibraryRevisionConflict => LibraryStoreError::StaleLibraryRevision,
        LibraryError::EntryRevisionConflict => LibraryStoreError::StaleEntryRevision,
        LibraryError::OperationAlreadyPending => LibraryStoreError::OperationAlreadyPending,
        LibraryError::TooManyEntries => LibraryStoreError::TooManyEntries,
        LibraryError::TooManyPendingMutations => LibraryStoreError::TooManyPendingMutations,
        LibraryError::WrongCompletion => LibraryStoreError::WrongCompletion,
        LibraryError::RefreshCollectionNotFound => LibraryStoreError::RefreshCollectionNotFound,
        LibraryError::SourceNotRefreshable => LibraryStoreError::SourceNotRefreshable,
        LibraryError::InvalidRefreshPreview => LibraryStoreError::InvalidRefreshPreview,
        LibraryError::InvalidRefreshResult => LibraryStoreError::InvalidRefreshResult,
    }
}

pub(super) fn valid_identifier(value: &str) -> bool {
    !value.is_empty() && value.len() <= core_service_types::MAX_IDENTIFIER_BYTES
}
