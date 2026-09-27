// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Core-executed Memory tools and exact browser-owned write settlement.

use core_runtime::wire;
use core_runtime::{
    ActionId, ActionOutcome, ActionResultCode, Command, DispatchId, MonotonicMillis, TaskId,
    TaskMemoryExecution, TaskMemoryExecutionError,
};

use super::PendingTaskEffect;
use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_task::TypedTaskResult;
use crate::service_bridge_workspace::{workspace_effect_response, WorkspaceCommandEffect};

pub(super) fn complete(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    if wire::TaskEffectCompletionStatus::from_wire(u32::from(terminal.status))
        != Some(wire::TaskEffectCompletionStatus::Succeeded)
        || terminal.action_id != pending.action_id
        || !terminal_payload_is_empty(terminal)
    {
        return response(
            &pending.operation.operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(
            &pending.operation.operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    let now_utc_millis = runtime.utc_millis();
    let execution = runtime.core_mut().begin_task_memory_tool(
        &TaskId::new(pending.task_id.clone()),
        &ActionId::new(pending.action_id.clone()),
        pending.effect_id.clone(),
        now_utc_millis,
    );
    match execution {
        Ok(TaskMemoryExecution::Search(outcome)) => {
            bridge.pending_task_effects.remove(&pending.effect_id);
            settle_action(
                bridge,
                pending,
                ActionResultCode::Verified,
                now_monotonic_ms,
                Some(outcome),
            )
        }
        Ok(TaskMemoryExecution::AlreadyCurrent) => {
            bridge.pending_task_effects.remove(&pending.effect_id);
            settle_action(
                bridge,
                pending,
                ActionResultCode::Verified,
                now_monotonic_ms,
                None,
            )
        }
        Ok(TaskMemoryExecution::Save(request)) => stage_write(
            bridge,
            pending,
            WorkspaceCommandEffect::MemorySave(*request),
        ),
        Ok(TaskMemoryExecution::Delete(request)) => stage_write(
            bridge,
            pending,
            WorkspaceCommandEffect::MemoryDelete(request),
        ),
        Err(error) => {
            bridge.pending_task_effects.remove(&pending.effect_id);
            settle_action(bridge, pending, result_code(error), now_monotonic_ms, None)
        }
    }
}

fn stage_write(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    effect: WorkspaceCommandEffect,
) -> ffi::BridgeResponse {
    let Some(active) = bridge.pending_task_effects.get_mut(&pending.effect_id) else {
        return response(
            &pending.operation.operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    if active.durable_storage_pending {
        return response(
            &pending.operation.operation_id,
            wire::AdmissionStatus::Duplicate as u8,
            Vec::new(),
        );
    }
    active.durable_storage_pending = true;
    workspace_effect_response(
        pending.operation.operation_id.clone(),
        pending.operation,
        effect,
    )
}

pub(crate) fn deliver_storage_completion(
    bridge: &mut ServiceBridge,
    completion: ffi::BridgeStorageCompletion,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let operation_id = completion.operation.operation_id.clone();
    let Some(pending) = bridge
        .pending_task_effects
        .get(&completion.effect_id)
        .filter(|value| {
            value.kind == wire::TaskReducerEffectKind::RunMemoryTool
                && value.durable_storage_pending
        })
        .cloned()
    else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    if !storage_completion_matches(&pending, &completion, bridge.generation.value()) {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    }
    let Some(status) = wire::EffectStatus::from_wire(u32::from(completion.status)) else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    let code = if status == wire::EffectStatus::Completed {
        if runtime
            .core_mut()
            .complete_memory_mutation(&pending.effect_id, completion.committed_revision)
            .is_err()
        {
            bridge.runtime = None;
            return response(
                &operation_id,
                wire::AdmissionStatus::CoreUnavailable as u8,
                Vec::new(),
            );
        }
        ActionResultCode::Verified
    } else {
        if !runtime
            .core_mut()
            .reject_memory_mutation(&pending.effect_id)
        {
            bridge.runtime = None;
            return response(
                &operation_id,
                wire::AdmissionStatus::CoreUnavailable as u8,
                Vec::new(),
            );
        }
        storage_result_code(status)
    };
    bridge.pending_task_effects.remove(&pending.effect_id);
    // The Memory mutation is durable, but the task action outcome is only
    // staged by settle_action. Its AppendTaskCommit terminal publishes the one
    // complete state after both causal writes have committed.
    settle_action(bridge, pending, code, now_monotonic_ms, None)
}

fn settle_action(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    code: ActionResultCode,
    now_monotonic_ms: u64,
    memory_search_result: Option<core_runtime::TaskMemorySearchTranscriptOutcome>,
) -> ffi::BridgeResponse {
    let command = Command::RecordActionOutcome {
        action_id: ActionId::new(pending.action_id.clone()),
        outcome: Box::new(ActionOutcome {
            code,
            dispatch_id: pending.dispatch_id.clone().map(DispatchId::new),
            observed_at: MonotonicMillis(now_monotonic_ms),
            observation: None,
            discovered_source: None,
        }),
    };
    super::completion_submit::with_typed_result(
        bridge,
        pending,
        command,
        now_monotonic_ms,
        memory_search_result.map(TypedTaskResult::MemorySearch),
    )
}

fn storage_completion_matches(
    pending: &PendingTaskEffect,
    completion: &ffi::BridgeStorageCompletion,
    generation: u64,
) -> bool {
    completion.effect_id == pending.effect_id
        && completion.operation.operation_id == pending.operation.operation_id
        && completion.operation.service_generation == generation
        && completion.operation.task_revision == pending.operation.task_revision
        && completion.operation.deadline_monotonic_ms == pending.operation.deadline_monotonic_ms
        && completion.operation.idempotency_key == pending.operation.idempotency_key
}

fn terminal_payload_is_empty(terminal: &ffi::BridgeTaskTerminal) -> bool {
    terminal.result_operation_id.is_empty()
        && terminal.capability_id.is_empty()
        && !terminal.has_discovered_source
        && !terminal.has_discovery_tab
        && !terminal.has_task_tab_result
        && !terminal.has_task_download_result
        && !terminal.has_observation
        && !terminal.has_media_observation
        && !terminal.has_model_completion
}

const fn result_code(error: TaskMemoryExecutionError) -> ActionResultCode {
    match error {
        TaskMemoryExecutionError::MissingResidentOperand => ActionResultCode::ValueReferenceUnknown,
        TaskMemoryExecutionError::Store(core_runtime::MemoryStoreError::PrivateProfile) => {
            ActionResultCode::DeniedByPolicy
        }
        TaskMemoryExecutionError::Store(
            core_runtime::MemoryStoreError::TooManyRecords
            | core_runtime::MemoryStoreError::TooManyPendingMutations,
        ) => ActionResultCode::BudgetExceeded,
        TaskMemoryExecutionError::Store(core_runtime::MemoryStoreError::DigestUnavailable) => {
            ActionResultCode::InternalError
        }
        TaskMemoryExecutionError::Store(_) => ActionResultCode::PostconditionFailed,
        TaskMemoryExecutionError::UnknownTask
        | TaskMemoryExecutionError::UnknownAction
        | TaskMemoryExecutionError::WrongActionState
        | TaskMemoryExecutionError::WrongIntent => ActionResultCode::InternalError,
    }
}

const fn storage_result_code(status: wire::EffectStatus) -> ActionResultCode {
    match status {
        wire::EffectStatus::Completed => ActionResultCode::Verified,
        wire::EffectStatus::Denied => ActionResultCode::DeniedByPolicy,
        wire::EffectStatus::Cancelled => ActionResultCode::CancelledByUser,
        wire::EffectStatus::DeadlineExceeded => ActionResultCode::PostconditionTimeout,
        wire::EffectStatus::ResourceLimit => ActionResultCode::BudgetExceeded,
        wire::EffectStatus::Unavailable => ActionResultCode::DispatchFailed,
        wire::EffectStatus::OutcomeUnknown => ActionResultCode::OutcomeUnknown,
        wire::EffectStatus::InvalidResult => ActionResultCode::InternalError,
    }
}
