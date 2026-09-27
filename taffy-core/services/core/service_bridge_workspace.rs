// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Workspace command and persistence projections for the CXX boundary.

use core_runtime::wire;
use core_runtime::WorkspaceExportFormat;
use core_runtime::{
    MemoryScopeInput, MemorySensitivityInput, MemoryUserUpsert, MemoryWorkspaceInput,
};

use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

mod command;
mod completion;
mod effect;

pub(crate) use completion::deliver_workspace_completion;
use completion::{
    export_error_status, library_error_status, memory_error_status, store_error_status,
};
pub(crate) use effect::{workspace_effect_response, WorkspaceCommandEffect};

#[allow(non_snake_case)]
pub(crate) fn SubmitWorkspace(
    bridge: &mut ServiceBridge,
    command: ffi::BridgeWorkspaceCommand,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = command.operation.operation_id.clone();
    let Some(command) = command::to_wire(command) else {
        return response(&operation_id, 5, Vec::new());
    };
    if !command::valid_fields(&command) {
        return response(&operation_id, 5, Vec::new());
    }
    if let Err(status) = command::validate_operation(
        &command.operation,
        bridge.generation.value(),
        now_monotonic_ms,
    ) {
        return response(&operation_id, status, Vec::new());
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(&operation_id, 6, Vec::new());
    };
    runtime.set_utc_millis(now_utc_millis);
    let result = match command.kind {
        wire::CoreServiceCommandKind::CorrectWorkspaceFact => {
            let Some(body) = command.correct_workspace_fact else {
                return response(&operation_id, 5, Vec::new());
            };
            runtime
                .core_mut()
                .begin_workspace_correction(
                    operation_id.clone(),
                    &body.workspace_id,
                    body.expected_revision,
                    &body.fact_id,
                    body.value,
                    now_utc_millis,
                )
                .map(WorkspaceCommandEffect::Persist)
                .map_err(store_error_status)
        }
        wire::CoreServiceCommandKind::ExcludeWorkspaceSource => {
            let Some(body) = command.exclude_workspace_source else {
                return response(&operation_id, 5, Vec::new());
            };
            runtime
                .core_mut()
                .begin_workspace_exclusion(
                    operation_id.clone(),
                    &body.workspace_id,
                    body.expected_revision,
                    &body.source_id,
                    now_utc_millis,
                )
                .map(WorkspaceCommandEffect::Persist)
                .map_err(store_error_status)
        }
        wire::CoreServiceCommandKind::RequestWorkspaceExport => {
            let Some(body) = command.request_workspace_export else {
                return response(&operation_id, 5, Vec::new());
            };
            let format = match body.format {
                wire::WorkspaceExportFormat::Markdown => WorkspaceExportFormat::Markdown,
                wire::WorkspaceExportFormat::Csv => WorkspaceExportFormat::Csv,
            };
            return match runtime.core_mut().request_workspace_export(
                &body.request_id,
                &body.workspace_id,
                body.expected_revision,
                format,
            ) {
                Ok(_) => response_after_change(bridge, response(&operation_id, 0, Vec::new())),
                Err(error) => response(&operation_id, export_error_status(&error), Vec::new()),
            };
        }
        wire::CoreServiceCommandKind::SaveWorkspace => {
            let Some(body) = command.save_workspace else {
                return response(&operation_id, 5, Vec::new());
            };
            runtime
                .core_mut()
                .begin_workspace_save(
                    operation_id.clone(),
                    &body.workspace_id,
                    body.expected_revision,
                    now_utc_millis,
                )
                .map(WorkspaceCommandEffect::Persist)
                .map_err(store_error_status)
        }
        wire::CoreServiceCommandKind::RenameWorkspace => {
            let Some(body) = command.rename_workspace else {
                return response(&operation_id, 5, Vec::new());
            };
            runtime
                .core_mut()
                .begin_workspace_rename(
                    operation_id.clone(),
                    &body.workspace_id,
                    body.expected_revision,
                    body.display_name,
                    now_utc_millis,
                )
                .map(WorkspaceCommandEffect::Persist)
                .map_err(store_error_status)
        }
        wire::CoreServiceCommandKind::DeleteWorkspace => {
            let Some(body) = command.delete_workspace else {
                return response(&operation_id, 5, Vec::new());
            };
            let preview = match runtime
                .core()
                .preview_workspace_deletion(&body.workspace_id)
            {
                Ok(preview) => preview,
                Err(error) => {
                    return response(&operation_id, store_error_status(error), Vec::new());
                }
            };
            if preview.expected_revision != body.expected_revision {
                return response(
                    &operation_id,
                    wire::AdmissionStatus::StaleRevision as u8,
                    Vec::new(),
                );
            }
            if preview.confirmation_token != body.confirmation_token {
                return response(
                    &operation_id,
                    wire::AdmissionStatus::InvalidCommand as u8,
                    Vec::new(),
                );
            }
            runtime
                .core_mut()
                .begin_workspace_deletion(operation_id.clone(), &preview, now_utc_millis)
                .map(WorkspaceCommandEffect::Delete)
                .map_err(store_error_status)
        }
        wire::CoreServiceCommandKind::DiscardWorkspace => {
            let Some(body) = command.discard_workspace else {
                return response(&operation_id, 5, Vec::new());
            };
            runtime
                .core_mut()
                .begin_workspace_discard(
                    operation_id.clone(),
                    &body.workspace_id,
                    body.expected_revision,
                    now_utc_millis,
                )
                .map(WorkspaceCommandEffect::Delete)
                .map_err(store_error_status)
        }
        wire::CoreServiceCommandKind::SearchLibrary => {
            let Some(body) = command.search_library else {
                return response(&operation_id, 5, Vec::new());
            };
            return match runtime.core_mut().search_library(
                &body.request_id,
                &body.query,
                body.limit,
                body.requested_at_epoch_ms,
            ) {
                Ok(_) => response_after_change(bridge, response(&operation_id, 0, Vec::new())),
                Err(error) => response(&operation_id, library_error_status(error), Vec::new()),
            };
        }
        wire::CoreServiceCommandKind::SaveLibraryFact => {
            let Some(body) = command.save_library_fact else {
                return response(&operation_id, 5, Vec::new());
            };
            match runtime.core_mut().begin_library_save(
                operation_id.clone(),
                &body.workspace_id,
                body.expected_workspace_revision,
                &body.fact_id,
                body.expected_library_revision,
                body.expected_entry_revision,
                body.approved_at_epoch_ms,
            ) {
                Ok(Some(effect)) => Ok(WorkspaceCommandEffect::LibrarySave(effect)),
                Ok(None) => return response(&operation_id, 0, Vec::new()),
                Err(error) => {
                    return response(&operation_id, library_error_status(error), Vec::new());
                }
            }
        }
        wire::CoreServiceCommandKind::RemoveLibraryEntry => {
            let Some(body) = command.remove_library_entry else {
                return response(&operation_id, 5, Vec::new());
            };
            runtime
                .core_mut()
                .begin_library_remove(
                    operation_id.clone(),
                    &body.entry_id,
                    body.expected_library_revision,
                    body.expected_entry_revision,
                    body.removed_at_epoch_ms,
                )
                .map(WorkspaceCommandEffect::LibraryRemove)
                .map_err(library_error_status)
        }
        wire::CoreServiceCommandKind::RequestLibraryExport => {
            let Some(body) = command.request_library_export else {
                return response(&operation_id, 5, Vec::new());
            };
            let format = match body.format {
                wire::WorkspaceExportFormat::Markdown => WorkspaceExportFormat::Markdown,
                wire::WorkspaceExportFormat::Csv => WorkspaceExportFormat::Csv,
            };
            return match runtime.core_mut().request_library_export(
                &body.request_id,
                body.expected_library_revision,
                body.collection_id.as_deref(),
                format,
            ) {
                Ok(_) => response_after_change(bridge, response(&operation_id, 0, Vec::new())),
                Err(error) => response(&operation_id, library_error_status(error), Vec::new()),
            };
        }
        wire::CoreServiceCommandKind::SearchMemory => {
            let Some(body) = command.search_memory else {
                return response(&operation_id, 5, Vec::new());
            };
            return match runtime.core_mut().search_memory_for_person(
                &body.request_id,
                &body.query,
                body.limit,
                body.requested_at_epoch_ms,
            ) {
                Ok(()) => response_after_change(bridge, response(&operation_id, 0, Vec::new())),
                Err(error) => response(&operation_id, memory_error_status(error), Vec::new()),
            };
        }
        wire::CoreServiceCommandKind::UpsertMemory => {
            let Some(body) = command.upsert_memory else {
                return response(&operation_id, 5, Vec::new());
            };
            let scope = match body.scope_kind {
                wire::MemoryScopeKind::AllTasks => MemoryScopeInput::AllTasks,
                wire::MemoryScopeKind::Workspace => {
                    let Some(workspace) = body.scope_workspace else {
                        return response(&operation_id, 5, Vec::new());
                    };
                    MemoryScopeInput::Workspace(MemoryWorkspaceInput {
                        workspace_id: workspace.workspace_id,
                        display_name: workspace.display_name,
                    })
                }
            };
            let sensitivity = match body.sensitivity {
                wire::MemorySensitivity::Standard => MemorySensitivityInput::Standard,
                wire::MemorySensitivity::Sensitive => MemorySensitivityInput::Sensitive,
            };
            match runtime.core_mut().begin_user_memory_upsert(
                operation_id.clone(),
                MemoryUserUpsert {
                    memory_id: body.memory_id,
                    statement: body.statement,
                    scope,
                    sensitivity,
                    expected_memory_revision: body.expected_memory_revision,
                    expected_record_revision: body.expected_record_revision,
                    expires_at_epoch_ms: (body.expires_at_epoch_ms != 0)
                        .then_some(body.expires_at_epoch_ms),
                    approved_at_epoch_ms: body.approved_at_epoch_ms,
                },
            ) {
                Ok(Some(effect)) => Ok(WorkspaceCommandEffect::MemorySave(effect)),
                Ok(None) => return response(&operation_id, 0, Vec::new()),
                Err(error) => {
                    return response(&operation_id, memory_error_status(error), Vec::new());
                }
            }
        }
        wire::CoreServiceCommandKind::DeleteMemory => {
            let Some(body) = command.delete_memory else {
                return response(&operation_id, 5, Vec::new());
            };
            runtime
                .core_mut()
                .begin_memory_delete(
                    operation_id.clone(),
                    &body.memory_id,
                    body.expected_memory_revision,
                    body.expected_record_revision,
                    body.deleted_at_epoch_ms,
                )
                .map(WorkspaceCommandEffect::MemoryDelete)
                .map_err(memory_error_status)
        }
        _ => return response(&operation_id, 5, Vec::new()),
    };
    match result {
        // Like the four siblings in this same `match` that already do it. A
        // bridge that changes core state and answers a bare response compiles,
        // passes every lane, and is silent — a surface reads `CoreStatus`, and
        // `CoreStatus` reaches it only through here.
        //
        // Expect no visible change from this today, and do not delete it as a
        // no-op when you find that out: `begin_discard` inserts into
        // `pending_deletions` and touches no projected fact, `canDiscard` reads
        // the projected snapshot only, and the Android repository additionally
        // collapses on the projection version. It is load-bearing the day any
        // pending fact reaches the wire, and it is what makes the withdrawal
        // rule apply to this branch — which is why the sweep above lands with
        // it rather than after it.
        Ok(effect) => response_after_change(
            bridge,
            workspace_effect_response(operation_id, command.operation, effect),
        ),
        Err(status) => response(&operation_id, status, Vec::new()),
    }
}
