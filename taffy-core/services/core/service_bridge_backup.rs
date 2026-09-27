// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Projection into the source-profile-owned portable restore protocol.
//!
//! The generated values below carry only identities, digests, closed outcomes,
//! and consumptive authority. Bounded staged procedure definitions additionally
//! cross for transient bootstrap-equivalent validation before commit issuance.
//! Recovery material, other record payloads, paths, and the browser's physical
//! reservation handle cannot cross this module.

mod conversion;
mod recovery;
mod recovery_resolution;

pub(crate) use recovery::InspectBackupRestoreRecovery;
pub(crate) use recovery_resolution::{
    ChooseBackupRestoreRecoveryResolution, ReportBackupRestoreRecoveryResolutionOutcome,
};

use core_runtime::backup_restore_protocol::BackupRestoreProtocolError;
use core_runtime::wire;

use crate::service_bridge_backup_ffi::ffi::{
    BridgeBackupRestoreCancellationRequest, BridgeBackupRestoreCommitAuthorizationResult,
    BridgeBackupRestoreCommitOutcomeReport, BridgeBackupRestorePlanConfirmationRequest,
    BridgeBackupRestorePlanRequest, BridgeBackupRestorePlanResult,
    BridgeBackupRestoreProtocolResult, BridgeBackupRestoreResolutionAuthorizationResult,
    BridgeBackupRestoreResolutionOutcomeReport, BridgeBackupRestoreResolutionRequest,
    BridgeBackupRestoreStageAuthorizationResult, BridgeBackupRestoreStageVerificationRequest,
};
use crate::service_bridge_backup_ffi::implementation::{
    invalid_restore, operation_to_wire, plan_restore_to_wire, restore_from_wire,
};
use crate::service_bridge_runtime::ServiceBridge;

use self::conversion::{
    binding_to_runtime, clone_operation, commit_from_runtime, commit_outcome, commit_to_runtime,
    failed_commit, failed_resolution, failed_stage, protocol_result, resolution_choice,
    resolution_from_runtime, resolution_outcome, resolution_to_runtime, stage_from_runtime,
    stage_to_runtime, succeeded,
};

#[allow(non_snake_case)]
pub(crate) fn PlanBackupRestore(
    bridge: &mut ServiceBridge,
    request: BridgeBackupRestorePlanRequest,
    manifest_plaintext: &[u8],
    now_monotonic_ms: u64,
) -> BridgeBackupRestorePlanResult {
    let operation = clone_operation(&request.operation);
    let target_kind = request.target_kind;
    let Some(runtime) = bridge.runtime.as_mut() else {
        return invalid_restore(
            operation,
            target_kind,
            wire::BackupPlanningStatus::Unavailable,
        );
    };
    let Some(request) = plan_restore_to_wire(request, manifest_plaintext) else {
        return invalid_restore(
            operation,
            target_kind,
            wire::BackupPlanningStatus::InvalidRequest,
        );
    };
    restore_from_wire(runtime.plan_backup_restore(request, now_monotonic_ms))
}

#[allow(non_snake_case)]
pub(crate) fn ConfirmBackupRestorePlan(
    bridge: &mut ServiceBridge,
    request: BridgeBackupRestorePlanConfirmationRequest,
    now_monotonic_ms: u64,
) -> BridgeBackupRestoreStageAuthorizationResult {
    let operation = clone_operation(&request.operation);
    let Some(binding) = binding_to_runtime(request.binding) else {
        return failed_stage(operation, BackupRestoreProtocolError::BindingMismatch);
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return failed_stage(operation, BackupRestoreProtocolError::Unavailable);
    };
    match runtime.confirm_backup_restore_plan(
        operation_to_wire(request.operation),
        &binding,
        request.confirmed_sha256,
        now_monotonic_ms,
    ) {
        Ok(authorization) => BridgeBackupRestoreStageAuthorizationResult {
            operation,
            status: wire::BackupRestoreProtocolStatus::Succeeded as u8,
            has_authorization: true,
            authorization: stage_from_runtime(&authorization),
        },
        Err(error) => failed_stage(operation, error),
    }
}

