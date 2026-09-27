// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Pure, bounded planning and comparison for an approved Library refresh.
//!
//! A preview is computed only from the exact resident Library revision and an
//! exact saved workspace revision. It carries canonical locators internally,
//! but callers project only source identity, title, host, provider disclosure,
//! and work counts to UI. Recomputing and matching the preview before work
//! begins means deletion or any intervening mutation withdraws the plan rather
//! than recreating the collection from stale task state.

use std::collections::BTreeMap;

use crate::ids::{SourceId, WorkspaceId};
use crate::workspace::{FactKind, WorkspacePhase, WorkspaceSnapshot};

use super::{LibraryEntry, LibraryError, LibraryStore};

const PREVIEW_DOMAIN: &[u8] = b"taffy.library-refresh.preview.v1\0";
const SOURCE_CONTENT_DOMAIN: &[u8] = b"taffy.library-refresh.source-content.v1\0";

/// Cryptographic digest dependency supplied by the profile runtime.
pub trait LibraryRefreshDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], LibraryError>;
}

/// One exact saved page the approved task may revisit.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRefreshSource {
    pub source_id: SourceId,
    pub title: String,
    pub host: String,
    /// Browser-owned canonical address. Never project this field to UI.
    pub canonical_locator: String,
    /// Digest of the original single-source page facts, not page content.
    pub original_content_digest: [u8; 32],
}

/// Immutable pre-egress plan bound to exact Library and workspace revisions.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRefreshPreview {
    pub preview_id: String,
    pub library_revision: u64,
    pub collection_id: WorkspaceId,
    pub collection_name: String,
    pub source_workspace_revision: u64,
    pub sources: Vec<LibraryRefreshSource>,
}

impl LibraryRefreshPreview {
    pub fn estimated_navigation_count(&self) -> u32 {
        u32::try_from(self.sources.len()).unwrap_or(u32::MAX)
    }

    pub fn estimated_observation_count(&self) -> u32 {
        self.estimated_navigation_count()
    }

    pub fn estimated_work_units(&self) -> u32 {
        self.estimated_navigation_count()
            .saturating_add(self.estimated_observation_count())
    }
}

/// Exact comparison of one refreshed page with its preserved original.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LibraryRefreshDisposition {
    Unchanged,
    Changed,
    Missing,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRefreshSourceResult {
    pub source_id: SourceId,
    pub disposition: LibraryRefreshDisposition,
}

/// Result of a terminal refresh task. This never mutates the original.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct LibraryRefreshResult {
    pub preview_id: String,
    pub collection_id: WorkspaceId,
    pub original_workspace_revision: u64,
    pub refreshed_workspace_id: WorkspaceId,
    pub sources: Vec<LibraryRefreshSourceResult>,
}

impl LibraryStore {
    /// Produces an exact, deterministic plan without navigation or egress.
    pub fn preview_collection_refresh(
        &self,
        collection_id: WorkspaceId,
        expected_library_revision: u64,
        workspace: &WorkspaceSnapshot,
        expected_workspace_revision: u64,
        digest: &dyn LibraryRefreshDigest,
    ) -> Result<LibraryRefreshPreview, LibraryError> {
        if self.revision() != expected_library_revision {
            return Err(LibraryError::LibraryRevisionConflict);
        }
        if !workspace.saved || workspace.workspace_id != collection_id {
            return Err(LibraryError::RefreshCollectionNotFound);
        }
        if workspace.revision != expected_workspace_revision {
            return Err(LibraryError::WorkspaceRevisionConflict);
        }
        let entries = self
            .entries()
            .filter(|entry| entry.collection_id == collection_id)
            .collect::<Vec<_>>();
        if entries.is_empty() {
            return Err(LibraryError::RefreshCollectionNotFound);
        }
        if entries.iter().any(|entry| {
            entry.source_workspace_id != collection_id
                || entry.sources.iter().any(|source| {
                    workspace
                        .source(source.source_id)
                        .is_none_or(|held| held.excluded)
                })
        }) {
            return Err(LibraryError::InvalidRefreshPreview);
        }
        let mut source_entries = BTreeMap::<SourceId, Vec<&LibraryEntry>>::new();
        for entry in entries {
            for source in &entry.sources {
                source_entries
                    .entry(source.source_id)
                    .or_default()
                    .push(entry);
            }
        }
        let mut sources = Vec::with_capacity(source_entries.len());
        for source_id in source_entries.keys() {
            let source = workspace
                .source(*source_id)
                .ok_or(LibraryError::InvalidRefreshPreview)?;
            let locator = source
                .canonical_locator
                .as_ref()
                .ok_or(LibraryError::SourceNotRefreshable)?;
            sources.push(LibraryRefreshSource {
                source_id: *source_id,
                title: source.title.clone(),
                host: source.host.clone(),
                canonical_locator: locator.clone(),
                original_content_digest: source_content_digest(workspace, *source_id, digest)?,
            });
        }
        if sources.is_empty() {
            return Err(LibraryError::InvalidRefreshPreview);
        }
        let collection_name = source_entries
            .values()
            .flat_map(|entries| entries.iter().copied())
            .next()
            .map(|entry| entry.collection_name.clone())
            .ok_or(LibraryError::InvalidRefreshPreview)?;
        let preview_id = preview_digest(
            expected_library_revision,
            collection_id,
            expected_workspace_revision,
            &sources,
            digest,
        )?;
        Ok(LibraryRefreshPreview {
            preview_id,
            library_revision: expected_library_revision,
            collection_id,
            collection_name,
            source_workspace_revision: expected_workspace_revision,
            sources,
        })
    }

