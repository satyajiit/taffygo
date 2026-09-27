// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable submission shared by effect-terminal decoders.

use core_runtime::wire;
use core_runtime::{Command, CommandEnvelope, IdempotencyKey, TaskId, TraceId};

use super::PendingTaskEffect;
use crate::ffi;
use crate::service_bridge_runtime::{note_refusal, response, PendingModelAttempt, ServiceBridge};
use crate::service_bridge_task::{begin_submit, begin_submit_typed, TypedTaskResult};

/// Validates the optional scalar companion to one durable tool receipt.
///
/// Presence is derived from the exact pending action, not from the terminal:
/// a probe success with no facts and a non-probe success carrying facts are
/// both another effect shape wearing this effect's identity.
pub(super) fn decode_media_probe(
    bridge: &ServiceBridge,
    pending: &PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    status: wire::TaskEffectCompletionStatus,
) -> Result<Option<core_runtime::MediaProbeTranscriptOutcome>, ()> {
    let task_id = TaskId::new(pending.task_id.clone());
    let action_id = core_runtime::ActionId::new(pending.action_id.clone());
    let expects_media_probe = bridge
        .runtime
        .as_ref()
        .and_then(|runtime| runtime.core().task(&task_id))
        .and_then(|task| task.action_effect_facts(&action_id))
        .is_some_and(|facts| {
            matches!(
                facts.proposal.intent(),
                core_runtime::ActionIntent::MediaTool(media)
                    if media.operation == core_runtime::MediaOperation::Probe
            )
        });
    let succeeded = status == wire::TaskEffectCompletionStatus::Succeeded;
    if terminal.has_media_probe != (succeeded && expects_media_probe) {
        return Err(());
    }
    if !terminal.has_media_probe {
        return (terminal.media_probe_duration_ms == 0
            && terminal.media_probe_audio_streams == 0
            && terminal.media_probe_video_streams == 0
            && terminal.media_probe_width_px == 0
            && terminal.media_probe_height_px == 0)
            .then_some(None)
            .ok_or(());
    }
    core_runtime::MediaProbeTranscriptOutcome::new(
        terminal.media_probe_duration_ms,
        terminal.media_probe_audio_streams,
        terminal.media_probe_video_streams,
        terminal.media_probe_width_px,
        terminal.media_probe_height_px,
    )
    .map(Some)
    .map_err(|_| ())
}

pub(super) fn media_probe(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    command: Command,
    now_monotonic_ms: u64,
    result: core_runtime::MediaProbeTranscriptOutcome,
) -> ffi::BridgeResponse {
    with_typed_result(
        bridge,
        pending,
        command,
        now_monotonic_ms,
        Some(TypedTaskResult::MediaProbe(result)),
    )
}

pub(super) fn plain(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    command: Command,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    with_typed_result(bridge, pending, command, now_monotonic_ms, None)
}

/// Stages the budget charge for one exact live model sub-attempt.
///
/// The route plan is retained only when `begin_submit` produced its one
/// storage commit. The browser receives no model binding until that commit
/// returns successfully through `deliver_task_completion`.
pub(super) fn model_attempt(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    dispatch: core_runtime::ModelAttemptDispatch,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let task_id = TaskId::new(pending.task_id.clone());
    let revision = bridge
        .runtime
        .as_ref()
        .and_then(|runtime| runtime.core().task(&task_id))
        .map_or(0, core_runtime::TaskEnginePort::revision);
    let operation_id = format!("completion-{}", pending.effect_id);
    let idempotency_key = format!("completion-key-{}", pending.effect_id);
    let operation = wire::OperationEnvelope {
        operation_id: operation_id.clone(),
        service_generation: bridge.generation.value(),
        task_revision: revision,
        deadline_monotonic_ms: pending.operation.deadline_monotonic_ms,
        idempotency_key: idempotency_key.clone(),
    };
    let envelope = CommandEnvelope::new(
        IdempotencyKey::new(idempotency_key),
        revision,
        TraceId::new(format!("completion-trace-{}", pending.effect_id)),
        Command::RequestModelAttempt {
            call_id: core_runtime::ModelCallId::new(pending.call_id.clone()),
            attempt_ordinal: dispatch.attempt_ordinal,
            candidate_ordinal: dispatch.candidate_ordinal,
            kind: dispatch.kind,
        },
    );
    let submitted = begin_submit(bridge, task_id, envelope, operation, now_monotonic_ms);
    if submitted.admission.status == wire::AdmissionStatus::Accepted as u8 {
        if submitted.storage_effects.len() != 1
            || !submitted.states.is_empty()
            || bridge
                .pending_model_attempts
                .insert(
                    operation_id.clone(),
                    PendingModelAttempt { pending, dispatch },
                )
                .is_some()
        {
            bridge.pending_submits.remove(&operation_id);
            bridge.pending_model_attempts.remove(&operation_id);
            note_refusal(bridge, "model_attempt_shape");
            bridge.runtime = None;
            return response(
                &operation_id,
                wire::AdmissionStatus::CoreUnavailable as u8,
                Vec::new(),
            );
        }
    }
    submitted
}

pub(super) fn with_typed_result(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    command: Command,
    now_monotonic_ms: u64,
    typed_result: Option<TypedTaskResult>,
) -> ffi::BridgeResponse {
    let task_id = TaskId::new(pending.task_id);
    let revision = bridge
        .runtime
        .as_ref()
        .and_then(|runtime| runtime.core().task(&task_id))
        .map_or(0, core_runtime::TaskEnginePort::revision);
    let operation_id = format!("completion-{}", pending.effect_id);
    let idempotency_key = format!("completion-key-{}", pending.effect_id);
    let trace_id = format!("completion-trace-{}", pending.effect_id);
    let operation = wire::OperationEnvelope {
        operation_id,
        service_generation: bridge.generation.value(),
        task_revision: revision,
        // The browser operation has finished (possibly by deadline). Its
        // exact envelope remains the callback identity; recording that result
        // gets its own bounded storage deadline.
        deadline_monotonic_ms: now_monotonic_ms.saturating_add(30_000),
        idempotency_key: idempotency_key.clone(),
    };
    let envelope = CommandEnvelope::new(
        IdempotencyKey::new(idempotency_key),
        revision,
        TraceId::new(trace_id),
        command,
    );
    begin_submit_typed(
        bridge,
        task_id,
        envelope,
        operation,
        now_monotonic_ms,
        typed_result,
    )
}
