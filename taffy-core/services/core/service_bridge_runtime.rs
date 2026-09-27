// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical runtime calls behind the CXX data projection.

mod restore;
mod skill;

use std::collections::{BTreeMap, BTreeSet};

use core_runtime::wire;
use core_runtime::{
    decode_storage_completion, CommandEnvelope, OpenTaskCommitOutcome, PageSnapshotExporter,
    ProfileServiceRuntime, ServiceGeneration, TaskId,
};

use crate::service_bridge_state_ffi::ffi::BridgeInitializationStatus;
pub(crate) use restore::{ChromiumDigest, CreateServiceBridge};

use self::skill::PendingSkillRun;
pub(crate) use self::skill::{commit_skill_run_plan, plan_skill_run};

use crate::ffi;
use crate::service_bridge_configuration::PendingAssistantConfiguration;
use crate::service_bridge_skills::PendingSkillMutation;
use crate::service_bridge_status::{next_state, response_after_task_change, state_after_change};
use crate::service_bridge_task_effect::{
    project_initial_effects, PendingTaskEffect, PendingTaskEffects, RestoredTaskEffects, TaskGrants,
};
use crate::service_bridge_workflow::continue_reviewed_workflow;

pub(crate) struct ServiceBridge {
    pub(crate) runtime: Option<ProfileServiceRuntime>,
    pub(crate) initialization_status: BridgeInitializationStatus,
    pub(crate) generation: ServiceGeneration,
    pub(crate) state_sequence: u64,
    pub(crate) pending_opens: BTreeMap<String, String>,
    pub(crate) pending_submits: BTreeMap<String, String>,
    pub(crate) pending_task_cancellations: BTreeMap<String, PendingTaskCancellation>,
    pub(crate) pending_skill_runs: BTreeMap<String, PendingSkillRun>,
    pub(crate) pending_assistant_configurations: BTreeMap<String, PendingAssistantConfiguration>,
    pub(crate) pending_skill_mutations: BTreeMap<String, PendingSkillMutation>,
    pub(crate) page_snapshot_exporter: PageSnapshotExporter,
    pub(crate) recorded_skill_run_tasks: BTreeSet<String>,
    pub(crate) pending_task_effects: PendingTaskEffects,
    /// Exact live route plans waiting for their subattempt charge to commit.
    pub(crate) pending_model_attempts: BTreeMap<String, PendingModelAttempt>,
    pub(crate) task_grants: TaskGrants,
    /// Every model call of every task open in this profile generation.
    pub(crate) restored_task_effects: Vec<RestoredTaskEffects>,
    pub(crate) initial_monotonic_millis: u64,
    pub(crate) initial_utc_millis: u64,
    /// The content-free name of the branch that last answered
    /// `CoreUnavailable` or refused a restore. The wire carries one admission
    /// status and nothing else, so without this a dead core generation names
    /// no branch; the browser reads it beside the status it logs.
    pub(crate) last_refusal: &'static str,
}

/// Records which branch is about to answer that the core is unavailable.
pub(crate) fn note_refusal(bridge: &mut ServiceBridge, label: &'static str) {
    bridge.last_refusal = label;
}

/// The last recorded refusal, for the browser's log line.
#[allow(non_snake_case)]
pub(crate) fn LastRefusal(bridge: &ServiceBridge) -> String {
    bridge.last_refusal.to_owned()
}

pub(crate) struct PendingTaskCancellation {
    pub(crate) task_id: TaskId,
    pub(crate) envelope: CommandEnvelope,
    pub(crate) operation: wire::OperationEnvelope,
}

#[derive(Debug)]
pub(crate) struct PendingModelAttempt {
    pub(crate) pending: PendingTaskEffect,
    pub(crate) dispatch: core_runtime::ModelAttemptDispatch,
}

