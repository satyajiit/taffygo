// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact terminal decoding for one policy evaluation.
//!
//! A granted evaluation leaves the minted grant's facts on the bridge for the
//! dispatch that spends it; every other answer becomes the reducer's decision
//! about the proposal, recorded under the action that proposed it.

use core_runtime::wire;
use core_runtime::{ActionId, ActionResultCode, Command, DispatchId, ProposalDecision};

use super::{PendingTaskEffect, TaskGrantFacts};
use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};

pub(super) fn complete(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    terminal: ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let status = wire::PolicyEvaluationStatus::from_wire(u32::from(terminal.status));
    let decision = match status {
        Some(wire::PolicyEvaluationStatus::Granted) => {
            if terminal.capability_id.is_empty()
                || terminal.frame_id.is_empty()
                || terminal.page_epoch.is_empty()
                || terminal.has_opaque_origin_id != terminal.normalized_origin.is_empty()
                || terminal.has_opaque_origin_id != !terminal.opaque_origin_id.is_empty()
            {
                return response(
                    &pending.operation.operation_id,
                    wire::AdmissionStatus::InvalidCommand as u8,
                    Vec::new(),
                );
            }
            bridge.task_grants.insert(
                (pending.task_id.clone(), pending.action_id.clone()),
                TaskGrantFacts {
                    capability_id: terminal.capability_id.clone(),
                    frame_id: terminal.frame_id,
                    page_epoch: terminal.page_epoch,
                    graph_revision: terminal.graph_revision,
                    normalized_origin: terminal.normalized_origin,
                    opaque_origin_id: terminal
                        .has_opaque_origin_id
                        .then_some(terminal.opaque_origin_id),
                    destination_origin: terminal
                        .has_destination_origin
                        .then_some(terminal.destination_origin),
                    destination_address: terminal
                        .has_destination_address
                        .then_some(terminal.destination_address),
                },
            );
            ProposalDecision::Authorize(core_runtime::Authorization {
                capability_id: core_runtime::CapabilityId::new(terminal.capability_id),
            })
        }
        Some(wire::PolicyEvaluationStatus::ApprovalRequired) => ProposalDecision::RequireApproval,
        Some(wire::PolicyEvaluationStatus::Denied) => {
            ProposalDecision::Deny(core_runtime::Denial::new(denial_code(&terminal)))
        }
        // A policy terminal always settles the action. An answer the browser
        // could not evaluate — a malformed effect, a core it could not reach,
        // a status this build does not know — refuses the proposal as
        // unsupported, so the walk continues and the model is told, rather
        // than leaving the action open with nothing recorded against it.
        // Answering `InvalidCommand` here instead ended the whole core.
        Some(
            wire::PolicyEvaluationStatus::InvalidRequest
            | wire::PolicyEvaluationStatus::CoreUnavailable,
        )
        | None => ProposalDecision::Deny(core_runtime::Denial::new(ActionResultCode::Unsupported)),
    };
    let dispatch_id = matches!(decision, ProposalDecision::Authorize(_))
        .then(|| DispatchId::new(format!("dispatch-{}", pending.effect_id)));
    let command = Command::RecordPolicyDecision {
        action_id: ActionId::new(pending.action_id.clone()),
        decision: Box::new(decision),
        dispatch_id,
    };
    bridge.pending_task_effects.remove(&pending.effect_id);
    super::submit_completion_command(bridge, pending, command, now_monotonic_ms)
}

/// The code a denied evaluation carries, or the bare policy refusal when the
/// browser answered without one. `Verified` is not a refusal and reads as the
/// bare code too.
fn denial_code(terminal: &ffi::BridgeTaskTerminal) -> ActionResultCode {
    if !terminal.has_denial_code {
        return ActionResultCode::DeniedByPolicy;
    }
    match wire::TaskActionResultCode::from_wire(u32::from(terminal.denial_code)) {
        Some(wire::TaskActionResultCode::Verified) | None => ActionResultCode::DeniedByPolicy,
        Some(code) => core_runtime::action_result_code_from_wire(code),
    }
}
