// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Portable invariants for explicit encrypted backup and restore.
//!
//! A backup is an immutable, user-requested snapshot. It is not a sync peer and
//! never participates in conflict resolution. The browser remains the physical
//! reader/writer; this module admits only the record classes that may cross the
//! backup boundary and validates the encrypted manifest before any restore is
//! staged.

mod codec;
mod create;
mod header;
mod open;
mod plan_codec;
mod prepare;
mod recovery;
mod restore;

pub use codec::{decode_manifest, encode_manifest, MANIFEST_FORMAT_VERSION};
pub use create::{BackupCreation, BackupCreationPhase};
pub use header::{
    decode_aib_header, encode_aib_header, AibHeader, AIB_FORMAT_VERSION, AIB_MAGIC,
    AIB_PUBLIC_HEADER_BYTES,
};
pub use open::{
    open_backup_manifest, BackupPayloadLayout, BackupPayloadLayoutEntry, OpenedBackupManifest,
    StagedBackupRecord,
};
pub use prepare::{prepare_backup_manifest, BackupManifestDraft, PreparedBackupManifest};
pub use recovery::{
    validate_restore_recovery_history, RestoreObservedOutcome, RestorePhysicalIntent,
    RestoreRecoveryBinding, RestoreRecoveryError, RestoreRecoveryFact, RestoreRecoveryRecord,
    RestoreRecoveryStatus, RESTORE_RECOVERY_RECORD_VERSION,
};
pub use restore::{
    CurrentRecord, RestoreAction, RestoreEntry, RestorePlan, RestoreSession, RestoreSessionPhase,
    RestoreTarget, RestoreTargetKind,
};

use core::fmt;
use std::collections::BTreeSet;

/// A defensive record bound before encrypted bytes are allocated.
pub const MAX_BACKUP_RECORDS: usize = 100_000;
/// A separate bound for content-free physical recovery facts.
///
/// Exhausting this history keeps the candidate quarantined. It never expands
/// into the much larger payload-record bound and never authorizes replay.
pub const MAX_BACKUP_RESTORE_RECOVERY_RECORDS: usize = 128;
/// A defensive per-record plaintext bound. Larger blobs are never backup rows.
pub const MAX_BACKUP_RECORD_BYTES: u64 = 64 * 1024 * 1024;
/// A defensive total plaintext bound for one archive.
pub const MAX_BACKUP_PLAINTEXT_BYTES: u64 = 8 * 1024 * 1024 * 1024;
/// The encrypted manifest is opened as one bounded allocation.
pub const MAX_BACKUP_MANIFEST_BYTES: u64 = 64 * 1024 * 1024;
/// Payload records are concatenated inside an encrypted fixed-chunk stream;
/// chunk boundaries therefore disclose only total archive size, not records.
pub const BACKUP_PAYLOAD_CHUNK_BYTES: u64 = 1024 * 1024;
/// Maximum opaque identifier length inside the encrypted manifest.
pub const MAX_BACKUP_ID_BYTES: usize = 160;

/// The only cryptographic suite accepted by archive format version 1.
///
/// The browser crypto adapter implements this with its reviewed platform
/// crypto. The portable storage domain deliberately contains no cipher.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum BackupCipherSuite {
    /// A random 256-bit archive key; AES-256-GCM records; the archive key is
    /// wrapped by a random user-held recovery key through HKDF-SHA-256 and
    /// AES-256-GCM. Every seal uses a distinct 96-bit nonce.
    Aes256GcmHkdfSha256,
}

/// Record classes that a person may explicitly include.
///
/// Secrets, live task authority, browser sessions, derived indexes, caches,
/// downloaded model/tool artifacts and built-in skills are absent by type.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum BackupRecordKind {
    AssistantConfiguration,
    SavedWorkspace,
    LibraryEntry,
    MemoryRecord,
    UserAuthoredSkill,
    LearnedProcedure,
    Bookmark,
    BrowserPreference,
}

impl BackupRecordKind {
    pub const ALL: [Self; 8] = [
        Self::AssistantConfiguration,
        Self::SavedWorkspace,
        Self::LibraryEntry,
        Self::MemoryRecord,
        Self::UserAuthoredSkill,
        Self::LearnedProcedure,
        Self::Bookmark,
        Self::BrowserPreference,
    ];
}

/// User-confirmed archive selection. An empty selection is invalid.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupSelection {
    included: BTreeSet<BackupRecordKind>,
}

impl BackupSelection {
    pub fn new(included: impl IntoIterator<Item = BackupRecordKind>) -> Result<Self, BackupError> {
        let included = included.into_iter().collect::<BTreeSet<_>>();
        if included.is_empty() {
            return Err(BackupError::EmptySelection);
        }
        Ok(Self { included })
    }

    #[must_use]
    pub fn includes(&self, kind: BackupRecordKind) -> bool {
        self.included.contains(&kind)
    }

    pub fn iter(&self) -> impl Iterator<Item = BackupRecordKind> + '_ {
        self.included.iter().copied()
    }
}

/// One stable domain record described inside the encrypted manifest.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupRecord {
    pub kind: BackupRecordKind,
    pub stable_id: String,
    pub revision: u64,
    pub schema_version: u32,
    pub state: BackupRecordState,
    pub plaintext_bytes: u64,
    pub plaintext_sha256: [u8; 32],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BackupRecordState {
    Active,
    Tombstone,
}

/// The authenticated, encrypted archive manifest after successful opening.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupManifest {
    pub backup_id: String,
    pub source_installation_id: String,
    pub created_at_utc: String,
    pub selection: BackupSelection,
    pub records: Vec<BackupRecord>,
    /// Digest of canonical record descriptors and plaintext hashes. It binds a
    /// staged restore without exposing record identities in the public header.
    pub snapshot_sha256: [u8; 32],
}

