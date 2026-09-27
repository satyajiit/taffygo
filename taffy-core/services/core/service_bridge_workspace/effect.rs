// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Typed workspace, Library, and Memory effects for the browser boundary.

use core_runtime::wire;

use crate::ffi;

pub(crate) enum WorkspaceCommandEffect {
    Persist(core_runtime::WorkspacePersistRequest),
    Delete(core_runtime::WorkspaceDeleteRequest),
    LibrarySave(core_runtime::LibrarySaveRequest),
    LibraryRemove(core_runtime::LibraryRemoveRequest),
    MemorySave(core_runtime::MemorySaveRequest),
    MemoryDelete(core_runtime::MemoryDeleteRequest),
}

pub(crate) fn workspace_effect_response(
    operation_id: String,
    operation: wire::OperationEnvelope,
    effect: WorkspaceCommandEffect,
) -> ffi::BridgeResponse {
    let effect = match effect {
        WorkspaceCommandEffect::Persist(effect) => {
            let mut output = blank_workspace_effect(
                operation,
                effect.operation_id,
                wire::StorageOperation::UpsertWorkspace,
            );
            output.workspace_id = effect.workspace_id;
            output.expected_revision = effect.expected_revision;
            output.resulting_revision = effect.resulting_revision;
            output.snapshot = effect.snapshot;
            output
        }
        WorkspaceCommandEffect::Delete(effect) => {
            let mut output = blank_workspace_effect(
                operation,
                effect.operation_id,
                wire::StorageOperation::DeleteWorkspace,
            );
            output.workspace_id = effect.workspace_id;
            output.expected_revision = effect.expected_revision;
            output.resulting_revision = effect.resulting_revision;
            output.sources = effect.counts.sources;
            output.facts = effect.counts.facts;
            output.artifact_metadata = effect.counts.artifact_metadata;
            output.derived_indexes = effect.counts.derived_indexes;
            output.confirmation_token = effect.confirmation_token;
            output
        }
        WorkspaceCommandEffect::LibrarySave(effect) => {
            let mut output = blank_workspace_effect(
                operation,
                effect.operation_id,
                wire::StorageOperation::UpsertLibraryEntry,
            );
            output.expected_revision = effect.expected_library_revision;
            output.resulting_revision = effect.resulting_library_revision;
            output.expected_entry_revision = effect.expected_entry_revision;
            output.resulting_entry_revision = effect.entry.revision;
            output.entry_id = effect.entry.entry_id;
            output.collection_id = effect.entry.collection_id;
            output.collection_name = effect.entry.collection_name;
            output.source_workspace_id = effect.entry.source_workspace_id;
            output.source_workspace_revision = effect.entry.source_workspace_revision;
            output.source_fact_id = effect.entry.source_fact_id;
            output.field = effect.entry.field;
            output.original_value = effect.entry.original_value;
            output.has_correction = effect.entry.correction.is_some();
            output.correction = effect.entry.correction.unwrap_or_default();
            output.library_fact_kind = effect.entry.kind as u8;
            output.library_sources = effect
                .entry
                .sources
                .into_iter()
                .map(|source| ffi::BridgeLibrarySource {
                    source_id: source.source_id,
                    title: source.title,
                    host: source.host,
                    observed_at_epoch_ms: source.observed_at_epoch_ms,
                })
                .collect();
            output.captured_at_epoch_ms = effect.entry.captured_at_epoch_ms;
            output.last_checked_epoch_ms = effect.entry.last_checked_epoch_ms;
            output.has_conflict = effect.entry.has_conflict;
            output
        }
        WorkspaceCommandEffect::LibraryRemove(effect) => {
            let mut output = blank_workspace_effect(
                operation,
                effect.operation_id,
                wire::StorageOperation::RemoveLibraryEntry,
            );
            output.expected_revision = effect.expected_library_revision;
            output.resulting_revision = effect.resulting_library_revision;
            output.entry_id = effect.entry_id;
            output.expected_entry_revision = effect.expected_entry_revision;
            output.resulting_entry_revision = effect.resulting_entry_revision;
            output.removed_at_epoch_ms = effect.removed_at_epoch_ms;
            output
        }
        WorkspaceCommandEffect::MemorySave(effect) => {
            let mut output = blank_workspace_effect(
                operation,
                effect.operation_id,
                wire::StorageOperation::UpsertMemory,
            );
            output.expected_revision = effect.expected_memory_revision;
            output.resulting_revision = effect.resulting_memory_revision;
            output.expected_entry_revision = effect.expected_record_revision;
            output.resulting_entry_revision = effect.record.revision;
            output.memory_id = effect.record.memory_id;
            output.memory_statement = effect.record.statement;
            output.memory_source_kind = effect.record.source_kind as u8;
            output.has_memory_source_task_id = effect.record.source_task_id.is_some();
            output.memory_source_task_id = effect.record.source_task_id.unwrap_or_default();
            if let Some(workspace) = effect.record.source_workspace {
                output.has_memory_source_workspace = true;
                output.memory_source_workspace_id = workspace.workspace_id;
                output.memory_source_workspace_name = workspace.display_name;
            }
            output.memory_scope_kind = effect.record.scope_kind as u8;
            if let Some(workspace) = effect.record.scope_workspace {
                output.has_memory_scope_workspace = true;
                output.memory_scope_workspace_id = workspace.workspace_id;
                output.memory_scope_workspace_name = workspace.display_name;
            }
            output.memory_sensitivity = effect.record.sensitivity as u8;
            output.memory_created_at_epoch_ms = effect.record.created_at_epoch_ms;
            output.memory_updated_at_epoch_ms = effect.record.updated_at_epoch_ms;
            output.memory_reviewed_at_epoch_ms = effect.record.reviewed_at_epoch_ms;
            output.memory_expires_at_epoch_ms = effect.record.expires_at_epoch_ms;
            output
        }
        WorkspaceCommandEffect::MemoryDelete(effect) => {
            let mut output = blank_workspace_effect(
                operation,
                effect.operation_id,
                wire::StorageOperation::DeleteMemory,
            );
            output.expected_revision = effect.expected_memory_revision;
            output.resulting_revision = effect.resulting_memory_revision;
            output.memory_id = effect.memory_id;
            output.expected_entry_revision = effect.expected_record_revision;
            output.resulting_entry_revision = effect.resulting_record_revision;
            output.memory_deleted_at_epoch_ms = effect.deleted_at_epoch_ms;
            output
        }
    };
    ffi::BridgeResponse {
        admission: ffi::BridgeAdmission {
            operation_id,
            status: wire::AdmissionStatus::Accepted as u8,
        },
        storage_effects: Vec::new(),
        workspace_effects: vec![effect],
        account_effects: Vec::new(),
        asset_effects: Vec::new(),
        probe_effects: Vec::new(),
        listing_effects: Vec::new(),
        endpoint_probe_effects: Vec::new(),
        task_answer_events: Vec::new(),
        states: Vec::new(),
    }
}

