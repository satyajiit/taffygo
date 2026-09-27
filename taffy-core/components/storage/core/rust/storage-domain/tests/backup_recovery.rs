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

fn intent(sequence: u64, intent_id: &str, kind: RestorePhysicalIntent) -> RestoreRecoveryRecord {
    RestoreRecoveryRecord {
        format_version: RESTORE_RECOVERY_RECORD_VERSION,
        sequence,
        binding: binding(),
        fact: RestoreRecoveryFact::IntentRecorded {
            intent_id: intent_id.to_owned(),
            intent: kind,
        },
    }
}

fn outcome(
    sequence: u64,
    intent_id: &str,
    observed: RestoreObservedOutcome,
) -> RestoreRecoveryRecord {
    RestoreRecoveryRecord {
        format_version: RESTORE_RECOVERY_RECORD_VERSION,
        sequence,
        binding: binding(),
        fact: RestoreRecoveryFact::OutcomeObserved {
            intent_id: intent_id.to_owned(),
            outcome: observed,
        },
    }
}

fn committed() -> Vec<RestoreRecoveryRecord> {
    vec![
        intent(1, "commit-1", RestorePhysicalIntent::CommitCandidate),
        outcome(2, "commit-1", RestoreObservedOutcome::Completed),
    ]
}

#[test]
fn an_unobserved_or_unknown_intent_requires_reconciliation_not_replay() {
    let mut history = vec![intent(
        1,
        "commit-pending",
        RestorePhysicalIntent::CommitCandidate,
    )];
    let expected = RestoreRecoveryStatus::ReconcileRequired {
        intent_id: "commit-pending".to_owned(),
        intent: RestorePhysicalIntent::CommitCandidate,
    };
    assert_eq!(
        validate_restore_recovery_history(&history),
        Ok(expected.clone())
    );
    assert!(expected.requires_quarantine());

    history.push(outcome(
        2,
        "commit-pending",
        RestoreObservedOutcome::OutcomeUnknown,
    ));
    assert_eq!(validate_restore_recovery_history(&history), Ok(expected));
    history.push(intent(
        3,
        "accept-too-soon",
        RestorePhysicalIntent::AcceptCandidate,
    ));
    assert_eq!(
        validate_restore_recovery_history(&history),
        Err(RestoreRecoveryError::UnresolvedIntent)
    );
}

#[test]
fn a_later_definitive_observation_settles_the_same_unknown_intent() {
    let mut history = vec![
        intent(1, "commit-1", RestorePhysicalIntent::CommitCandidate),
        outcome(2, "commit-1", RestoreObservedOutcome::OutcomeUnknown),
        outcome(3, "commit-1", RestoreObservedOutcome::Completed),
    ];
    assert_eq!(
        validate_restore_recovery_history(&history),
        Ok(RestoreRecoveryStatus::RollbackAvailable)
    );

    history.push(intent(
        4,
        "discard-1",
        RestorePhysicalIntent::DiscardCandidate,
    ));
    history.push(outcome(
        5,
        "discard-1",
        RestoreObservedOutcome::OutcomeUnknown,
    ));
    assert_eq!(
        validate_restore_recovery_history(&history),
        Ok(RestoreRecoveryStatus::ReconcileRequired {
            intent_id: "discard-1".to_owned(),
            intent: RestorePhysicalIntent::DiscardCandidate,
        })
    );
    history.push(outcome(6, "discard-1", RestoreObservedOutcome::Completed));
    assert_eq!(
        validate_restore_recovery_history(&history),
        Ok(RestoreRecoveryStatus::VerifiedDeleted)
    );
}

