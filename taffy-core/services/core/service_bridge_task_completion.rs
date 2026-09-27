// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Browser-owned journal terminals and ordered deferred Stop admission.

use core_runtime::wire;
use core_runtime::{CommitOutcome, TaskId};

use crate::ffi;
use crate::service_bridge_runtime::{
    commit_skill_run_plan, note_refusal, plan_skill_run, response,
    storage_completion_operation_to_wire, ServiceBridge,
};
use crate::service_bridge_status::{response_after_task_change, state_after_change};
use crate::service_bridge_task_effect::{
    project_committed_effects, project_committed_model_attempt,
};
use crate::service_bridge_task_submit::begin_submit;
use crate::service_bridge_workflow::continue_reviewed_workflow;

fn begin_deferred_task_cancellation(
    bridge: &mut ServiceBridge,
    task_id: &TaskId,
    now_monotonic_ms: u64,
) -> Option<ffi::BridgeResponse> {
    let pending = bridge.pending_task_cancellations.remove(task_id.as_str())?;
    let operation_id = pending.operation.operation_id.clone();
    if pending.task_id != *task_id {
        note_refusal(bridge, "cancellation_task_mismatch");
        bridge.runtime = None;
        return Some(response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        ));
    }
    if let Some(runtime) = bridge.runtime.as_mut() {
        runtime.drop_person_answer(task_id.as_str());
    }
    let submitted = begin_submit(
        bridge,
        pending.task_id,
        pending.envelope,
        pending.operation,
        now_monotonic_ms,
    );
    let valid = submitted.admission.status == wire::AdmissionStatus::Accepted as u8
        && submitted.admission.operation_id == operation_id
        && submitted.storage_effects.len() == 1
        && submitted.storage_effects[0].operation.operation_id == operation_id
        && submitted.storage_effects[0].task_id == task_id.as_str();
    if valid {
        return Some(submitted);
    }
    note_refusal(bridge, "cancellation_shape");
    bridge.runtime = None;
    Some(response(
        &operation_id,
        wire::AdmissionStatus::CoreUnavailable as u8,
        Vec::new(),
    ))
}

