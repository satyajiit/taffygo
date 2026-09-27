// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::backup::{
    encode_manifest, open_backup_manifest, BackupDigest, BackupError, BackupManifest, BackupRecord,
    BackupRecordKind, BackupRecordState, BackupSelection, CurrentRecord, RestoreAction,
    RestoreSession, RestoreSessionPhase, RestoreTarget, RestoreTargetKind, StagedBackupRecord,
};

struct FixedDigest([u8; 32]);

impl BackupDigest for FixedDigest {
    fn sha256(&self, _input: &[u8]) -> Result<[u8; 32], BackupError> {
        Ok(self.0)
    }
}

const DIGEST: FixedDigest = FixedDigest([9; 32]);

struct PlanDigest;

impl BackupDigest for PlanDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], BackupError> {
        // A content-sensitive test double. Production supplies SHA-256 across
        // the service boundary; the domain test only needs to prove that a
        // changed plan cannot echo the previously issued confirmation.
        let mut output = [0_u8; 32];
        for (index, byte) in input.iter().copied().enumerate() {
            let slot = index % output.len();
            if let Some(value) = output.get_mut(slot) {
                *value = value
                    .wrapping_add(byte)
                    .rotate_left(u32::try_from(index % 7).unwrap_or(0));
            }
        }
        if let Some(first) = output.first_mut() {
            *first |= 1;
        }
        Ok(output)
    }
}

const PLAN_DIGEST: PlanDigest = PlanDigest;

fn target(kind: RestoreTargetKind) -> RestoreTarget {
    RestoreTarget {
        kind,
        profile_id: "target-profile-01".to_owned(),
    }
}

fn selection() -> Result<BackupSelection, BackupError> {
    BackupSelection::new([
        BackupRecordKind::SavedWorkspace,
        BackupRecordKind::MemoryRecord,
    ])
}

fn record(
    kind: BackupRecordKind,
    stable_id: &str,
    revision: u64,
    state: BackupRecordState,
    byte: u8,
) -> BackupRecord {
    BackupRecord {
        kind,
        stable_id: stable_id.to_owned(),
        revision,
        schema_version: 1,
        state,
        plaintext_bytes: if state == BackupRecordState::Active {
            12
        } else {
            0
        },
        plaintext_sha256: [byte; 32],
    }
}

fn manifest(records: Vec<BackupRecord>) -> Result<BackupManifest, BackupError> {
    let mut records = records;
    records.sort_unstable_by(|left, right| {
        (left.kind, left.stable_id.as_str()).cmp(&(right.kind, right.stable_id.as_str()))
    });
    Ok(BackupManifest {
        backup_id: "backup-01".to_owned(),
        source_installation_id: "installation-01".to_owned(),
        created_at_utc: "2026-09-04T18:00:00Z".to_owned(),
        selection: selection()?,
        records,
        snapshot_sha256: [9; 32],
    })
}

fn staged_records(manifest: &BackupManifest) -> Vec<StagedBackupRecord> {
    manifest
        .records
        .iter()
        .map(|record| StagedBackupRecord {
            plaintext_bytes: record.plaintext_bytes,
            plaintext_sha256: record.plaintext_sha256,
        })
        .collect()
}

fn plan_restore(
    manifest: &BackupManifest,
    current: &[CurrentRecord],
    target: RestoreTarget,
) -> Result<taffy_storage::backup::RestorePlan, BackupError> {
    open_backup_manifest(&encode_manifest(manifest)?, &DIGEST)?.plan_restore(
        &staged_records(manifest),
        current,
        target,
    )
}

