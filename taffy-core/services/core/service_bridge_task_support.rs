// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed validation and projection helpers for durable task commands.

use core_runtime::wire;
use core_runtime::{
    FieldNodeIds, FieldValueAskOutcome, PendingError, PermissionDecision, PlatformPermission,
    RefusalReason, SemanticNodeId, SubmitError, WorkspaceCommitError, WorkspaceStoreError,
};

use crate::ffi;
use crate::service_bridge_runtime::ChromiumDigest;

pub(super) fn validate_operation(
    operation: &wire::OperationEnvelope,
    generation: u64,
    now_monotonic_ms: u64,
) -> Result<(), u8> {
    if operation.service_generation != generation {
        return Err(wire::AdmissionStatus::StaleGeneration as u8);
    }
    if operation.task_revision == 0
        || operation.operation_id.is_empty()
        || operation.operation_id.len() > wire::MAX_OPERATION_ID_BYTES
        || operation.idempotency_key.is_empty()
        || operation.idempotency_key.len() > wire::MAX_IDEMPOTENCY_KEY_BYTES
    {
        return Err(wire::AdmissionStatus::InvalidCommand as u8);
    }
    if now_monotonic_ms >= operation.deadline_monotonic_ms {
        return Err(wire::AdmissionStatus::DeadlineExceeded as u8);
    }
    Ok(())
}

pub(super) fn storage_effect(
    envelope: core_runtime::OperationEnvelope<core_runtime::EffectRequest>,
) -> Option<ffi::BridgeStorageEffect> {
    let core_runtime::EffectRequest::Storage(storage) = envelope.body else {
        return None;
    };
    let wire = storage.to_wire();
    let workspace = wire.workspace;
    Some(ffi::BridgeStorageEffect {
        operation: ffi::BridgeOperation {
            operation_id: envelope.operation_id.as_str().to_owned(),
            service_generation: envelope.service_generation.value(),
            task_revision: envelope.task_revision,
            deadline_monotonic_ms: envelope.deadline.as_millis(),
            idempotency_key: envelope.idempotency_key.as_str().to_owned(),
        },
        effect_id: envelope.operation_id.as_str().to_owned(),
        operation_kind: wire::StorageOperation::AppendTaskCommit as u8,
        task_id: wire.task_id,
        expected_revision: wire.expected_revision,
        resulting_revision: wire.resulting_revision,
        transaction_batch: wire.transaction_batch,
        task_id_seed: wire.task_id_seed,
        workspace_id: workspace
            .as_ref()
            .map_or_else(String::new, |value| value.workspace_id.clone()),
        workspace_expected_revision: workspace
            .as_ref()
            .map_or(0, |value| value.expected_revision),
        workspace_resulting_revision: workspace
            .as_ref()
            .map_or(0, |value| value.resulting_revision),
        workspace_snapshot: workspace.map_or_else(Vec::new, |value| value.snapshot),
        skill_id: String::new(),
        skill_version: 0,
        skill_origin: String::new(),
        skill_provenance: 0,
        skill_status: 0,
        skill_definition: Vec::new(),
        skill_step_count: 0,
        skill_changed_at_utc_ms: 0,
        skill_task_id: String::new(),
        skill_run_outcome: 0,
        skill_ran_at_utc_ms: 0,
        configuration_disabled_abilities: Vec::new(),
        configuration_preset: 0,
        configuration_pace: 0,
        configuration_length: 0,
        configuration_check_in: 0,
    })
}

/// The content-free name of a refused submission, beside the status it maps to.
pub(super) fn submit_error_label(error: &SubmitError) -> &'static str {
    match error {
        SubmitError::RecoveryRequired { .. } => "submit_recovery_required",
        SubmitError::InvalidTaskResult { .. } => "submit_invalid_task_result",
        SubmitError::WorkspaceAfterApply { .. } => "submit_workspace_after_apply",
        SubmitError::AuditEncoding { .. } => "submit_audit_encoding",
        SubmitError::StorageEncoding { .. } => "submit_storage_encoding",
        SubmitError::PendingAfterApply { .. } => "submit_pending_after_apply",
        SubmitError::CommitInFlight => "submit_commit_in_flight",
        SubmitError::UnknownTask => "submit_unknown_task",
        SubmitError::Envelope(_) => "submit_envelope",
        SubmitError::Pending(_) => "submit_pending",
        SubmitError::Workspace(_) => "submit_workspace",
        // The reducer already carries one compiled-in, content-free name per
        // reason. Restating five of them here and collapsing the other fifty
        // into one word was a second, weaker copy of that table: a refused
        // completion reached the log as `label=task_refused`, which names the
        // seam rather than the reason, and the refusal had to be found by
        // decoding the phone's journal instead of by reading one line.
        SubmitError::Task(refusal) => refusal.reason.label(),
    }
}

