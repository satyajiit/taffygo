// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use taffy_storage::backup::MAX_BACKUP_RESTORE_RECOVERY_RECORDS;

use super::recovery_resolution::{
    cleanup, committed, durable_history, intent, outcome, recovery_binding, request,
};
use super::{commit_issued, operation, protocol, NOW};
use crate::wire;

#[test]
fn divergent_intent_choice_prefix_and_source_are_refused() {
    let binding = recovery_binding(None);
    let prefix = committed(&binding);
    let mut restore = protocol(false);
    let issued = restore.choose_recovery_resolution(
        request(
            "decision",
            NOW + 20,
            prefix.clone(),
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-1",
        ),
        NOW,
    );
    assert_eq!(issued.status, wire::BackupRestoreProtocolStatus::Succeeded);
    for mutation in 0..3 {
        let mut changed = request(
            "changed",
            NOW + 30,
            prefix.clone(),
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-1",
        );
        match mutation {
            0 => changed.intent_id = "other-intent".to_owned(),
            1 => changed.choice = wire::BackupRestoreResolutionChoice::AcceptCandidate,
            _ => changed.history_prefix[0].binding.reservation_id = "other-reservation".to_owned(),
        }
        assert_eq!(
            restore.choose_recovery_resolution(changed, NOW + 20).status,
            wire::BackupRestoreProtocolStatus::BindingMismatch
        );
    }

    let mut foreign_binding = recovery_binding(None);
    foreign_binding.owner_profile_id = "foreign-source".to_owned();
    let foreign = protocol(false).choose_recovery_resolution(
        request(
            "foreign",
            NOW + 50,
            committed(&foreign_binding),
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-foreign",
        ),
        NOW,
    );
    assert_eq!(
        foreign.status,
        wire::BackupRestoreProtocolStatus::BindingMismatch
    );
}

#[test]
fn unknown_suffix_never_settles_and_definitive_suffix_is_derived() {
    let binding = recovery_binding(None);
    let mut restore = protocol(false);
    let issued = restore.choose_recovery_resolution(
        request(
            "discard",
            NOW + 50,
            committed(&binding),
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-1",
        ),
        NOW,
    );
    let authorization = issued.authorization.expect("authority");
    for observations in [
        Vec::new(),
        vec![wire::BackupRestoreObservedOutcome::OutcomeUnknown],
    ] {
        let result = restore.report_recovery_resolution_outcome(
            wire::BackupRestoreRecoveryResolutionOutcomeReport {
                operation: operation("report-pending", NOW + 60),
                authorization: authorization.clone(),
                durable_history: durable_history(&authorization, &observations),
            },
            NOW + 1,
        );
        assert_eq!(
            result.status,
            wire::BackupRestoreProtocolStatus::ReconcileRequired
        );
    }
    let stale_authority = restore.choose_recovery_resolution(
        request(
            "stale-authority",
            NOW + 60,
            authorization.history_prefix.clone(),
            authorization.choice,
            &authorization.intent_id,
        ),
        NOW + 2,
    );
    assert_eq!(
        stale_authority.status,
        wire::BackupRestoreProtocolStatus::ReconcileRequired
    );
    let result = restore.report_recovery_resolution_outcome(
        wire::BackupRestoreRecoveryResolutionOutcomeReport {
            operation: operation("report-known", NOW + 60),
            authorization: authorization.clone(),
            durable_history: durable_history(
                &authorization,
                &[
                    wire::BackupRestoreObservedOutcome::OutcomeUnknown,
                    wire::BackupRestoreObservedOutcome::Completed,
                ],
            ),
        },
        NOW + 2,
    );
    assert_eq!(result.status, wire::BackupRestoreProtocolStatus::Succeeded);
}