#[test]
fn manifest_rejects_unselected_duplicate_and_contentful_tombstone_rows() -> Result<(), BackupError>
{
    let unselected = manifest(vec![record(
        BackupRecordKind::LibraryEntry,
        "library-1",
        1,
        BackupRecordState::Active,
        1,
    )])?;
    assert_eq!(
        unselected.validate(),
        Err(BackupError::UnselectedRecord(
            BackupRecordKind::LibraryEntry
        ))
    );

    let row = record(
        BackupRecordKind::MemoryRecord,
        "memory-1",
        1,
        BackupRecordState::Active,
        2,
    );
    let duplicate = manifest(vec![row.clone(), row])?;
    assert_eq!(duplicate.validate(), Err(BackupError::DuplicateRecord));

    let mut tombstone = record(
        BackupRecordKind::MemoryRecord,
        "memory-2",
        2,
        BackupRecordState::Tombstone,
        3,
    );
    tombstone.plaintext_bytes = 1;
    assert_eq!(
        manifest(vec![tombstone])?.validate(),
        Err(BackupError::TombstoneCarriesContent)
    );
    Ok(())
}

#[test]
fn restore_never_overwrites_conflicts_or_resurrects_deleted_records() -> Result<(), BackupError> {
    let source = manifest(vec![
        record(
            BackupRecordKind::SavedWorkspace,
            "new",
            1,
            BackupRecordState::Active,
            1,
        ),
        record(
            BackupRecordKind::SavedWorkspace,
            "deleted",
            4,
            BackupRecordState::Active,
            2,
        ),
        record(
            BackupRecordKind::MemoryRecord,
            "changed",
            2,
            BackupRecordState::Active,
            3,
        ),
    ])?;
    let current = vec![
        CurrentRecord {
            kind: BackupRecordKind::SavedWorkspace,
            stable_id: "deleted".to_owned(),
            revision: 5,
            schema_version: 1,
            state: BackupRecordState::Tombstone,
            plaintext_bytes: 0,
            plaintext_sha256: [0; 32],
        },
        CurrentRecord {
            kind: BackupRecordKind::MemoryRecord,
            stable_id: "changed".to_owned(),
            revision: 2,
            schema_version: 1,
            state: BackupRecordState::Active,
            plaintext_bytes: 12,
            plaintext_sha256: [4; 32],
        },
    ];
    let plan = plan_restore(
        &source,
        &current,
        target(RestoreTargetKind::ExistingRegularProfile),
    )?;
    let action = |stable_id: &str| {
        plan.entries()
            .iter()
            .find(|entry| entry.stable_id == stable_id)
            .map(|entry| entry.action)
    };
    assert_eq!(action("new"), Some(RestoreAction::StageCreate));
    assert_eq!(action("deleted"), Some(RestoreAction::BlockedByDeletion));
    assert_eq!(
        action("changed"),
        Some(RestoreAction::NeedsExplicitConflictChoice)
    );
    assert!(plan.has_conflicts());
    Ok(())
}

#[test]
fn staged_restore_commits_only_after_exact_digest_verification() -> Result<(), BackupError> {
    let source = manifest(vec![record(
        BackupRecordKind::SavedWorkspace,
        "workspace-1",
        1,
        BackupRecordState::Active,
        1,
    )])?;
    let plan = plan_restore(&source, &[], target(RestoreTargetKind::NewRegularProfile))?;
    let mut restore = RestoreSession::begin();
    restore.recovery_key_supplied()?;
    let confirmation = restore.archive_opened(&plan, &PLAN_DIGEST)?;
    assert_eq!(
        restore.confirm_plan(&plan, [6; 32], &PLAN_DIGEST),
        Err(BackupError::RestoreNotConfirmed)
    );
    let changed_target = plan_restore(
        &source,
        &[],
        RestoreTarget {
            kind: RestoreTargetKind::NewRegularProfile,
            profile_id: "another-profile".to_owned(),
        },
    )?;
    assert_eq!(
        restore.confirm_plan(&changed_target, confirmation, &PLAN_DIGEST),
        Err(BackupError::RestoreConflict)
    );
    restore.confirm_plan(&plan, confirmation, &PLAN_DIGEST)?;
    restore.staging_finished()?;
    assert_eq!(
        restore.staging_verified([8; 32]),
        Err(BackupError::SnapshotMismatch)
    );
    assert_eq!(restore.phase(), RestoreSessionPhase::RolledBack);

    let mut retry = RestoreSession::begin();
    retry.recovery_key_supplied()?;
    let confirmation = retry.archive_opened(&plan, &PLAN_DIGEST)?;
    retry.confirm_plan(&plan, confirmation, &PLAN_DIGEST)?;
    retry.staging_finished()?;
    retry.staging_verified(source.snapshot_sha256)?;
    retry.issue_commit()?;
    assert_eq!(retry.phase(), RestoreSessionPhase::CommitIssued);
    retry.commit_completed()?;
    assert_eq!(retry.phase(), RestoreSessionPhase::RollbackAvailable);
    retry.issue_accept_restored_profile()?;
    retry.accept_restored_profile_completed()?;
    assert_eq!(retry.phase(), RestoreSessionPhase::Complete);
    Ok(())
}

