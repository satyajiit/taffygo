// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One terminal task-effect completion crossing from Chromium into Rust.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeTaskTerminalOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeMediaObservationFact {
        kind: u8,
        evidence: u8,
        text: String,
        source_locator: String,
        source_start: u32,
        source_end: u32,
        page_index_plus_one: u32,
        timestamp_start_ms: u64,
        timestamp_end_ms: u64,
        row_index_plus_one: u32,
        confidence_ppm: u32,
        truncated: bool,
    }

    struct BridgeTaskTabSnapshot {
        tab_id: String,
        frame_id: String,
        page_epoch: String,
        graph_revision: u64,
        active: bool,
    }

    /// One content-free browser-owned download witness. The opaque manager
    /// identifier is correlation state and is never rendered to the model.
    struct BridgeTaskDownloadSnapshot {
        download_id: String,
        state: u8,
        media_type: u8,
        received_bytes: u64,
        directory_class: u8,
    }

    /// One row of an attached store, already reduced by the browser to a
    /// reference: a title, a host, a path with no query or fragment, and a
    /// time. Nothing else about a visit, a bookmark or a tab crosses.
    struct BridgeTaskStoreRow {
        title: String,
        host: String,
        path: String,
        when_utc_ms: u64,
    }

    struct BridgeTaskTerminal {
        operation: BridgeTaskTerminalOperation,
        effect_id: String,
        task_id: String,
        action_id: String,
        kind: u8,
        status: u8,
        result_operation_id: String,
        /// A denied policy evaluation's closed action result code.
        has_denial_code: bool,
        denial_code: u8,
        capability_id: String,
        frame_id: String,
        page_epoch: String,
        graph_revision: u64,
        normalized_origin: String,
        has_opaque_origin_id: bool,
        opaque_origin_id: String,
        has_destination_origin: bool,
        destination_origin: String,
        has_destination_address: bool,
        destination_address: String,
        has_discovered_source: bool,
        discovered_source_id: String,
        discovered_source_tab_id: String,
        discovered_source_normalized_origin: String,
        has_discovered_source_canonical_locator: bool,
        discovered_source_canonical_locator: String,
        has_discovery_tab: bool,
        discovery_tab_id: String,
        discovery_browser_session_id: String,
        has_task_tab_result: bool,
        task_tab_browser_session_id: String,
        task_tab_operation: u8,
        task_tab_postcondition: u8,
        task_tab_snapshots: Vec<BridgeTaskTabSnapshot>,
        has_task_tab_target: bool,
        task_tab_target_tab_id: String,
        task_tab_target_frame_id: String,
        task_tab_target_page_epoch: String,
        task_tab_target_graph_revision: u64,
        task_tab_state_was_already_satisfied: bool,
        has_task_download_result: bool,
        task_download_browser_session_id: String,
        task_download_operation: u8,
        task_download_postcondition: u8,
        task_download_snapshots: Vec<BridgeTaskDownloadSnapshot>,
        task_download_truncated: bool,
        has_task_store_result: bool,
        task_store_operation: u8,
        task_store_rows: Vec<BridgeTaskStoreRow>,
        task_store_omitted: u32,
        has_observation: bool,
        observation_status: u8,
        observation_schema_version: String,
        observation_tab_id: String,
        observation_frame_id: String,
        observation_page_epoch: String,
        observation_graph_revision: u64,
        observation_origin: String,
        observation_is_potentially_trustworthy: bool,
        observation_private_profile: bool,
        observation_node_count: u32,
        observation_total_bytes: u32,
        observation_truncated: bool,
        observation_may_change_answer: bool,
        observation_redacted_field_count: u32,
        observation_suppressed_secret_value_count: u32,
        observation_sensitive_zone_count: u32,
        observation_policy_filtered_frame_count: u32,
        observation_highest_sensitivity: u8,
        observation_graph_encoding: u8,
        observation_graph_payload: Vec<u8>,
        // Media facts are already bounded and redacted in the browser. Raw
        // pixels/PDF/video bytes are represented only by the opaque handle.
        has_media_observation: bool,
        media_observation_kind: u8,
        media_observation_facts: Vec<BridgeMediaObservationFact>,
        has_media_attachment: bool,
        media_attachment_handle: String,
        media_attachment_mime_type: String,
        media_attachment_width_px: u32,
        media_attachment_height_px: u32,
        media_has_meaningful_text: bool,
        media_scanned_pdf_ocr_required: bool,
        has_media_capture_provenance: bool,
        media_capture_x_dip: u32,
        media_capture_y_dip: u32,
        media_capture_width_dip: u32,
        media_capture_height_dip: u32,
        media_viewport_width_dip: u32,
        media_viewport_height_dip: u32,
        media_output_scale_ppm: u32,
        media_captured_at_monotonic_ms: u64,
        media_redacted_region_count: u32,
        // The provider's answer to one model call, carried unread. Reading a
        // provider's bytes is the sandbox's job (Rule of Two), so the browser
        // copies them across and forms no opinion about them.
        has_model_completion: bool,
        // Which model answered, so the core can refuse a reply to a request
        // its retained plan did not make.
        model_completion_model_id: String,
        model_completion: Vec<u8>,
        // True when raw bytes were already delivered through the ordered
        // incremental seam; in that shape model_completion is empty.
        model_completion_streamed: bool,
        // A definitive provider failure, never an unknown transport outcome.
        has_model_failure: bool,
        model_failure_class: u8,
        has_model_retry_after: bool,
        model_retry_after_millis: u64,
        // A tool result crosses durably as evidence only. Output bytes remain
        // resident in the browser; the journal receives their digest and
        // bounded counts, never their content.
        has_tool_output: bool,
        tool_output_digest: String,
        tool_output_bytes: u64,
        tool_output_chunks: u32,
        // Exact browser action-journal witness for ReconcileAction only. The
        // numeric code is validated against BIP's closed enumeration before it
        // crosses, then decoded independently in Rust.
        has_reconciled_action_result: bool,
        reconciled_action_result_code: u32,
        // Fixed-size browser-verified facts carried only by media.probe.
        // Their exact action/call binding is checked again before residency.
        has_media_probe: bool,
        media_probe_duration_ms: u64,
        media_probe_audio_streams: u32,
        media_probe_video_streams: u32,
        media_probe_width_px: u32,
        media_probe_height_px: u32,
    }
}