#[test]
fn definitive_resolution_outcomes_preserve_quarantine_until_terminal() {
    let mut accepted = committed();
    assert_eq!(
        validate_restore_recovery_history(&accepted),
        Ok(RestoreRecoveryStatus::RollbackAvailable)
    );
    assert!(RestoreRecoveryStatus::RollbackAvailable.requires_quarantine());
    accepted.push(intent(
        3,
        "accept-1",
        RestorePhysicalIntent::AcceptCandidate,
    ));
    accepted.push(outcome(4, "accept-1", RestoreObservedOutcome::Completed));
    assert_eq!(
        validate_restore_recovery_history(&accepted),
        Ok(RestoreRecoveryStatus::Published)
    );
    assert!(!RestoreRecoveryStatus::Published.requires_quarantine());

    let mut discarded = committed();
    discarded.push(intent(
        3,
        "discard-1",
        RestorePhysicalIntent::DiscardCandidate,
    ));
    discarded.push(outcome(4, "discard-1", RestoreObservedOutcome::Completed));
    assert_eq!(
        validate_restore_recovery_history(&discarded),
        Ok(RestoreRecoveryStatus::VerifiedDeleted)
    );
    assert!(!RestoreRecoveryStatus::VerifiedDeleted.requires_quarantine());
}

#[test]
fn definitely_not_completed_never_invents_the_opposite_outcome() {
    let not_committed = vec![
        intent(1, "commit-1", RestorePhysicalIntent::CommitCandidate),
        outcome(
            2,
            "commit-1",
            RestoreObservedOutcome::DefinitelyNotCompleted,
        ),
    ];
    assert_eq!(
        validate_restore_recovery_history(&not_committed),
        Ok(RestoreRecoveryStatus::CleanupRequired)
    );
    assert!(RestoreRecoveryStatus::CleanupRequired.requires_quarantine());

    let mut retry_resolution = committed();
    retry_resolution.push(intent(
        3,
        "accept-refused",
        RestorePhysicalIntent::AcceptCandidate,
    ));
    retry_resolution.push(outcome(
        4,
        "accept-refused",
        RestoreObservedOutcome::DefinitelyNotCompleted,
    ));
    retry_resolution.push(intent(
        5,
        "discard-after-refusal",
        RestorePhysicalIntent::DiscardCandidate,
    ));
    assert_eq!(
        validate_restore_recovery_history(&retry_resolution),
        Ok(RestoreRecoveryStatus::ReconcileRequired {
            intent_id: "discard-after-refusal".to_owned(),
            intent: RestorePhysicalIntent::DiscardCandidate,
        })
    );
}

#[test]
fn binding_is_exact_bounded_and_content_free() {
    let original = committed();
    for mutation in 0..12 {
        let mut changed = original.clone();
        if let Some(record) = changed.get_mut(1) {
            match mutation {
                0 => record.binding.reservation_id = "other-reservation".to_owned(),
                1 => record.binding.owner_profile_id = "other-source".to_owned(),
                2 => record.binding.target_profile_id = "other-target".to_owned(),
                3 => record.binding.backup_id = "other-backup".to_owned(),
                4 => record.binding.snapshot_sha256 = [3; 32],
                5 => record.binding.confirmation_sha256 = [4; 32],
                6 => record.binding.selection = vec![BackupRecordKind::MemoryRecord],
                7 => record.binding.record_count = 2,
                8 => record.binding.candidate_records_sha256 = [4; 32],
                9 => record.binding.target_kind = RestoreTargetKind::ExistingRegularProfile,
                10 => record.binding.owner_profile_id = record.binding.target_profile_id.clone(),
                _ => record.binding.backup_id = "bad\nidentity".to_owned(),
            }
        }
        let expected = if mutation < 9 {
            RestoreRecoveryError::BindingChanged
        } else {
            RestoreRecoveryError::InvalidBinding
        };
        assert_eq!(validate_restore_recovery_history(&changed), Err(expected));
    }

    for zero_snapshot in [true, false] {
        let mut changed = original.clone();
        if let Some(record) = changed.first_mut() {
            if zero_snapshot {
                record.binding.snapshot_sha256 = [0; 32];
            } else {
                record.binding.confirmation_sha256 = [0; 32];
            }
        }
        assert_eq!(
            validate_restore_recovery_history(&changed),
            Err(RestoreRecoveryError::InvalidBinding)
        );
    }
}

