// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::backup::{
    decode_manifest, open_backup_manifest, prepare_backup_manifest, BackupDigest, BackupError,
    BackupManifestDraft, BackupRecord, BackupRecordKind, BackupRecordState, BackupSelection,
    CurrentRecord, RestoreAction, RestoreTarget, RestoreTargetKind, StagedBackupRecord,
    MAX_BACKUP_RECORD_BYTES,
};

struct ContentDigest;

impl BackupDigest for ContentDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], BackupError> {
        let mut output = [0_u8; 32];
        for (index, byte) in input.iter().copied().enumerate() {
            let slot = index % output.len();
            if let Some(value) = output.get_mut(slot) {
                *value = value
                    .wrapping_add(byte)
                    .rotate_left(u32::try_from(index % 7).unwrap_or(0));
            }
        }
        output[0] |= 1;
        Ok(output)
    }
}

const DIGEST: ContentDigest = ContentDigest;

fn selection() -> BackupSelection {
    BackupSelection::new([
        BackupRecordKind::SavedWorkspace,
        BackupRecordKind::MemoryRecord,
    ])
    .unwrap_or_else(|_| unreachable!())
}

fn record(
    kind: BackupRecordKind,
    stable_id: &str,
    plaintext_bytes: u64,
    digest_byte: u8,
) -> BackupRecord {
    BackupRecord {
        kind,
        stable_id: stable_id.to_owned(),
        revision: 1,
        schema_version: 1,
        state: BackupRecordState::Active,
        plaintext_bytes,
        plaintext_sha256: [digest_byte; 32],
    }
}

fn draft(records: Vec<BackupRecord>) -> BackupManifestDraft {
    BackupManifestDraft {
        backup_id: "backup-planning-01".to_owned(),
        source_installation_id: "installation-01".to_owned(),
        created_at_utc: "2026-09-05T00:00:00Z".to_owned(),
        selection: selection(),
        records,
    }
}

fn observations(
    records: &[BackupRecord],
    source_order: &[u32],
) -> Result<Vec<StagedBackupRecord>, BackupError> {
    source_order
        .iter()
        .map(|ordinal| {
            let source = records
                .get(usize::try_from(*ordinal).map_err(|_| BackupError::TooManyRecords)?)
                .ok_or(BackupError::StagedPayloadMismatch)?;
            Ok(StagedBackupRecord {
                plaintext_bytes: source.plaintext_bytes,
                plaintext_sha256: source.plaintext_sha256,
            })
        })
        .collect()
}

#[test]
fn preparation_canonicalizes_order_and_returns_exact_source_mapping() -> Result<(), BackupError> {
    let records = vec![
        record(BackupRecordKind::MemoryRecord, "memory-b", 11, 1),
        record(BackupRecordKind::SavedWorkspace, "workspace-z", 22, 2),
        record(BackupRecordKind::SavedWorkspace, "workspace-a", 33, 3),
        record(BackupRecordKind::MemoryRecord, "memory-c", 44, 4),
    ];
    let prepared = prepare_backup_manifest(draft(records.clone()), &DIGEST)?;
    assert_eq!(prepared.source_order(), &[2, 1, 0, 3]);
    assert_eq!(prepared.payload_plaintext_bytes(), 110);
    assert_eq!(prepared.expected_sealed_chunks(), 2);

    let manifest = decode_manifest(prepared.manifest_plaintext())?;
    assert_eq!(
        manifest
            .records
            .iter()
            .map(|record| record.stable_id.as_str())
            .collect::<Vec<_>>(),
        vec!["workspace-a", "workspace-z", "memory-b", "memory-c"]
    );
    assert_eq!(manifest.snapshot_sha256, prepared.snapshot_sha256());

    let reordered = vec![
        records
            .get(2)
            .cloned()
            .ok_or(BackupError::MalformedManifest)?,
        records
            .get(3)
            .cloned()
            .ok_or(BackupError::MalformedManifest)?,
        records
            .get(1)
            .cloned()
            .ok_or(BackupError::MalformedManifest)?,
        records
            .first()
            .cloned()
            .ok_or(BackupError::MalformedManifest)?,
    ];
    let second = prepare_backup_manifest(draft(reordered), &DIGEST)?;
    assert_eq!(second.manifest_plaintext(), prepared.manifest_plaintext());
    assert_eq!(second.snapshot_sha256(), prepared.snapshot_sha256());
    assert_ne!(second.source_order(), prepared.source_order());
    Ok(())
}

