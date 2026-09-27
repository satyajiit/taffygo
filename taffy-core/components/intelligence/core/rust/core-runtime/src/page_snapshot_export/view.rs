// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Borrowed page-export projection across the CXX boundary.

use crate::{wire, ObservationEffectView};

/// Borrowed operation metadata for one page snapshot export.
#[derive(Clone, Copy, Debug)]
pub struct PageSnapshotExportOperationView<'a> {
    pub operation_id: &'a str,
    pub service_generation: u64,
    pub task_revision: u64,
    pub deadline_monotonic_ms: u64,
    pub idempotency_key: &'a str,
}

impl From<PageSnapshotExportOperationView<'_>> for wire::OperationEnvelope {
    fn from(value: PageSnapshotExportOperationView<'_>) -> Self {
        Self {
            operation_id: value.operation_id.to_owned(),
            service_generation: value.service_generation,
            task_revision: value.task_revision,
            deadline_monotonic_ms: value.deadline_monotonic_ms,
            idempotency_key: value.idempotency_key.to_owned(),
        }
    }
}

/// Borrowed observation fields needed by the exporter.
#[derive(Clone, Copy, Debug)]
pub struct PageSnapshotObservationView<'a> {
    pub evidence: ObservationEffectView<'a>,
    pub is_potentially_trustworthy: bool,
    pub has_media: bool,
}

/// Borrowed command view that keeps the graph in its caller-owned allocation.
#[derive(Clone, Copy, Debug)]
pub struct PageSnapshotExportCommandView<'a> {
    pub operation: PageSnapshotExportOperationView<'a>,
    pub format: wire::PageSnapshotExportFormat,
    pub expected_tab_id: &'a str,
    pub expected_frame_id: &'a str,
    pub expected_page_epoch: &'a str,
    pub expected_graph_revision: u64,
    pub expected_origin: &'a str,
    pub max_bytes: u32,
    pub observation: PageSnapshotObservationView<'a>,
    pub captured_at_epoch_ms: u64,
    pub source_query_withheld: bool,
    pub source_fragment_withheld: bool,
}

impl<'a> From<&'a wire::PageSnapshotExportCommand> for PageSnapshotExportCommandView<'a> {
    fn from(value: &'a wire::PageSnapshotExportCommand) -> Self {
        let observation = &value.observation;
        Self {
            operation: PageSnapshotExportOperationView {
                operation_id: value.operation.operation_id.as_str(),
                service_generation: value.operation.service_generation,
                task_revision: value.operation.task_revision,
                deadline_monotonic_ms: value.operation.deadline_monotonic_ms,
                idempotency_key: value.operation.idempotency_key.as_str(),
            },
            format: value.format,
            expected_tab_id: value.expected_tab_id.as_str(),
            expected_frame_id: value.expected_frame_id.as_str(),
            expected_page_epoch: value.expected_page_epoch.as_str(),
            expected_graph_revision: value.expected_graph_revision,
            expected_origin: value.expected_origin.as_str(),
            max_bytes: value.max_bytes,
            observation: PageSnapshotObservationView {
                evidence: ObservationEffectView::from(observation),
                is_potentially_trustworthy: observation.is_potentially_trustworthy,
                has_media: observation.media.is_some(),
            },
            captured_at_epoch_ms: value.captured_at_epoch_ms,
            source_query_withheld: value.source_query_withheld,
            source_fragment_withheld: value.source_fragment_withheld,
        }
    }
}
