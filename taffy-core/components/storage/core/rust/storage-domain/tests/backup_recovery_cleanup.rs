// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::backup::{
    validate_restore_recovery_history, BackupRecordKind, RestoreObservedOutcome,
    RestorePhysicalIntent, RestoreRecoveryBinding, RestoreRecoveryError, RestoreRecoveryFact,
    RestoreRecoveryRecord, RestoreRecoveryStatus, RestoreTargetKind,
    RESTORE_RECOVERY_RECORD_VERSION,
};

fn binding() -> RestoreRecoveryBinding {
    RestoreRecoveryBinding {
        reservation_id: "reservation-1".to_owned(),
        owner_profile_id: "source-profile".to_owned(),
        target_kind: RestoreTargetKind::NewRegularProfile,
        target_profile_id: "target-profile".to_owned(),
        backup_id: "backup-1".to_owned(),
        snapshot_sha256: [1; 32],
        confirmation_sha256: [2; 32],
        selection: vec![BackupRecordKind::LibraryEntry],
        record_count: 1,
        candidate_records_sha256: [3; 32],
    }
}

fn intent(sequence: u64, id: &str, value: RestorePhysicalIntent) -> RestoreRecoveryRecord {
    RestoreRecoveryRecord {
        format_version: RESTORE_RECOVERY_RECORD_VERSION,
        sequence,
        binding: binding(),
        fact: RestoreRecoveryFact::IntentRecorded {
            intent_id: id.to_owned(),
            intent: value,
        },
    }
}

fn outcome(sequence: u64, id: &str, value: RestoreObservedOutcome) -> RestoreRecoveryRecord {
    RestoreRecoveryRecord {
        format_version: RESTORE_RECOVERY_RECORD_VERSION,
        sequence,
        binding: binding(),
        fact: RestoreRecoveryFact::OutcomeObserved {
            intent_id: id.to_owned(),
            outcome: value,
        },
    }
}

#[test]
fn cleanup_requires_fresh_discard_and_preserves_it_when_deletion_did_not_complete() {
    let cleanup = vec![
        intent(1, "commit-1", RestorePhysicalIntent::CommitCandidate),
        outcome(
            2,
            "commit-1",
            RestoreObservedOutcome::DefinitelyNotCompleted,
        ),
    ];

    let mut accept = cleanup.clone();
    accept.push(intent(
        3,
        "accept-invalid",
        RestorePhysicalIntent::AcceptCandidate,
    ));
    assert_eq!(
        validate_restore_recovery_history(&accept),
        Err(RestoreRecoveryError::UnexpectedIntent)
    );

    let mut discard = cleanup;
    discard.push(intent(
        3,
        "discard-cleanup",
        RestorePhysicalIntent::DiscardCandidate,
    ));
    assert_eq!(
        validate_restore_recovery_history(&discard),
        Ok(RestoreRecoveryStatus::ReconcileRequired {
            intent_id: "discard-cleanup".to_owned(),
            intent: RestorePhysicalIntent::DiscardCandidate,
        })
    );
    discard.push(outcome(
        4,
        "discard-cleanup",
        RestoreObservedOutcome::DefinitelyNotCompleted,
    ));
    assert_eq!(
        validate_restore_recovery_history(&discard),
        Ok(RestoreRecoveryStatus::CleanupRequired)
    );

    discard.push(intent(
        5,
        "discard-cleanup-retry",
        RestorePhysicalIntent::DiscardCandidate,
    ));
    discard.push(outcome(
        6,
        "discard-cleanup-retry",
        RestoreObservedOutcome::Completed,
    ));
    assert_eq!(
        validate_restore_recovery_history(&discard),
        Ok(RestoreRecoveryStatus::VerifiedDeleted)
    );
}
