// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Projection between backup-only CXX records and the generated contract.

use core_runtime::{backup_planning, wire, DigestError, Sha256Port};

use super::ffi::{
    BridgeBackupManifestInspectResult, BridgeBackupManifestPrepareRequest,
    BridgeBackupManifestPrepareResult, BridgeBackupOperation, BridgeBackupPayloadLayoutEntry,
    BridgeBackupRecordDescriptor, BridgeBackupRestorePlanEntry, BridgeBackupRestorePlanRequest,
    BridgeBackupRestorePlanResult, BridgeStagedBackupRecord,
};

#[allow(non_snake_case)]
pub(super) fn PrepareBackupManifest(
    request: BridgeBackupManifestPrepareRequest,
    expected_generation: u64,
    now_monotonic_ms: u64,
) -> BridgeBackupManifestPrepareResult {
    let operation = request.operation;
    if request.selection.len() > wire::MAX_BACKUP_SELECTION_KINDS
        || request.records.len() > wire::MAX_BACKUP_RECORDS
    {
        return invalid_prepare(operation);
    }
    let selection = request
        .selection
        .into_iter()
        .map(|kind| wire::BackupRecordKind::from_wire(u32::from(kind)))
        .collect::<Option<Vec<_>>>();
    let records = request
        .records
        .into_iter()
        .map(record_to_wire)
        .collect::<Option<Vec<_>>>();
    let Some((selection, records)) = selection.zip(records) else {
        return invalid_prepare(operation);
    };
    prepare_from_wire(backup_planning::prepare_manifest(
        wire::BackupManifestPrepareRequest {
            operation: operation_to_wire(operation),
            backup_id: request.backup_id,
            source_installation_id: request.source_installation_id,
            created_at_utc: request.created_at_utc,
            selection,
            records,
        },
        expected_generation,
        now_monotonic_ms,
        &ChromiumBackupDigest,
    ))
}

#[allow(non_snake_case)]
pub(super) fn InspectBackupManifest(
    operation: BridgeBackupOperation,
    manifest_plaintext: &[u8],
    expected_generation: u64,
    now_monotonic_ms: u64,
) -> BridgeBackupManifestInspectResult {
    if manifest_plaintext.is_empty() || manifest_plaintext.len() > wire::MAX_BACKUP_MANIFEST_BYTES {
        return invalid_inspect(operation);
    }
    inspect_from_wire(backup_planning::inspect_manifest(
        wire::BackupManifestInspectRequest {
            operation: operation_to_wire(operation),
            manifest_plaintext: manifest_plaintext.to_vec(),
        },
        expected_generation,
        now_monotonic_ms,
        &ChromiumBackupDigest,
    ))
}

pub(crate) fn plan_restore_to_wire(
    request: BridgeBackupRestorePlanRequest,
    manifest_plaintext: &[u8],
) -> Option<wire::BackupRestorePlanRequest> {
    if manifest_plaintext.is_empty()
        || manifest_plaintext.len() > wire::MAX_BACKUP_MANIFEST_BYTES
        || request.staged_records.len() > wire::MAX_BACKUP_RECORDS
        || request.current_records.len() > wire::MAX_BACKUP_RECORDS
    {
        return None;
    }
    let target_kind = wire::BackupRestoreTargetKind::from_wire(u32::from(request.target_kind));
    let staged_records = request
        .staged_records
        .into_iter()
        .map(staged_to_wire)
        .collect::<Vec<_>>();
    let current_records = request
        .current_records
        .into_iter()
        .map(record_to_wire)
        .collect::<Option<Vec<_>>>();
    let (target_kind, current_records) = target_kind.zip(current_records)?;
    Some(wire::BackupRestorePlanRequest {
        operation: operation_to_wire(request.operation),
        manifest_plaintext: manifest_plaintext.to_vec(),
        staged_records,
        current_records,
        target: wire::BackupRestoreTarget {
            kind: target_kind,
            profile_id: request.target_profile_id,
        },
    })
}

pub(crate) fn operation_to_wire(input: BridgeBackupOperation) -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: input.operation_id,
        service_generation: input.service_generation,
        task_revision: input.task_revision,
        deadline_monotonic_ms: input.deadline_monotonic_ms,
        idempotency_key: input.idempotency_key,
    }
}

pub(crate) fn operation_from_wire(input: wire::OperationEnvelope) -> BridgeBackupOperation {
    BridgeBackupOperation {
        operation_id: input.operation_id,
        service_generation: input.service_generation,
        task_revision: input.task_revision,
        deadline_monotonic_ms: input.deadline_monotonic_ms,
        idempotency_key: input.idempotency_key,
    }
}

fn record_to_wire(input: BridgeBackupRecordDescriptor) -> Option<wire::BackupRecordDescriptor> {
    Some(wire::BackupRecordDescriptor {
        kind: wire::BackupRecordKind::from_wire(u32::from(input.kind))?,
        stable_id: input.stable_id,
        revision: input.revision,
        schema_version: input.schema_version,
        state: wire::BackupRecordState::from_wire(u32::from(input.state))?,
        plaintext_bytes: input.plaintext_bytes,
        plaintext_sha256: input.plaintext_sha256,
    })
}

fn staged_to_wire(input: BridgeStagedBackupRecord) -> wire::StagedBackupRecord {
    wire::StagedBackupRecord {
        plaintext_bytes: input.plaintext_bytes,
        plaintext_sha256: input.plaintext_sha256,
    }
}