pub(crate) fn deliver_task_completion(
    bridge: &mut ServiceBridge,
    completion: ffi::BridgeStorageCompletion,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = completion.operation.operation_id.clone();
    let Some(task_id) = bridge.pending_submits.get(&operation_id).cloned() else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let Some(status) = wire::EffectStatus::from_wire(u32::from(completion.status)) else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let parent_operation = wire::OperationEnvelope {
        operation_id: completion.operation.operation_id.clone(),
        service_generation: completion.operation.service_generation,
        task_revision: completion.operation.task_revision,
        deadline_monotonic_ms: completion.operation.deadline_monotonic_ms,
        idempotency_key: completion.operation.idempotency_key.clone(),
    };
    let result = wire::EffectResult {
        operation: storage_completion_operation_to_wire(completion.operation),
        effect_id: completion.effect_id,
        status,
        kind: wire::EffectKind::StorageCommit,
        storage: Some(wire::StorageEffectResult {
            committed_revision: completion.committed_revision,
        }),
        observation: None,
        model: None,
        network: None,
        browser_action: None,
        tool: None,
        secure_store: None,
        auth_surface: None,
        permission: None,
        asset_delivery: None,
        catalog: None,
        provider_listing: None,
        composer_completion: None,
        custom_endpoint_probe: None,
    };
    let Ok(terminal) = core_runtime::decode_storage_completion(&result) else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let Some(runtime) = bridge.runtime.as_mut() else {
        note_refusal(bridge, "completion_runtime_missing");
        return response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    match runtime.core_mut().complete_commit(
        &TaskId::new(task_id.clone()),
        terminal,
        now_monotonic_ms,
    ) {
        Ok(CommitOutcome::Committed(accepted)) => {
            bridge.pending_submits.remove(&operation_id);
            let task_id = TaskId::new(task_id);
            let staged_attempt = bridge.pending_model_attempts.remove(&operation_id);
            if let Some(cancelled) =
                begin_deferred_task_cancellation(bridge, &task_id, now_monotonic_ms)
            {
                return cancelled;
            }
            let projected = match staged_attempt {
                // The guard reads the staged record; the arm then gives both
                // halves of it away, so nothing here holds a copy of a request
                // body that is about to cross the seam.
                Some(staged) if staged.pending.task_id == task_id.as_str() => {
                    project_committed_model_attempt(
                        bridge,
                        staged.pending,
                        staged.dispatch,
                        accepted,
                    )
                }
                Some(_) => Err(()),
                None => project_committed_effects(bridge, &task_id, accepted, &parent_operation),
            };
            let Ok(effects) = projected else {
                note_refusal(bridge, "completion_effect_projection");
                bridge.runtime = None;
                return response(
                    &operation_id,
                    wire::AdmissionStatus::CoreUnavailable as u8,
                    Vec::new(),
                );
            };
            // Nothing left the core for this commit, so the task is waiting on
            // this bridge rather than on the browser. That is true of a batch
            // the reducer left empty and equally true of one whose only effect
            // was a model call no plan could be built for — and in the second
            // case the next command is the one that records the gap.
            // A committed bootstrap read may leave while the next source's
            // proposal is prepared. Every other browser effect stays a barrier.
            let mut durable_state = None;
            if effects.iter().all(|effect| {
                bridge
                    .pending_task_effects
                    .get(&effect.effect_id)
                    .is_some_and(|pending| {
                        pending.task_id == task_id.as_str()
                            && pending.is_bootstrap_observation(bridge)
                    })
            }) {
                // The commit that just landed is published before the walk
                // chains its next command — the same rule `DeliverStorageCompletion`
                // applies to a start's open commit, for the same reason: every
                // hop of the walk is then one status publication, so a surface
                // follows a task from planning through each turn instead of
                // hearing from it only when an effect leaves the core. The
                // snapshot is taken first, so it is the journaled state and not
                // the command applied in memory below. When nothing chains,
                // the final response must keep this snapshot too: creating a
                // second one would skip its already-reserved sequence.
                let durable = state_after_change(bridge);
                if durable.is_empty() {
                    note_refusal(bridge, "completion_durable_state_empty");
                    return response(
                        &operation_id,
                        wire::AdmissionStatus::CoreUnavailable as u8,
                        Vec::new(),
                    );
                }
                match continue_reviewed_workflow(bridge, &task_id, now_monotonic_ms) {
                    Ok(Some(mut chained)) => {
                        chained.states = durable;
                        // Publish the read beside the revision that authorized
                        // it, before the newly staged command becomes durable.
                        // A state projection failure above releases no effect.
                        if let Some(state) = chained.states.first_mut() {
                            state.task_effects = effects;
                        }
                        return chained;
                    }
                    Ok(None) => durable_state = Some(durable),
                    Err(()) => {
                        bridge.runtime = None;
                        return response(
                            &operation_id,
                            wire::AdmissionStatus::CoreUnavailable as u8,
                            Vec::new(),
                        );
                    }
                }
            }
            let skill_run = match plan_skill_run(bridge, &task_id, now_monotonic_ms, now_utc_millis)
            {
                Ok(plan) => plan,
                Err(()) => {
                    note_refusal(bridge, "completion_skill_run_plan");
                    bridge.runtime = None;
                    return response(
                        &operation_id,
                        wire::AdmissionStatus::CoreUnavailable as u8,
                        Vec::new(),
                    );
                }
            };
            let mut published = response(
                &operation_id,
                wire::AdmissionStatus::Accepted as u8,
                Vec::new(),
            );
            if let Some(mut states) = durable_state {
                // `durable` was checked above before the continuation ran.
                states[0].task_effects = effects;
                published.states = states;
            } else {
                published = response_after_task_change(bridge, published, effects);
            }
            if !published.states.is_empty() {
                if let Some(plan) = skill_run {
                    commit_skill_run_plan(bridge, &plan);
                    published.storage_effects.push(plan.effect);
                }
                if let Some(effect) = crate::service_bridge_skills::stage_completed_flow(
                    bridge,
                    &task_id,
                    now_monotonic_ms,
                    now_utc_millis,
                ) {
                    published.storage_effects.push(effect);
                }
            }
            published
        }
        Ok(CommitOutcome::RecoveryRequired { .. }) => {
            note_refusal(bridge, "completion_recovery_required");
            bridge.pending_submits.remove(&operation_id);
            bridge.pending_model_attempts.remove(&operation_id);
            bridge.pending_task_cancellations.remove(&task_id);
            response(
                &operation_id,
                wire::AdmissionStatus::CoreUnavailable as u8,
                Vec::new(),
            )
        }
        Err(_) => response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        ),
    }
}