    /// Compares terminal refreshed facts after proving the preview is still
    /// current. Deletion and revision drift are refusal, never resurrection.
    pub fn compare_collection_refresh(
        &self,
        preview: &LibraryRefreshPreview,
        original: &WorkspaceSnapshot,
        refreshed: &WorkspaceSnapshot,
        digest: &dyn LibraryRefreshDigest,
    ) -> Result<LibraryRefreshResult, LibraryError> {
        let current = self.preview_collection_refresh(
            preview.collection_id,
            preview.library_revision,
            original,
            preview.source_workspace_revision,
            digest,
        )?;
        if &current != preview
            || refreshed.workspace_id == original.workspace_id
            || refreshed.saved
            || !matches!(
                refreshed.phase,
                WorkspacePhase::Done
                    | WorkspacePhase::PartlyDone
                    | WorkspacePhase::Stopped
                    | WorkspacePhase::Failed
            )
            || refreshed.sources.len() != preview.sources.len()
        {
            return Err(LibraryError::InvalidRefreshResult);
        }
        let mut results = Vec::with_capacity(preview.sources.len());
        for expected in &preview.sources {
            let source = refreshed
                .source(expected.source_id)
                .filter(|source| {
                    !source.excluded
                        && source.host == expected.host
                        && source.canonical_locator.as_deref()
                            == Some(expected.canonical_locator.as_str())
                })
                .ok_or(LibraryError::InvalidRefreshResult)?;
            let disposition = if source.read_at_epoch_ms == 0 {
                LibraryRefreshDisposition::Missing
            } else if source_content_digest(refreshed, source.source_id, digest)?
                == expected.original_content_digest
            {
                LibraryRefreshDisposition::Unchanged
            } else {
                LibraryRefreshDisposition::Changed
            };
            results.push(LibraryRefreshSourceResult {
                source_id: source.source_id,
                disposition,
            });
        }
        Ok(LibraryRefreshResult {
            preview_id: preview.preview_id.clone(),
            collection_id: preview.collection_id,
            original_workspace_revision: preview.source_workspace_revision,
            refreshed_workspace_id: refreshed.workspace_id,
            sources: results,
        })
    }
}

fn source_content_digest(
    workspace: &WorkspaceSnapshot,
    source_id: SourceId,
    digest: &dyn LibraryRefreshDigest,
) -> Result<[u8; 32], LibraryError> {
    let mut values = workspace
        .facts
        .iter()
        .filter(|fact| fact.kind == FactKind::FromPage && fact.sources.as_slice() == [source_id])
        .map(|fact| fact.value.as_str())
        .collect::<Vec<_>>();
    values.sort_unstable();
    let mut material = Vec::from(SOURCE_CONTENT_DOMAIN);
    material.extend_from_slice(source_id.to_text().as_bytes());
    for value in values {
        append_bounded(&mut material, value.as_bytes())?;
    }
    digest.sha256(&material)
}

fn preview_digest(
    library_revision: u64,
    collection_id: WorkspaceId,
    workspace_revision: u64,
    sources: &[LibraryRefreshSource],
    digest: &dyn LibraryRefreshDigest,
) -> Result<String, LibraryError> {
    let mut material = Vec::from(PREVIEW_DOMAIN);
    material.extend_from_slice(&library_revision.to_be_bytes());
    material.extend_from_slice(collection_id.to_text().as_bytes());
    material.extend_from_slice(&workspace_revision.to_be_bytes());
    for source in sources {
        material.extend_from_slice(source.source_id.to_text().as_bytes());
        append_bounded(&mut material, source.canonical_locator.as_bytes())?;
        material.extend_from_slice(&source.original_content_digest);
    }
    Ok(hex(&digest.sha256(&material)?))
}

fn append_bounded(material: &mut Vec<u8>, value: &[u8]) -> Result<(), LibraryError> {
    let length = u32::try_from(value.len()).map_err(|_| LibraryError::InvalidRefreshPreview)?;
    material.extend_from_slice(&length.to_be_bytes());
    material.extend_from_slice(value);
    Ok(())
}

fn hex(value: &[u8; 32]) -> String {
    let mut out = String::with_capacity(64);
    for byte in value {
        out.push(hex_digit(byte >> 4));
        out.push(hex_digit(byte & 0x0f));
    }
    out
}

const fn hex_digit(value: u8) -> char {
    match value {
        0 => '0',
        1 => '1',
        2 => '2',
        3 => '3',
        4 => '4',
        5 => '5',
        6 => '6',
        7 => '7',
        8 => '8',
        9 => '9',
        10 => 'a',
        11 => 'b',
        12 => 'c',
        13 => 'd',
        14 => 'e',
        _ => 'f',
    }
}