pub(super) fn submit_error_status(error: &SubmitError) -> u8 {
    match error {
        SubmitError::Task(refusal) if refusal.reason == RefusalReason::RevisionConflict => {
            wire::AdmissionStatus::StaleRevision as u8
        }
        SubmitError::Pending(PendingError::StaleGeneration) => {
            wire::AdmissionStatus::StaleGeneration as u8
        }
        SubmitError::Pending(PendingError::StaleRevision) => {
            wire::AdmissionStatus::StaleRevision as u8
        }
        SubmitError::Pending(PendingError::DeadlineExceeded) => {
            wire::AdmissionStatus::DeadlineExceeded as u8
        }
        SubmitError::Pending(PendingError::DuplicateOperation | PendingError::AlreadyCompleted) => {
            wire::AdmissionStatus::Duplicate as u8
        }
        SubmitError::Pending(PendingError::Saturated) | SubmitError::CommitInFlight => {
            wire::AdmissionStatus::Backpressure as u8
        }
        SubmitError::Workspace(WorkspaceCommitError::Store(
            WorkspaceStoreError::OperationAlreadyPending
            | WorkspaceStoreError::TooManyPendingMutations,
        )) => wire::AdmissionStatus::Backpressure as u8,
        SubmitError::RecoveryRequired { .. }
        | SubmitError::InvalidTaskResult { .. }
        | SubmitError::WorkspaceAfterApply { .. }
        | SubmitError::AuditEncoding { .. }
        | SubmitError::StorageEncoding { .. }
        | SubmitError::PendingAfterApply { .. }
        | SubmitError::Workspace(WorkspaceCommitError::DigestUnavailable) => {
            wire::AdmissionStatus::CoreUnavailable as u8
        }
        SubmitError::UnknownTask
        | SubmitError::Envelope(_)
        | SubmitError::Task(_)
        | SubmitError::Workspace(_) => wire::AdmissionStatus::InvalidCommand as u8,
    }
}

pub(super) fn permission_from_wire(value: u8) -> Option<PlatformPermission> {
    match wire::PlatformPermission::from_wire(u32::from(value))? {
        wire::PlatformPermission::Notifications => Some(PlatformPermission::Notifications),
        wire::PlatformPermission::Microphone => Some(PlatformPermission::Microphone),
        wire::PlatformPermission::Camera => Some(PlatformPermission::Camera),
        wire::PlatformPermission::Location => Some(PlatformPermission::Location),
    }
}

pub(super) fn permission_decision_from_wire(value: u8) -> Option<PermissionDecision> {
    match wire::PermissionDecision::from_wire(u32::from(value))? {
        wire::PermissionDecision::Granted => Some(PermissionDecision::Granted),
        wire::PermissionDecision::Denied => Some(PermissionDecision::Denied),
        wire::PermissionDecision::Dismissed => Some(PermissionDecision::Dismissed),
        wire::PermissionDecision::Unavailable => Some(PermissionDecision::Unavailable),
    }
}

/// One `FieldValueAskOutcome` off the wire, or nothing (decision 0215).
///
/// Total over the generated enumeration, so a member added to the contract
/// stops this compiling rather than silently folding into an arm beside it.
pub(super) fn supplied_outcome_from_wire(value: u8) -> Option<FieldValueAskOutcome> {
    match wire::FieldValueAskOutcome::from_wire(u32::from(value))? {
        wire::FieldValueAskOutcome::Answered => Some(FieldValueAskOutcome::Answered),
        wire::FieldValueAskOutcome::Dismissed => Some(FieldValueAskOutcome::Dismissed),
        wire::FieldValueAskOutcome::NotAField => Some(FieldValueAskOutcome::NotAField),
        wire::FieldValueAskOutcome::ChallengeOffScreen => {
            Some(FieldValueAskOutcome::ChallengeOffScreen)
        }
        wire::FieldValueAskOutcome::CannotBeShown => Some(FieldValueAskOutcome::CannotBeShown),
        wire::FieldValueAskOutcome::PageMoved => Some(FieldValueAskOutcome::PageMoved),
        wire::FieldValueAskOutcome::NoSurface => Some(FieldValueAskOutcome::NoSurface),
    }
}

/// The field each held value was minted for, off the wire, or nothing
/// (decision 0238).
///
/// Exactly one entry per supplied value, each a bounded non-empty identifier
/// and none repeated, so a list of none answers a count of none. Anything
/// else refuses the crossing rather than being trimmed to fit: a list that
/// disagrees with the count is the browser and this process disagreeing about
/// which value goes where, and a placement read from it could put a value
/// into a field it was not minted for. These are observation node ids and
/// never values (decision 0063 section 4).
pub(super) fn supplied_field_node_ids_from_wire(
    ids: &[String],
    supplied: u32,
) -> Option<FieldNodeIds> {
    if u32::try_from(ids.len()).ok()? != supplied || !ids.iter().all(|id| valid_identifier(id)) {
        return None;
    }
    FieldNodeIds::new(ids.iter().cloned().map(SemanticNodeId::new).collect()).ok()
}

pub(super) fn settlement_identities(
    task_id: &str,
    revision: u64,
    kind: wire::TaskSettlementKind,
) -> Option<(String, String, String)> {
    core_runtime::effect_identity::task_settlement_identities(
        &ChromiumDigest,
        task_id,
        revision,
        kind as u32,
    )
    .ok()
}

pub(super) fn task_effect_identities(
    scope: core_runtime::effect_identity::TaskEffectIdentityScope<'_>,
    task_id: &str,
    revision: u64,
    parent_operation_id: &str,
    batch_ordinal: u32,
    kind: wire::TaskReducerEffectKind,
) -> Option<(String, String, String)> {
    core_runtime::effect_identity::task_effect_identities(
        &ChromiumDigest,
        scope,
        task_id,
        revision,
        parent_operation_id,
        batch_ordinal,
        kind as u32,
    )
    .ok()
}

pub(super) fn model_attempt_identities(
    root_effect_id: &str,
    attempt_ordinal: u32,
) -> Option<(String, String, String)> {
    core_runtime::effect_identity::model_attempt_identities(
        &ChromiumDigest,
        root_effect_id,
        attempt_ordinal,
    )
    .ok()
}

pub(super) fn valid_identifier(value: &str) -> bool {
    !value.is_empty() && value.len() <= wire::MAX_IDENTIFIER_BYTES
}

pub(super) fn is_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}