fn blank_workspace_effect(
    operation: wire::OperationEnvelope,
    effect_id: String,
    operation_kind: wire::StorageOperation,
) -> ffi::BridgeWorkspaceEffect {
    ffi::BridgeWorkspaceEffect {
        operation: operation_from_wire(operation),
        effect_id,
        operation_kind: operation_kind as u8,
        workspace_id: String::new(),
        expected_revision: 0,
        resulting_revision: 0,
        snapshot: Vec::new(),
        sources: 0,
        facts: 0,
        artifact_metadata: 0,
        derived_indexes: 0,
        confirmation_token: String::new(),
        entry_id: String::new(),
        expected_entry_revision: 0,
        resulting_entry_revision: 0,
        collection_id: String::new(),
        collection_name: String::new(),
        source_workspace_id: String::new(),
        source_workspace_revision: 0,
        source_fact_id: String::new(),
        field: String::new(),
        original_value: String::new(),
        has_correction: false,
        correction: String::new(),
        library_fact_kind: 0,
        library_sources: Vec::new(),
        captured_at_epoch_ms: 0,
        last_checked_epoch_ms: 0,
        has_conflict: false,
        removed_at_epoch_ms: 0,
        memory_id: String::new(),
        memory_statement: String::new(),
        memory_source_kind: 0,
        has_memory_source_task_id: false,
        memory_source_task_id: String::new(),
        has_memory_source_workspace: false,
        memory_source_workspace_id: String::new(),
        memory_source_workspace_name: String::new(),
        memory_scope_kind: 0,
        has_memory_scope_workspace: false,
        memory_scope_workspace_id: String::new(),
        memory_scope_workspace_name: String::new(),
        memory_sensitivity: 0,
        memory_created_at_epoch_ms: 0,
        memory_updated_at_epoch_ms: 0,
        memory_reviewed_at_epoch_ms: 0,
        memory_expires_at_epoch_ms: 0,
        memory_deleted_at_epoch_ms: 0,
    }
}

fn operation_from_wire(value: wire::OperationEnvelope) -> ffi::BridgeOperation {
    ffi::BridgeOperation {
        operation_id: value.operation_id,
        service_generation: value.service_generation,
        task_revision: value.task_revision,
        deadline_monotonic_ms: value.deadline_monotonic_ms,
        idempotency_key: value.idempotency_key,
    }
}
