// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::backup::{RestoreSessionPhase, MAX_BACKUP_RESTORE_RECOVERY_RECORDS};

use super::{operation, protocol, restore_request, TestDigest, NOW, OWNER};
use crate::wire;

fn binding() -> wire::BackupRestoreRecoveryBinding {
    wire::BackupRestoreRecoveryBinding {
        reservation_id: "reservation-1".to_owned(),
        owner_profile_id: OWNER.to_owned(),
        target_kind: wire::BackupRestoreTargetKind::NewRegularProfile,
        target_profile_id: "target-profile".to_owned(),
        backup_id: "backup-1".to_owned(),
        snapshot_sha256: [1; 32],
        confirmation_sha256: [2; 32],
        selection: vec![wire::BackupRecordKind::LibraryEntry],
        record_count: 1,
        candidate_records_sha256: [3; 32],
    }
}

fn intent(
    sequence: u64,
    id: &str,
    value: wire::BackupRestorePhysicalIntent,
) -> wire::BackupRestoreRecoveryRecord {
    wire::BackupRestoreRecoveryRecord {
        format_version: 1,
        sequence,
        binding: binding(),
        fact_kind: wire::BackupRestoreRecoveryFactKind::IntentRecorded,
        intent: Some(wire::BackupRestoreRecoveryIntentFact {
            intent_id: id.to_owned(),
            intent: value,
        }),
        outcome: None,
    }
}

fn outcome(
    sequence: u64,
    id: &str,
    value: wire::BackupRestoreObservedOutcome,
) -> wire::BackupRestoreRecoveryRecord {
    wire::BackupRestoreRecoveryRecord {
        format_version: 1,
        sequence,
        binding: binding(),
        fact_kind: wire::BackupRestoreRecoveryFactKind::OutcomeObserved,
        intent: None,
        outcome: Some(wire::BackupRestoreRecoveryOutcomeFact {
            intent_id: id.to_owned(),
            outcome: value,
        }),
    }
}

fn request(
    records: Vec<wire::BackupRestoreRecoveryRecord>,
) -> wire::BackupRestoreRecoveryInspectionRequest {
    wire::BackupRestoreRecoveryInspectionRequest {
        operation: operation("inspect-recovery", NOW + 50),
        records,
    }
}

#[test]
fn pending_and_unknown_intents_only_name_reconciliation() {
    for records in [
        vec![intent(
            1,
            "commit-1",
            wire::BackupRestorePhysicalIntent::CommitCandidate,
        )],
        vec![
            intent(
                1,
                "commit-1",
                wire::BackupRestorePhysicalIntent::CommitCandidate,
            ),
            outcome(
                2,
                "commit-1",
                wire::BackupRestoreObservedOutcome::OutcomeUnknown,
            ),
        ],
    ] {
        let result = protocol(false).inspect_recovery(request(records), NOW);
        assert_eq!(
            result.status,
            wire::BackupRestoreRecoveryInspectionStatus::Succeeded
        );
        assert!(result.failure.is_none());
        let classification = result.classification.expect("classification");
        assert_eq!(
            classification.kind,
            wire::BackupRestoreRecoveryClassificationKind::ReconcileRequired
        );
        let reconciliation = classification
            .reconciliation
            .expect("exact unresolved intent");
        assert_eq!(reconciliation.intent_id, "commit-1");
        assert_eq!(
            reconciliation.intent,
            wire::BackupRestorePhysicalIntent::CommitCandidate
        );
    }
}

#[test]
fn definitive_history_maps_to_observational_classification() {
    let result = protocol(false).inspect_recovery(
        request(vec![
            intent(
                1,
                "commit-1",
                wire::BackupRestorePhysicalIntent::CommitCandidate,
            ),
            outcome(2, "commit-1", wire::BackupRestoreObservedOutcome::Completed),
        ]),
        NOW,
    );
    assert_eq!(
        result.status,
        wire::BackupRestoreRecoveryInspectionStatus::Succeeded
    );
    let classification = result.classification.expect("classification");
    assert_eq!(
        classification.kind,
        wire::BackupRestoreRecoveryClassificationKind::RollbackAvailable
    );
    assert!(classification.reconciliation.is_none());
    assert!(result.failure.is_none());
}

#[test]
fn inspection_is_current_source_owned_and_does_not_mutate_active_restore() {
    let mut restore = protocol(false);
    let planned =
        restore.plan_restore(restore_request("active-target", NOW + 50), NOW, &TestDigest);
    assert_eq!(planned.status, wire::BackupPlanningStatus::Succeeded);
    let active = restore.active_binding().expect("active binding").clone();

    let result = restore.inspect_recovery(
        request(vec![intent(
            1,
            "historical-commit",
            wire::BackupRestorePhysicalIntent::CommitCandidate,
        )]),
        NOW,
    );
    assert_eq!(
        result.status,
        wire::BackupRestoreRecoveryInspectionStatus::Succeeded
    );
    assert_eq!(restore.active_binding(), Some(&active));
    assert_eq!(
        restore.phase(),
        Some(RestoreSessionPhase::AwaitingPlanConfirmation)
    );

    let mut foreign = intent(
        1,
        "foreign-commit",
        wire::BackupRestorePhysicalIntent::CommitCandidate,
    );
    foreign.binding.owner_profile_id = "another-source".to_owned();
    let refused = restore.inspect_recovery(request(vec![foreign]), NOW);
    assert_eq!(
        refused.status,
        wire::BackupRestoreRecoveryInspectionStatus::InvalidRecord
    );
    assert!(refused.classification.is_none());
    assert!(refused.failure.is_none());
    assert_eq!(restore.active_binding(), Some(&active));
}

