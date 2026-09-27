// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One reducer effect, flat, as the browser performs it.
//!
//! The largest record the seam carries, in a bridge of its own so that the
//! published-state vocabulary in `service_bridge_state_ffi.rs` stays a list of
//! the records a state and a response are *made of* rather than the whole of
//! one of them. `BridgeState` holds `Vec<BridgeTaskEffect>` through a trivial
//! extern alias, and names that vector in exactly one bridge (its own, with
//! `impl Vec<BridgeTaskEffect> {}`); this module never names it, because two
//! bridges instantiating the same `Vec<T>` would emit the same `VecElement`
//! impl and the same C++ symbols twice.
//!
//! The include order is the rule that keeps the generated headers a DAG: this
//! module includes the operation envelope's header and nothing else, the state
//! bridge includes this one, and the root includes them all.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    unsafe extern "C++" {
        include!("taffy/services/core/service_bridge_operation_ffi.rs.h");

        type BridgeOperation = crate::service_bridge_operation_ffi::ffi::BridgeOperation;
    }

    struct BridgeTaskEffect {
        operation: BridgeOperation,
        effect_id: String,
        task_id: String,
        ordinal: u32,
        kind: u8,
        principal_id: String,
        action_id: String,
        action_class: u8,
        action_operation: u8,
        canonical_intent: Vec<u8>,
        proposal_digest: String,
        action_idempotency_key: String,
        tab_id: String,
        has_node_id: bool,
        node_id: String,
        /// The page's other fields only the person can supply, asked about on
        /// the same sheet as `node_id` when it names a field rather than a
        /// form (decision 0238). Observation node ids and never values; the
        /// browser re-reads each one's classification before it shows it.
        /// Filled for `RequestFieldValues` and empty for every other kind.
        field_values_companion_node_ids: Vec<String>,
        principal: u8,
        data_classes: Vec<u8>,
        context_risk: u8,
        has_approval: bool,
        approval_receipt_id: String,
        approval_expires_at_monotonic_ms: u64,
        approval_expires_at_utc_ms: u64,
        approval_browser_session_id: String,
        control_mode: u8,
        policy_version: u32,
        capability_id: String,
        dispatch_id: String,
        frame_id: String,
        page_epoch: String,
        graph_revision: u64,
        normalized_origin: String,
        has_opaque_origin_id: bool,
        opaque_origin_id: String,
        tool_name: String,
        has_destination_origin: bool,
        destination_origin: String,
        has_destination_address: bool,
        destination_address: String,
        has_operand_handle: bool,
        operand_handle: String,
        has_transient_search_query: bool,
        transient_search_query: String,
        // Exact browser-owned task-tab authority. The model never sees these
        // identities; Rust resolves its short resident handle before this
        // record is projected.
        has_task_tab_binding: bool,
        task_tab_browser_session_id: String,
        has_task_tab_target: bool,
        task_tab_target_tab_id: String,
        task_tab_target_frame_id: String,
        task_tab_target_page_epoch: String,
        task_tab_target_graph_revision: u64,
        // Exact live download-manager incarnation. Download identities are
        // never accepted without this binding.
        has_task_download_binding: bool,
        task_download_browser_session_id: String,
        task_download_id: String,
        // One read of a store the start attached (decision 0133). The words
        // cross only for the two search kinds, resolved from the resident
        // operand the model named; the limit is the row cap it asked for.
        has_task_store_binding: bool,
        task_store_has_query: bool,
        task_store_query: String,
        task_store_limit: u32,
        has_policy_discovery: bool,
        policy_discovery_tab_id: String,
        policy_discovery_browser_session_id: String,
        policy_discovery_remaining_new_source_cap: u32,
        action_input_kind: u8,
        has_supplied_value: bool,
        supplied_value_request_id: String,
        supplied_value_index: u32,
        has_toggle_state: bool,
        toggle_checked: bool,
        preconditions: Vec<u8>,
        postcondition: u8,
        request_id: String,
        permission: u8,
        deadline_monotonic_ms: u64,
        deadline_utc_ms: u64,
        permission_browser_session_id: String,
        handover_id: String,
        handover_window_ms: u32,
        call_id: String,
        route_id: String,
        model_id: String,
        disclosure: u8,
        wire_api: u8,
        provider_id: String,
        endpoint: String,
        /// Which authority named the address above, and so which rule the
        /// browser is being asked to revalidate it under (decision 0096).
        ///
        /// It travels beside the address rather than being inferred from its
        /// shape, and it is carried rather than stated by either end: the
        /// layer that supplied the candidate is the only party that knows
        /// whether the address came from the merged catalog or from what a
        /// person typed, and a seam that answered for it would answer in the
        /// direction that sends a person's own endpoint under the catalog
        /// rule.
        endpoint_kind: u8,
        request_body: Vec<u8>,
        /// The non-secret extra headers the composed plan named, as two
        /// parallel lists of equal length.
        ///
        /// Two lists rather than a vector of pairs: a pair record would be one
        /// more shared struct for this bridge to define and for the state
        /// bridge to instantiate a `Vec` of, and the two flat lists cost
        /// neither. The C++ side refuses a record whose two lengths disagree;
        /// it does not repair one, because a repaired pairing is a header sent
        /// under a name the plan did not give it.
        ///
        /// Only the per-conversation ones travel here. What a wire family
        /// always sends is fixed in the browser's own route table, where it
        /// cannot be proposed by anything inside this process.
        static_header_names: Vec<String>,
        static_header_values: Vec<String>,
        max_output_bytes: u32,
        not_before_monotonic_ms: u64,
        has_credential_handle: bool,
        credential_handle: String,
        has_media_attachment: bool,
        media_attachment_handle: String,
        media_attachment_mime_type: String,
        discovery_bootstrap_browser_session_id: String,
        discovery_bootstrap_remaining_new_source_cap: u32,
        settlement_kind: u8,
        recovery_rule: u8,
        artifact_kind: u8,
        artifact_id: String,
        artifact_workspace_revision: u64,
        artifact_content: Vec<u8>,
        observation_scope: u8,
        observation_max_bytes: u32,
        observation_max_nodes: u32,
        observation_max_text_bytes: u32,
        observation_max_frames: u32,
        observation_deadline_ms: u32,
        job_id: String,
        tool_runtime: u8,
        has_python_job: bool,
        python_entrypoint: String,
        python_input: Vec<u8>,
        has_media_job: bool,
        media_operation: u8,
        media_source_id: String,
        media_source_browser_session_id: String,
        media_source_bytes: u64,
        media_max_frames: u32,
    }
}
