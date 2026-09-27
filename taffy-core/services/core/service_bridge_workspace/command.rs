// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed conversion and shape checks for workspace commands at the CXX seam.

use core_runtime::wire;

use crate::ffi;

pub(super) fn to_wire(value: ffi::BridgeWorkspaceCommand) -> Option<wire::CoreServiceCommand> {
    let kind = wire::CoreServiceCommandKind::from_wire(u32::from(value.kind))?;
    let mut command = wire::CoreServiceCommand {
        operation: operation_to_wire(value.operation),
        kind,
        start_task: None,
        cancel_task: None,
        user_decision: None,
        auth_callback: None,
        permission_result: None,
        start_auth: None,
        request_email_link: None,
        sign_out: None,
        auth_credential_result: None,
        correct_workspace_fact: None,
        exclude_workspace_source: None,
        request_workspace_export: None,
        set_asset_delivery_policy: None,
        request_asset: None,
        remove_asset: None,
        save_provider_credential: None,
        forget_provider_credential: None,
        start_provider_auth: None,
        provider_auth_callback: None,
        save_custom_provider: None,
        remove_custom_provider: None,
        complete_handover: None,
        expire_handover: None,
        supply_user_input: None,
        follow_up: None,
        supply_field_values: None,
        set_provider_credential_state: None,
        probe_provider_credential: None,
        set_provider_model_preference: None,
        probe_custom_endpoint: None,
        request_composer_completion: None,
        cancel_composer_completion: None,
        pause_task: None,
        resume_task: None,
        take_over: None,
        set_assistant_configuration: None,
        save_workspace: None,
        rename_workspace: None,
        delete_workspace: None,
        discard_workspace: None,
        search_library: None,
        save_library_fact: None,
        remove_library_entry: None,
        request_library_export: None,
        search_memory: None,
        upsert_memory: None,
        delete_memory: None,
        accept_task_artifact: None,
        export_task_artifact: None,
        replace_saved_data_snapshot: None,
        mutate_skill: None,
        cancel_provider_auth: None,
    };
    match kind {
        wire::CoreServiceCommandKind::CorrectWorkspaceFact => {
            command.correct_workspace_fact = Some(wire::CorrectWorkspaceFactCommand {
                workspace_id: value.workspace_id,
                expected_revision: value.expected_revision,
                fact_id: value.fact_id,
                value: value.value,
            });
        }
        wire::CoreServiceCommandKind::ExcludeWorkspaceSource => {
            command.exclude_workspace_source = Some(wire::ExcludeWorkspaceSourceCommand {
                workspace_id: value.workspace_id,
                expected_revision: value.expected_revision,
                source_id: value.source_id,
            });
        }
        wire::CoreServiceCommandKind::RequestWorkspaceExport => {
            command.request_workspace_export = Some(wire::RequestWorkspaceExportCommand {
                request_id: value.request_id,
                workspace_id: value.workspace_id,
                expected_revision: value.expected_revision,
                format: wire::WorkspaceExportFormat::from_wire(u32::from(value.format))?,
            });
        }
        wire::CoreServiceCommandKind::SaveWorkspace => {
            command.save_workspace = Some(wire::SaveWorkspaceCommand {
                workspace_id: value.workspace_id,
                expected_revision: value.expected_revision,
            });
        }
        wire::CoreServiceCommandKind::RenameWorkspace => {
            command.rename_workspace = Some(wire::RenameWorkspaceCommand {
                workspace_id: value.workspace_id,
                expected_revision: value.expected_revision,
                display_name: value.display_name,
            });
        }
        wire::CoreServiceCommandKind::DeleteWorkspace => {
            command.delete_workspace = Some(wire::DeleteWorkspaceCommand {
                workspace_id: value.workspace_id,
                expected_revision: value.expected_revision,
                confirmation_token: value.confirmation_token,
            });
        }
        wire::CoreServiceCommandKind::DiscardWorkspace => {
            command.discard_workspace = Some(wire::DiscardWorkspaceCommand {
                workspace_id: value.workspace_id,
                expected_revision: value.expected_revision,
            });
        }
        wire::CoreServiceCommandKind::SearchLibrary => {
            command.search_library = Some(wire::SearchLibraryCommand {
                request_id: value.request_id,
                query: value.query,
                limit: value.limit,
                requested_at_epoch_ms: value.requested_at_epoch_ms,
            });
        }
        wire::CoreServiceCommandKind::SaveLibraryFact => {
            command.save_library_fact = Some(wire::SaveLibraryFactCommand {
                workspace_id: value.workspace_id,
                expected_workspace_revision: value.expected_workspace_revision,
                fact_id: value.fact_id,
                expected_library_revision: value.expected_library_revision,
                expected_entry_revision: value.expected_entry_revision,
                approved_at_epoch_ms: value.approved_at_epoch_ms,
            });
        }
        wire::CoreServiceCommandKind::RemoveLibraryEntry => {
            command.remove_library_entry = Some(wire::RemoveLibraryEntryCommand {
                entry_id: value.entry_id,
                expected_library_revision: value.expected_library_revision,
                expected_entry_revision: value.expected_entry_revision,
                removed_at_epoch_ms: value.removed_at_epoch_ms,
            });
        }
        wire::CoreServiceCommandKind::RequestLibraryExport => {
            command.request_library_export = Some(wire::RequestLibraryExportCommand {
                request_id: value.request_id,
                expected_library_revision: value.expected_library_revision,
                collection_id: value.has_collection_id.then_some(value.collection_id),
                format: wire::WorkspaceExportFormat::from_wire(u32::from(value.format))?,
            });
        }
        wire::CoreServiceCommandKind::SearchMemory => {
            command.search_memory = Some(wire::SearchMemoryCommand {
                request_id: value.request_id,
                query: value.query,
                limit: value.limit,
                requested_at_epoch_ms: value.requested_at_epoch_ms,
            });
        }
        wire::CoreServiceCommandKind::UpsertMemory => {
            command.upsert_memory = Some(wire::UpsertMemoryCommand {
                memory_id: value.has_memory_id.then_some(value.memory_id),
                statement: value.memory_statement,
                scope_kind: wire::MemoryScopeKind::from_wire(u32::from(value.memory_scope_kind))?,
                scope_workspace: value.has_memory_scope_workspace.then_some(
                    wire::MemoryWorkspaceRecord {
                        workspace_id: value.memory_scope_workspace_id,
                        display_name: value.memory_scope_workspace_name,
                    },
                ),
                sensitivity: wire::MemorySensitivity::from_wire(u32::from(
                    value.memory_sensitivity,
                ))?,
                expected_memory_revision: value.expected_memory_revision,
                expected_record_revision: value.expected_record_revision,
                expires_at_epoch_ms: value.memory_expires_at_epoch_ms,
                approved_at_epoch_ms: value.memory_approved_at_epoch_ms,
            });
        }
        wire::CoreServiceCommandKind::DeleteMemory => {
            command.delete_memory = Some(wire::DeleteMemoryCommand {
                memory_id: value.memory_id,
                expected_memory_revision: value.expected_memory_revision,
                expected_record_revision: value.expected_record_revision,
                deleted_at_epoch_ms: value.memory_deleted_at_epoch_ms,
            });
        }
        _ => return None,
    }
    command.has_valid_body().then_some(command)
}