#[test]
fn operation_liveness_and_protocol_incarnation_fail_closed() {
    let records = vec![intent(
        1,
        "commit-1",
        wire::BackupRestorePhysicalIntent::CommitCandidate,
    )];
    let mut stale = request(records.clone());
    stale.operation.service_generation += 1;
    assert_eq!(
        protocol(false).inspect_recovery(stale, NOW).status,
        wire::BackupRestoreRecoveryInspectionStatus::InvalidOperation
    );
    let mut expired = request(records.clone());
    expired.operation.deadline_monotonic_ms = NOW;
    assert_eq!(
        protocol(false).inspect_recovery(expired, NOW).status,
        wire::BackupRestoreRecoveryInspectionStatus::InvalidOperation
    );
    assert_eq!(
        protocol(true)
            .inspect_recovery(request(records.clone()), NOW)
            .status,
        wire::BackupRestoreRecoveryInspectionStatus::Unavailable
    );
    let mut disconnected = protocol(false);
    disconnected.service_disconnected().expect("disconnect");
    assert_eq!(
        disconnected.inspect_recovery(request(records), NOW).status,
        wire::BackupRestoreRecoveryInspectionStatus::Unavailable
    );
}

#[test]
fn malformed_shapes_bounds_and_semantic_failures_are_distinct() {
    let mut malformed = intent(
        1,
        "commit-1",
        wire::BackupRestorePhysicalIntent::CommitCandidate,
    );
    malformed.intent = None;
    malformed.outcome = Some(wire::BackupRestoreRecoveryOutcomeFact {
        intent_id: "commit-1".to_owned(),
        outcome: wire::BackupRestoreObservedOutcome::Completed,
    });
    let malformed = protocol(false).inspect_recovery(request(vec![malformed]), NOW);
    assert_eq!(
        malformed.status,
        wire::BackupRestoreRecoveryInspectionStatus::InvalidRecord
    );
    assert!(malformed.failure.is_none());

    let mut invalid_sequence = intent(
        2,
        "commit-1",
        wire::BackupRestorePhysicalIntent::CommitCandidate,
    );
    invalid_sequence.binding.selection.clear();
    invalid_sequence.binding.record_count = 0;
    let semantic = protocol(false).inspect_recovery(request(vec![invalid_sequence]), NOW);
    assert_eq!(
        semantic.status,
        wire::BackupRestoreRecoveryInspectionStatus::InvalidHistory
    );
    assert_eq!(
        semantic.failure.expect("portable error").error,
        wire::BackupRestoreRecoveryError::InvalidSequence
    );

    let row = intent(
        1,
        "commit-1",
        wire::BackupRestorePhysicalIntent::CommitCandidate,
    );
    let oversized = protocol(false).inspect_recovery(
        request(vec![row; MAX_BACKUP_RESTORE_RECOVERY_RECORDS + 1]),
        NOW,
    );
    assert_eq!(
        oversized.status,
        wire::BackupRestoreRecoveryInspectionStatus::InvalidHistory
    );
    assert_eq!(
        oversized.failure.expect("bounded error").error,
        wire::BackupRestoreRecoveryError::TooManyRecords
    );
}

#[test]
fn candidate_witness_requires_canonical_present_kinds_but_allows_empty_projection() {
    let mut empty = intent(
        1,
        "empty-commit",
        wire::BackupRestorePhysicalIntent::CommitCandidate,
    );
    empty.binding.selection.clear();
    empty.binding.record_count = 0;
    assert_eq!(
        protocol(false)
            .inspect_recovery(request(vec![empty]), NOW)
            .status,
        wire::BackupRestoreRecoveryInspectionStatus::Succeeded
    );

    for mut invalid in [
        {
            let mut row = intent(
                1,
                "duplicate-kind",
                wire::BackupRestorePhysicalIntent::CommitCandidate,
            );
            row.binding
                .selection
                .push(wire::BackupRecordKind::LibraryEntry);
            row
        },
        {
            let mut row = intent(
                1,
                "missing-kind",
                wire::BackupRestorePhysicalIntent::CommitCandidate,
            );
            row.binding.selection.clear();
            row
        },
    ] {
        let result = protocol(false).inspect_recovery(request(vec![invalid.clone()]), NOW);
        assert_eq!(
            result.status,
            wire::BackupRestoreRecoveryInspectionStatus::InvalidHistory
        );
        assert_eq!(
            result.failure.expect("binding error").error,
            wire::BackupRestoreRecoveryError::InvalidBinding
        );
        invalid.binding.candidate_records_sha256 = [0; 32];
        let zero = protocol(false).inspect_recovery(request(vec![invalid]), NOW);
        assert_eq!(
            zero.failure.expect("binding error").error,
            wire::BackupRestoreRecoveryError::InvalidBinding
        );
    }
}
