// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Start-task-only CXX records.
//!
//! These records enter one start submission and are never published in a
//! state or response. Their operation and consent-source shapes intentionally
//! mirror the shared vocabulary once at the start-plane boundary.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeStartOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeBudget {
        kind: u8,
        limit: u64,
    }

    struct BridgeStartConsentSource {
        source_id: String,
        tab_id: String,
        normalized_origin: String,
        has_canonical_locator: bool,
        canonical_locator: String,
    }

    struct BridgeStartTask {
        operation: BridgeStartOperation,
        task_id: String,
        has_workspace_id: bool,
        workspace_id: String,
        browser_profile_id: String,
        kind: u8,
        goal: String,
        control_mode: u8,
        has_provider_route_id: bool,
        provider_route_id: String,
        assistant_config_version: u32,
        policy_version: u32,
        has_skill_version_id: bool,
        skill_version_id: String,
        has_builtin_skill: bool,
        builtin_skill_id: u8,
        builtin_skill_version: u32,
        tool_allowlist: Vec<String>,
        milestone: u8,
        budgets: Vec<BridgeBudget>,
        has_task_deadline: bool,
        task_deadline_monotonic_ms: u64,
        task_deadline_utc_ms: u64,
        has_predecessor_task_id: bool,
        predecessor_task_id: String,
        trace_id: String,
        task_id_seed: [u8; 32],
        template_id: u8,
        consent_sources: Vec<BridgeStartConsentSource>,
        source_discovery_enabled: bool,
        new_source_cap: u32,
        consent_provider_route: u8,
        initial_consent_receipt_id: String,
        browser_session_id: String,
        has_library_refresh: bool,
        library_refresh_preview_id: String,
        library_refresh_library_revision: u64,
        library_refresh_collection_id: String,
        library_refresh_workspace_revision: u64,
    }
}