#[test]
fn cleanup_allows_only_discard_and_failed_deletion_remains_cleanup() {
    let binding = recovery_binding(None);
    let prefix = cleanup(&binding);
    let accept = protocol(false).choose_recovery_resolution(
        request(
            "accept-cleanup",
            NOW + 50,
            prefix.clone(),
            wire::BackupRestoreResolutionChoice::AcceptCandidate,
            "accept-1",
        ),
        NOW,
    );
    assert_eq!(accept.status, wire::BackupRestoreProtocolStatus::WrongPhase);

    let mut restore = protocol(false);
    let issued = restore.choose_recovery_resolution(
        request(
            "discard-cleanup",
            NOW + 50,
            prefix,
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-1",
        ),
        NOW,
    );
    let authorization = issued.authorization.expect("discard authority");
    let history = durable_history(
        &authorization,
        &[wire::BackupRestoreObservedOutcome::DefinitelyNotCompleted],
    );
    let report = restore.report_recovery_resolution_outcome(
        wire::BackupRestoreRecoveryResolutionOutcomeReport {
            operation: operation("report-cleanup", NOW + 60),
            authorization: authorization.clone(),
            durable_history: history.clone(),
        },
        NOW + 1,
    );
    assert_eq!(report.status, wire::BackupRestoreProtocolStatus::Succeeded);
    let continued = restore.choose_recovery_resolution(
        request(
            "discard-cleanup-again",
            NOW + 70,
            history,
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-2",
        ),
        NOW + 2,
    );
    assert_eq!(
        continued.status,
        wire::BackupRestoreProtocolStatus::Succeeded
    );
}

#[test]
fn pending_terminal_oversized_and_disconnected_requests_never_authorize() {
    let binding = recovery_binding(None);
    let pending = protocol(false).choose_recovery_resolution(
        request(
            "pending",
            NOW + 50,
            vec![intent(
                1,
                &binding,
                "commit-1",
                wire::BackupRestorePhysicalIntent::CommitCandidate,
            )],
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-1",
        ),
        NOW,
    );
    assert_eq!(
        pending.status,
        wire::BackupRestoreProtocolStatus::ReconcileRequired
    );

    let mut terminal = committed(&binding);
    terminal.push(intent(
        3,
        &binding,
        "discard-1",
        wire::BackupRestorePhysicalIntent::DiscardCandidate,
    ));
    terminal.push(outcome(
        4,
        &binding,
        "discard-1",
        wire::BackupRestoreObservedOutcome::Completed,
    ));
    let terminal = protocol(false).choose_recovery_resolution(
        request(
            "terminal",
            NOW + 50,
            terminal,
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-2",
        ),
        NOW,
    );
    assert_eq!(
        terminal.status,
        wire::BackupRestoreProtocolStatus::WrongPhase
    );

    let row = committed(&binding)[0].clone();
    let oversized = protocol(false).choose_recovery_resolution(
        request(
            "oversized",
            NOW + 50,
            vec![row; MAX_BACKUP_RESTORE_RECOVERY_RECORDS - 2],
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-oversized",
        ),
        NOW,
    );
    assert_eq!(
        oversized.status,
        wire::BackupRestoreProtocolStatus::WrongPhase
    );

    let mut disconnected = protocol(false);
    disconnected.service_disconnected().expect("disconnect");
    let disconnected = disconnected.choose_recovery_resolution(
        request(
            "disconnected",
            NOW + 50,
            committed(&binding),
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-disconnected",
        ),
        NOW,
    );
    assert_eq!(
        disconnected.status,
        wire::BackupRestoreProtocolStatus::Unavailable
    );
}

#[test]
fn decision_must_be_current_and_an_active_different_restore_is_not_adopted() {
    let binding = recovery_binding(None);
    for operation in [
        operation("wrong-generation", NOW + 50),
        operation("expired", NOW),
    ] {
        let mut changed = request(
            &operation.operation_id,
            operation.deadline_monotonic_ms,
            committed(&binding),
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-1",
        );
        changed.operation = operation;
        if changed.operation.operation_id == "wrong-generation" {
            changed.operation.service_generation += 1;
        }
        assert_eq!(
            protocol(false)
                .choose_recovery_resolution(changed, NOW)
                .status,
            wire::BackupRestoreProtocolStatus::InvalidOperation
        );
    }

    let (mut restore, _active, _commit) = commit_issued("other-target");
    let refused = restore.choose_recovery_resolution(
        request(
            "different-live-restore",
            NOW + 50,
            committed(&binding),
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            "discard-1",
        ),
        NOW,
    );
    assert_eq!(
        refused.status,
        wire::BackupRestoreProtocolStatus::BindingMismatch
    );
    assert!(refused.authorization.is_none());
}
