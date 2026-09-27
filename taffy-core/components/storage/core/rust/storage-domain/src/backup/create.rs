// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Creation state: recovery-key acknowledgement precedes sealing, and remote
//! success is not claimed until the written ciphertext is read and verified.

use std::collections::BTreeSet;

use super::{BackupError, BackupSelection, PreparedBackupManifest};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BackupCreationPhase {
    AwaitingRecoveryKeyConfirmation,
    ReadingSnapshot,
    Sealing,
    Writing,
    VerifyingWrittenCopy,
    Complete,
    Failed,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BackupCreation {
    backup_id: String,
    selection: BackupSelection,
    phase: BackupCreationPhase,
    recovery_key_fingerprint: Option<[u8; 16]>,
    snapshot_sha256: Option<[u8; 32]>,
    next_chunk: u32,
    expected_chunks: Option<u32>,
    sealed_nonces: BTreeSet<[u8; 12]>,
}

impl BackupCreation {
    pub fn begin(backup_id: String, selection: BackupSelection) -> Result<Self, BackupError> {
        super::validate_id("backup id", &backup_id)?;
        Ok(Self {
            backup_id,
            selection,
            phase: BackupCreationPhase::AwaitingRecoveryKeyConfirmation,
            recovery_key_fingerprint: None,
            snapshot_sha256: None,
            next_chunk: 0,
            expected_chunks: None,
            sealed_nonces: BTreeSet::new(),
        })
    }

    #[must_use]
    pub fn phase(&self) -> BackupCreationPhase {
        self.phase
    }

    pub fn confirm_recovery_key(&mut self, fingerprint: [u8; 16]) -> Result<(), BackupError> {
        if self.phase != BackupCreationPhase::AwaitingRecoveryKeyConfirmation {
            return Err(BackupError::WrongPhase);
        }
        if fingerprint.iter().all(|byte| *byte == 0) {
            return Err(BackupError::RecoveryKeyNotConfirmed);
        }
        self.recovery_key_fingerprint = Some(fingerprint);
        self.phase = BackupCreationPhase::ReadingSnapshot;
        Ok(())
    }

    pub fn accept_prepared_manifest(
        &mut self,
        prepared: &PreparedBackupManifest,
    ) -> Result<(), BackupError> {
        if self.phase != BackupCreationPhase::ReadingSnapshot {
            return Err(BackupError::WrongPhase);
        }
        let manifest = prepared.manifest();
        if manifest.backup_id != self.backup_id || manifest.selection != self.selection {
            return Err(BackupError::SnapshotMismatch);
        }
        self.snapshot_sha256 = Some(manifest.snapshot_sha256);
        self.expected_chunks = Some(prepared.expected_sealed_chunks());
        self.phase = BackupCreationPhase::Sealing;
        Ok(())
    }

    pub fn record_sealed_chunk(
        &mut self,
        chunk_index: u32,
        nonce: [u8; 12],
    ) -> Result<(), BackupError> {
        if self.phase != BackupCreationPhase::Sealing {
            return Err(BackupError::WrongPhase);
        }
        if chunk_index != self.next_chunk {
            return Err(BackupError::ChunkOutOfOrder);
        }
        if nonce.iter().all(|byte| *byte == 0) {
            return Err(BackupError::InvalidNonce);
        }
        if !self.sealed_nonces.insert(nonce) {
            return Err(BackupError::ReusedNonce);
        }
        self.next_chunk = self
            .next_chunk
            .checked_add(1)
            .ok_or(BackupError::TooManyRecords)?;
        if Some(self.next_chunk) == self.expected_chunks {
            self.phase = BackupCreationPhase::Writing;
        }
        Ok(())
    }

    pub fn written(&mut self) -> Result<(), BackupError> {
        if self.phase != BackupCreationPhase::Writing {
            return Err(BackupError::WrongPhase);
        }
        self.phase = BackupCreationPhase::VerifyingWrittenCopy;
        Ok(())
    }

    pub fn verified_written_copy(
        &mut self,
        opened_snapshot_sha256: [u8; 32],
    ) -> Result<(), BackupError> {
        if self.phase != BackupCreationPhase::VerifyingWrittenCopy {
            return Err(BackupError::WrongPhase);
        }
        if self.snapshot_sha256 != Some(opened_snapshot_sha256) {
            self.phase = BackupCreationPhase::Failed;
            return Err(BackupError::SnapshotMismatch);
        }
        self.phase = BackupCreationPhase::Complete;
        Ok(())
    }

    pub fn fail(&mut self) {
        if self.phase != BackupCreationPhase::Complete {
            self.phase = BackupCreationPhase::Failed;
        }
    }
}
