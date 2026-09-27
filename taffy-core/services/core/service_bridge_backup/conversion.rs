// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed value conversion for the backup restore protocol bridge.

use core_runtime::backup_restore_protocol::{
    BackupRestoreBinding, BackupRestoreCommitAuthorization, BackupRestoreCommitOutcome,
    BackupRestoreProtocolError, BackupRestoreResolutionAuthorization,
    BackupRestoreResolutionChoice, BackupRestoreResolutionOutcome, BackupRestoreStageAuthorization,
};
use core_runtime::wire;

use crate::service_bridge_backup_ffi::ffi::{
    BridgeBackupOperation, BridgeBackupRestoreBinding, BridgeBackupRestoreCommitAuthorization,
    BridgeBackupRestoreCommitAuthorizationResult, BridgeBackupRestoreProtocolResult,
    BridgeBackupRestoreResolutionAuthorization, BridgeBackupRestoreResolutionAuthorizationResult,
    BridgeBackupRestoreStageAuthorization, BridgeBackupRestoreStageAuthorizationResult,
};
use crate::service_bridge_backup_ffi::implementation::{
    binding_from_wire, empty_binding, empty_operation, operation_from_wire, operation_to_wire,
};

pub(super) fn binding_to_runtime(
    input: BridgeBackupRestoreBinding,
) -> Option<BackupRestoreBinding> {
    Some(BackupRestoreBinding::from_wire(
        wire::BackupRestoreBinding {
            planning_operation: operation_to_wire(input.planning_operation),
            owner_profile_id: input.owner_profile_id,
            target: wire::BackupRestoreTarget {
                kind: wire::BackupRestoreTargetKind::from_wire(u32::from(input.target_kind))?,
                profile_id: input.target_profile_id,
            },
            backup_id: input.backup_id,
            snapshot_sha256: input.snapshot_sha256,
            confirmation_sha256: input.confirmation_sha256,
        },
    ))
}

pub(super) fn stage_to_runtime(
    input: BridgeBackupRestoreStageAuthorization,
) -> Option<BackupRestoreStageAuthorization> {
    Some(BackupRestoreStageAuthorization::from_parts(
        binding_to_runtime(input.binding)?,
        operation_to_wire(input.decision_operation),
    ))
}

pub(super) fn commit_to_runtime(
    input: BridgeBackupRestoreCommitAuthorization,
) -> Option<BackupRestoreCommitAuthorization> {
    Some(BackupRestoreCommitAuthorization::from_parts(
        binding_to_runtime(input.binding)?,
        operation_to_wire(input.decision_operation),
    ))
}

pub(super) fn resolution_to_runtime(
    input: BridgeBackupRestoreResolutionAuthorization,
) -> Option<BackupRestoreResolutionAuthorization> {
    Some(BackupRestoreResolutionAuthorization::from_parts(
        binding_to_runtime(input.binding)?,
        operation_to_wire(input.decision_operation),
        resolution_choice(input.choice)?,
    ))
}

pub(super) fn stage_from_runtime(
    input: &BackupRestoreStageAuthorization,
) -> BridgeBackupRestoreStageAuthorization {
    BridgeBackupRestoreStageAuthorization {
        binding: binding_from_wire(input.binding().to_wire()),
        decision_operation: operation_from_wire(input.decision_operation().clone()),
    }
}

pub(super) fn commit_from_runtime(
    input: &BackupRestoreCommitAuthorization,
) -> BridgeBackupRestoreCommitAuthorization {
    BridgeBackupRestoreCommitAuthorization {
        binding: binding_from_wire(input.binding().to_wire()),
        decision_operation: operation_from_wire(input.decision_operation().clone()),
    }
}

pub(super) fn resolution_from_runtime(
    input: &BackupRestoreResolutionAuthorization,
) -> BridgeBackupRestoreResolutionAuthorization {
    BridgeBackupRestoreResolutionAuthorization {
        binding: binding_from_wire(input.binding().to_wire()),
        decision_operation: operation_from_wire(input.decision_operation().clone()),
        choice: match input.choice() {
            BackupRestoreResolutionChoice::AcceptCandidate => {
                wire::BackupRestoreResolutionChoice::AcceptCandidate as u8
            }
            BackupRestoreResolutionChoice::DiscardCandidate => {
                wire::BackupRestoreResolutionChoice::DiscardCandidate as u8
            }
        },
    }
}

