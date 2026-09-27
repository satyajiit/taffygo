// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Immutable reducer facts to one closed generated task-effect binding.

mod binding;
mod empty;
mod media;
mod python;

use core_runtime::wire;
use core_runtime::{Accepted, ActionId, ArtifactCustody, Effect, TaskId};

use self::binding::project_effect;
use super::enums::{effect_action_id, effect_kind};
use super::model::{compose_model_turn, hold_composed_turn, ComposedTurn};
use super::{PendingTaskEffect, RestoredTaskEffects};
use crate::ffi;
use crate::service_bridge_runtime::ServiceBridge;
use crate::service_bridge_task_support::model_attempt_identities;
use crate::service_bridge_task_support::task_effect_identities;

/// Projects one router-authorized paid sub-attempt after its exact budget
/// charge and effect intent are durable.
///
/// `pending` is taken by value because it becomes the next pending effect:
/// the staged attempt that carried it was removed from
/// `pending_model_attempts` by the caller, so the caller has nothing left to
/// read from it, and re-keying it in place is one move rather than a clone
/// and a drop.
pub(crate) fn project_committed_model_attempt(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    dispatch: core_runtime::ModelAttemptDispatch,
    mut accepted: Accepted,
) -> Result<Vec<ffi::BridgeTaskEffect>, ()> {
    let effect = accepted.effects.pop().ok_or(())?;
    let Effect::CallModel { call_id } = effect else {
        return Err(());
    };
    let request = dispatch.request;
    if pending.kind != wire::TaskReducerEffectKind::CallModel
        || !accepted.effects.is_empty()
        || call_id.as_str() != pending.call_id
        || dispatch.attempt_ordinal != pending.attempt_ordinal.checked_add(1).ok_or(())?
        || dispatch.attempt_ordinal >= core_runtime::MAX_MODEL_ATTEMPTS_PER_TURN
        || (request.not_before_monotonic_ms != 0
            && request.not_before_monotonic_ms >= pending.operation.deadline_monotonic_ms)
    {
        return Err(());
    }
    let (operation_id, effect_id, idempotency_key) =
        model_attempt_identities(&pending.root_effect_id, dispatch.attempt_ordinal).ok_or(())?;
    let operation = wire::OperationEnvelope {
        operation_id,
        service_generation: bridge.generation.value(),
        task_revision: accepted.revision,
        deadline_monotonic_ms: pending.operation.deadline_monotonic_ms,
        idempotency_key,
    };
    let task_id = TaskId::new(pending.task_id.clone());
    let mut binding = empty::empty_binding(&task_id, 0, effect_id.clone(), operation.clone());
    binding.kind = wire::TaskReducerEffectKind::CallModel as u8;
    super::model::project_model_request(
        &mut binding,
        &core_runtime::ModelCallId::new(pending.call_id.clone()),
        request,
    )?;
    bridge.pending_task_effects.remove(&pending.effect_id);
    let mut next = pending;
    next.operation = operation;
    next.effect_id = effect_id.clone();
    next.attempt_ordinal = dispatch.attempt_ordinal;
    next.stream_sequence = 0;
    next.answer_sequence = 0;
    if bridge
        .pending_task_effects
        .insert(effect_id, next)
        .is_some()
    {
        return Err(());
    }
    Ok(vec![binding])
}

pub(crate) fn project_committed_effects(
    bridge: &mut ServiceBridge,
    task_id: &TaskId,
    accepted: Accepted,
    parent_operation: &wire::OperationEnvelope,
) -> Result<Vec<ffi::BridgeTaskEffect>, ()> {
    project_effect_batch(
        bridge,
        task_id,
        accepted.revision,
        accepted.effects,
        parent_operation,
        0,
    )
}

pub(crate) fn project_initial_effects(
    bridge: &mut ServiceBridge,
    task_id: &TaskId,
    task_revision: u64,
    effects: Vec<Effect>,
    parent_operation: &wire::OperationEnvelope,
) -> Result<Vec<ffi::BridgeTaskEffect>, ()> {
    project_effect_batch(bridge, task_id, task_revision, effects, parent_operation, 0)
}

pub(crate) fn project_restored_effects(
    bridge: &mut ServiceBridge,
    restored: RestoredTaskEffects,
    ordinal_start: u32,
) -> Result<Vec<ffi::BridgeTaskEffect>, ()> {
    let deadline_monotonic_ms = bridge
        .initial_monotonic_millis
        .checked_add(30_000)
        .ok_or(())?;
    let parent_operation = wire::OperationEnvelope {
        operation_id: restored.parent_operation_id,
        service_generation: bridge.generation.value(),
        task_revision: restored.task_revision,
        deadline_monotonic_ms,
        idempotency_key: "restored-task-effect-batch".to_owned(),
    };
    project_effect_batch(
        bridge,
        &restored.task_id,
        restored.task_revision,
        restored.effects,
        &parent_operation,
        ordinal_start,
    )
}

