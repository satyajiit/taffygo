// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Page-export-only CXX records. Graph bytes cross this seam opaque.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgePageExportOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgePageObservation {
        status: u8,
        schema_version: String,
        tab_id: String,
        frame_id: String,
        page_epoch: String,
        graph_revision: u64,
        origin: String,
        is_potentially_trustworthy: bool,
        private_profile: bool,
        node_count: u32,
        total_bytes: u32,
        truncated: bool,
        may_change_answer: bool,
        redacted_field_count: u32,
        suppressed_secret_value_count: u32,
        sensitive_zone_count: u32,
        policy_filtered_frame_count: u32,
        highest_sensitivity: u8,
        graph_encoding: u8,
    }

    struct BridgePageExportCommand {
        operation: BridgePageExportOperation,
        format: u8,
        expected_tab_id: String,
        expected_frame_id: String,
        expected_page_epoch: String,
        expected_graph_revision: u64,
        expected_origin: String,
        max_bytes: u32,
        observation: BridgePageObservation,
        captured_at_epoch_ms: u64,
        source_query_withheld: bool,
        source_fragment_withheld: bool,
    }

    struct BridgePageExportResult {
        operation: BridgePageExportOperation,
        status: u8,
        format: u8,
        tab_id: String,
        frame_id: String,
        page_epoch: String,
        graph_revision: u64,
        origin: String,
        mime_type: String,
        suggested_file_name: String,
        content: Vec<u8>,
        node_count: u32,
        redacted_field_count: u32,
        suppressed_secret_value_count: u32,
        withheld_field_count: u32,
        captured_at_epoch_ms: u64,
        source_query_withheld: bool,
        source_fragment_withheld: bool,
        secure_context: bool,
    }
}
