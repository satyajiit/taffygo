// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::backup::{
    prepare_backup_manifest, BackupCreation, BackupCreationPhase, BackupDigest, BackupError,
    BackupManifestDraft, BackupRecord, BackupRecordKind, BackupRecordState, BackupSelection,
};

struct FixedDigest;

impl BackupDigest for FixedDigest {
    fn sha256(&self, _input: &[u8]) -> Result<[u8; 32], BackupError> {
        Ok([9; 32])
    }
}

fn selection() -> Result<BackupSelection, BackupError> {
    BackupSelection::new([BackupRecordKind::SavedWorkspace])
}

fn draft(backup_id: &str) -> Result<BackupManifestDraft, BackupError> {
    Ok(BackupManifestDraft {
        backup_id: backup_id.to_owned(),
        source_installation_id: "installation-01".to_owned(),
        created_at_utc: "2026-09-04T18:00:00Z".to_owned(),
        selection: selection()?,
        records: vec![BackupRecord {
            kind: BackupRecordKind::SavedWorkspace,
            stable_id: "workspace-1".to_owned(),
            revision: 3,
            schema_version: 1,
            state: BackupRecordState::Active,
            plaintext_bytes: 12,
            plaintext_sha256: [4; 32],
        }],
    })
}

#[test]
fn creation_requires_recovery_ack_and_readback_verification() -> Result<(), BackupError> {
    let mut creation = BackupCreation::begin("backup-01".to_owned(), selection()?)?;
    let prepared = prepare_backup_manifest(draft("backup-01")?, &FixedDigest)?;
    assert_eq!(
        creation.accept_prepared_manifest(&prepared),
        Err(BackupError::WrongPhase)
    );
    assert_eq!(
        creation.confirm_recovery_key([0; 16]),
        Err(BackupError::RecoveryKeyNotConfirmed)
    );
    creation.confirm_recovery_key([7; 16])?;
    let wrong = prepare_backup_manifest(draft("different-backup")?, &FixedDigest)?;
    assert_eq!(
        creation.accept_prepared_manifest(&wrong),
        Err(BackupError::SnapshotMismatch)
    );
    creation.accept_prepared_manifest(&prepared)?;
    creation.record_sealed_chunk(0, [1; 12])?;
    assert_eq!(
        creation.record_sealed_chunk(0, [2; 12]),
        Err(BackupError::ChunkOutOfOrder)
    );
    assert_eq!(
        creation.record_sealed_chunk(1, [1; 12]),
        Err(BackupError::ReusedNonce)
    );
    creation.record_sealed_chunk(1, [2; 12])?;
    assert_eq!(creation.phase(), BackupCreationPhase::Writing);
    creation.written()?;
    assert_eq!(
        creation.verified_written_copy([8; 32]),
        Err(BackupError::SnapshotMismatch)
    );
    assert_eq!(creation.phase(), BackupCreationPhase::Failed);
    Ok(())
}