#[test]
fn sequence_version_and_intent_identity_fail_closed() {
    assert_eq!(
        validate_restore_recovery_history(&[]),
        Err(RestoreRecoveryError::MissingCommitIntent)
    );
    let original = committed();
    let mut bad_version = original.clone();
    if let Some(record) = bad_version.first_mut() {
        record.format_version += 1;
    }
    assert_eq!(
        validate_restore_recovery_history(&bad_version),
        Err(RestoreRecoveryError::UnsupportedVersion)
    );
    for sequence in [0, 2, u64::MAX] {
        let mut changed = original.clone();
        if let Some(record) = changed.first_mut() {
            record.sequence = sequence;
        }
        assert_eq!(
            validate_restore_recovery_history(&changed),
            Err(RestoreRecoveryError::InvalidSequence)
        );
    }
    let mut invalid_intent = original.clone();
    if let Some(record) = invalid_intent.first_mut() {
        if let RestoreRecoveryFact::IntentRecorded { intent_id, .. } = &mut record.fact {
            *intent_id = "bad\u{7f}intent".to_owned();
        }
    }
    assert_eq!(
        validate_restore_recovery_history(&invalid_intent),
        Err(RestoreRecoveryError::InvalidIntentId)
    );
}

#[test]
fn malformed_ordering_cannot_change_or_repeat_physical_work() {
    let cases = [
        (
            vec![intent(
                1,
                "accept-first",
                RestorePhysicalIntent::AcceptCandidate,
            )],
            RestoreRecoveryError::UnexpectedIntent,
        ),
        (
            vec![outcome(1, "commit-1", RestoreObservedOutcome::Completed)],
            RestoreRecoveryError::OutcomeWithoutIntent,
        ),
        (
            vec![
                intent(1, "commit-1", RestorePhysicalIntent::CommitCandidate),
                outcome(2, "commit-other", RestoreObservedOutcome::Completed),
            ],
            RestoreRecoveryError::OutcomeIntentMismatch,
        ),
        (
            vec![
                intent(1, "commit-1", RestorePhysicalIntent::CommitCandidate),
                intent(2, "commit-2", RestorePhysicalIntent::CommitCandidate),
            ],
            RestoreRecoveryError::UnresolvedIntent,
        ),
        (
            vec![
                intent(1, "commit-1", RestorePhysicalIntent::CommitCandidate),
                outcome(2, "commit-1", RestoreObservedOutcome::OutcomeUnknown),
                outcome(3, "commit-1", RestoreObservedOutcome::OutcomeUnknown),
            ],
            RestoreRecoveryError::DuplicateUnknownOutcome,
        ),
    ];
    for (history, expected) in cases {
        assert_eq!(validate_restore_recovery_history(&history), Err(expected));
    }

    let mut reused = committed();
    reused.push(intent(
        3,
        "resolution-reused",
        RestorePhysicalIntent::AcceptCandidate,
    ));
    reused.push(outcome(
        4,
        "resolution-reused",
        RestoreObservedOutcome::DefinitelyNotCompleted,
    ));
    reused.push(intent(
        5,
        "resolution-reused",
        RestorePhysicalIntent::DiscardCandidate,
    ));
    assert_eq!(
        validate_restore_recovery_history(&reused),
        Err(RestoreRecoveryError::IntentIdReused)
    );
}

#[test]
fn terminal_history_rejects_every_late_suffix() {
    for mut terminal in [
        {
            let mut history = committed();
            history.push(intent(
                3,
                "accept-1",
                RestorePhysicalIntent::AcceptCandidate,
            ));
            history.push(outcome(4, "accept-1", RestoreObservedOutcome::Completed));
            history
        },
        {
            let mut history = committed();
            history.push(intent(
                3,
                "discard-1",
                RestorePhysicalIntent::DiscardCandidate,
            ));
            history.push(outcome(4, "discard-1", RestoreObservedOutcome::Completed));
            history
        },
    ] {
        let next = terminal.len() as u64 + 1;
        terminal.push(intent(
            next,
            "late-action",
            RestorePhysicalIntent::DiscardCandidate,
        ));
        assert_eq!(
            validate_restore_recovery_history(&terminal),
            Err(RestoreRecoveryError::TerminalHistoryExtended)
        );
    }
}
