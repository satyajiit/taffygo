// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact generated-shape and durable-suffix checks for recovery resolution.

use taffy_storage::backup::{
    RestorePhysicalIntent, RestoreRecoveryStatus, MAX_BACKUP_RESTORE_RECOVERY_RECORDS,
    RESTORE_RECOVERY_RECORD_VERSION,
};

use super::{
    BackupRestoreProtocolError, RecoveryResolutionIssue, RECOVERY_RESOLUTION_SUFFIX_CAPACITY,
};
use crate::wire;

pub(super) fn validate_report_suffix(
    authorization: &wire::BackupRestoreRecoveryResolutionAuthorization,
    history: &[wire::BackupRestoreRecoveryRecord],
) -> Result<(), BackupRestoreProtocolError> {
    let prefix_len = authorization.history_prefix.len();
    if history.len() <= prefix_len
        || history.len() > prefix_len + RECOVERY_RESOLUTION_SUFFIX_CAPACITY
        || history.len() > MAX_BACKUP_RESTORE_RECOVERY_RECORDS
        || history.get(..prefix_len) != Some(authorization.history_prefix.as_slice())
    {
        return Err(BackupRestoreProtocolError::BindingMismatch);
    }
    // The guard above already proved `history.len() > prefix_len`; `get` is
    // what keeps that proof out of the panic path the workspace denies.
    let suffix = history
        .get(prefix_len..)
        .ok_or(BackupRestoreProtocolError::BindingMismatch)?;
    let expected_sequence =
        u64::try_from(prefix_len + 1).map_err(|_| BackupRestoreProtocolError::BindingMismatch)?;
    let first = suffix
        .first()
        .ok_or(BackupRestoreProtocolError::BindingMismatch)?;
    let intent_matches = first.format_version == RESTORE_RECOVERY_RECORD_VERSION
        && first.sequence == expected_sequence
        && first.binding == authorization.binding
        && first.fact_kind == wire::BackupRestoreRecoveryFactKind::IntentRecorded
        && first.outcome.is_none()
        && first.intent.as_ref().is_some_and(|fact| {
            fact.intent_id == authorization.intent_id
                && fact.intent == intent_to_wire(intent_for_choice(authorization.choice))
        });
    if !intent_matches {
        return Err(BackupRestoreProtocolError::BindingMismatch);
    }
    for (offset, record) in suffix.iter().enumerate().skip(1) {
        let sequence = u64::try_from(prefix_len + offset + 1)
            .map_err(|_| BackupRestoreProtocolError::BindingMismatch)?;
        if record.format_version != RESTORE_RECOVERY_RECORD_VERSION
            || record.sequence != sequence
            || record.binding != authorization.binding
            || record.fact_kind != wire::BackupRestoreRecoveryFactKind::OutcomeObserved
            || record.intent.is_some()
            || record
                .outcome
                .as_ref()
                .is_none_or(|fact| fact.intent_id != authorization.intent_id)
        {
            return Err(BackupRestoreProtocolError::BindingMismatch);
        }
    }
    Ok(())
}

pub(super) fn is_expected_settlement(
    issue: &RecoveryResolutionIssue,
    status: &RestoreRecoveryStatus,
) -> bool {
    matches!(
        (&issue.prior_status, issue.authorization.choice, status),
        (
            RestoreRecoveryStatus::RollbackAvailable,
            wire::BackupRestoreResolutionChoice::AcceptCandidate,
            RestoreRecoveryStatus::Published | RestoreRecoveryStatus::RollbackAvailable,
        ) | (
            RestoreRecoveryStatus::RollbackAvailable,
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            RestoreRecoveryStatus::VerifiedDeleted | RestoreRecoveryStatus::RollbackAvailable,
        ) | (
            RestoreRecoveryStatus::CleanupRequired,
            wire::BackupRestoreResolutionChoice::DiscardCandidate,
            RestoreRecoveryStatus::VerifiedDeleted | RestoreRecoveryStatus::CleanupRequired,
        )
    )
}

