// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact-page saved-skill matching in the Rust-owned procedure catalogue.
//!
//! This entry deliberately does not publish `CoreStatus`: matching is a
//! read-only projection of one request-bound page and the restored catalogue.
//! The immutable terminal returned here is the complete visible result.

use core_runtime::wire;

use crate::service_bridge_runtime::ServiceBridge;
use crate::service_bridge_skill_match_ffi::ffi::{
    BridgeSiteSkillMatchCommand, BridgeSiteSkillMatchOffer, BridgeSiteSkillMatchOperation,
    BridgeSiteSkillMatchResult,
};

#[allow(non_snake_case)]
pub(crate) fn MatchSiteSkills(
    bridge: &mut ServiceBridge,
    input: BridgeSiteSkillMatchCommand,
    now_monotonic_ms: u64,
) -> BridgeSiteSkillMatchResult {
    if input.operation.service_generation != bridge.generation.value()
        || input.operation.deadline_monotonic_ms < now_monotonic_ms
    {
        return malformed(input.operation);
    }
    let Some(status) = wire::BipObservationStatus::from_wire(u32::from(input.observation.status))
    else {
        return malformed(input.operation);
    };
    let Some(highest_sensitivity) =
        wire::BipSensitivity::from_wire(u32::from(input.observation.highest_sensitivity))
    else {
        return malformed(input.operation);
    };
    let Some(graph_encoding) =
        wire::BipGraphEncoding::from_wire(u32::from(input.observation.graph_encoding))
    else {
        return malformed(input.operation);
    };
    let Some(runtime) = bridge.runtime.as_ref() else {
        return failed(input.operation, wire::SiteSkillMatchStatus::Unavailable);
    };
    let command = to_wire(input, status, highest_sensitivity, graph_encoding);
    from_wire(runtime.match_site_skills(command))
}

fn to_wire(
    input: BridgeSiteSkillMatchCommand,
    status: wire::BipObservationStatus,
    highest_sensitivity: wire::BipSensitivity,
    graph_encoding: wire::BipGraphEncoding,
) -> wire::SiteSkillMatchCommand {
    wire::SiteSkillMatchCommand {
        operation: wire::OperationEnvelope {
            operation_id: input.operation.operation_id,
            service_generation: input.operation.service_generation,
            task_revision: input.operation.task_revision,
            deadline_monotonic_ms: input.operation.deadline_monotonic_ms,
            idempotency_key: input.operation.idempotency_key,
        },
        expected_tab_id: input.expected_tab_id,
        expected_frame_id: input.expected_frame_id,
        expected_page_epoch: input.expected_page_epoch,
        expected_graph_revision: input.expected_graph_revision,
        expected_origin: input.expected_origin,
        observation: wire::ObservationEffectResult {
            status,
            schema_version: input.observation.schema_version,
            tab_id: input.observation.tab_id,
            frame_id: input.observation.frame_id,
            page_epoch: input.observation.page_epoch,
            graph_revision: input.observation.graph_revision,
            origin: input.observation.origin,
            is_potentially_trustworthy: input.observation.is_potentially_trustworthy,
            private_profile: input.observation.private_profile,
            node_count: input.observation.node_count,
            total_bytes: input.observation.total_bytes,
            truncated: input.observation.truncated,
            may_change_answer: input.observation.may_change_answer,
            redacted_field_count: input.observation.redacted_field_count,
            suppressed_secret_value_count: input.observation.suppressed_secret_value_count,
            sensitive_zone_count: input.observation.sensitive_zone_count,
            policy_filtered_frame_count: input.observation.policy_filtered_frame_count,
            highest_sensitivity,
            graph_encoding,
            graph_payload: input.observation.graph_payload,
            media: None,
        },
    }
}

fn from_wire(input: wire::SiteSkillMatchResult) -> BridgeSiteSkillMatchResult {
    BridgeSiteSkillMatchResult {
        operation: BridgeSiteSkillMatchOperation {
            operation_id: input.operation.operation_id,
            service_generation: input.operation.service_generation,
            task_revision: input.operation.task_revision,
            deadline_monotonic_ms: input.operation.deadline_monotonic_ms,
            idempotency_key: input.operation.idempotency_key,
        },
        status: input.status as u8,
        tab_id: input.tab_id,
        frame_id: input.frame_id,
        page_epoch: input.page_epoch,
        graph_revision: input.graph_revision,
        origin: input.origin,
        offers: input
            .offers
            .into_iter()
            .map(|offer| BridgeSiteSkillMatchOffer {
                skill_version_id: offer.skill_version_id,
                skill_id: offer.skill_id,
                active_version: offer.active_version,
                step_count: offer.step_count,
            })
            .collect(),
    }
}

fn malformed(operation: BridgeSiteSkillMatchOperation) -> BridgeSiteSkillMatchResult {
    failed(operation, wire::SiteSkillMatchStatus::Malformed)
}

fn failed(
    operation: BridgeSiteSkillMatchOperation,
    status: wire::SiteSkillMatchStatus,
) -> BridgeSiteSkillMatchResult {
    BridgeSiteSkillMatchResult {
        operation,
        status: status as u8,
        tab_id: String::new(),
        frame_id: String::new(),
        page_epoch: String::new(),
        graph_revision: 0,
        origin: String::new(),
        offers: Vec::new(),
    }
}