/// Narrow digest boundary implemented by the reviewed browser/service crypto
/// adapter in production. The portable domain owns the canonical bytes.
pub trait BackupDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], BackupError>;
}

impl BackupManifest {
    pub fn validate(&self) -> Result<(), BackupError> {
        self.validate_content()?;
        if self.snapshot_sha256.iter().all(|byte| *byte == 0) {
            return Err(BackupError::InvalidDigest);
        }
        Ok(())
    }

    pub fn verify_snapshot_digest(&self, digest: &dyn BackupDigest) -> Result<(), BackupError> {
        self.validate()?;
        if self.calculate_snapshot_digest(digest)? != self.snapshot_sha256 {
            return Err(BackupError::SnapshotMismatch);
        }
        Ok(())
    }

    pub fn calculate_snapshot_digest(
        &self,
        digest: &dyn BackupDigest,
    ) -> Result<[u8; 32], BackupError> {
        self.validate_content()?;
        let material = codec::encode_snapshot_material(self)?;
        let calculated = digest.sha256(&material)?;
        if calculated.iter().all(|byte| *byte == 0) {
            return Err(BackupError::InvalidDigest);
        }
        Ok(calculated)
    }

    fn validate_content(&self) -> Result<(), BackupError> {
        self.validate_content_shape()?;
        for records in self.records.windows(2) {
            let previous = records.first().ok_or(BackupError::MalformedManifest)?;
            let current = records.get(1).ok_or(BackupError::MalformedManifest)?;
            match (previous.kind, previous.stable_id.as_str())
                .cmp(&(current.kind, current.stable_id.as_str()))
            {
                core::cmp::Ordering::Less => {}
                core::cmp::Ordering::Equal => return Err(BackupError::DuplicateRecord),
                core::cmp::Ordering::Greater => return Err(BackupError::RecordOutOfOrder),
            }
        }
        Ok(())
    }

    fn validate_content_shape(&self) -> Result<(), BackupError> {
        validate_id("backup id", &self.backup_id)?;
        validate_id("source installation id", &self.source_installation_id)?;
        if self.created_at_utc.is_empty() || self.created_at_utc.len() > 40 {
            return Err(BackupError::InvalidTimestamp);
        }
        if self.records.len() > MAX_BACKUP_RECORDS {
            return Err(BackupError::TooManyRecords);
        }
        let mut total = 0_u64;
        for record in &self.records {
            validate_id("record id", &record.stable_id)?;
            if !self.selection.includes(record.kind) {
                return Err(BackupError::UnselectedRecord(record.kind));
            }
            if record.revision == 0 || record.schema_version == 0 {
                return Err(BackupError::InvalidRecordVersion);
            }
            match record.state {
                BackupRecordState::Active if record.plaintext_bytes == 0 => {
                    return Err(BackupError::EmptyActiveRecord);
                }
                BackupRecordState::Active
                    if record.plaintext_sha256.iter().all(|byte| *byte == 0) =>
                {
                    return Err(BackupError::InvalidDigest);
                }
                BackupRecordState::Tombstone if record.plaintext_bytes != 0 => {
                    return Err(BackupError::TombstoneCarriesContent);
                }
                BackupRecordState::Tombstone
                    if record.plaintext_sha256.iter().any(|byte| *byte != 0) =>
                {
                    return Err(BackupError::InvalidDigest);
                }
                _ => {}
            }
            if record.plaintext_bytes > MAX_BACKUP_RECORD_BYTES {
                return Err(BackupError::RecordTooLarge);
            }
            total = total
                .checked_add(record.plaintext_bytes)
                .ok_or(BackupError::ArchiveTooLarge)?;
            if total > MAX_BACKUP_PLAINTEXT_BYTES {
                return Err(BackupError::ArchiveTooLarge);
            }
        }
        Ok(())
    }

    fn payload_plaintext_bytes(&self) -> Result<u64, BackupError> {
        self.validate_content()?;
        self.records.iter().try_fold(0_u64, |total, record| {
            total
                .checked_add(record.plaintext_bytes)
                .ok_or(BackupError::ArchiveTooLarge)
        })
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum BackupError {
    EmptySelection,
    InvalidIdentifier(&'static str),
    InvalidTimestamp,
    TooManyRecords,
    UnselectedRecord(BackupRecordKind),
    InvalidRecordVersion,
    EmptyActiveRecord,
    TombstoneCarriesContent,
    RecordTooLarge,
    ArchiveTooLarge,
    DuplicateRecord,
    RecordOutOfOrder,
    WrongMagic,
    UnsupportedFormat(u16),
    UnsupportedCipherSuite(u16),
    MalformedHeader,
    InvalidNonce,
    ReusedNonce,
    MissingManifest,
    WrongPhase,
    RecoveryKeyNotConfirmed,
    ChunkOutOfOrder,
    SnapshotMismatch,
    RestoreConflict,
    RestoreNotConfirmed,
    InvalidDigest,
    DigestUnavailable,
    MalformedManifest,
    UnsupportedManifestVersion(u16),
    UnknownRecordKind(u8),
    UnknownRecordState(u8),
    TrailingManifestData,
    NonCanonicalManifest,
    StagedPayloadMismatch,
}

impl fmt::Display for BackupError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "backup operation refused: {self:?}")
    }
}

fn validate_id(what: &'static str, value: &str) -> Result<(), BackupError> {
    if value.is_empty() || value.len() > MAX_BACKUP_ID_BYTES || value.chars().any(char::is_control)
    {
        return Err(BackupError::InvalidIdentifier(what));
    }
    Ok(())
}
