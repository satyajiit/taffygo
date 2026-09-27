// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Strict terminal observation projection into durable content-free evidence.

use core_runtime::wire;
use core_runtime::{ActionId, PageObservationEvidence, ProfileServiceRuntime, TaskId};

use super::TaskGrantFacts;
use crate::ffi;

pub(super) fn decode(
    runtime: &mut ProfileServiceRuntime,
    task_id: &TaskId,
    action_id: &ActionId,
    terminal: &ffi::BridgeTaskTerminal,
    service_generation: u64,
    expected_tab_id: &str,
    grant: &TaskGrantFacts,
    operation: wire::TaskActionOperationKind,
) -> Result<PageObservationEvidence, &'static str> {
    // Each refusal names itself. Every one of these used to answer `()`, which
    // the browser logged as `[taffy_core_invalid_command] label=` — a refused
    // read with no reason at all, and the only way to tell them apart was to
    // rebuild with a print in it.
    if !terminal.has_observation {
        return Err("observation_absent");
    }
    if terminal.observation_tab_id != expected_tab_id {
        return Err("observation_tab");
    }
    if terminal.observation_frame_id != grant.frame_id {
        return Err("observation_frame");
    }
    if terminal.observation_page_epoch != grant.page_epoch {
        return Err("observation_page_epoch");
    }
    // A grant freezes the document and sets a revision floor, not an exact
    // snapshot revision. The first renderer snapshot is revision 1 even when
    // the pre-observation browser floor was 0; lower revisions are still stale
    // and remain refused here.
    if terminal.observation_graph_revision < grant.graph_revision {
        return Err("observation_graph_revision");
    }
    if terminal.observation_origin != grant.normalized_origin {
        return Err("observation_origin");
    }
    // Borrowed, never cloned: the graph payload can reach the process byte
    // cap and this decoder reads it once. The same pass fills the live page
    // the next model turn will project; a second pass could disagree.
    let result = core_runtime::ObservationEffectView {
        status: wire::BipObservationStatus::from_wire(u32::from(terminal.observation_status))
            .ok_or("observation_status")?,
        schema_version: terminal.observation_schema_version.as_str(),
        tab_id: terminal.observation_tab_id.as_str(),
        frame_id: terminal.observation_frame_id.as_str(),
        page_epoch: terminal.observation_page_epoch.as_str(),
        graph_revision: terminal.observation_graph_revision,
        origin: terminal.observation_origin.as_str(),
        private_profile: terminal.observation_private_profile,
        node_count: terminal.observation_node_count,
        total_bytes: terminal.observation_total_bytes,
        truncated: terminal.observation_truncated,
        may_change_answer: terminal.observation_may_change_answer,
        redacted_field_count: terminal.observation_redacted_field_count,
        suppressed_secret_value_count: terminal.observation_suppressed_secret_value_count,
        sensitive_zone_count: terminal.observation_sensitive_zone_count,
        policy_filtered_frame_count: terminal.observation_policy_filtered_frame_count,
        highest_sensitivity: wire::BipSensitivity::from_wire(u32::from(
            terminal.observation_highest_sensitivity,
        ))
        .ok_or("observation_sensitivity")?,
        graph_encoding: wire::BipGraphEncoding::from_wire(u32::from(
            terminal.observation_graph_encoding,
        ))
        .ok_or("observation_graph_encoding")?,
        graph_payload: terminal.observation_graph_payload.as_slice(),
    };
    let media_wire = if terminal.has_media_observation {
        let facts = terminal
            .media_observation_facts
            .iter()
            .map(|fact| {
                Ok(wire::MediaObservationFact {
                    kind: wire::MediaFactKind::from_wire(u32::from(fact.kind))
                        .ok_or("observation_media_fact_kind")?,
                    evidence: wire::MediaEvidenceKind::from_wire(u32::from(fact.evidence))
                        .ok_or("observation_media_evidence")?,
                    text: fact.text.clone(),
                    source_locator: fact.source_locator.clone(),
                    source_start: fact.source_start,
                    source_end: fact.source_end,
                    page_index_plus_one: fact.page_index_plus_one,
                    timestamp_start_ms: fact.timestamp_start_ms,
                    timestamp_end_ms: fact.timestamp_end_ms,
                    row_index_plus_one: fact.row_index_plus_one,
                    confidence_ppm: fact.confidence_ppm,
                    truncated: fact.truncated,
                })
            })
            .collect::<Result<Vec<_>, &'static str>>()?;
        Some(wire::MediaObservationResult {
            kind: wire::MediaObservationKind::from_wire(u32::from(terminal.media_observation_kind))
                .ok_or("observation_media_kind")?,
            facts,
            attachment_handle: terminal
                .has_media_attachment
                .then(|| terminal.media_attachment_handle.clone()),
            attachment_mime_type: terminal
                .has_media_attachment
                .then(|| terminal.media_attachment_mime_type.clone()),
            width_px: terminal.media_attachment_width_px,
            height_px: terminal.media_attachment_height_px,
            has_meaningful_text: terminal.media_has_meaningful_text,
            scanned_pdf_ocr_required: terminal.media_scanned_pdf_ocr_required,
            capture_provenance: terminal.has_media_capture_provenance.then(|| {
                wire::MediaCaptureProvenance {
                    capture_x_dip: terminal.media_capture_x_dip,
                    capture_y_dip: terminal.media_capture_y_dip,
                    capture_width_dip: terminal.media_capture_width_dip,
                    capture_height_dip: terminal.media_capture_height_dip,
                    viewport_width_dip: terminal.media_viewport_width_dip,
                    viewport_height_dip: terminal.media_viewport_height_dip,
                    output_scale_ppm: terminal.media_output_scale_ppm,
                    captured_at_monotonic_ms: terminal.media_captured_at_monotonic_ms,
                    redacted_region_count: terminal.media_redacted_region_count,
                }
            }),
        })
    } else {
        if !terminal.media_observation_facts.is_empty()
            || terminal.has_media_attachment
            || !terminal.media_attachment_handle.is_empty()
            || !terminal.media_attachment_mime_type.is_empty()
            || terminal.media_attachment_width_px != 0
            || terminal.media_attachment_height_px != 0
            || terminal.media_has_meaningful_text
            || terminal.media_scanned_pdf_ocr_required
            || terminal.has_media_capture_provenance
            || terminal.media_capture_x_dip != 0
            || terminal.media_capture_y_dip != 0
            || terminal.media_capture_width_dip != 0
            || terminal.media_capture_height_dip != 0
            || terminal.media_viewport_width_dip != 0
            || terminal.media_viewport_height_dip != 0
            || terminal.media_output_scale_ppm != 0
            || terminal.media_captured_at_monotonic_ms != 0
            || terminal.media_redacted_region_count != 0
        {
            return Err("observation_media_without_observation");
        }
        None
    };
    let media = core_runtime::decode_media_observation(operation, media_wire.as_ref())
        .map_err(|_| "observation_media_shape")?;
    // The adoption names its own refusal, so this passes it through rather
    // than flattening six facts into one word (decision 0171).
    runtime.adopt_observation(task_id, action_id, service_generation, result, media)
}
