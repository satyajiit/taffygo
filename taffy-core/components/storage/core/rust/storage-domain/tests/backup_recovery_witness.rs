// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::backup::{
    validate_restore_recovery_history, BackupRecordKind, RestorePhysicalIntent,
    RestoreRecoveryBinding, RestoreRecoveryError, RestoreRecoveryFact, RestoreRecoveryRecord,
    RestoreTargetKind, MAX_BACKUP_RECORDS, MAX_BACKUP_RESTORE_RECOVERY_RECORDS,
    RESTORE_RECOVERY_RECORD_VERSION,
};

fn record(binding: RestoreRecoveryBinding) -> RestoreRecoveryRecord {
    RestoreRecoveryRecord {
        format_version: RESTORE_RECOVERY_RECORD_VERSION,
        sequence: 1,
        binding,
        fact: RestoreRecoveryFact::IntentRecorded {
            intent_id: "commit-1".to_owned(),
            intent: RestorePhysicalIntent::CommitCandidate,
        },
    }
}

fn binding() -> RestoreRecoveryBinding {
    RestoreRecoveryBinding {
        reservation_id: "reservation-1".to_owned(),
        owner_profile_id: "source-profile".to_owned(),
        target_kind: RestoreTargetKind::NewRegularProfile,
        target_profile_id: "target-profile".to_owned(),
        backup_id: "backup-1".to_owned(),
        snapshot_sha256: [1; 32],
        confirmation_sha256: [2; 32],
        selection: vec![
            BackupRecordKind::AssistantConfiguration,
            BackupRecordKind::LibraryEntry,
        ],
        record_count: 2,
        candidate_records_sha256: [3; 32],
    }
}

fn is_invalid(binding: RestoreRecoveryBinding) {
    assert_eq!(
        validate_restore_recovery_history(&[record(binding)]),
        Err(RestoreRecoveryError::InvalidBinding)
    );
}

#[test]
fn witness_selection_is_canonical_and_matches_the_projected_count() {
    let mut duplicate = binding();
    duplicate.selection.push(BackupRecordKind::LibraryEntry);
    is_invalid(duplicate);

    let mut unordered = binding();
    unordered.selection.swap(0, 1);
    is_invalid(unordered);

    let mut too_few_records = binding();
    too_few_records.record_count = 1;
    is_invalid(too_few_records);

    let mut too_many_records = binding();
    too_many_records.record_count = MAX_BACKUP_RECORDS as u64 + 1;
    is_invalid(too_many_records);

    let mut zero_digest = binding();
    zero_digest.candidate_records_sha256 = [0; 32];
    is_invalid(zero_digest);
}

#[test]
fn canonical_empty_projection_is_the_only_empty_selection() {
    let mut empty = binding();
    empty.selection.clear();
    empty.record_count = 0;
    assert!(validate_restore_recovery_history(&[record(empty)]).is_ok());

    let mut missing_kinds = binding();
    missing_kinds.selection.clear();
    is_invalid(missing_kinds);

    let mut zero_records = binding();
    zero_records.record_count = 0;
    is_invalid(zero_records);
}

#[test]
fn recovery_history_has_its_own_small_non_authorizing_bound() {
    let first = record(binding());
    let oversized = vec![first; MAX_BACKUP_RESTORE_RECOVERY_RECORDS + 1];
    assert_eq!(
        validate_restore_recovery_history(&oversized),
        Err(RestoreRecoveryError::TooManyRecords)
    );
}
