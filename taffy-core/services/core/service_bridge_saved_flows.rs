// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Read-only saved-flow query. No state publication or durable write occurs.

use crate::service_bridge_runtime::ServiceBridge;
use crate::service_bridge_skills_ffi::ffi::{
    BridgeSavedFlowQuery, BridgeSavedFlowQueryResult, BridgeSavedFlowReview, BridgeSkillArgument,
    BridgeSkillOperation, BridgeSkillStep,
};
use core_runtime::wire;

#[allow(non_snake_case)]
pub(crate) fn QuerySavedFlows(
    bridge: &mut ServiceBridge,
    input: BridgeSavedFlowQuery,
    now_monotonic_ms: u64,
) -> BridgeSavedFlowQueryResult {
    let Some(kind) = wire::SavedFlowQueryKind::from_wire(u32::from(input.kind)) else {
        return refused(input.operation, wire::SavedFlowQueryStatus::InvalidRequest);
    };
    if input.operation.service_generation != bridge.generation.value()
        || input.operation.deadline_monotonic_ms < now_monotonic_ms
    {
        return refused(input.operation, wire::SavedFlowQueryStatus::StaleRequest);
    }
    let Some(runtime) = bridge.runtime.as_ref() else {
        return refused(input.operation, wire::SavedFlowQueryStatus::Unavailable);
    };
    let query = wire::SavedFlowQueryCommand {
        operation: wire::OperationEnvelope {
            operation_id: input.operation.operation_id.clone(),
            service_generation: input.operation.service_generation,
            task_revision: input.operation.task_revision,
            deadline_monotonic_ms: input.operation.deadline_monotonic_ms,
            idempotency_key: input.operation.idempotency_key.clone(),
        },
        kind,
        goal: input.goal,
        skill_id: input.skill_id,
        expected_version: input.expected_version,
    };
    let (status, flows) = runtime.query_saved_flows(&query);
    BridgeSavedFlowQueryResult {
        operation: input.operation,
        status: status as u8,
        flows: flows.into_iter().map(review).collect(),
    }
}

fn refused(
    operation: BridgeSkillOperation,
    status: wire::SavedFlowQueryStatus,
) -> BridgeSavedFlowQueryResult {
    BridgeSavedFlowQueryResult {
        operation,
        status: status as u8,
        flows: Vec::new(),
    }
}

fn review(flow: wire::SavedFlowReview) -> BridgeSavedFlowReview {
    BridgeSavedFlowReview {
        skill_id: flow.skill_id,
        origin: flow.origin,
        provenance: flow.provenance as u8,
        status: flow.status as u8,
        active_version: flow.active_version,
        step_count: flow.step_count,
        installed_at_epoch_ms: flow.installed_at_epoch_ms,
        updated_at_epoch_ms: flow.updated_at_epoch_ms,
        has_recorded_task: flow.recorded_from_task_id.is_some(),
        recorded_from_task_id: flow.recorded_from_task_id.unwrap_or_default(),
        reviewed_steps: flow
            .reviewed_steps
            .into_iter()
            .map(|step| BridgeSkillStep {
                verb: step.verb,
                postcondition: step.postcondition,
                has_fill: step.has_fill,
                fill_purpose: step.fill_purpose,
                arguments: step
                    .arguments
                    .into_iter()
                    .map(|arg| BridgeSkillArgument {
                        parameter: arg.parameter,
                        kind: arg.kind as u8,
                        value: arg.value,
                        purpose: arg.purpose,
                        has_public_address: arg.public_address.is_some(),
                        public_address: arg.public_address.unwrap_or_default(),
                        has_semantic_target: arg.semantic_target.is_some(),
                        semantic_role: arg.semantic_target.as_ref().map_or(0, |target| target.role),
                        semantic_phrase: arg.semantic_target.map_or(0, |target| target.phrase),
                    })
                    .collect(),
            })
            .collect(),
    }
}
