// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable storage completion and refusal mapping for workspace-owned planes.

use core_runtime::wire;
use core_runtime::{WorkspaceExportError, WorkspaceMutationError, WorkspaceStoreError};

use crate::ffi;
use crate::service_bridge_runtime::{note_refusal, response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

/// Refuses this completion and names the branch that refused it.
///
/// Seven branches here answered `kInvalidCommand`, and the browser logged
/// `[taffy_core_invalid_command] at=storage-completion label=` with nothing
/// after the equals sign, because none of them recorded a name. A person
/// confirming "Discard temporary workspace" saw the dialog close, the button
/// stay, and a row reading "Working on it…" that never resolved — and the only
/// way to tell which of the seven had refused was to read them all. Decision
/// 0136 section 1 is the rule this now keeps.
fn invalid(
    bridge: &mut ServiceBridge,
    operation_id: &str,
    label: &'static str,
) -> ffi::BridgeResponse {
    note_refusal(bridge, label);
    response(
        operation_id,
        wire::AdmissionStatus::InvalidCommand as u8,
        Vec::new(),
    )
}

pub(crate) fn deliver_workspace_completion(
    bridge: &mut ServiceBridge,
    completion: ffi::BridgeStorageCompletion,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let operation_id = completion.operation.operation_id.clone();
    if let Some(pending) = bridge.pending_task_effects.get(&completion.effect_id) {
        if pending.durable_storage_pending {
            return match pending.durable_storage_kind() {
                Some(wire::TaskReducerEffectKind::RunLibraryTool) => {
                    crate::service_bridge_task_effect::deliver_library_storage_completion(
                        bridge,
                        completion,
                        now_monotonic_ms,
                    )
                }
                Some(wire::TaskReducerEffectKind::RunMemoryTool) => {
                    crate::service_bridge_task_effect::deliver_memory_storage_completion(
                        bridge,
                        completion,
                        now_monotonic_ms,
                    )
                }
                _ => return invalid(bridge, &operation_id, "durable_storage_kind"),
            };
        }
    }
    if completion.operation.service_generation != bridge.generation.value() {
        return invalid(bridge, &operation_id, "workspace_completion_generation");
    }
    if completion.operation.task_revision != 0 {
        return invalid(bridge, &operation_id, "workspace_completion_task_revision");
    }
    if completion.effect_id != operation_id {
        return invalid(bridge, &operation_id, "workspace_completion_effect_id");
    }
    if wire::EffectStatus::from_wire(u32::from(completion.status)).is_none() {
        return invalid(bridge, &operation_id, "workspace_completion_status");
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        note_refusal(bridge, "workspace_completion_no_runtime");
        return response(&operation_id, 6, Vec::new());
    };
    if completion.status == wire::EffectStatus::Completed as u8 {
        // Four planes share one durable operation identity, so the completion
        // is offered to each until one claims it. `WrongCompletion` means "not
        // mine"; any other error is the plane that *is* waiting saying the
        // commit does not match what it staged, and that is the answer worth
        // keeping — it used to be discarded, and the discard a person
        // confirmed then sat on "Working on it…" for ever with no way to see
        // which of the four had spoken.
        let mut refusal: Option<&'static str> = None;
        let persist = runtime
            .core_mut()
            .complete_workspace_persist(&operation_id, completion.committed_revision);
        let workspace_completed = match persist {
            Ok(()) => true,
            Err(WorkspaceStoreError::WrongCompletion) => {
                match runtime
                    .core_mut()
                    .complete_workspace_deletion(&operation_id, completion.committed_revision)
                {
                    Ok(()) => true,
                    Err(WorkspaceStoreError::WrongCompletion) => false,
                    Err(_) => {
                        refusal = Some("workspace_deletion_completion");
                        false
                    }
                }
            }
            Err(_) => {
                refusal = Some("workspace_persist_completion");
                false
            }
        };
        let completed = workspace_completed
            || runtime
                .core_mut()
                .complete_library_mutation(&operation_id, completion.committed_revision)
                .is_ok()
            || runtime
                .core_mut()
                .complete_memory_mutation(&operation_id, completion.committed_revision)
                .is_ok();
        return if completed {
            response_after_change(bridge, response(&operation_id, 0, Vec::new()))
        } else {
            // Nothing claimed it: the browser committed a transaction no plane
            // in this core is waiting for.
            invalid(
                bridge,
                &operation_id,
                refusal.unwrap_or("workspace_completion_unclaimed"),
            )
        };
    }
    if runtime.core_mut().reject_workspace_persist(&operation_id)
        || runtime.core_mut().reject_workspace_deletion(&operation_id)
        || runtime.core_mut().reject_library_mutation(&operation_id)
        || runtime.core_mut().reject_memory_mutation(&operation_id)
    {
        note_refusal(bridge, "workspace_completion_rejected");
        response(&operation_id, 6, Vec::new())
    } else {
        invalid(bridge, &operation_id, "workspace_completion_unclaimed_refusal")
    }
}

pub(super) const fn store_error_status(error: WorkspaceStoreError) -> u8 {
    match error {
        WorkspaceStoreError::Mutation(WorkspaceMutationError::StaleRevision) => {
            wire::AdmissionStatus::StaleRevision as u8
        }
        WorkspaceStoreError::StaleRevision => wire::AdmissionStatus::StaleRevision as u8,
        WorkspaceStoreError::TooManyPendingMutations => wire::AdmissionStatus::Backpressure as u8,
        WorkspaceStoreError::OperationAlreadyPending => wire::AdmissionStatus::Duplicate as u8,
        // A digest the core could not produce is the core being unable, not
        // the command being wrong, which is what `library_error_status` and
        // `memory_error_status` already say for the same error. It was the one
        // of the three that did not, so a person was told they had asked for
        // something invalid.
        WorkspaceStoreError::DigestUnavailable => wire::AdmissionStatus::CoreUnavailable as u8,
        _ => wire::AdmissionStatus::InvalidCommand as u8,
    }
}

pub(super) const fn library_error_status(error: core_runtime::LibraryStoreError) -> u8 {
    match error {
        core_runtime::LibraryStoreError::StaleLibraryRevision
        | core_runtime::LibraryStoreError::StaleEntryRevision
        | core_runtime::LibraryStoreError::StaleWorkspaceRevision => {
            wire::AdmissionStatus::StaleRevision as u8
        }
        core_runtime::LibraryStoreError::OperationAlreadyPending => {
            wire::AdmissionStatus::Duplicate as u8
        }
        core_runtime::LibraryStoreError::TooManyEntries
        | core_runtime::LibraryStoreError::TooManyPendingMutations => {
            wire::AdmissionStatus::Backpressure as u8
        }
        core_runtime::LibraryStoreError::PrivateProfile => {
            wire::AdmissionStatus::InvalidCommand as u8
        }
        core_runtime::LibraryStoreError::DigestUnavailable => {
            wire::AdmissionStatus::CoreUnavailable as u8
        }
        _ => wire::AdmissionStatus::InvalidCommand as u8,
    }
}

pub(super) const fn memory_error_status(error: core_runtime::MemoryStoreError) -> u8 {
    match error {
        core_runtime::MemoryStoreError::StaleMemoryRevision
        | core_runtime::MemoryStoreError::StaleRecordRevision => {
            wire::AdmissionStatus::StaleRevision as u8
        }
        core_runtime::MemoryStoreError::OperationAlreadyPending => {
            wire::AdmissionStatus::Duplicate as u8
        }
        core_runtime::MemoryStoreError::TooManyRecords
        | core_runtime::MemoryStoreError::TooManyPendingMutations => {
            wire::AdmissionStatus::Backpressure as u8
        }
        core_runtime::MemoryStoreError::DigestUnavailable => {
            wire::AdmissionStatus::CoreUnavailable as u8
        }
        core_runtime::MemoryStoreError::PrivateProfile
        | core_runtime::MemoryStoreError::InvalidIdentifier
        | core_runtime::MemoryStoreError::InvalidRecord
        | core_runtime::MemoryStoreError::InvalidQuery
        | core_runtime::MemoryStoreError::UnknownRecord
        | core_runtime::MemoryStoreError::UnknownWorkspace
        | core_runtime::MemoryStoreError::WrongCompletion => {
            wire::AdmissionStatus::InvalidCommand as u8
        }
    }
}

pub(super) const fn export_error_status(error: &WorkspaceExportError) -> u8 {
    match error {
        WorkspaceExportError::StaleRevision => wire::AdmissionStatus::StaleRevision as u8,
        _ => wire::AdmissionStatus::InvalidCommand as u8,
    }
}
