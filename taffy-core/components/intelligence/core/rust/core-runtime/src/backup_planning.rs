// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generated Core Service values projected into the portable backup domain.
//!
//! Record payload bytes do not have a type in this module. The browser owns
//! those bytes, the portable storage domain owns the manifest and restore
//! rules, and the only cryptographic primitive lent to either is the existing
//! reviewed SHA-256 port.

use std::collections::BTreeSet;

use taffy_storage::backup::{
    open_backup_manifest, prepare_backup_manifest, BackupDigest, BackupError, BackupManifestDraft,
    BackupSelection,
};

use crate::{wire, DigestError, Sha256Port};

mod conversion;
mod restore;

pub(crate) use restore::{prepare_restore, PreparedRestore};

use self::conversion::{
    record_kind_from_domain, record_kind_to_domain, record_state_from_domain, record_to_domain,
};

/// Canonicalizes one typed browser snapshot without receiving record payload.
#[must_use]
pub fn prepare_manifest(
    request: wire::BackupManifestPrepareRequest,
    expected_generation: u64,
    now_monotonic_ms: u64,
    digest: &dyn Sha256Port,
) -> wire::BackupManifestPrepareResult {
    let operation = request.operation.clone();
    if !valid_operation(&operation, expected_generation, now_monotonic_ms)
        || !valid_prepare_shape(&request)
    {
        return failed_prepare(operation, wire::BackupPlanningStatus::InvalidRequest);
    }
    let selection =
        match BackupSelection::new(request.selection.into_iter().map(record_kind_to_domain)) {
            Ok(selection) => selection,
            Err(error) => return failed_prepare(operation, prepare_error_status(&error)),
        };
    let records = request.records.into_iter().map(record_to_domain).collect();
    let prepared = match prepare_backup_manifest(
        BackupManifestDraft {
            backup_id: request.backup_id,
            source_installation_id: request.source_installation_id,
            created_at_utc: request.created_at_utc,
            selection,
            records,
        },
        &BackupDigestPort(digest),
    ) {
        Ok(prepared) => prepared,
        Err(error) => return failed_prepare(operation, prepare_error_status(&error)),
    };
    wire::BackupManifestPrepareResult {
        operation,
        status: wire::BackupPlanningStatus::Succeeded,
        manifest_plaintext: prepared.manifest_plaintext().to_vec(),
        snapshot_sha256: prepared.snapshot_sha256(),
        payload_plaintext_bytes: prepared.payload_plaintext_bytes(),
        source_order: prepared.source_order().to_vec(),
        expected_sealed_chunks: prepared.expected_sealed_chunks(),
    }
}

/// Validates one decrypted manifest and returns only its preview and layout.
#[must_use]
pub fn inspect_manifest(
    request: wire::BackupManifestInspectRequest,
    expected_generation: u64,
    now_monotonic_ms: u64,
    digest: &dyn Sha256Port,
) -> wire::BackupManifestInspectResult {
    let operation = request.operation;
    if !valid_operation(&operation, expected_generation, now_monotonic_ms)
        || request.manifest_plaintext.is_empty()
        || request.manifest_plaintext.len() > wire::MAX_BACKUP_MANIFEST_BYTES
    {
        return failed_inspect(operation, wire::BackupPlanningStatus::InvalidRequest);
    }
    let opened = match open_backup_manifest(&request.manifest_plaintext, &BackupDigestPort(digest))
    {
        Ok(opened) => opened,
        Err(error) => return failed_inspect(operation, open_error_status(&error)),
    };
    let layout = match opened.payload_layout() {
        Ok(layout) => layout,
        Err(error) => return failed_inspect(operation, open_error_status(&error)),
    };
    let Ok(record_count) = u32::try_from(opened.record_count()) else {
        return failed_inspect(operation, wire::BackupPlanningStatus::InvalidManifest);
    };
    wire::BackupManifestInspectResult {
        operation,
        status: wire::BackupPlanningStatus::Succeeded,
        backup_id: opened.backup_id().to_owned(),
        source_installation_id: opened.source_installation_id().to_owned(),
        created_at_utc: opened.created_at_utc().to_owned(),
        selection: opened.selection().map(record_kind_from_domain).collect(),
        record_count,
        snapshot_sha256: layout.snapshot_sha256,
        payload_plaintext_bytes: layout.payload_plaintext_bytes,
        records: layout
            .records
            .into_iter()
            .map(|entry| wire::BackupPayloadLayoutEntry {
                state: record_state_from_domain(entry.state),
                plaintext_bytes: entry.plaintext_bytes,
            })
            .collect(),
    }
}

/// Produces no restore plan until every staged record range agrees with the
/// authenticated manifest's exact position, length, and digest.
#[must_use]
pub fn plan_restore(
    request: wire::BackupRestorePlanRequest,
    expected_generation: u64,
    now_monotonic_ms: u64,
    digest: &dyn Sha256Port,
) -> wire::BackupRestorePlanResult {
    match prepare_restore(request, expected_generation, now_monotonic_ms, digest) {
        Ok(prepared) => prepared.to_wire(),
        Err(failure) => *failure,
    }
}

fn valid_prepare_shape(request: &wire::BackupManifestPrepareRequest) -> bool {
    if !valid_id(&request.backup_id)
        || !valid_id(&request.source_installation_id)
        || request.created_at_utc.is_empty()
        || request.created_at_utc.len() > wire::MAX_BACKUP_TIMESTAMP_BYTES
        || request.selection.is_empty()
        || request.selection.len() > wire::MAX_BACKUP_SELECTION_KINDS
    {
        return false;
    }
    let mut selected = BTreeSet::new();
    if request
        .selection
        .iter()
        .any(|kind| !selected.insert(*kind as u32))
    {
        return false;
    }
    valid_record_shape(&request.records, Some(&selected))
}

