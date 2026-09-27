// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Projection into the Rust-owned PageArena exporter.
//!
//! These two entries deliberately do not publish `CoreStatus`. The bounded
//! replay/cancellation cache is request correlation state, not profile state
//! any status surface reads; the immutable export or typed terminal returned
//! here is the complete visible result. Both entries are therefore registered
//! with a written no-publication discipline in the totality checker.

use core_runtime::page_snapshot_export::{
    PageSnapshotExportCommandView, PageSnapshotExportOperationView, PageSnapshotObservationView,
};
use core_runtime::{wire, ObservationEffectView};

use crate::service_bridge_page_export_ffi::ffi::{
    BridgePageExportCommand, BridgePageExportOperation, BridgePageExportResult,
};
use crate::service_bridge_runtime::{ChromiumDigest, ServiceBridge};

#[allow(non_snake_case)]
pub(crate) fn ExportPageSnapshot(
    bridge: &mut ServiceBridge,
    input: BridgePageExportCommand,
    graph_payload: &[u8],
    now_monotonic_ms: u64,
) -> BridgePageExportResult {
    if input.operation.service_generation != bridge.generation.value()
        || input.operation.deadline_monotonic_ms < now_monotonic_ms
    {
        return malformed(&input);
    }
    let Some(command) = to_command_view(&input, graph_payload) else {
        return malformed(&input);
    };
    let result = bridge
        .page_snapshot_exporter
        .export_view(&command, &ChromiumDigest);
    from_wire_result(result)
}

#[allow(non_snake_case)]
pub(crate) fn CancelPageSnapshotExport(
    bridge: &mut ServiceBridge,
    operation_id: String,
    idempotency_key: String,
) -> bool {
    bridge
        .page_snapshot_exporter
        .cancel(&operation_id, &idempotency_key)
}

fn to_command_view<'a>(
    input: &'a BridgePageExportCommand,
    graph_payload: &'a [u8],
) -> Option<PageSnapshotExportCommandView<'a>> {
    let observation = &input.observation;
    Some(PageSnapshotExportCommandView {
        operation: PageSnapshotExportOperationView {
            operation_id: input.operation.operation_id.as_str(),
            service_generation: input.operation.service_generation,
            task_revision: input.operation.task_revision,
            deadline_monotonic_ms: input.operation.deadline_monotonic_ms,
            idempotency_key: input.operation.idempotency_key.as_str(),
        },
        format: wire::PageSnapshotExportFormat::from_wire(u32::from(input.format))?,
        expected_tab_id: input.expected_tab_id.as_str(),
        expected_frame_id: input.expected_frame_id.as_str(),
        expected_page_epoch: input.expected_page_epoch.as_str(),
        expected_graph_revision: input.expected_graph_revision,
        expected_origin: input.expected_origin.as_str(),
        max_bytes: input.max_bytes,
        observation: PageSnapshotObservationView {
            evidence: ObservationEffectView {
                status: wire::BipObservationStatus::from_wire(u32::from(observation.status))?,
                schema_version: observation.schema_version.as_str(),
                tab_id: observation.tab_id.as_str(),
                frame_id: observation.frame_id.as_str(),
                page_epoch: observation.page_epoch.as_str(),
                graph_revision: observation.graph_revision,
                origin: observation.origin.as_str(),
                private_profile: observation.private_profile,
                node_count: observation.node_count,
                total_bytes: observation.total_bytes,
                truncated: observation.truncated,
                may_change_answer: observation.may_change_answer,
                redacted_field_count: observation.redacted_field_count,
                suppressed_secret_value_count: observation.suppressed_secret_value_count,
                sensitive_zone_count: observation.sensitive_zone_count,
                policy_filtered_frame_count: observation.policy_filtered_frame_count,
                highest_sensitivity: wire::BipSensitivity::from_wire(u32::from(
                    observation.highest_sensitivity,
                ))?,
                graph_encoding: wire::BipGraphEncoding::from_wire(u32::from(
                    observation.graph_encoding,
                ))?,
                graph_payload,
            },
            is_potentially_trustworthy: observation.is_potentially_trustworthy,
            has_media: false,
        },
        captured_at_epoch_ms: input.captured_at_epoch_ms,
        source_query_withheld: input.source_query_withheld,
        source_fragment_withheld: input.source_fragment_withheld,
    })
}

fn from_wire_result(result: wire::PageSnapshotExportResult) -> BridgePageExportResult {
    BridgePageExportResult {
        operation: BridgePageExportOperation {
            operation_id: result.operation.operation_id,
            service_generation: result.operation.service_generation,
            task_revision: result.operation.task_revision,
            deadline_monotonic_ms: result.operation.deadline_monotonic_ms,
            idempotency_key: result.operation.idempotency_key,
        },
        status: result.status as u8,
        format: result.format as u8,
        tab_id: result.tab_id,
        frame_id: result.frame_id,
        page_epoch: result.page_epoch,
        graph_revision: result.graph_revision,
        origin: result.origin,
        mime_type: result.mime_type,
        suggested_file_name: result.suggested_file_name,
        content: result.content,
        node_count: result.node_count,
        redacted_field_count: result.redacted_field_count,
        suppressed_secret_value_count: result.suppressed_secret_value_count,
        withheld_field_count: result.withheld_field_count,
        captured_at_epoch_ms: result.captured_at_epoch_ms,
        source_query_withheld: result.source_query_withheld,
        source_fragment_withheld: result.source_fragment_withheld,
        secure_context: result.secure_context,
    }
}

fn malformed(input: &BridgePageExportCommand) -> BridgePageExportResult {
    BridgePageExportResult {
        operation: BridgePageExportOperation {
            operation_id: input.operation.operation_id.clone(),
            service_generation: input.operation.service_generation,
            task_revision: input.operation.task_revision,
            deadline_monotonic_ms: input.operation.deadline_monotonic_ms,
            idempotency_key: input.operation.idempotency_key.clone(),
        },
        status: wire::PageSnapshotExportStatus::Malformed as u8,
        format: input.format,
        tab_id: String::new(),
        frame_id: String::new(),
        page_epoch: String::new(),
        graph_revision: 0,
        origin: String::new(),
        mime_type: String::new(),
        suggested_file_name: String::new(),
        content: Vec::new(),
        node_count: 0,
        redacted_field_count: 0,
        suppressed_secret_value_count: 0,
        withheld_field_count: 0,
        captured_at_epoch_ms: 0,
        source_query_withheld: false,
        source_fragment_withheld: false,
        secure_context: false,
    }
}