#[test]
fn preparation_refuses_duplicate_identity_and_aggregate_overflow() {
    let duplicate = record(BackupRecordKind::MemoryRecord, "same", 1, 7);
    assert_eq!(
        prepare_backup_manifest(draft(vec![duplicate.clone(), duplicate]), &DIGEST),
        Err(BackupError::DuplicateRecord)
    );

    let oversized = (0..129)
        .map(|index| {
            record(
                BackupRecordKind::MemoryRecord,
                &format!("memory-{index:03}"),
                MAX_BACKUP_RECORD_BYTES,
                u8::try_from(index + 1).unwrap_or(1),
            )
        })
        .collect();
    assert_eq!(
        prepare_backup_manifest(draft(oversized), &DIGEST),
        Err(BackupError::ArchiveTooLarge)
    );
}

#[test]
fn record_hash_changes_the_bound_snapshot_digest() -> Result<(), BackupError> {
    let source = record(BackupRecordKind::SavedWorkspace, "workspace", 12, 1);
    let first = prepare_backup_manifest(draft(vec![source.clone()]), &DIGEST)?;
    let mut changed = source;
    changed.plaintext_sha256 = [2; 32];
    let second = prepare_backup_manifest(draft(vec![changed]), &DIGEST)?;
    assert_ne!(first.snapshot_sha256(), second.snapshot_sha256());
    assert_ne!(first.manifest_plaintext(), second.manifest_plaintext());
    Ok(())
}

#[test]
fn restore_plan_requires_every_staged_range_to_match() -> Result<(), BackupError> {
    let records = vec![
        record(BackupRecordKind::MemoryRecord, "memory", 9, 9),
        record(BackupRecordKind::SavedWorkspace, "workspace", 7, 7),
    ];
    let prepared = prepare_backup_manifest(draft(records.clone()), &DIGEST)?;
    let opened = open_backup_manifest(prepared.manifest_plaintext(), &DIGEST)?;
    let layout = opened.payload_layout()?;
    assert_eq!(layout.snapshot_sha256, prepared.snapshot_sha256());
    assert_eq!(layout.payload_plaintext_bytes, 16);
    assert_eq!(
        layout
            .records
            .iter()
            .map(|entry| entry.plaintext_bytes)
            .collect::<Vec<_>>(),
        vec![7, 9]
    );

    let observed = observations(&records, prepared.source_order())?;
    let target = RestoreTarget {
        kind: RestoreTargetKind::NewRegularProfile,
        profile_id: "clean-profile".to_owned(),
    };

    let mut wrong_length = observed.clone();
    wrong_length
        .first_mut()
        .ok_or(BackupError::StagedPayloadMismatch)?
        .plaintext_bytes += 1;
    assert_eq!(
        opened.plan_restore(&wrong_length, &[], target.clone()),
        Err(BackupError::StagedPayloadMismatch)
    );
    let mut wrong_hash = observed.clone();
    *wrong_hash
        .get_mut(1)
        .and_then(|record| record.plaintext_sha256.first_mut())
        .ok_or(BackupError::StagedPayloadMismatch)? ^= 1;
    assert_eq!(
        opened.plan_restore(&wrong_hash, &[], target.clone()),
        Err(BackupError::StagedPayloadMismatch)
    );
    assert_eq!(
        opened.plan_restore(
            observed
                .get(..1)
                .ok_or(BackupError::StagedPayloadMismatch)?,
            &[],
            target.clone(),
        ),
        Err(BackupError::StagedPayloadMismatch)
    );
    let mut reordered = observed.clone();
    reordered.swap(0, 1);
    assert_eq!(
        opened.plan_restore(&reordered, &[], target.clone()),
        Err(BackupError::StagedPayloadMismatch)
    );

    let plan = opened.plan_restore(&observed, &[], target)?;
    assert!(plan
        .entries()
        .iter()
        .all(|entry| entry.action == RestoreAction::StageCreate));
    let first = plan.entries().first().ok_or(BackupError::RestoreConflict)?;
    assert_eq!(first.schema_version, 1);
    assert_eq!(first.state, BackupRecordState::Active);
    assert_eq!(first.plaintext_bytes, 7);
    assert_eq!(first.plaintext_sha256, [7; 32]);
    Ok(())
}