pub(super) fn failed_stage(
    operation: BridgeBackupOperation,
    error: BackupRestoreProtocolError,
) -> BridgeBackupRestoreStageAuthorizationResult {
    BridgeBackupRestoreStageAuthorizationResult {
        operation,
        status: status(error),
        has_authorization: false,
        authorization: BridgeBackupRestoreStageAuthorization {
            binding: empty_binding(),
            decision_operation: empty_operation(),
        },
    }
}

pub(super) fn failed_commit(
    operation: BridgeBackupOperation,
    error: BackupRestoreProtocolError,
) -> BridgeBackupRestoreCommitAuthorizationResult {
    BridgeBackupRestoreCommitAuthorizationResult {
        operation,
        status: status(error),
        has_authorization: false,
        authorization: BridgeBackupRestoreCommitAuthorization {
            binding: empty_binding(),
            decision_operation: empty_operation(),
        },
    }
}

pub(super) fn failed_resolution(
    operation: BridgeBackupOperation,
    error: BackupRestoreProtocolError,
) -> BridgeBackupRestoreResolutionAuthorizationResult {
    BridgeBackupRestoreResolutionAuthorizationResult {
        operation,
        status: status(error),
        has_authorization: false,
        authorization: BridgeBackupRestoreResolutionAuthorization {
            binding: empty_binding(),
            decision_operation: empty_operation(),
            choice: wire::BackupRestoreResolutionChoice::AcceptCandidate as u8,
        },
    }
}

pub(super) fn succeeded(operation: BridgeBackupOperation) -> BridgeBackupRestoreProtocolResult {
    BridgeBackupRestoreProtocolResult {
        operation,
        status: wire::BackupRestoreProtocolStatus::Succeeded as u8,
    }
}

pub(super) fn protocol_result(
    operation: BridgeBackupOperation,
    error: BackupRestoreProtocolError,
) -> BridgeBackupRestoreProtocolResult {
    BridgeBackupRestoreProtocolResult {
        operation,
        status: status(error),
    }
}

const fn status(error: BackupRestoreProtocolError) -> u8 {
    (match error {
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
    }) as u8
}

pub(super) const fn commit_outcome(value: u8) -> Option<BackupRestoreCommitOutcome> {
    match value {
        value if value == wire::BackupRestoreCommitOutcome::Committed as u8 => {
            Some(BackupRestoreCommitOutcome::Committed)
        }
        value if value == wire::BackupRestoreCommitOutcome::DefinitelyNotCommitted as u8 => {
            Some(BackupRestoreCommitOutcome::DefinitelyNotCommitted)
        }
        value if value == wire::BackupRestoreCommitOutcome::OutcomeUnknown as u8 => {
            Some(BackupRestoreCommitOutcome::OutcomeUnknown)
        }
        _ => None,
    }
}

pub(super) const fn resolution_choice(value: u8) -> Option<BackupRestoreResolutionChoice> {
    match value {
        value if value == wire::BackupRestoreResolutionChoice::AcceptCandidate as u8 => {
            Some(BackupRestoreResolutionChoice::AcceptCandidate)
        }
        value if value == wire::BackupRestoreResolutionChoice::DiscardCandidate as u8 => {
            Some(BackupRestoreResolutionChoice::DiscardCandidate)
        }
        _ => None,
    }
}

pub(super) const fn resolution_outcome(value: u8) -> Option<BackupRestoreResolutionOutcome> {
    match value {
        value if value == wire::BackupRestoreResolutionOutcome::Completed as u8 => {
            Some(BackupRestoreResolutionOutcome::Completed)
        }
        value if value == wire::BackupRestoreResolutionOutcome::DefinitelyNotCompleted as u8 => {
            Some(BackupRestoreResolutionOutcome::DefinitelyNotCompleted)
        }
        value if value == wire::BackupRestoreResolutionOutcome::OutcomeUnknown as u8 => {
            Some(BackupRestoreResolutionOutcome::OutcomeUnknown)
        }
        _ => None,
    }
}

pub(super) fn clone_operation(input: &BridgeBackupOperation) -> BridgeBackupOperation {
    BridgeBackupOperation {
        operation_id: input.operation_id.clone(),
        service_generation: input.service_generation,
        task_revision: input.task_revision,
        deadline_monotonic_ms: input.deadline_monotonic_ms,
        idempotency_key: input.idempotency_key.clone(),
    }
}
