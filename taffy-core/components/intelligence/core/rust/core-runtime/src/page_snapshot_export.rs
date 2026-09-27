// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bounded page snapshot export owned entirely by the isolated Rust core.

mod render;
mod view;

pub use self::view::{
    PageSnapshotExportCommandView, PageSnapshotExportOperationView, PageSnapshotObservationView,
};

use std::collections::{BTreeMap, VecDeque};

use crate::account::Sha256Port;
use crate::context::{PageArena, PageIdentity};
use crate::{decode_page_observation, wire};

const MAX_REPLAY_ENTRIES: usize = 32;
const MAX_CANCELLED_ENTRIES: usize = 64;

#[derive(Clone, Debug, Eq, PartialEq)]
struct ReplayOperationKey {
    service_generation: u64,
    task_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct ReplayRequestKey {
    format: wire::PageSnapshotExportFormat,
    tab_id: String,
    frame_id: String,
    page_epoch: String,
    graph_revision: u64,
    origin: String,
    max_bytes: u32,
    source_query_withheld: bool,
    source_fragment_withheld: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct ReplayObservationKey {
    status: wire::BipObservationStatus,
    schema_version: String,
    tab_id: String,
    frame_id: String,
    page_epoch: String,
    graph_revision: u64,
    origin: String,
    private_profile: bool,
    node_count: u32,
    total_bytes: u32,
    truncated: bool,
    may_change_answer: bool,
    redacted_field_count: u32,
    suppressed_secret_value_count: u32,
    sensitive_zone_count: u32,
    policy_filtered_frame_count: u32,
    highest_sensitivity: wire::BipSensitivity,
    graph_encoding: wire::BipGraphEncoding,
    graph_digest: [u8; 32],
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct ReplayObservationContextKey {
    is_potentially_trustworthy: bool,
    has_media: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct ReplayKey {
    operation: ReplayOperationKey,
    request: ReplayRequestKey,
    observation: ReplayObservationKey,
    observation_context: ReplayObservationContextKey,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct CompletedExport {
    key: ReplayKey,
    result: wire::PageSnapshotExportResult,
}

/// Bounded in-generation logical replay and attempt cancellation authority.
#[derive(Debug, Default)]
pub struct PageSnapshotExporter {
    completed: BTreeMap<String, CompletedExport>,
    completion_order: VecDeque<String>,
    cancelled: BTreeMap<String, String>,
    cancellation_order: VecDeque<String>,
}

impl PageSnapshotExporter {
    /// Creates an empty exporter for one service generation.
    pub const fn new() -> Self {
        Self {
            completed: BTreeMap::new(),
            completion_order: VecDeque::new(),
            cancelled: BTreeMap::new(),
            cancellation_order: VecDeque::new(),
        }
    }

    /// Records cancellation of one exact attempt and its logical replay identity.
    pub fn cancel(&mut self, operation_id: &str, idempotency_key: &str) -> bool {
        if !bounded_identifier(operation_id)
            || idempotency_key.is_empty()
            || idempotency_key.len() > wire::MAX_IDEMPOTENCY_KEY_BYTES
        {
            return false;
        }
        if self.cancelled.contains_key(operation_id) {
            return false;
        }
        if self.cancelled.len() >= MAX_CANCELLED_ENTRIES {
            if let Some(expired) = self.cancellation_order.pop_front() {
                self.cancelled.remove(expired.as_str());
            }
        }
        self.cancelled
            .insert(operation_id.to_owned(), idempotency_key.to_owned());
        self.cancellation_order.push_back(operation_id.to_owned());
        true
    }

    /// Decodes, validates, renders, and caches one logical export request.
    pub fn export(
        &mut self,
        command: &wire::PageSnapshotExportCommand,
        digest: &dyn Sha256Port,
    ) -> wire::PageSnapshotExportResult {
        let view = PageSnapshotExportCommandView::from(command);
        self.export_view(&view, digest)
    }

    /// Exports from a borrowed bridge projection without cloning graph bytes.
    pub fn export_view(
        &mut self,
        command: &PageSnapshotExportCommandView<'_>,
        digest: &dyn Sha256Port,
    ) -> wire::PageSnapshotExportResult {
        let Some(key) = replay_key(command, digest) else {
            return failure(command, wire::PageSnapshotExportStatus::Malformed);
        };
        if let Some(cancelled_idempotency_key) = self.cancelled.get(command.operation.operation_id)
        {
            return if cancelled_idempotency_key == command.operation.idempotency_key {
                failure(command, wire::PageSnapshotExportStatus::Cancelled)
            } else {
                failure(command, wire::PageSnapshotExportStatus::ReplayConflict)
            };
        }
        if let Some(completed) = self.completed.get(command.operation.idempotency_key) {
            return if completed.key == key {
                replay_result(command, &completed.result)
            } else {
                failure(command, wire::PageSnapshotExportStatus::ReplayConflict)
            };
        }
        if self.completed.len() >= MAX_REPLAY_ENTRIES {
            if let Some(expired) = self.completion_order.pop_front() {
                self.completed.remove(expired.as_str());
            }
        }
        let result = export_once(command);
        let idempotency_key = command.operation.idempotency_key.to_owned();
        self.completed.insert(
            idempotency_key.clone(),
            CompletedExport {
                key,
                result: result.clone(),
            },
        );
        self.completion_order.push_back(idempotency_key);
        result
    }
}

fn replay_key(
    command: &PageSnapshotExportCommandView<'_>,
    digest: &dyn Sha256Port,
) -> Option<ReplayKey> {
    let operation = command.operation;
    let observation = command.observation;
    let evidence = observation.evidence;
    let payload_len = evidence.graph_payload.len();
    if !bounded_identifier(operation.operation_id)
        || operation.service_generation == 0
        || operation.deadline_monotonic_ms == 0
        || operation.idempotency_key.is_empty()
        || operation.idempotency_key.len() > wire::MAX_IDEMPOTENCY_KEY_BYTES
        || command.max_bytes == 0
        || usize::try_from(command.max_bytes).map_or(true, |maximum| {
            maximum > wire::MAX_PAGE_SNAPSHOT_EXPORT_BYTES
        })
        || !bounded_identifier(command.expected_tab_id)
        || !bounded_identifier(command.expected_frame_id)
        || !bounded_identifier(command.expected_page_epoch)
        || command.expected_graph_revision == 0
        || command.expected_origin.is_empty()
        || command.expected_origin.len() > wire::MAX_NORMALIZED_ORIGIN_BYTES
        || command.captured_at_epoch_ms == 0
        || observation.has_media
        || !bounded_identifier(evidence.schema_version)
        || !bounded_identifier(evidence.tab_id)
        || !bounded_identifier(evidence.frame_id)
        || !bounded_identifier(evidence.page_epoch)
        || evidence.origin.is_empty()
        || evidence.origin.len() > wire::MAX_NORMALIZED_ORIGIN_BYTES
        || payload_len == 0
        || payload_len > wire::MAX_TASK_OBSERVATION_TOTAL_BYTES
        || usize::try_from(evidence.total_bytes).ok()? != payload_len
        || usize::try_from(evidence.node_count)
            .map_or(true, |count| count > wire::MAX_TASK_OBSERVATION_NODES)
    {
        return None;
    }
    Some(ReplayKey {
        operation: ReplayOperationKey {
            service_generation: operation.service_generation,
            task_revision: operation.task_revision,
        },
        request: ReplayRequestKey {
            format: command.format,
            tab_id: command.expected_tab_id.to_owned(),
            frame_id: command.expected_frame_id.to_owned(),
            page_epoch: command.expected_page_epoch.to_owned(),
            graph_revision: command.expected_graph_revision,
            origin: command.expected_origin.to_owned(),
            max_bytes: command.max_bytes,
            source_query_withheld: command.source_query_withheld,
            source_fragment_withheld: command.source_fragment_withheld,
        },
        observation: ReplayObservationKey {
            status: evidence.status,
            schema_version: evidence.schema_version.to_owned(),
            tab_id: evidence.tab_id.to_owned(),
            frame_id: evidence.frame_id.to_owned(),
            page_epoch: evidence.page_epoch.to_owned(),
            graph_revision: evidence.graph_revision,
            origin: evidence.origin.to_owned(),
            private_profile: evidence.private_profile,
            node_count: evidence.node_count,
            total_bytes: evidence.total_bytes,
            truncated: evidence.truncated,
            may_change_answer: evidence.may_change_answer,
            redacted_field_count: evidence.redacted_field_count,
            suppressed_secret_value_count: evidence.suppressed_secret_value_count,
            sensitive_zone_count: evidence.sensitive_zone_count,
            policy_filtered_frame_count: evidence.policy_filtered_frame_count,
            highest_sensitivity: evidence.highest_sensitivity,
            graph_encoding: evidence.graph_encoding,
            graph_digest: digest.sha256(evidence.graph_payload).ok()?,
        },
        observation_context: ReplayObservationContextKey {
            is_potentially_trustworthy: observation.is_potentially_trustworthy,
            has_media: observation.has_media,
        },
    })
}

fn replay_result(
    command: &PageSnapshotExportCommandView<'_>,
    completed: &wire::PageSnapshotExportResult,
) -> wire::PageSnapshotExportResult {
    let mut replay = completed.clone();
    replay.operation = command.operation.into();
    replay
}

fn export_once(command: &PageSnapshotExportCommandView<'_>) -> wire::PageSnapshotExportResult {
    let observation = command.observation;
    let evidence_view = observation.evidence;
    if evidence_view.private_profile {
        return failure(command, wire::PageSnapshotExportStatus::PrivateProfile);
    }
    if evidence_view.status == wire::BipObservationStatus::Incomplete
        || evidence_view.truncated
        || evidence_view.may_change_answer
    {
        return failure(command, wire::PageSnapshotExportStatus::Incomplete);
    }
    if evidence_view.status != wire::BipObservationStatus::Ok {
        return failure(command, wire::PageSnapshotExportStatus::Unavailable);
    }
    if evidence_view.tab_id != command.expected_tab_id
        || evidence_view.frame_id != command.expected_frame_id
        || evidence_view.page_epoch != command.expected_page_epoch
        || evidence_view.graph_revision != command.expected_graph_revision
        || evidence_view.origin != command.expected_origin
    {
        return failure(command, wire::PageSnapshotExportStatus::StalePage);
    }
    let mut arena = PageArena::new();
    let Ok(evidence) = decode_page_observation(
        command.operation.service_generation,
        evidence_view,
        Some(&mut arena),
    ) else {
        return failure(command, wire::PageSnapshotExportStatus::Malformed);
    };
    if PageIdentity::from_evidence(&evidence).is_err() {
        return failure(command, wire::PageSnapshotExportStatus::Malformed);
    }
    let withheld_field_count = arena.nodes().iter().fold(0_u32, |count, node| {
        count
            .saturating_add(u32::from(node.name_withheld))
            .saturating_add(u32::from(node.text_withheld))
    });
    let metadata = render::ExportMetadata {
        origin: evidence_view.origin,
        graph_revision: evidence_view.graph_revision,
        secure_context: observation.is_potentially_trustworthy,
        node_count: evidence_view.node_count,
        redacted_field_count: evidence_view.redacted_field_count,
        suppressed_secret_value_count: evidence_view.suppressed_secret_value_count,
        withheld_field_count,
        captured_at_epoch_ms: command.captured_at_epoch_ms,
        source_query_withheld: command.source_query_withheld,
        source_fragment_withheld: command.source_fragment_withheld,
    };
    let Ok(rendered) = render::render(command.format, &arena, &metadata, command.max_bytes) else {
        return failure(command, wire::PageSnapshotExportStatus::Oversize);
    };
    wire::PageSnapshotExportResult {
        operation: command.operation.into(),
        status: wire::PageSnapshotExportStatus::Exported,
        format: command.format,
        tab_id: evidence_view.tab_id.to_owned(),
        frame_id: evidence_view.frame_id.to_owned(),
        page_epoch: evidence_view.page_epoch.to_owned(),
        graph_revision: evidence_view.graph_revision,
        origin: evidence_view.origin.to_owned(),
        mime_type: rendered.mime_type.to_owned(),
        suggested_file_name: rendered.file_name.to_owned(),
        content: rendered.content,
        node_count: evidence_view.node_count,
        redacted_field_count: evidence_view.redacted_field_count,
        suppressed_secret_value_count: evidence_view.suppressed_secret_value_count,
        withheld_field_count,
        captured_at_epoch_ms: command.captured_at_epoch_ms,
        source_query_withheld: command.source_query_withheld,
        source_fragment_withheld: command.source_fragment_withheld,
        secure_context: observation.is_potentially_trustworthy,
    }
}

fn failure(
    command: &PageSnapshotExportCommandView<'_>,
    status: wire::PageSnapshotExportStatus,
) -> wire::PageSnapshotExportResult {
    wire::PageSnapshotExportResult {
        operation: command.operation.into(),
        status,
        format: command.format,
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

fn bounded_identifier(value: &str) -> bool {
    !value.is_empty() && value.len() <= wire::MAX_IDENTIFIER_BYTES
}

#[cfg(test)]
mod tests;