fn valid_record_shape(
    records: &[wire::BackupRecordDescriptor],
    selected: Option<&BTreeSet<u32>>,
) -> bool {
    if records.len() > wire::MAX_BACKUP_RECORDS {
        return false;
    }
    let mut total = 0_u64;
    for record in records {
        if !valid_id(&record.stable_id)
            || record.revision == 0
            || record.schema_version == 0
            || record.plaintext_bytes > wire::MAX_BACKUP_RECORD_BYTES as u64
            || selected.is_some_and(|kinds| !kinds.contains(&(record.kind as u32)))
        {
            return false;
        }
        match record.state {
            wire::BackupRecordState::Active
                if record.plaintext_bytes == 0
                    || record.plaintext_sha256.iter().all(|byte| *byte == 0) =>
            {
                return false;
            }
            wire::BackupRecordState::Tombstone
                if record.plaintext_bytes != 0
                    || record.plaintext_sha256.iter().any(|byte| *byte != 0) =>
            {
                return false;
            }
            wire::BackupRecordState::Active | wire::BackupRecordState::Tombstone => {}
        }
        let Some(next) = total.checked_add(record.plaintext_bytes) else {
            return false;
        };
        if next > wire::MAX_BACKUP_PLAINTEXT_BYTES as u64 {
            return false;
        }
        total = next;
    }
    true
}

pub(super) fn valid_staged_shape(records: &[wire::StagedBackupRecord]) -> bool {
    let mut total = 0_u64;
    for record in records {
        if record.plaintext_bytes > wire::MAX_BACKUP_RECORD_BYTES as u64 {
            return false;
        }
        let Some(next) = total.checked_add(record.plaintext_bytes) else {
            return false;
        };
        if next > wire::MAX_BACKUP_PLAINTEXT_BYTES as u64 {
            return false;
        }
        total = next;
    }
    true
}

pub(crate) fn valid_operation(
    operation: &wire::OperationEnvelope,
    expected_generation: u64,
    now_monotonic_ms: u64,
) -> bool {
    expected_generation != 0
        && operation.service_generation == expected_generation
        && operation.task_revision == 0
        && operation.deadline_monotonic_ms > now_monotonic_ms
        && valid_protocol_text(&operation.operation_id, wire::MAX_OPERATION_ID_BYTES)
        && valid_protocol_text(&operation.idempotency_key, wire::MAX_IDEMPOTENCY_KEY_BYTES)
}

fn valid_protocol_text(value: &str, maximum_bytes: usize) -> bool {
    !value.is_empty() && value.len() <= maximum_bytes && !value.chars().any(char::is_control)
}

pub(super) fn valid_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= wire::MAX_BACKUP_ID_BYTES
        && !value.chars().any(char::is_control)
}

fn failed_prepare(
    operation: wire::OperationEnvelope,
    status: wire::BackupPlanningStatus,
) -> wire::BackupManifestPrepareResult {
    wire::BackupManifestPrepareResult {
        operation,
        status,
        manifest_plaintext: Vec::new(),
        snapshot_sha256: [0; 32],
        payload_plaintext_bytes: 0,
        source_order: Vec::new(),
        expected_sealed_chunks: 0,
    }
}

fn failed_inspect(
    operation: wire::OperationEnvelope,
    status: wire::BackupPlanningStatus,
) -> wire::BackupManifestInspectResult {
    wire::BackupManifestInspectResult {
        operation,
        status,
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

pub(crate) fn failed_restore(
    operation: wire::OperationEnvelope,
    target_kind: wire::BackupRestoreTargetKind,
    status: wire::BackupPlanningStatus,
) -> wire::BackupRestorePlanResult {
    wire::BackupRestorePlanResult {
        operation,
        status,
        backup_id: String::new(),
        snapshot_sha256: [0; 32],
        target: wire::BackupRestoreTarget {
            kind: target_kind,
            profile_id: String::new(),
        },
        entries: Vec::new(),
        has_conflicts: false,
        confirmation_sha256: [0; 32],
        binding: None,
    }
}

const fn prepare_error_status(error: &BackupError) -> wire::BackupPlanningStatus {
    match error {
        BackupError::DigestUnavailable => wire::BackupPlanningStatus::DigestUnavailable,
        _ => wire::BackupPlanningStatus::InvalidRequest,
    }
}

pub(super) const fn open_error_status(error: &BackupError) -> wire::BackupPlanningStatus {
    match error {
        BackupError::DigestUnavailable => wire::BackupPlanningStatus::DigestUnavailable,
        BackupError::SnapshotMismatch => wire::BackupPlanningStatus::SnapshotMismatch,
        _ => wire::BackupPlanningStatus::InvalidManifest,
    }
}

pub(super) const fn restore_error_status(error: &BackupError) -> wire::BackupPlanningStatus {
    match error {
        BackupError::DigestUnavailable => wire::BackupPlanningStatus::DigestUnavailable,
        BackupError::StagedPayloadMismatch => wire::BackupPlanningStatus::StagedPayloadMismatch,
        BackupError::SnapshotMismatch => wire::BackupPlanningStatus::SnapshotMismatch,
        _ => wire::BackupPlanningStatus::RestoreConflict,
    }
}

pub(crate) struct BackupDigestPort<'a>(pub(crate) &'a dyn Sha256Port);

impl BackupDigest for BackupDigestPort<'_> {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], BackupError> {
        self.0
            .sha256(input)
            .map_err(|DigestError::Unavailable| BackupError::DigestUnavailable)
    }
}

#[cfg(test)]
mod tests;
