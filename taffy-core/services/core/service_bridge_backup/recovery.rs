// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Content-free recovery inspection on the existing backup protocol seam.

use core_runtime::wire;

use crate::service_bridge_backup_ffi::ffi::{
    BridgeBackupOperation, BridgeBackupRestoreRecoveryBinding,
    BridgeBackupRestoreRecoveryClassification, BridgeBackupRestoreRecoveryFailure,
    BridgeBackupRestoreRecoveryInspectionRequest, BridgeBackupRestoreRecoveryInspectionResult,
    BridgeBackupRestoreRecoveryIntentFact, BridgeBackupRestoreRecoveryOutcomeFact,
    BridgeBackupRestoreRecoveryReconciliation, BridgeBackupRestoreRecoveryRecord,
};
use crate::service_bridge_backup_ffi::implementation::{operation_from_wire, operation_to_wire};
use crate::service_bridge_runtime::ServiceBridge;

use super::conversion::clone_operation;

#[allow(non_snake_case)]
pub(crate) fn InspectBackupRestoreRecovery(
    bridge: &ServiceBridge,
    request: BridgeBackupRestoreRecoveryInspectionRequest,
    now_monotonic_ms: u64,
) -> BridgeBackupRestoreRecoveryInspectionResult {
    let operation = clone_operation(&request.operation);
    let Some(request) = request_to_wire(request) else {
        return failure(
            operation,
            wire::BackupRestoreRecoveryInspectionStatus::InvalidRecord,
            None,
        );
    };
    let Some(runtime) = bridge.runtime.as_ref() else {
        return failure(
            operation,
            wire::BackupRestoreRecoveryInspectionStatus::Unavailable,
            None,
        );
    };
    result_from_wire(runtime.inspect_backup_restore_recovery(request, now_monotonic_ms))
}

fn request_to_wire(
    input: BridgeBackupRestoreRecoveryInspectionRequest,
) -> Option<wire::BackupRestoreRecoveryInspectionRequest> {
    let records = input
        .records
        .into_iter()
        .map(record_to_wire)
        .collect::<Option<Vec<_>>>()?;
    Some(wire::BackupRestoreRecoveryInspectionRequest {
        operation: operation_to_wire(input.operation),
        records,
    })
}

pub(super) fn record_to_wire(
    input: BridgeBackupRestoreRecoveryRecord,
) -> Option<wire::BackupRestoreRecoveryRecord> {
    let intent = if input.has_intent {
        Some(wire::BackupRestoreRecoveryIntentFact {
            intent_id: input.intent.intent_id,
            intent: wire::BackupRestorePhysicalIntent::from_wire(u32::from(input.intent.intent))?,
        })
    } else {
        None
    };
    let outcome = if input.has_outcome {
        Some(wire::BackupRestoreRecoveryOutcomeFact {
            intent_id: input.outcome.intent_id,
            outcome: wire::BackupRestoreObservedOutcome::from_wire(u32::from(
                input.outcome.outcome,
            ))?,
        })
    } else {
        None
    };
    Some(wire::BackupRestoreRecoveryRecord {
        format_version: input.format_version,
        sequence: input.sequence,
        binding: binding_to_wire(input.binding)?,
        fact_kind: wire::BackupRestoreRecoveryFactKind::from_wire(u32::from(input.fact_kind))?,
        intent,
        outcome,
    })
}

pub(super) fn binding_to_wire(
    input: BridgeBackupRestoreRecoveryBinding,
) -> Option<wire::BackupRestoreRecoveryBinding> {
    Some(wire::BackupRestoreRecoveryBinding {
        reservation_id: input.reservation_id,
        owner_profile_id: input.owner_profile_id,
        target_kind: wire::BackupRestoreTargetKind::from_wire(u32::from(input.target_kind))?,
        target_profile_id: input.target_profile_id,
        backup_id: input.backup_id,
        snapshot_sha256: input.snapshot_sha256,
        confirmation_sha256: input.confirmation_sha256,
        selection: input
            .selection
            .into_iter()
            .map(|kind| wire::BackupRecordKind::from_wire(u32::from(kind)))
            .collect::<Option<Vec<_>>>()?,
        record_count: input.record_count,
        candidate_records_sha256: input.candidate_records_sha256,
    })
}

