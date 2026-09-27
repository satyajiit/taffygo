// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact terminal decoding for one dispatched tool job.

use core_runtime::wire;
use core_runtime::{ActionId, Command, TaskId};

use super::{completion_submit, PendingTaskEffect};
use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};

/// Records what became of one dispatched tool job, as a durable outcome.
///
/// Every terminal status has a durable answer, because the reducer settles
/// the action either way and the walk continues from what was recorded. A
/// succeeded terminal must carry the browser-validated output digest and
/// counts. Content never enters the Rust journal.
pub(super) fn complete(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let status = wire::TaskEffectCompletionStatus::from_wire(u32::from(terminal.status));
    let task_id = TaskId::new(pending.task_id.clone());
    let action_id = ActionId::new(pending.action_id.clone());
    let Some(status) = status.filter(|candidate| {
        !pending.action_id.is_empty()
            && terminal.action_id == pending.action_id
            && !terminal.has_observation
            && (terminal.has_tool_output
                == (*candidate == wire::TaskEffectCompletionStatus::Succeeded))
            && (!terminal.has_tool_output
                || (terminal.tool_output_digest.len() == 64
                    && terminal
                        .tool_output_digest
                        .bytes()
                        .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
                    && terminal.tool_output_bytes > 0
                    && terminal.tool_output_chunks == 1))
    }) else {
        return response(
            &pending.operation.operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let Ok(media_probe) = completion_submit::decode_media_probe(bridge, &pending, terminal, status)
    else {
        return response(
            &pending.operation.operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let job_status = match status {
        wire::TaskEffectCompletionStatus::Succeeded => core_runtime::ToolJobStatus::Succeeded,
        wire::TaskEffectCompletionStatus::Refused => core_runtime::ToolJobStatus::Failed,
        wire::TaskEffectCompletionStatus::Unavailable => core_runtime::ToolJobStatus::Unavailable,
        wire::TaskEffectCompletionStatus::Cancelled => core_runtime::ToolJobStatus::Cancelled,
        wire::TaskEffectCompletionStatus::OutcomeUnknown => {
            core_runtime::ToolJobStatus::OutcomeUnknown
        }
        wire::TaskEffectCompletionStatus::ValueReferenceUnknown => {
            return response(
                &pending.operation.operation_id,
                wire::AdmissionStatus::InvalidCommand as u8,
                Vec::new(),
            )
        }
    };
    let command = Command::RecordToolJobOutcome {
        job_id: core_runtime::job_id_for_action(&task_id, &action_id),
        action_id,
        outcome: Box::new(core_runtime::ToolJobOutcome {
            status: job_status,
            output_digest: terminal
                .has_tool_output
                .then(|| terminal.tool_output_digest.clone()),
            output_bytes: terminal.tool_output_bytes,
            output_chunks: terminal.tool_output_chunks,
        }),
    };
    bridge.pending_task_effects.remove(&pending.effect_id);
    if let Some(result) = media_probe {
        completion_submit::media_probe(bridge, pending, command, now_monotonic_ms, result)
    } else {
        super::submit_completion_command(bridge, pending, command, now_monotonic_ms)
    }
}