fn project_effect_batch(
    bridge: &mut ServiceBridge,
    task_id: &TaskId,
    task_revision: u64,
    effects: Vec<Effect>,
    parent_operation: &wire::OperationEnvelope,
    ordinal_start: u32,
) -> Result<Vec<ffi::BridgeTaskEffect>, ()> {
    if effects.len() > wire::MAX_TASK_EFFECTS_PER_STATE {
        return Err(());
    }
    let mut projected = Vec::with_capacity(effects.len());
    for (index, effect) in effects.into_iter().enumerate() {
        let artifact_content = match &effect {
            Effect::GenerateArtifact {
                artifact_id,
                kind,
                workspace_revision,
            } => {
                let runtime = bridge.runtime.as_mut().ok_or(())?;
                match workspace_format(*kind) {
                    Some(format) => runtime
                        .core_mut()
                        .ensure_task_artifact(
                            task_id,
                            artifact_id.as_str(),
                            *workspace_revision,
                            format,
                        )
                        .map_err(|_| ())?,
                    None => runtime
                        .core()
                        .ensure_rich_task_artifact(task_id, *workspace_revision, *kind)
                        .map_err(|_| ())?,
                }
                Vec::new()
            }
            Effect::ExportArtifact {
                artifact_id,
                kind,
                workspace_revision,
            } => {
                let custody = bridge
                    .runtime
                    .as_ref()
                    .ok_or(())?
                    .core()
                    .task_artifact_custody(task_id, artifact_id)
                    .ok_or(())?;
                let content = if custody == ArtifactCustody::Browser {
                    Vec::new()
                } else if let Some(format) = workspace_format(*kind) {
                    bridge
                        .runtime
                        .as_mut()
                        .ok_or(())?
                        .core_mut()
                        .publish_task_artifact(
                            task_id,
                            artifact_id.as_str(),
                            artifact_id.as_str(),
                            *workspace_revision,
                            format,
                        )
                        .map_err(|_| ())?
                        .content
                        .into_bytes()
                } else {
                    bridge
                        .runtime
                        .as_ref()
                        .ok_or(())?
                        .core()
                        .export_rich_task_artifact(task_id, *workspace_revision, *kind)
                        .map_err(|_| ())?
                };
                if (custody == ArtifactCustody::Workspace && content.is_empty())
                    || content.len() > wire::MAX_TASK_ARTIFACT_EXPORT_BYTES
                {
                    return Err(());
                }
                content
            }
            _ => Vec::new(),
        };
        let composed = match compose_model_turn(bridge, task_id, &effect)? {
            ComposedTurn::None => None,
            ComposedTurn::Turn(turn) => Some(turn),
            // The call is recorded as a gap rather than dispatched, and the
            // batch carries on. Failing it whole would poison the runtime for
            // what is often an ordinary fact about a device — a person who has
            // not saved a provider key yet — and taking a browser's core down
            // for that is not a refusal, it is an outage.
            ComposedTurn::Refused => continue,
        };
        let batch_ordinal = u32::try_from(index).map_err(|_| ())?;
        // Two different numbers, and conflating them is how one skipped call
        // takes the browser's whole assistant down.
        //
        // `batch_ordinal` is the effect's place in the batch its reducer
        // produced, and it is an input to the durable identity below, so it
        // stays that even when an earlier effect in the same batch was
        // skipped: moving it would mint a second identity for work that has
        // already been claimed. The published ordinal is a different fact —
        // where this effect sits in the vector the state actually carries —
        // and the browser reads it as exactly that, refusing a state whose
        // ordinals do not run with their positions
        // (`rust_core_state.cc`, at=task-effect/ordinal).
        //
        // The `continue` above skips a model call the profile could not plan.
        // Counting the source index rather than the published one left a hole
        // at every skip, so the next effect in the batch arrived claiming a
        // position it was not in, the state would not project, and the core
        // died mid-task — and then refused to start at all, because the same
        // effects come back through the restore path on the next launch.
        let published_ordinal = u32::try_from(projected.len()).map_err(|_| ())?;
        let ordinal = ordinal_start.checked_add(published_ordinal).ok_or(())?;
        let kind = effect_kind(&effect);
        // A paid model call is the durable logical work: restoring it after a
        // utility-process death must rediscover the same execution claim, not
        // mint permission to spend again. Other reducer effects are live or
        // retryable attempts, so their identities carry the full browser
        // incarnation. The batch-local ordinal is identity; `ordinal` above is
        // only this snapshot's presentation position.
        let identities = if matches!(
            kind,
            wire::TaskReducerEffectKind::CallModel
                | wire::TaskReducerEffectKind::RunToolJob
                | wire::TaskReducerEffectKind::RunLibraryTool
                | wire::TaskReducerEffectKind::RunMemoryTool
        ) {
            task_effect_identities(
                core_runtime::effect_identity::TaskEffectIdentityScope::DurableWork,
                task_id.as_str(),
                task_revision,
                &parent_operation.operation_id,
                batch_ordinal,
                kind,
            )
        } else {
            let browser_session_id = bridge
                .runtime
                .as_ref()
                .ok_or(())?
                .browser_session_id()
                .as_str();
            task_effect_identities(
                core_runtime::effect_identity::TaskEffectIdentityScope::LiveAttempt {
                    browser_session_id,
                    generation: bridge.generation.value(),
                },
                task_id.as_str(),
                task_revision,
                &parent_operation.operation_id,
                batch_ordinal,
                kind,
            )
        };
        let (operation_id, effect_id, idempotency_key) = identities.ok_or(())?;
        let mut operation = wire::OperationEnvelope {
            operation_id,
            service_generation: bridge.generation.value(),
            task_revision,
            deadline_monotonic_ms: parent_operation.deadline_monotonic_ms,
            idempotency_key,
        };
        let action_id = effect_action_id(&effect).map_or_else(String::new, |id| id.0.clone());
        let action = if action_id.is_empty() {
            None
        } else {
            bridge
                .runtime
                .as_ref()
                .and_then(|runtime| runtime.core().task(task_id))
                .and_then(|task| task.action_effect_facts(&ActionId::new(action_id.clone())))
        };
        // An action's idempotency identity is fixed when the proposal is
        // created. ASK_POLICY, REQUEST_APPROVAL, and DISPATCH_ACTION are
        // different effects about that same logical action; giving them a
        // batch-derived task-effect key breaks the grant/action binding and
        // makes a real Rust projection fail the browser's closed validator.
        // Keep the separately derived operation and effect identities, but
        // carry the proposal key in the operation envelope whenever an action
        // owns the effect. Effects with no action retain their own retry key.
        if let Some(facts) = action.as_ref() {
            operation.idempotency_key = facts.proposal.idempotency_key.as_str().to_owned();
        }
        let dispatch_id = action.as_ref().and_then(|facts| facts.dispatch_id.clone());
        let binding = project_effect(
            bridge,
            task_id,
            ordinal,
            effect_id.clone(),
            operation.clone(),
            effect,
            action,
            composed.as_deref(),
            artifact_content,
        )?;
        // The plan is retained for exactly as long as the call it plans is in
        // flight. Reading the reply is a comparison against the request that
        // was sent, and every number that comparison needs — the protocol
        // family, the context window, the answer allowance — is on the plan
        // and on nothing else that crosses.
        if let Some(turn) = composed {
            hold_composed_turn(bridge, task_id, &binding.call_id, turn);
        }
        let action_operation = (!action_id.is_empty())
            .then(|| wire::TaskActionOperationKind::from_wire(u32::from(binding.action_operation)))
            .flatten();
        if bridge
            .pending_task_effects
            .insert(
                effect_id.clone(),
                PendingTaskEffect {
                    operation,
                    root_effect_id: effect_id.clone(),
                    effect_id,
                    attempt_ordinal: 0,
                    task_id: task_id.as_str().to_owned(),
                    action_id,
                    tab_id: binding.tab_id.clone(),
                    call_id: binding.call_id.clone(),
                    stream_sequence: 0,
                    answer_sequence: 0,
                    dispatch_id,
                    kind,
                    action_operation,
                    durable_storage_pending: false,
                },
            )
            .is_some()
        {
            return Err(());
        }
        projected.push(binding);
    }
    Ok(projected)
}

const fn workspace_format(
    kind: core_runtime::ArtifactKind,
) -> Option<core_runtime::WorkspaceExportFormat> {
    match kind {
        core_runtime::ArtifactKind::Markdown => Some(core_runtime::WorkspaceExportFormat::Markdown),
        core_runtime::ArtifactKind::Csv => Some(core_runtime::WorkspaceExportFormat::Csv),
        core_runtime::ArtifactKind::Xlsx
        | core_runtime::ArtifactKind::Pdf
        | core_runtime::ArtifactKind::Docx
        | core_runtime::ArtifactKind::Pptx
        | core_runtime::ArtifactKind::WaveAudio
        | core_runtime::ArtifactKind::FrameArchive => None,
    }
}