#[allow(non_snake_case)]
pub(crate) fn ReportBackupRestoreStageVerified(
    bridge: &mut ServiceBridge,
    request: BridgeBackupRestoreStageVerificationRequest,
    now_monotonic_ms: u64,
) -> BridgeBackupRestoreCommitAuthorizationResult {
    let operation = clone_operation(&request.operation);
    let Some(authorization) = stage_to_runtime(request.authorization) else {
        return failed_commit(operation, BackupRestoreProtocolError::BindingMismatch);
    };
    let Some(skills) = crate::service_bridge_bootstrap_ffi::skill_records_to_wire(request.skills)
    else {
        return failed_commit(operation, BackupRestoreProtocolError::SnapshotMismatch);
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return failed_commit(operation, BackupRestoreProtocolError::Unavailable);
    };
    match runtime.report_backup_restore_stage_verified(
        operation_to_wire(request.operation),
        &authorization,
        request.staged_snapshot_sha256,
        skills,
        now_monotonic_ms,
    ) {
        Ok(authorization) => BridgeBackupRestoreCommitAuthorizationResult {
            operation,
            status: wire::BackupRestoreProtocolStatus::Succeeded as u8,
            has_authorization: true,
            authorization: commit_from_runtime(&authorization),
        },
        Err(error) => failed_commit(operation, error),
    }
}

#[allow(non_snake_case)]
pub(crate) fn ReportBackupRestoreCommitOutcome(
    bridge: &mut ServiceBridge,
    report: BridgeBackupRestoreCommitOutcomeReport,
    now_monotonic_ms: u64,
) -> BridgeBackupRestoreProtocolResult {
    let operation = clone_operation(&report.operation);
    let Some(authorization) = commit_to_runtime(report.authorization) else {
        return protocol_result(operation, BackupRestoreProtocolError::BindingMismatch);
    };
    let Some(outcome) = commit_outcome(report.outcome) else {
        return protocol_result(operation, BackupRestoreProtocolError::WrongPhase);
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return protocol_result(operation, BackupRestoreProtocolError::Unavailable);
    };
    match runtime.resolve_backup_restore_commit(
        &operation_to_wire(report.operation),
        &authorization,
        outcome,
        now_monotonic_ms,
    ) {
        Ok(()) => succeeded(operation),
        Err(error) => protocol_result(operation, error),
    }
}

#[allow(non_snake_case)]
pub(crate) fn ChooseBackupRestoreResolution(
    bridge: &mut ServiceBridge,
    request: BridgeBackupRestoreResolutionRequest,
    now_monotonic_ms: u64,
) -> BridgeBackupRestoreResolutionAuthorizationResult {
    let operation = clone_operation(&request.operation);
    let Some(binding) = binding_to_runtime(request.binding) else {
        return failed_resolution(operation, BackupRestoreProtocolError::BindingMismatch);
    };
    let Some(choice) = resolution_choice(request.choice) else {
        return failed_resolution(operation, BackupRestoreProtocolError::WrongPhase);
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return failed_resolution(operation, BackupRestoreProtocolError::Unavailable);
    };
    match runtime.choose_backup_restore_resolution(
        operation_to_wire(request.operation),
        &binding,
        choice,
        now_monotonic_ms,
    ) {
        Ok(authorization) => BridgeBackupRestoreResolutionAuthorizationResult {
            operation,
            status: wire::BackupRestoreProtocolStatus::Succeeded as u8,
            has_authorization: true,
            authorization: resolution_from_runtime(&authorization),
        },
        Err(error) => failed_resolution(operation, error),
    }
}

#[allow(non_snake_case)]
pub(crate) fn ReportBackupRestoreResolutionOutcome(
    bridge: &mut ServiceBridge,
    report: BridgeBackupRestoreResolutionOutcomeReport,
    now_monotonic_ms: u64,
) -> BridgeBackupRestoreProtocolResult {
    let operation = clone_operation(&report.operation);
    let Some(authorization) = resolution_to_runtime(report.authorization) else {
        return protocol_result(operation, BackupRestoreProtocolError::BindingMismatch);
    };
    let Some(outcome) = resolution_outcome(report.outcome) else {
        return protocol_result(operation, BackupRestoreProtocolError::WrongPhase);
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return protocol_result(operation, BackupRestoreProtocolError::Unavailable);
    };
    match runtime.resolve_backup_restore_resolution(
        &operation_to_wire(report.operation),
        &authorization,
        outcome,
        now_monotonic_ms,
    ) {
        Ok(()) => succeeded(operation),
        Err(error) => protocol_result(operation, error),
    }
}

#[allow(non_snake_case)]
pub(crate) fn CancelBackupRestoreBeforeCommit(
    bridge: &mut ServiceBridge,
    request: BridgeBackupRestoreCancellationRequest,
    now_monotonic_ms: u64,
) -> BridgeBackupRestoreProtocolResult {
    let operation = clone_operation(&request.operation);
    let Some(binding) = binding_to_runtime(request.binding) else {
        return protocol_result(operation, BackupRestoreProtocolError::BindingMismatch);
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return protocol_result(operation, BackupRestoreProtocolError::Unavailable);
    };
    match runtime.cancel_backup_restore_before_commit(
        &operation_to_wire(request.operation),
        &binding,
        now_monotonic_ms,
    ) {
        Ok(()) => succeeded(operation),
        Err(error) => protocol_result(operation, error),
    }
}