#[allow(non_snake_case)]
pub(crate) fn Initialization(bridge: &mut ServiceBridge) -> ffi::BridgeInitialization {
    if bridge.runtime.is_none() {
        return invalid_initialization(bridge.initialization_status);
    }
    let Ok(state) = next_state(bridge) else {
        note_refusal(bridge, "restore_state_projection");
        bridge.runtime = None;
        return invalid_initialization(BridgeInitializationStatus::StateProjectionRefused);
    };
    let resumable_tasks = state
        .accepted_task_consents
        .iter()
        .map(|consent| TaskId::new(consent.task_id.clone()))
        .collect::<Vec<_>>();
    let mut storage_effects = Vec::new();
    let restore_now_monotonic_ms = bridge.initial_monotonic_millis;
    for task_id in resumable_tasks {
        match continue_reviewed_workflow(bridge, &task_id, restore_now_monotonic_ms) {
            Ok(Some(response))
                if response.admission.status == wire::AdmissionStatus::Accepted as u8
                    && response.storage_effects.len() == 1
                    && response.states.is_empty() =>
            {
                storage_effects.extend(response.storage_effects);
            }
            Ok(Some(_)) => {
                note_refusal(bridge, "restore_walk_shape");
                bridge.runtime = None;
                return invalid_initialization(
                    BridgeInitializationStatus::ReviewedWorkflowRestoreRefused,
                );
            }
            Err(()) => {
                bridge.runtime = None;
                return invalid_initialization(
                    BridgeInitializationStatus::ReviewedWorkflowRestoreRefused,
                );
            }
            Ok(None) => {}
        }
    }
    let terminal_tasks = state
        .terminal_tasks
        .iter()
        .map(|task| TaskId::new(task.task_id.clone()))
        .collect::<Vec<_>>();
    for task_id in terminal_tasks {
        let plan = match plan_skill_run(
            bridge,
            &task_id,
            restore_now_monotonic_ms,
            bridge.initial_utc_millis,
        ) {
            Ok(plan) => plan,
            Err(()) => {
                note_refusal(bridge, "restore_skill_run_plan");
                bridge.runtime = None;
                return invalid_initialization(BridgeInitializationStatus::SkillRunRestoreRefused);
            }
        };
        if let Some(plan) = plan {
            commit_skill_run_plan(bridge, &plan);
            storage_effects.push(plan.effect);
        }
    }
    // The start-up plan, taken once the browser's scan of the asset store has
    // been adopted. It is answered here rather than waiting for a command
    // because nothing would send one: an artifact the product needs and does
    // not have is a fact about start-up, not a request from a person.
    let asset_effects =
        crate::service_bridge_assets::initial_effects(bridge, restore_now_monotonic_ms);
    ffi::BridgeInitialization {
        status: BridgeInitializationStatus::Ready,
        accepted_generation: bridge.generation.value(),
        storage_effects,
        asset_effects,
        states: vec![state],
    }
}

fn invalid_initialization(status: BridgeInitializationStatus) -> ffi::BridgeInitialization {
    ffi::BridgeInitialization {
        status,
        accepted_generation: 0,
        storage_effects: Vec::new(),
        asset_effects: Vec::new(),
        states: Vec::new(),
    }
}

pub(crate) fn storage_completion_operation_to_wire(
    value: crate::service_bridge_storage_completion_ffi::ffi::BridgeStorageCompletionOperation,
) -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: value.operation_id,
        service_generation: value.service_generation,
        task_revision: value.task_revision,
        deadline_monotonic_ms: value.deadline_monotonic_ms,
        idempotency_key: value.idempotency_key,
    }
}