pub(super) fn recovery_binding_matches_active(
    recovery: &wire::BackupRestoreRecoveryBinding,
    active: &super::super::BackupRestoreBinding,
) -> bool {
    recovery.owner_profile_id == active.owner_profile_id
        && recovery.target_kind == active.target_kind
        && recovery.target_profile_id == active.target_profile_id
        && recovery.backup_id == active.backup_id
        && recovery.snapshot_sha256 == active.snapshot_sha256
        && recovery.confirmation_sha256 == active.confirmation_sha256
}

/// Whether `choice` may be authorized against `prior_status`.
///
/// Split out of `issue_recovery_resolution` only because that reducer is over
/// the hundred-line cap. The refusal it returns is part of the protocol: a
/// history still awaiting reconciliation answers `ReconcileRequired`, and
/// everything else answers `WrongPhase`, so a caller can tell "come back with
/// an observation" from "this was never your decision to make".
pub(super) fn admit_resolution_choice(
    prior_status: &RestoreRecoveryStatus,
    choice: wire::BackupRestoreResolutionChoice,
) -> Result<(), BackupRestoreProtocolError> {
    let permitted = matches!(prior_status, RestoreRecoveryStatus::RollbackAvailable)
        || matches!(
            (prior_status, choice),
            (
                RestoreRecoveryStatus::CleanupRequired,
                wire::BackupRestoreResolutionChoice::DiscardCandidate
            )
        );
    if permitted {
        return Ok(());
    }
    Err(match prior_status {
        RestoreRecoveryStatus::ReconcileRequired { .. } => {
            BackupRestoreProtocolError::ReconcileRequired
        }
        _ => BackupRestoreProtocolError::WrongPhase,
    })
}

pub(super) const fn intent_for_choice(
    choice: wire::BackupRestoreResolutionChoice,
) -> RestorePhysicalIntent {
    match choice {
        wire::BackupRestoreResolutionChoice::AcceptCandidate => {
            RestorePhysicalIntent::AcceptCandidate
        }
        wire::BackupRestoreResolutionChoice::DiscardCandidate => {
            RestorePhysicalIntent::DiscardCandidate
        }
    }
}

const fn intent_to_wire(intent: RestorePhysicalIntent) -> wire::BackupRestorePhysicalIntent {
    match intent {
        RestorePhysicalIntent::CommitCandidate => {
            wire::BackupRestorePhysicalIntent::CommitCandidate
        }
        RestorePhysicalIntent::AcceptCandidate => {
            wire::BackupRestorePhysicalIntent::AcceptCandidate
        }
        RestorePhysicalIntent::DiscardCandidate => {
            wire::BackupRestorePhysicalIntent::DiscardCandidate
        }
    }
}

pub(super) fn authorization_result(
    operation: wire::OperationEnvelope,
    status: wire::BackupRestoreProtocolStatus,
    authorization: Option<wire::BackupRestoreRecoveryResolutionAuthorization>,
) -> wire::BackupRestoreRecoveryResolutionAuthorizationResult {
    wire::BackupRestoreRecoveryResolutionAuthorizationResult {
        operation,
        status,
        authorization,
    }
}

pub(super) const fn status_for(
    error: BackupRestoreProtocolError,
) -> wire::BackupRestoreProtocolStatus {
    match error {
        BackupRestoreProtocolError::Unavailable => wire::BackupRestoreProtocolStatus::Unavailable,
        BackupRestoreProtocolError::InvalidOperation => {
            wire::BackupRestoreProtocolStatus::InvalidOperation
        }
        BackupRestoreProtocolError::BindingMismatch => {
            wire::BackupRestoreProtocolStatus::BindingMismatch
        }
        BackupRestoreProtocolError::WrongPhase => wire::BackupRestoreProtocolStatus::WrongPhase,
        BackupRestoreProtocolError::ConfirmationMismatch => {
            wire::BackupRestoreProtocolStatus::ConfirmationMismatch
        }
        BackupRestoreProtocolError::SnapshotMismatch => {
            wire::BackupRestoreProtocolStatus::SnapshotMismatch
        }
        BackupRestoreProtocolError::ReconcileRequired => {
            wire::BackupRestoreProtocolStatus::ReconcileRequired
        }
    }
}
