// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Authenticated manifest opening and staged-payload verification.
//!
//! The browser crypto adapter authenticates and decrypts bytes. This module
//! then requires the one canonical portable manifest and compares the hashes
//! and lengths observed from the decrypted staging file before a restore plan
//! can be constructed.

use super::{
    decode_manifest, encode_manifest, restore, BackupDigest, BackupError, BackupManifest,
    BackupRecordState, CurrentRecord, RestorePlan, RestoreTarget,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BackupPayloadLayoutEntry {
    pub state: BackupRecordState,
    pub plaintext_bytes: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupPayloadLayout {
    pub snapshot_sha256: [u8; 32],
    pub payload_plaintext_bytes: u64,
    pub records: Vec<BackupPayloadLayoutEntry>,
}

/// What the browser observed while hashing one exact staged record range.
/// Positions correspond to the layout returned by [`OpenedBackupManifest`].
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct StagedBackupRecord {
    pub plaintext_bytes: u64,
    pub plaintext_sha256: [u8; 32],
}

/// A decoded canonical manifest whose snapshot digest has been verified.
/// Its fields stay private so callers cannot construct a restore-plan bypass.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OpenedBackupManifest {
    manifest: BackupManifest,
}

pub fn open_backup_manifest(
    manifest_plaintext: &[u8],
    digest: &dyn BackupDigest,
) -> Result<OpenedBackupManifest, BackupError> {
    let manifest = decode_manifest(manifest_plaintext)?;
    manifest.verify_snapshot_digest(digest)?;
    if encode_manifest(&manifest)?.as_slice() != manifest_plaintext {
        return Err(BackupError::NonCanonicalManifest);
    }
    Ok(OpenedBackupManifest { manifest })
}

impl OpenedBackupManifest {
    #[must_use]
    pub fn backup_id(&self) -> &str {
        &self.manifest.backup_id
    }

    #[must_use]
    pub fn source_installation_id(&self) -> &str {
        &self.manifest.source_installation_id
    }

    #[must_use]
    pub fn created_at_utc(&self) -> &str {
        &self.manifest.created_at_utc
    }

    pub fn selection(&self) -> impl Iterator<Item = super::BackupRecordKind> + '_ {
        self.manifest.selection.iter()
    }

    #[must_use]
    pub fn record_count(&self) -> usize {
        self.manifest.records.len()
    }

    pub fn payload_layout(&self) -> Result<BackupPayloadLayout, BackupError> {
        let payload_plaintext_bytes = self.manifest.payload_plaintext_bytes()?;
        let records = self
            .manifest
            .records
            .iter()
            .map(|record| BackupPayloadLayoutEntry {
                state: record.state,
                plaintext_bytes: record.plaintext_bytes,
            })
            .collect();
        Ok(BackupPayloadLayout {
            snapshot_sha256: self.manifest.snapshot_sha256,
            payload_plaintext_bytes,
            records,
        })
    }

    /// Produces no plan unless every record range from the decrypted staging
    /// file matches the authenticated manifest exactly.
    pub fn plan_restore(
        &self,
        staged_records: &[StagedBackupRecord],
        current: &[CurrentRecord],
        target: RestoreTarget,
    ) -> Result<RestorePlan, BackupError> {
        self.verify_staged_payload(staged_records)?;
        restore::plan_verified_restore(&self.manifest, current, target)
    }

    fn verify_staged_payload(
        &self,
        staged_records: &[StagedBackupRecord],
    ) -> Result<(), BackupError> {
        if staged_records.len() != self.manifest.records.len() {
            return Err(BackupError::StagedPayloadMismatch);
        }
        let mut observed_total = 0_u64;
        for (expected, observed) in self.manifest.records.iter().zip(staged_records) {
            observed_total = observed_total
                .checked_add(observed.plaintext_bytes)
                .ok_or(BackupError::StagedPayloadMismatch)?;
            if observed.plaintext_bytes != expected.plaintext_bytes
                || observed.plaintext_sha256 != expected.plaintext_sha256
            {
                return Err(BackupError::StagedPayloadMismatch);
            }
        }
        if observed_total != self.manifest.payload_plaintext_bytes()? {
            return Err(BackupError::StagedPayloadMismatch);
        }
        Ok(())
    }
}
