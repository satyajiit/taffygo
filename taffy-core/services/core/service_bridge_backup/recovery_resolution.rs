// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Restart-aware recovery resolution on the source-profile protocol owner.

use core_runtime::wire;

use crate::service_bridge_backup_ffi::ffi::{
    BridgeBackupOperation, BridgeBackupRestoreProtocolResult,
    BridgeBackupRestoreRecoveryResolutionAuthorization,
    BridgeBackupRestoreRecoveryResolutionAuthorizationResult,
    BridgeBackupRestoreRecoveryResolutionOutcomeReport,
    BridgeBackupRestoreRecoveryResolutionRequest,
};
use crate::service_bridge_backup_ffi::implementation::{operation_from_wire, operation_to_wire};
use crate::service_bridge_runtime::ServiceBridge;

use super::conversion::clone_operation;
use super::recovery::{binding_from_wire, binding_to_wire, record_from_wire, record_to_wire};

#[allow(non_snake_case)]
pub(crate) fn ChooseBackupRestoreRecoveryResolution(
    bridge: &mut ServiceBridge,
    request: BridgeBackupRestoreRecoveryResolutionRequest,
    now_monotonic_ms: u64,
) -> BridgeBackupRestoreRecoveryResolutionAuthorizationResult {
    let operation = clone_operation(&request.operation);
    let Some(request) = request_to_wire(request) else {
        return failed_authorization(
            operation,
            wire::BackupRestoreProtocolStatus::BindingMismatch,
        );
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return failed_authorization(operation, wire::BackupRestoreProtocolStatus::Unavailable);
    };
    authorization_result_from_wire(
        runtime.choose_backup_restore_recovery_resolution(request, now_monotonic_ms),
    )
}

#[allow(non_snake_case)]
pub(crate) fn ReportBackupRestoreRecoveryResolutionOutcome(
    bridge: &mut ServiceBridge,
    report: BridgeBackupRestoreRecoveryResolutionOutcomeReport,
    now_monotonic_ms: u64,
) -> BridgeBackupRestoreProtocolResult {
    let operation = clone_operation(&report.operation);
    let Some(report) = report_to_wire(report) else {
        return protocol_result(
            operation,
            wire::BackupRestoreProtocolStatus::BindingMismatch,
        );
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return protocol_result(operation, wire::BackupRestoreProtocolStatus::Unavailable);
    };
    protocol_result_from_wire(
        runtime.report_backup_restore_recovery_resolution_outcome(report, now_monotonic_ms),
    )
}

fn request_to_wire(
    input: BridgeBackupRestoreRecoveryResolutionRequest,
) -> Option<wire::BackupRestoreRecoveryResolutionRequest> {
    Some(wire::BackupRestoreRecoveryResolutionRequest {
        operation: operation_to_wire(input.operation),
        history_prefix: input
            .history_prefix
            .into_iter()
            .map(record_to_wire)
            .collect::<Option<Vec<_>>>()?,
        choice: wire::BackupRestoreResolutionChoice::from_wire(u32::from(input.choice))?,
        intent_id: input.intent_id,
    })
}

fn report_to_wire(
    input: BridgeBackupRestoreRecoveryResolutionOutcomeReport,
) -> Option<wire::BackupRestoreRecoveryResolutionOutcomeReport> {
    Some(wire::BackupRestoreRecoveryResolutionOutcomeReport {
        operation: operation_to_wire(input.operation),
        authorization: authorization_to_wire(input.authorization)?,
        durable_history: input
            .durable_history
            .into_iter()
            .map(record_to_wire)
            .collect::<Option<Vec<_>>>()?,
    })
}

fn authorization_to_wire(
    input: BridgeBackupRestoreRecoveryResolutionAuthorization,
) -> Option<wire::BackupRestoreRecoveryResolutionAuthorization> {
    Some(wire::BackupRestoreRecoveryResolutionAuthorization {
        binding: binding_to_wire(input.binding)?,
        decision_operation: operation_to_wire(input.decision_operation),
        choice: wire::BackupRestoreResolutionChoice::from_wire(u32::from(input.choice))?,
        intent_id: input.intent_id,
        history_prefix: input
            .history_prefix
            .into_iter()
            .map(record_to_wire)
            .collect::<Option<Vec<_>>>()?,
    })
}

fn authorization_from_wire(
    input: wire::BackupRestoreRecoveryResolutionAuthorization,
) -> BridgeBackupRestoreRecoveryResolutionAuthorization {
    BridgeBackupRestoreRecoveryResolutionAuthorization {
        binding: binding_from_wire(input.binding),
        decision_operation: operation_from_wire(input.decision_operation),
        choice: input.choice as u8,
        intent_id: input.intent_id,
        history_prefix: input
            .history_prefix
            .into_iter()
            .map(record_from_wire)
            .collect(),
    }
}

fn authorization_result_from_wire(
    input: wire::BackupRestoreRecoveryResolutionAuthorizationResult,
) -> BridgeBackupRestoreRecoveryResolutionAuthorizationResult {
    let has_authorization = input.authorization.is_some();
    let authorization = input
        .authorization
        .map_or_else(empty_authorization, authorization_from_wire);
    BridgeBackupRestoreRecoveryResolutionAuthorizationResult {
        operation: operation_from_wire(input.operation),
        status: input.status as u8,
        has_authorization,
        authorization,
    }
}

fn protocol_result_from_wire(
    input: wire::BackupRestoreProtocolResult,
) -> BridgeBackupRestoreProtocolResult {
    BridgeBackupRestoreProtocolResult {
        operation: operation_from_wire(input.operation),
        status: input.status as u8,
    }
}

fn failed_authorization(
    operation: BridgeBackupOperation,
    status: wire::BackupRestoreProtocolStatus,
) -> BridgeBackupRestoreRecoveryResolutionAuthorizationResult {
    BridgeBackupRestoreRecoveryResolutionAuthorizationResult {
        operation,
        status: status as u8,
        has_authorization: false,
        authorization: empty_authorization(),
    }
}

fn protocol_result(
    operation: BridgeBackupOperation,
    status: wire::BackupRestoreProtocolStatus,
) -> BridgeBackupRestoreProtocolResult {
    BridgeBackupRestoreProtocolResult {
        operation,
        status: status as u8,
    }
}

fn empty_authorization() -> BridgeBackupRestoreRecoveryResolutionAuthorization {
    authorization_from_wire(wire::BackupRestoreRecoveryResolutionAuthorization {
        binding: wire::BackupRestoreRecoveryBinding {
            reservation_id: String::new(),
            owner_profile_id: String::new(),
            target_kind: wire::BackupRestoreTargetKind::NewRegularProfile,
            target_profile_id: String::new(),
            backup_id: String::new(),
            snapshot_sha256: [0; 32],
            confirmation_sha256: [0; 32],
            selection: Vec::new(),
            record_count: 0,
            candidate_records_sha256: [0; 32],
        },
        decision_operation: wire::OperationEnvelope {
            operation_id: String::new(),
            service_generation: 0,
            task_revision: 0,
            deadline_monotonic_ms: 0,
            idempotency_key: String::new(),
        },
        choice: wire::BackupRestoreResolutionChoice::AcceptCandidate,
        intent_id: String::new(),
        history_prefix: Vec::new(),
    })
}
