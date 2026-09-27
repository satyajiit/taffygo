// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical creation plan from browser-owned typed record descriptors.
//!
//! Payload bytes never cross this interface. `source_order` tells the browser
//! which already-typed source record to append for each manifest position;
//! the snapshot digest binds that ordered descriptor, length, and content
//! digest before the physical archive adapter sees a destination.

use super::{
    encode_manifest, BackupDigest, BackupError, BackupManifest, BackupRecord, BackupSelection,
    BACKUP_PAYLOAD_CHUNK_BYTES, MAX_BACKUP_RECORDS,
};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupManifestDraft {
    pub backup_id: String,
    pub source_installation_id: String,
    pub created_at_utc: String,
    pub selection: BackupSelection,
    pub records: Vec<BackupRecord>,
}

/// A complete content-free instruction for the browser archive adapter.
///
/// For manifest record `i`, `source_order[i]` is the zero-based position of
/// the matching descriptor in the draft consumed by
/// [`prepare_backup_manifest`]. The adapter must append that record's exact
/// payload bytes and no others. The manifest's snapshot digest covers the
/// resulting canonical record order, every plaintext length, and every
/// plaintext digest.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PreparedBackupManifest {
    manifest: BackupManifest,
    manifest_plaintext: Vec<u8>,
    payload_plaintext_bytes: u64,
    source_order: Vec<u32>,
    expected_sealed_chunks: u32,
}

impl PreparedBackupManifest {
    #[must_use]
    pub fn manifest_plaintext(&self) -> &[u8] {
        &self.manifest_plaintext
    }

    #[must_use]
    pub fn snapshot_sha256(&self) -> [u8; 32] {
        self.manifest.snapshot_sha256
    }

    #[must_use]
    pub fn payload_plaintext_bytes(&self) -> u64 {
        self.payload_plaintext_bytes
    }

    #[must_use]
    pub fn source_order(&self) -> &[u32] {
        &self.source_order
    }

    #[must_use]
    pub fn expected_sealed_chunks(&self) -> u32 {
        self.expected_sealed_chunks
    }

    pub(super) fn manifest(&self) -> &BackupManifest {
        &self.manifest
    }
}

pub fn prepare_backup_manifest(
    draft: BackupManifestDraft,
    digest: &dyn BackupDigest,
) -> Result<PreparedBackupManifest, BackupError> {
    if draft.records.len() > MAX_BACKUP_RECORDS {
        return Err(BackupError::TooManyRecords);
    }

    let mut manifest = BackupManifest {
        backup_id: draft.backup_id,
        source_installation_id: draft.source_installation_id,
        created_at_utc: draft.created_at_utc,
        selection: draft.selection,
        records: draft.records,
        snapshot_sha256: [0; 32],
    };
    // Check every scalar and aggregate byte bound before allocating either
    // of the two output vectors below.
    manifest.validate_content_shape()?;

    let mut indexed = manifest.records.drain(..).enumerate().collect::<Vec<_>>();
    indexed.sort_unstable_by(|left, right| {
        (left.1.kind, left.1.stable_id.as_str()).cmp(&(right.1.kind, right.1.stable_id.as_str()))
    });

    let mut source_order = Vec::with_capacity(indexed.len());
    manifest.records.reserve_exact(indexed.len());
    for (source_ordinal, record) in indexed {
        source_order.push(u32::try_from(source_ordinal).map_err(|_| BackupError::TooManyRecords)?);
        manifest.records.push(record);
    }
    manifest.validate_content()?;
    let payload_plaintext_bytes = manifest.payload_plaintext_bytes()?;
    manifest.snapshot_sha256 = manifest.calculate_snapshot_digest(digest)?;
    let manifest_plaintext = encode_manifest(&manifest)?;
    let payload_chunks = payload_plaintext_bytes.div_ceil(BACKUP_PAYLOAD_CHUNK_BYTES);
    let expected_sealed_chunks = u32::try_from(
        payload_chunks
            .checked_add(1)
            .ok_or(BackupError::ArchiveTooLarge)?,
    )
    .map_err(|_| BackupError::ArchiveTooLarge)?;

    Ok(PreparedBackupManifest {
        manifest,
        manifest_plaintext,
        payload_plaintext_bytes,
        source_order,
        expected_sealed_chunks,
    })
}