#[test]
fn conflict_table_distinguishes_equal_and_newer_tombstones() -> Result<(), BackupError> {
    let source = manifest(vec![
        record(
            BackupRecordKind::MemoryRecord,
            "active-newer-than-tombstone",
            6,
            BackupRecordState::Active,
            1,
        ),
        record(
            BackupRecordKind::MemoryRecord,
            "equal-state-divergence",
            4,
            BackupRecordState::Tombstone,
            0,
        ),
        record(
            BackupRecordKind::MemoryRecord,
            "same-tombstone",
            3,
            BackupRecordState::Tombstone,
            0,
        ),
    ])?;
    let current = vec![
        CurrentRecord {
            kind: BackupRecordKind::MemoryRecord,
            stable_id: "active-newer-than-tombstone".to_owned(),
            revision: 5,
            schema_version: 1,
            state: BackupRecordState::Tombstone,
            plaintext_bytes: 0,
            plaintext_sha256: [0; 32],
        },
        CurrentRecord {
            kind: BackupRecordKind::MemoryRecord,
            stable_id: "equal-state-divergence".to_owned(),
            revision: 4,
            schema_version: 1,
            state: BackupRecordState::Active,
            plaintext_bytes: 12,
            plaintext_sha256: [2; 32],
        },
        CurrentRecord {
            kind: BackupRecordKind::MemoryRecord,
            stable_id: "same-tombstone".to_owned(),
            revision: 3,
            schema_version: 1,
            state: BackupRecordState::Tombstone,
            plaintext_bytes: 0,
            plaintext_sha256: [0; 32],
        },
    ];
    let plan = plan_restore(
        &source,
        &current,
        target(RestoreTargetKind::ExistingRegularProfile),
    )?;
    let action = |stable_id: &str| {
        plan.entries()
            .iter()
            .find(|entry| entry.stable_id == stable_id)
            .map(|entry| entry.action)
    };
    assert_eq!(
        action("active-newer-than-tombstone"),
        Some(RestoreAction::NeedsExplicitConflictChoice)
    );
    assert_eq!(
        action("equal-state-divergence"),
        Some(RestoreAction::NeedsExplicitConflictChoice)
    );
    assert_eq!(
        action("same-tombstone"),
        Some(RestoreAction::AlreadyPresent)
    );
    Ok(())
}

#[test]
fn committed_restore_keeps_rollback_until_person_accepts() -> Result<(), BackupError> {
    let source = manifest(vec![record(
        BackupRecordKind::SavedWorkspace,
        "workspace-rollback",
        1,
        BackupRecordState::Active,
        5,
    )])?;
    let plan = plan_restore(&source, &[], target(RestoreTargetKind::NewRegularProfile))?;
    let mut restore = RestoreSession::begin();
    restore.recovery_key_supplied()?;
    let confirmation = restore.archive_opened(&plan, &PLAN_DIGEST)?;
    restore.confirm_plan(&plan, confirmation, &PLAN_DIGEST)?;
    restore.staging_finished()?;
    restore.staging_verified(source.snapshot_sha256)?;
    restore.issue_commit()?;
    restore.commit_completed()?;
    restore.issue_restore_previous_generation()?;
    restore.restore_previous_generation_completed()?;
    assert_eq!(restore.phase(), RestoreSessionPhase::RolledBack);
    Ok(())
}