pub(super) fn record_from_wire(
    input: wire::BackupRestoreRecoveryRecord,
) -> BridgeBackupRestoreRecoveryRecord {
    let has_intent = input.intent.is_some();
    let intent =
        input
            .intent
            .map_or_else(empty_intent, |fact| BridgeBackupRestoreRecoveryIntentFact {
                intent_id: fact.intent_id,
                intent: fact.intent as u8,
            });
    let has_outcome = input.outcome.is_some();
    let outcome = input.outcome.map_or_else(empty_outcome, |fact| {
        BridgeBackupRestoreRecoveryOutcomeFact {
            intent_id: fact.intent_id,
            outcome: fact.outcome as u8,
        }
    });
    BridgeBackupRestoreRecoveryRecord {
        format_version: input.format_version,
        sequence: input.sequence,
        binding: binding_from_wire(input.binding),
        fact_kind: input.fact_kind as u8,
        has_intent,
        intent,
        has_outcome,
        outcome,
    }
}

pub(super) fn binding_from_wire(
    input: wire::BackupRestoreRecoveryBinding,
) -> BridgeBackupRestoreRecoveryBinding {
    BridgeBackupRestoreRecoveryBinding {
        reservation_id: input.reservation_id,
        owner_profile_id: input.owner_profile_id,
        target_kind: input.target_kind as u8,
        target_profile_id: input.target_profile_id,
        backup_id: input.backup_id,
        snapshot_sha256: input.snapshot_sha256,
        confirmation_sha256: input.confirmation_sha256,
        selection: input.selection.into_iter().map(|kind| kind as u8).collect(),
        record_count: input.record_count,
        candidate_records_sha256: input.candidate_records_sha256,
    }
}

fn empty_intent() -> BridgeBackupRestoreRecoveryIntentFact {
    BridgeBackupRestoreRecoveryIntentFact {
        intent_id: String::new(),
        intent: wire::BackupRestorePhysicalIntent::CommitCandidate as u8,
    }
}

fn empty_outcome() -> BridgeBackupRestoreRecoveryOutcomeFact {
    BridgeBackupRestoreRecoveryOutcomeFact {
        intent_id: String::new(),
        outcome: wire::BackupRestoreObservedOutcome::OutcomeUnknown as u8,
    }
}

fn result_from_wire(
    input: wire::BackupRestoreRecoveryInspectionResult,
) -> BridgeBackupRestoreRecoveryInspectionResult {
    let has_classification = input.classification.is_some();
    let classification = input
        .classification
        .map_or_else(empty_classification, classification_from_wire);
    let has_failure = input.failure.is_some();
    let failure =
        input
            .failure
            .map_or_else(empty_failure, |body| BridgeBackupRestoreRecoveryFailure {
                error: body.error as u8,
            });
    BridgeBackupRestoreRecoveryInspectionResult {
        operation: operation_from_wire(input.operation),
        status: input.status as u8,
        has_classification,
        classification,
        has_failure,
        failure,
    }
}

fn classification_from_wire(
    input: wire::BackupRestoreRecoveryClassification,
) -> BridgeBackupRestoreRecoveryClassification {
    let has_reconciliation = input.reconciliation.is_some();
    let reconciliation = input
        .reconciliation
        .map_or_else(empty_reconciliation, |body| {
            BridgeBackupRestoreRecoveryReconciliation {
                intent_id: body.intent_id,
                intent: body.intent as u8,
            }
        });
    BridgeBackupRestoreRecoveryClassification {
        kind: input.kind as u8,
        has_reconciliation,
        reconciliation,
    }
}

fn failure(
    operation: BridgeBackupOperation,
    status: wire::BackupRestoreRecoveryInspectionStatus,
    error: Option<wire::BackupRestoreRecoveryError>,
) -> BridgeBackupRestoreRecoveryInspectionResult {
    BridgeBackupRestoreRecoveryInspectionResult {
        operation,
        status: status as u8,
        has_classification: false,
        classification: empty_classification(),
        has_failure: error.is_some(),
        failure: error.map_or_else(empty_failure, |error| BridgeBackupRestoreRecoveryFailure {
            error: error as u8,
        }),
    }
}

fn empty_classification() -> BridgeBackupRestoreRecoveryClassification {
    BridgeBackupRestoreRecoveryClassification {
        kind: wire::BackupRestoreRecoveryClassificationKind::RollbackAvailable as u8,
        has_reconciliation: false,
        reconciliation: empty_reconciliation(),
    }
}

fn empty_reconciliation() -> BridgeBackupRestoreRecoveryReconciliation {
    BridgeBackupRestoreRecoveryReconciliation {
        intent_id: String::new(),
        intent: wire::BackupRestorePhysicalIntent::CommitCandidate as u8,
    }
}

fn empty_failure() -> BridgeBackupRestoreRecoveryFailure {
    BridgeBackupRestoreRecoveryFailure {
        error: wire::BackupRestoreRecoveryError::MissingCommitIntent as u8,
    }
}
