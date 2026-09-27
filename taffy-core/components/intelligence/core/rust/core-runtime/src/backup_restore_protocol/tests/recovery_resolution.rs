// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::backup::RestoreSessionPhase;

use super::{
    commit_issued, operation, protocol, restore_request, BackupRestoreBinding, TestDigest, NOW,
    OWNER,
};
use crate::backup_restore_protocol::{
    BackupRestoreCommitOutcome, BackupRestoreProtocolError, BackupRestoreResolutionChoice,
};
use crate::wire;

pub(super) fn recovery_binding(
    active: Option<&BackupRestoreBinding>,
) -> wire::BackupRestoreRecoveryBinding {
    wire::BackupRestoreRecoveryBinding {
        reservation_id: "reservation-1".to_owned(),
        owner_profile_id: OWNER.to_owned(),
        target_kind: wire::BackupRestoreTargetKind::NewRegularProfile,
        target_profile_id: active
            .map_or("target-profile", |value| value.target_profile_id())
            .to_owned(),
        backup_id: active
            .map_or("backup-1", |value| value.backup_id())
            .to_owned(),
        snapshot_sha256: active.map_or([1; 32], BackupRestoreBinding::snapshot_sha256),
        confirmation_sha256: active.map_or([2; 32], BackupRestoreBinding::confirmation_sha256),
        selection: vec![wire::BackupRecordKind::MemoryRecord],
        record_count: 1,
        candidate_records_sha256: [3; 32],
    }
}

pub(super) fn intent(
    sequence: u64,
    binding: &wire::BackupRestoreRecoveryBinding,
    id: &str,
    value: wire::BackupRestorePhysicalIntent,
) -> wire::BackupRestoreRecoveryRecord {
    wire::BackupRestoreRecoveryRecord {
        format_version: 1,
        sequence,
        binding: binding.clone(),
        fact_kind: wire::BackupRestoreRecoveryFactKind::IntentRecorded,
        intent: Some(wire::BackupRestoreRecoveryIntentFact {
            intent_id: id.to_owned(),
            intent: value,
        }),
        outcome: None,
    }
}

pub(super) fn outcome(
    sequence: u64,
    binding: &wire::BackupRestoreRecoveryBinding,
    id: &str,
    value: wire::BackupRestoreObservedOutcome,
) -> wire::BackupRestoreRecoveryRecord {
    wire::BackupRestoreRecoveryRecord {
        format_version: 1,
        sequence,
        binding: binding.clone(),
        fact_kind: wire::BackupRestoreRecoveryFactKind::OutcomeObserved,
        intent: None,
        outcome: Some(wire::BackupRestoreRecoveryOutcomeFact {
            intent_id: id.to_owned(),
            outcome: value,
        }),
    }
}

pub(super) fn committed(
    binding: &wire::BackupRestoreRecoveryBinding,
) -> Vec<wire::BackupRestoreRecoveryRecord> {
    vec![
        intent(
            1,
            binding,
            "commit-1",
            wire::BackupRestorePhysicalIntent::CommitCandidate,
        ),
        outcome(
            2,
            binding,
            "commit-1",
            wire::BackupRestoreObservedOutcome::Completed,
        ),
    ]
}

pub(super) fn cleanup(
    binding: &wire::BackupRestoreRecoveryBinding,
) -> Vec<wire::BackupRestoreRecoveryRecord> {
    vec![
        intent(
            1,
            binding,
            "commit-1",
            wire::BackupRestorePhysicalIntent::CommitCandidate,
        ),
        outcome(
            2,
            binding,
            "commit-1",
            wire::BackupRestoreObservedOutcome::DefinitelyNotCompleted,
        ),
    ]
}

pub(super) fn request(
    operation_id: &str,
    deadline: u64,
    prefix: Vec<wire::BackupRestoreRecoveryRecord>,
    choice: wire::BackupRestoreResolutionChoice,
    intent_id: &str,
) -> wire::BackupRestoreRecoveryResolutionRequest {
    wire::BackupRestoreRecoveryResolutionRequest {
        operation: operation(operation_id, deadline),
        history_prefix: prefix,
        choice,
        intent_id: intent_id.to_owned(),
    }
}

pub(super) fn durable_history(
    authorization: &wire::BackupRestoreRecoveryResolutionAuthorization,
    observations: &[wire::BackupRestoreObservedOutcome],
) -> Vec<wire::BackupRestoreRecoveryRecord> {
    let mut history = authorization.history_prefix.clone();
    history.push(intent(
        u64::try_from(history.len() + 1).expect("bounded"),
        &authorization.binding,
        &authorization.intent_id,
        match authorization.choice {
            wire::BackupRestoreResolutionChoice::AcceptCandidate => {
                wire::BackupRestorePhysicalIntent::AcceptCandidate
            }
            wire::BackupRestoreResolutionChoice::DiscardCandidate => {
                wire::BackupRestorePhysicalIntent::DiscardCandidate
            }
        },
    ));
    for observation in observations {
        history.push(outcome(
            u64::try_from(history.len() + 1).expect("bounded"),
            &authorization.binding,
            &authorization.intent_id,
            *observation,
        ));
    }
    history
}

