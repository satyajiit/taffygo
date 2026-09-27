// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Workspace-only inbound CXX records kept out of the shared state bridge.
//!
//! The operation record mirrors `BridgeOperation` field for field, the same
//! way the account, composer, policy, provider and task-terminal bridges
//! mirror it: two cxx bridge modules cannot share a by-value struct without an
//! include cycle between their generated headers, so the shape is duplicated
//! under the plane's own name and `service_bridge_workspace.rs` converts it
//! exactly once. The workspace *effect* is not here — a response carries it,
//! so it stays with the rest of the published vocabulary.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeWorkspaceOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeWorkspaceCommand {
        operation: BridgeWorkspaceOperation,
        kind: u8,
        workspace_id: String,
        expected_revision: u64,
        fact_id: String,
        value: String,
        source_id: String,
        request_id: String,
        format: u8,
        display_name: String,
        confirmation_token: String,
        query: String,
        limit: u32,
        requested_at_epoch_ms: u64,
        expected_workspace_revision: u64,
        expected_library_revision: u64,
        expected_entry_revision: u64,
        entry_id: String,
        approved_at_epoch_ms: u64,
        removed_at_epoch_ms: u64,
        has_collection_id: bool,
        collection_id: String,
        has_memory_id: bool,
        memory_id: String,
        memory_statement: String,
        memory_scope_kind: u8,
        has_memory_scope_workspace: bool,
        memory_scope_workspace_id: String,
        memory_scope_workspace_name: String,
        memory_sensitivity: u8,
        expected_memory_revision: u64,
        expected_record_revision: u64,
        memory_expires_at_epoch_ms: u64,
        memory_approved_at_epoch_ms: u64,
        memory_deleted_at_epoch_ms: u64,
    }
}