pub(super) fn validate_operation(
    operation: &wire::OperationEnvelope,
    generation: u64,
    now_monotonic_ms: u64,
) -> Result<(), u8> {
    if operation.service_generation != generation {
        return Err(wire::AdmissionStatus::StaleGeneration as u8);
    }
    if operation.task_revision != 0
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

pub(super) fn valid_fields(command: &wire::CoreServiceCommand) -> bool {
    match command.kind {
        wire::CoreServiceCommandKind::CorrectWorkspaceFact => {
            command.correct_workspace_fact.as_ref().is_some_and(|body| {
                valid_identifier(&body.workspace_id)
                    && valid_identifier(&body.fact_id)
                    && !body.value.is_empty()
                    && body.value.len() <= wire::MAX_WORKSPACE_VALUE_BYTES
            })
        }
        wire::CoreServiceCommandKind::ExcludeWorkspaceSource => command
            .exclude_workspace_source
            .as_ref()
            .is_some_and(|body| {
                valid_identifier(&body.workspace_id) && valid_identifier(&body.source_id)
            }),
        wire::CoreServiceCommandKind::RequestWorkspaceExport => command
            .request_workspace_export
            .as_ref()
            .is_some_and(|body| {
                valid_identifier(&body.request_id) && valid_identifier(&body.workspace_id)
            }),
        wire::CoreServiceCommandKind::SaveWorkspace => command
            .save_workspace
            .as_ref()
            .is_some_and(|body| valid_identifier(&body.workspace_id)),
        wire::CoreServiceCommandKind::RenameWorkspace => {
            command.rename_workspace.as_ref().is_some_and(|body| {
                valid_identifier(&body.workspace_id)
                    && !body.display_name.is_empty()
                    && body.display_name.len() <= wire::MAX_WORKSPACE_DISPLAY_NAME_BYTES
                    && body.display_name.trim() == body.display_name
                    && !body.display_name.chars().any(char::is_control)
            })
        }
        wire::CoreServiceCommandKind::DeleteWorkspace => {
            command.delete_workspace.as_ref().is_some_and(|body| {
                valid_identifier(&body.workspace_id)
                    && valid_confirmation_token(&body.confirmation_token)
            })
        }
        wire::CoreServiceCommandKind::DiscardWorkspace => command
            .discard_workspace
            .as_ref()
            .is_some_and(|body| valid_identifier(&body.workspace_id)),
        wire::CoreServiceCommandKind::SearchLibrary => {
            command.search_library.as_ref().is_some_and(|body| {
                valid_identifier(&body.request_id)
                    && !body.query.trim().is_empty()
                    && body.query.len() <= wire::MAX_LIBRARY_QUERY_BYTES
                    && body.limit > 0
                    && usize::try_from(body.limit)
                        .is_ok_and(|limit| limit <= wire::MAX_LIBRARY_SEARCH_RESULTS)
            })
        }
        wire::CoreServiceCommandKind::SaveLibraryFact => {
            command.save_library_fact.as_ref().is_some_and(|body| {
                valid_identifier(&body.workspace_id) && valid_identifier(&body.fact_id)
            })
        }
        wire::CoreServiceCommandKind::RemoveLibraryEntry => command
            .remove_library_entry
            .as_ref()
            .is_some_and(|body| valid_identifier(&body.entry_id)),
        wire::CoreServiceCommandKind::RequestLibraryExport => {
            command.request_library_export.as_ref().is_some_and(|body| {
                valid_identifier(&body.request_id)
                    && body.collection_id.as_deref().is_none_or(valid_identifier)
            })
        }
        wire::CoreServiceCommandKind::SearchMemory => {
            command.search_memory.as_ref().is_some_and(|body| {
                valid_identifier(&body.request_id)
                    && valid_text(&body.query, wire::MAX_MEMORY_QUERY_BYTES)
                    && body.limit > 0
                    && usize::try_from(body.limit)
                        .is_ok_and(|limit| limit <= wire::MAX_MEMORY_SEARCH_RESULTS)
            })
        }
        wire::CoreServiceCommandKind::UpsertMemory => {
            command.upsert_memory.as_ref().is_some_and(|body| {
                body.memory_id.as_deref().is_none_or(valid_memory_id)
                    && valid_text(&body.statement, wire::MAX_MEMORY_STATEMENT_BYTES)
                    && valid_memory_scope(body.scope_kind, body.scope_workspace.as_ref())
                    && (body.expires_at_epoch_ms == 0
                        || body.expires_at_epoch_ms > body.approved_at_epoch_ms)
            })
        }
        wire::CoreServiceCommandKind::DeleteMemory => command
            .delete_memory
            .as_ref()
            .is_some_and(|body| valid_memory_id(&body.memory_id)),
        _ => false,
    }
}

fn valid_identifier(value: &str) -> bool {
    !value.is_empty() && value.len() <= wire::MAX_IDENTIFIER_BYTES
}

fn valid_confirmation_token(value: &str) -> bool {
    value.len() == wire::MAX_WORKSPACE_CONFIRMATION_TOKEN_BYTES
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn valid_memory_id(value: &str) -> bool {
    value.len() == 32
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn valid_memory_scope(
    kind: wire::MemoryScopeKind,
    workspace: Option<&wire::MemoryWorkspaceRecord>,
) -> bool {
    match kind {
        wire::MemoryScopeKind::AllTasks => workspace.is_none(),
        wire::MemoryScopeKind::Workspace => workspace.is_some_and(|workspace| {
            valid_memory_id(&workspace.workspace_id)
                && valid_text(
                    &workspace.display_name,
                    wire::MAX_WORKSPACE_DISPLAY_NAME_BYTES,
                )
        }),
    }
}

fn valid_text(value: &str, maximum: usize) -> bool {
    !value.is_empty()
        && value.len() <= maximum
        && value.trim() == value
        && !value.chars().any(char::is_control)
}

fn operation_to_wire(value: ffi::BridgeWorkspaceOperation) -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: value.operation_id,
        service_generation: value.service_generation,
        task_revision: value.task_revision,
        deadline_monotonic_ms: value.deadline_monotonic_ms,
        idempotency_key: value.idempotency_key,
    }
}