#[allow(non_snake_case)]
pub(crate) fn DeliverStorageCompletion(
    bridge: &mut ServiceBridge,
    completion: ffi::BridgeStorageCompletion,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = completion.operation.operation_id.clone();
    if bridge
        .pending_assistant_configurations
        .contains_key(&operation_id)
    {
        return crate::service_bridge_configuration::deliver_assistant_configuration_completion(
            bridge, completion,
        );
    }
    if bridge.pending_skill_mutations.contains_key(&operation_id) {
        return crate::service_bridge_skills::deliver_skill_mutation_completion(bridge, completion);
    }
    if let Some(expected) = bridge.pending_skill_runs.get(&operation_id) {
        if completion.operation.service_generation != expected.service_generation
            || completion.operation.task_revision != expected.task_revision
            || completion.operation.deadline_monotonic_ms != expected.deadline_monotonic_ms
            || completion.operation.idempotency_key != expected.idempotency_key
            || completion.effect_id != expected.effect_id
        {
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
        let task_id = expected.task_id.clone();
        bridge.pending_skill_runs.remove(&operation_id);
        let accepted = status == wire::EffectStatus::Completed;
        if !accepted {
            bridge.recorded_skill_run_tasks.remove(&task_id);
        }
        return response(
            &operation_id,
            if accepted {
                wire::AdmissionStatus::Accepted as u8
            } else {
                wire::AdmissionStatus::CoreUnavailable as u8
            },
            Vec::new(),
        );
    }
    if bridge.pending_submits.contains_key(&operation_id) {
        return crate::service_bridge_task::deliver_task_completion(
            bridge,
            completion,
            now_monotonic_ms,
            now_utc_millis,
        );
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(&operation_id, 6, Vec::new());
    };
    runtime.set_utc_millis(now_utc_millis);
    let Some(task_id) = bridge.pending_opens.get(&operation_id).cloned() else {
        return crate::service_bridge_workspace::deliver_workspace_completion(
            bridge,
            completion,
            now_monotonic_ms,
        );
    };
    let Some(status) = wire::EffectStatus::from_wire(u32::from(completion.status)) else {
        return response(&operation_id, 5, Vec::new());
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
    let parent_operation = result.operation.clone();
    let Ok(terminal) = decode_storage_completion(&result) else {
        return response(&operation_id, 5, Vec::new());
    };
    let task_id = TaskId::new(task_id);
    let outcome = runtime
        .core_mut()
        .complete_open_task(&task_id, terminal, now_monotonic_ms);
    match outcome {
        Ok(OpenTaskCommitOutcome::Opened(opened)) => {
            bridge.pending_opens.remove(&operation_id);
            let projected = match project_initial_effects(
                bridge,
                &task_id,
                opened.revision,
                opened.effects,
                &parent_operation,
            ) {
                Ok(projected) => projected,
                Err(()) => {
                    bridge.runtime = None;
                    return response(
                        &operation_id,
                        wire::AdmissionStatus::CoreUnavailable as u8,
                        Vec::new(),
                    );
                }
            };
            if projected.is_empty() {
                // The task is durable now, and this is the first moment a
                // surface can be told so. The snapshot is taken before the
                // walk chains its next command, so what is published is what
                // the journal holds — a restart would restore exactly this —
                // and never a command applied in memory whose commit is still
                // in flight. Before this, the open, the executor start and the
                // first model turn chained with no publication between them,
                // and an admitted start showed nothing until some effect
                // happened to leave the core (verification report 2.7).
                let durable = state_after_change(bridge);
                if durable.is_empty() {
                    return response(
                        &operation_id,
                        wire::AdmissionStatus::CoreUnavailable as u8,
                        Vec::new(),
                    );
                }
                match continue_reviewed_workflow(bridge, &task_id, now_monotonic_ms) {
                    Ok(Some(mut chained)) => {
                        chained.states = durable;
                        return chained;
                    }
                    Ok(None) => {
                        let mut settled = response(
                            &operation_id,
                            wire::AdmissionStatus::Accepted as u8,
                            Vec::new(),
                        );
                        settled.states = durable;
                        return settled;
                    }
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
            response_after_task_change(
                bridge,
                response(
                    &operation_id,
                    wire::AdmissionStatus::Accepted as u8,
                    Vec::new(),
                ),
                projected,
            )
        }
        Ok(OpenTaskCommitOutcome::NotOpened(_)) => {
            bridge.pending_opens.remove(&operation_id);
            response(&operation_id, 6, Vec::new())
        }
        Err(_) => response(&operation_id, 5, Vec::new()),
    }
}

pub(crate) fn response(
    operation_id: &str,
    status: u8,
    storage_effects: Vec<ffi::BridgeStorageEffect>,
) -> ffi::BridgeResponse {
    ffi::BridgeResponse {
        admission: ffi::BridgeAdmission {
            operation_id: operation_id.to_owned(),
            status,
        },
        storage_effects,
        workspace_effects: Vec::new(),
        account_effects: Vec::new(),
        asset_effects: Vec::new(),
        probe_effects: Vec::new(),
        listing_effects: Vec::new(),
        endpoint_probe_effects: Vec::new(),
        task_answer_events: Vec::new(),
        states: Vec::new(),
    }
}

#[allow(non_snake_case)]
pub(crate) fn PrepareForShutdown(bridge: &mut ServiceBridge) {
    if let Some(runtime) = bridge.runtime.as_mut() {
        runtime.cancel_account_operations();
        let _ = runtime.disconnect_backup_restore();
        let _ = runtime.core_mut().prepare_for_shutdown();
    }
    bridge.pending_opens.clear();
    bridge.pending_submits.clear();
    bridge.pending_task_cancellations.clear();
    bridge.pending_skill_runs.clear();
    bridge.pending_assistant_configurations.clear();
    bridge.pending_skill_mutations.clear();
    bridge.recorded_skill_run_tasks.clear();
    bridge.pending_task_effects.clear();
    bridge.pending_model_attempts.clear();
    bridge.task_grants.clear();
}