#[test]
fn post_restart_resolution_is_exact_and_consumptive() {
    let binding = recovery_binding(None);
    let prefix = committed(&binding);
    let mut restore = protocol(false);
    let result = restore.choose_recovery_resolution(
        request(
            "recover-accept",
            NOW + 50,
            prefix.clone(),
            wire::BackupRestoreResolutionChoice::AcceptCandidate,
            "accept-1",
        ),
        NOW,
    );
    assert_eq!(result.status, wire::BackupRestoreProtocolStatus::Succeeded);
    let authorization = result.authorization.expect("authority");
    assert_eq!(authorization.binding, binding);
    assert_eq!(authorization.history_prefix, prefix);
    assert_eq!(authorization.intent_id, "accept-1");

    let history = durable_history(
        &authorization,
        &[wire::BackupRestoreObservedOutcome::Completed],
    );
    let reported = restore.report_recovery_resolution_outcome(
        wire::BackupRestoreRecoveryResolutionOutcomeReport {
            operation: operation("report-accept", NOW + 60),
            authorization: authorization.clone(),
            durable_history: history.clone(),
        },
        NOW + 1,
    );
    assert_eq!(
        reported.status,
        wire::BackupRestoreProtocolStatus::Succeeded
    );
    let duplicate = restore.report_recovery_resolution_outcome(
        wire::BackupRestoreRecoveryResolutionOutcomeReport {
            operation: operation("repeat-report", NOW + 60),
            authorization,
            durable_history: history,
        },
        NOW + 2,
    );
    assert_eq!(
        duplicate.status,
        wire::BackupRestoreProtocolStatus::Succeeded
    );
    let stale = restore.choose_recovery_resolution(
        request(
            "stale-terminal",
            NOW + 70,
            prefix,
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "different-intent",
        ),
        NOW + 3,
    );
    assert_eq!(stale.status, wire::BackupRestoreProtocolStatus::WrongPhase);
}

#[test]
fn matching_live_session_advances_once_and_excludes_legacy_resolution() {
    let (mut restore, active, _commit) = commit_issued("target-profile");
    let binding = recovery_binding(Some(&active));
    let result = restore.choose_recovery_resolution(
        request(
            "recover-live",
            NOW + 60,
            committed(&binding),
            wire::BackupRestoreResolutionChoice::AcceptCandidate,
            "accept-live",
        ),
        NOW + 2,
    );
    assert_eq!(result.status, wire::BackupRestoreProtocolStatus::Succeeded);
    assert_eq!(restore.phase(), Some(RestoreSessionPhase::AcceptIssued));
    assert_eq!(
        restore.choose_resolution(
            operation("legacy-after-recovery", NOW + 60),
            &active,
            BackupRestoreResolutionChoice::AcceptCandidate,
            NOW + 3,
        ),
        Err(BackupRestoreProtocolError::WrongPhase)
    );

    let (mut legacy, active, commit) = commit_issued("target-profile");
    legacy
        .resolve_commit(
            &operation("commit-completed", NOW + 60),
            &commit,
            BackupRestoreCommitOutcome::Committed,
            NOW + 1,
        )
        .expect("committed candidate");
    legacy
        .choose_resolution(
            operation("legacy-first", NOW + 60),
            &active,
            BackupRestoreResolutionChoice::DiscardCandidate,
            NOW + 2,
        )
        .expect("legacy authority");
    let binding = recovery_binding(Some(&active));
    let refused = legacy.choose_recovery_resolution(
        request(
            "recover-too-late",
            NOW + 60,
            committed(&binding),
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-live",
        ),
        NOW + 3,
    );
    assert_eq!(
        refused.status,
        wire::BackupRestoreProtocolStatus::WrongPhase
    );
    assert!(refused.authorization.is_none());
}

#[test]
fn definitely_not_committed_history_retires_matching_live_commit_before_cleanup() {
    let (mut restore, active, _commit) = commit_issued("target-profile");
    let binding = recovery_binding(Some(&active));
    let issued = restore.choose_recovery_resolution(
        request(
            "recover-cleanup-live",
            NOW + 60,
            cleanup(&binding),
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-cleanup-live",
        ),
        NOW + 1,
    );
    assert_eq!(issued.status, wire::BackupRestoreProtocolStatus::Succeeded);
    assert_eq!(restore.phase(), None);
}

#[test]
fn lost_authorization_retries_only_the_same_logical_intent_after_expiry() {
    let binding = recovery_binding(None);
    let prefix = committed(&binding);
    let mut restore = protocol(false);
    let original_request = request(
        "decision-1",
        NOW + 10,
        prefix.clone(),
        wire::BackupRestoreResolutionChoice::DiscardCandidate,
        "discard-1",
    );
    let first = restore.choose_recovery_resolution(original_request.clone(), NOW);
    let exact_retry = restore.choose_recovery_resolution(original_request, NOW + 1);
    assert_eq!(first, exact_retry);
    assert_eq!(
        restore
            .plan_restore(
                restore_request("another-target", NOW + 30),
                NOW + 1,
                &TestDigest,
            )
            .status,
        wire::BackupPlanningStatus::Unavailable
    );

    let early = restore.choose_recovery_resolution(
        request(
            "decision-2",
            NOW + 30,
            prefix.clone(),
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-1",
        ),
        NOW + 2,
    );
    assert_eq!(early.status, wire::BackupRestoreProtocolStatus::WrongPhase);

    let renewed = restore.choose_recovery_resolution(
        request(
            "decision-2",
            NOW + 30,
            prefix,
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-1",
        ),
        NOW + 10,
    );
    assert_eq!(renewed.status, wire::BackupRestoreProtocolStatus::Succeeded);
    assert_eq!(
        renewed
            .authorization
            .expect("renewed")
            .decision_operation
            .operation_id,
        "decision-2"
    );
}