fn prepare_from_wire(
    input: wire::BackupManifestPrepareResult,
) -> BridgeBackupManifestPrepareResult {
    BridgeBackupManifestPrepareResult {
        operation: operation_from_wire(input.operation),
        status: input.status as u8,
        manifest_plaintext: input.manifest_plaintext,
        snapshot_sha256: input.snapshot_sha256,
        payload_plaintext_bytes: input.payload_plaintext_bytes,
        source_order: input.source_order,
        expected_sealed_chunks: input.expected_sealed_chunks,
    }
}

fn inspect_from_wire(
    input: wire::BackupManifestInspectResult,
) -> BridgeBackupManifestInspectResult {
    BridgeBackupManifestInspectResult {
        operation: operation_from_wire(input.operation),
        status: input.status as u8,
        backup_id: input.backup_id,
        source_installation_id: input.source_installation_id,
        created_at_utc: input.created_at_utc,
        selection: input.selection.into_iter().map(|kind| kind as u8).collect(),
        record_count: input.record_count,
        snapshot_sha256: input.snapshot_sha256,
        payload_plaintext_bytes: input.payload_plaintext_bytes,
        records: input
            .records
            .into_iter()
            .map(|entry| BridgeBackupPayloadLayoutEntry {
                state: entry.state as u8,
                plaintext_bytes: entry.plaintext_bytes,
            })
            .collect(),
    }
}

pub(crate) fn restore_from_wire(
    input: wire::BackupRestorePlanResult,
) -> BridgeBackupRestorePlanResult {
    let binding = input.binding.map(binding_from_wire);
    let has_binding = binding.is_some();
    BridgeBackupRestorePlanResult {
        operation: operation_from_wire(input.operation),
        status: input.status as u8,
        backup_id: input.backup_id,
        snapshot_sha256: input.snapshot_sha256,
        target_kind: input.target.kind as u8,
        target_profile_id: input.target.profile_id,
        entries: input
            .entries
            .into_iter()
            .map(|entry| BridgeBackupRestorePlanEntry {
                kind: entry.kind as u8,
                stable_id: entry.stable_id,
                archive_revision: entry.archive_revision,
                action: entry.action as u8,
                schema_version: entry.schema_version,
                state: entry.state as u8,
                plaintext_bytes: entry.plaintext_bytes,
                plaintext_sha256: entry.plaintext_sha256,
            })
            .collect(),
        has_conflicts: input.has_conflicts,
        confirmation_sha256: input.confirmation_sha256,
        has_binding,
        binding: binding.unwrap_or_else(empty_binding),
    }
}

pub(crate) fn binding_from_wire(
    input: wire::BackupRestoreBinding,
) -> super::ffi::BridgeBackupRestoreBinding {
    super::ffi::BridgeBackupRestoreBinding {
        planning_operation: operation_from_wire(input.planning_operation),
        owner_profile_id: input.owner_profile_id,
        target_kind: input.target.kind as u8,
        target_profile_id: input.target.profile_id,
        backup_id: input.backup_id,
        snapshot_sha256: input.snapshot_sha256,
        confirmation_sha256: input.confirmation_sha256,
    }
}

fn invalid_prepare(operation: BridgeBackupOperation) -> BridgeBackupManifestPrepareResult {
    BridgeBackupManifestPrepareResult {
        operation,
        status: wire::BackupPlanningStatus::InvalidRequest as u8,
        manifest_plaintext: Vec::new(),
        snapshot_sha256: [0; 32],
        payload_plaintext_bytes: 0,
        source_order: Vec::new(),
        expected_sealed_chunks: 0,
    }
}

fn invalid_inspect(operation: BridgeBackupOperation) -> BridgeBackupManifestInspectResult {
    BridgeBackupManifestInspectResult {
        operation,
        status: wire::BackupPlanningStatus::InvalidRequest as u8,
        backup_id: String::new(),
        source_installation_id: String::new(),
        created_at_utc: String::new(),
        selection: Vec::new(),
        record_count: 0,
        snapshot_sha256: [0; 32],
        payload_plaintext_bytes: 0,
        records: Vec::new(),
    }
}

pub(crate) fn invalid_restore(
    operation: BridgeBackupOperation,
    target_kind: u8,
    status: wire::BackupPlanningStatus,
) -> BridgeBackupRestorePlanResult {
    BridgeBackupRestorePlanResult {
        operation,
        status: status as u8,
        backup_id: String::new(),
        snapshot_sha256: [0; 32],
        target_kind,
        target_profile_id: String::new(),
        entries: Vec::new(),
        has_conflicts: false,
        confirmation_sha256: [0; 32],
        has_binding: false,
        binding: empty_binding(),
    }
}

pub(crate) fn empty_operation() -> BridgeBackupOperation {
    BridgeBackupOperation {
        operation_id: String::new(),
        service_generation: 0,
        task_revision: 0,
        deadline_monotonic_ms: 0,
        idempotency_key: String::new(),
    }
}

pub(crate) fn empty_binding() -> super::ffi::BridgeBackupRestoreBinding {
    super::ffi::BridgeBackupRestoreBinding {
        planning_operation: empty_operation(),
        owner_profile_id: String::new(),
        target_kind: wire::BackupRestoreTargetKind::NewRegularProfile as u8,
        target_profile_id: String::new(),
        backup_id: String::new(),
        snapshot_sha256: [0; 32],
        confirmation_sha256: [0; 32],
    }
}

struct ChromiumBackupDigest;

impl Sha256Port for ChromiumBackupDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], DigestError> {
        crate::ffi::ChromiumSha256(input)
            .try_into()
            .map_err(|_| DigestError::Unavailable)
    }
}