#[test]
fn tombstone_layout_requires_the_content_free_sentinel() -> Result<(), BackupError> {
    let tombstone = BackupRecord {
        kind: BackupRecordKind::MemoryRecord,
        stable_id: "deleted-memory".to_owned(),
        revision: 3,
        schema_version: 1,
        state: BackupRecordState::Tombstone,
        plaintext_bytes: 0,
        plaintext_sha256: [0; 32],
    };
    let prepared = prepare_backup_manifest(draft(vec![tombstone]), &DIGEST)?;
    let opened = open_backup_manifest(prepared.manifest_plaintext(), &DIGEST)?;
    let target = RestoreTarget {
        kind: RestoreTargetKind::NewRegularProfile,
        profile_id: "clean-profile".to_owned(),
    };
    assert_eq!(
        opened.plan_restore(
            &[StagedBackupRecord {
                plaintext_bytes: 0,
                plaintext_sha256: [1; 32],
            }],
            &[],
            target.clone(),
        ),
        Err(BackupError::StagedPayloadMismatch)
    );
    let plan = opened.plan_restore(
        &[StagedBackupRecord {
            plaintext_bytes: 0,
            plaintext_sha256: [0; 32],
        }],
        &[],
        target,
    )?;
    assert_eq!(
        plan.entries()
            .first()
            .ok_or(BackupError::RestoreConflict)?
            .action,
        RestoreAction::StageDeletion
    );
    Ok(())
}

#[test]
fn equal_revision_is_present_only_when_the_complete_descriptor_matches() -> Result<(), BackupError>
{
    let archived = record(BackupRecordKind::MemoryRecord, "memory", 12, 5);
    let prepared = prepare_backup_manifest(draft(vec![archived.clone()]), &DIGEST)?;
    let opened = open_backup_manifest(prepared.manifest_plaintext(), &DIGEST)?;
    let observed = observations(std::slice::from_ref(&archived), prepared.source_order())?;
    let target = RestoreTarget {
        kind: RestoreTargetKind::ExistingRegularProfile,
        profile_id: "existing-profile".to_owned(),
    };
    let current = CurrentRecord {
        kind: archived.kind,
        stable_id: archived.stable_id,
        revision: archived.revision,
        schema_version: archived.schema_version,
        state: archived.state,
        plaintext_bytes: archived.plaintext_bytes,
        plaintext_sha256: archived.plaintext_sha256,
    };
    let exact = opened.plan_restore(&observed, std::slice::from_ref(&current), target.clone())?;
    assert_eq!(
        exact
            .entries()
            .first()
            .ok_or(BackupError::RestoreConflict)?
            .action,
        RestoreAction::AlreadyPresent
    );

    let mut wrong_schema = current.clone();
    wrong_schema.schema_version += 1;
    let schema_plan = opened.plan_restore(&observed, &[wrong_schema], target.clone())?;
    assert!(schema_plan.has_conflicts());
    let mut wrong_length = current;
    wrong_length.plaintext_bytes += 1;
    let length_plan = opened.plan_restore(&observed, &[wrong_length], target)?;
    assert!(length_plan.has_conflicts());
    Ok(())
}
