// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Restore-plan construction that retains the validated portable authority.

use taffy_storage::backup::{
    open_backup_manifest, RestorePlan, RestoreSession, RestoreTarget, StagedBackupRecord,
};

use super::conversion::{
    action_from_domain, current_record_to_domain, record_kind_from_domain,
    record_state_from_domain, target_kind_from_domain, target_kind_to_domain,
};
use super::{
    failed_restore, open_error_status, restore_error_status, valid_id, valid_operation,
    valid_record_shape, valid_staged_shape, BackupDigestPort,
};
use crate::{wire, Sha256Port};

/// One validated domain plan and its exact confirmation session.
///
/// The browser-facing result is always projected from this retained value. A
/// later phase must never rebuild authority from its wire projection.
pub(crate) struct PreparedRestore {
    pub(crate) operation: wire::OperationEnvelope,
    pub(crate) plan: RestorePlan,
    pub(crate) session: RestoreSession,
    pub(crate) confirmation_sha256: [u8; 32],
}

impl PreparedRestore {
    pub(crate) fn to_wire(&self) -> wire::BackupRestorePlanResult {
        wire::BackupRestorePlanResult {
            operation: self.operation.clone(),
            status: wire::BackupPlanningStatus::Succeeded,
            backup_id: self.plan.backup_id().to_owned(),
            snapshot_sha256: self.plan.snapshot_sha256(),
            target: wire::BackupRestoreTarget {
                kind: target_kind_from_domain(self.plan.target().kind),
                profile_id: self.plan.target().profile_id.clone(),
            },
            entries: self
                .plan
                .entries()
                .iter()
                .map(|entry| wire::BackupRestorePlanEntry {
                    kind: record_kind_from_domain(entry.kind),
                    stable_id: entry.stable_id.clone(),
                    archive_revision: entry.archive_revision,
                    action: action_from_domain(entry.action),
                    schema_version: entry.schema_version,
                    state: record_state_from_domain(entry.state),
                    plaintext_bytes: entry.plaintext_bytes,
                    plaintext_sha256: entry.plaintext_sha256,
                })
                .collect(),
            has_conflicts: self.plan.has_conflicts(),
            confirmation_sha256: self.confirmation_sha256,
            binding: None,
        }
    }
}

pub(crate) fn prepare_restore(
    request: wire::BackupRestorePlanRequest,
    expected_generation: u64,
    now_monotonic_ms: u64,
    digest: &dyn Sha256Port,
) -> Result<PreparedRestore, Box<wire::BackupRestorePlanResult>> {
    let operation = request.operation;
    let failure_target_kind = request.target.kind;
    if !valid_operation(&operation, expected_generation, now_monotonic_ms)
        || request.manifest_plaintext.is_empty()
        || request.manifest_plaintext.len() > wire::MAX_BACKUP_MANIFEST_BYTES
        || request.staged_records.len() > wire::MAX_BACKUP_RECORDS
        || !valid_staged_shape(&request.staged_records)
        || !valid_record_shape(&request.current_records, None)
        || !valid_id(&request.target.profile_id)
    {
        return Err(Box::new(failed_restore(
            operation,
            failure_target_kind,
            wire::BackupPlanningStatus::InvalidRequest,
        )));
    }
    let opened = open_backup_manifest(&request.manifest_plaintext, &BackupDigestPort(digest))
        .map_err(|error| {
            Box::new(failed_restore(
                operation.clone(),
                failure_target_kind,
                open_error_status(&error),
            ))
        })?;
    let staged = request
        .staged_records
        .into_iter()
        .map(|record| StagedBackupRecord {
            plaintext_bytes: record.plaintext_bytes,
            plaintext_sha256: record.plaintext_sha256,
        })
        .collect::<Vec<_>>();
    let current = request
        .current_records
        .into_iter()
        .map(current_record_to_domain)
        .collect::<Vec<_>>();
    let target = RestoreTarget {
        kind: target_kind_to_domain(request.target.kind),
        profile_id: request.target.profile_id,
    };
    let plan = opened
        .plan_restore(&staged, &current, target)
        .map_err(|error| {
            Box::new(failed_restore(
                operation.clone(),
                failure_target_kind,
                restore_error_status(&error),
            ))
        })?;
    let mut session = RestoreSession::begin();
    session.recovery_key_supplied().map_err(|error| {
        Box::new(failed_restore(
            operation.clone(),
            failure_target_kind,
            restore_error_status(&error),
        ))
    })?;
    let confirmation_sha256 = session
        .archive_opened(&plan, &BackupDigestPort(digest))
        .map_err(|error| {
            Box::new(failed_restore(
                operation.clone(),
                failure_target_kind,
                restore_error_status(&error),
            ))
        })?;
    Ok(PreparedRestore {
        operation,
        plan,
        session,
        confirmation_sha256,
    })
}
