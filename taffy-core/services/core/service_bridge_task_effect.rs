// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exhaustive post-commit reducer-effect projection and terminal correlation.

use std::collections::BTreeMap;

use core_runtime::wire;
use core_runtime::{Command, TaskId};

use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;
use crate::service_bridge_workflow::continue_reviewed_workflow;

mod action_projection;
mod action_terminal;
mod completion_submit;
mod discovery_tab;
mod enums;
mod model;
mod observation;
mod policy_terminal;
mod projection;
mod reconciliation;
mod task_download;
mod task_library;
mod task_memory;
mod task_store;
mod task_tab;
mod tool_job_terminal;

use model::complete_model;
pub(crate) use model::DeliverModelStreamChunk;
pub(crate) use projection::{
    project_committed_effects, project_committed_model_attempt, project_initial_effects,
    project_restored_effects,
};

#[derive(Clone, Debug)]
pub(crate) struct PendingTaskEffect {
    pub(crate) operation: wire::OperationEnvelope,
    effect_id: String,
    /// Initial durable model-effect claim, retained across sub-attempts.
    root_effect_id: String,
    /// Zero for the initial effect; increasing for each paid sub-attempt.
    attempt_ordinal: u32,
    pub(crate) task_id: String,
    action_id: String,
    tab_id: String,
    /// The model call this effect stands for, empty for every other kind.
    call_id: String,
    /// Exact next raw response chunk expected for a model call.
    stream_sequence: u32,
    /// Exact next sanitized visible answer event emitted for a model call.
    answer_sequence: u32,
    dispatch_id: Option<String>,
    kind: wire::TaskReducerEffectKind,
    action_operation: Option<wire::TaskActionOperationKind>,
    /// A durable Library or Memory write awaiting exact browser storage.
    pub(crate) durable_storage_pending: bool,
}

impl PendingTaskEffect {
    /// A settlement waiting for in-flight work to finish, which is not itself
    /// work in flight. See `continue_reviewed_workflow`.
    pub(crate) fn is_settlement_wait(&self) -> bool {
        self.kind == wire::TaskReducerEffectKind::AwaitInFlightWork
    }

    /// Only scheduler-created whole-page reads may overlap the next proposal.
    /// The resident proposal is the authority for the shape, not a wire label.
    pub(crate) fn is_bootstrap_observation(&self, bridge: &ServiceBridge) -> bool {
        use core_runtime::{ActionId, ActionIntent, BrowserIntent};

        if self.kind != wire::TaskReducerEffectKind::DispatchAction
            || self.action_operation != Some(wire::TaskActionOperationKind::DomRead)
        {
            return false;
        }
        bridge
            .runtime
            .as_ref()
            .and_then(|runtime| runtime.core().task(&TaskId::new(self.task_id.clone())))
            .and_then(|task| task.action_effect_facts(&ActionId::new(self.action_id.clone())))
            .is_some_and(|facts| {
                facts
                    .proposal
                    .idempotency_key
                    .as_str()
                    .starts_with("agent-observation-")
                    && matches!(
                        facts.proposal.intent(),
                        ActionIntent::Browser(BrowserIntent::DomRead { target: None, .. })
                    )
            })
    }

    pub(crate) const fn durable_storage_kind(&self) -> Option<wire::TaskReducerEffectKind> {
        match self.kind {
            wire::TaskReducerEffectKind::RunLibraryTool
            | wire::TaskReducerEffectKind::RunMemoryTool => Some(self.kind),
            _ => None,
        }
    }
}

#[derive(Clone, Debug)]
pub(crate) struct TaskGrantFacts {
    capability_id: String,
    frame_id: String,
    page_epoch: String,
    graph_revision: u64,
    normalized_origin: String,
    opaque_origin_id: Option<String>,
    destination_origin: Option<String>,
    destination_address: Option<String>,
}

pub(crate) type PendingTaskEffects = BTreeMap<String, PendingTaskEffect>;
pub(crate) type TaskGrants = BTreeMap<(String, String), TaskGrantFacts>;
#[derive(Clone, Debug)]
pub(crate) struct RestoredTaskEffects {
    pub(crate) task_id: TaskId,
    pub(crate) parent_operation_id: String,
    pub(crate) task_revision: u64,
    pub(crate) effects: Vec<core_runtime::Effect>,
}

/// Drops the wait an accepted settlement answers. The wait was emitted at the
/// revision that began settling, and a reply recorded while settling moves the
/// task past it, so every wait at or below the accepted revision is answered,
/// not only one at exactly that revision.
pub(crate) fn acknowledge_settlement_effect(
    bridge: &mut ServiceBridge,
    task_id: &str,
    task_revision: u64,
) {
    bridge.pending_task_effects.retain(|_, pending| {
        pending.kind != wire::TaskReducerEffectKind::AwaitInFlightWork
            || pending.task_id != task_id
            || pending.operation.task_revision > task_revision
    });
}

#[allow(non_snake_case)]
pub(crate) fn CompleteTaskEffect(
    bridge: &mut ServiceBridge,
    terminal: ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let effect_id = terminal.effect_id.clone();
    let Some(pending) = bridge.pending_task_effects.get(&effect_id).cloned() else {
        return response("", wire::AdmissionStatus::InvalidCommand as u8, Vec::new());
    };
    if !terminal_matches(&pending, &terminal, bridge.generation.value()) {
        return response(
            &pending.operation.operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    }
    match pending.kind {
        wire::TaskReducerEffectKind::AskPolicy => {
            policy_terminal::complete(bridge, pending, terminal, now_monotonic_ms)
        }
        wire::TaskReducerEffectKind::DispatchAction => {
            action_terminal::complete(bridge, pending, terminal, now_monotonic_ms)
        }
        wire::TaskReducerEffectKind::CallModel => {
            complete_model(bridge, pending, &terminal, now_monotonic_ms)
        }
        wire::TaskReducerEffectKind::RunToolJob => {
            tool_job_terminal::complete(bridge, pending, &terminal, now_monotonic_ms)
        }
        wire::TaskReducerEffectKind::RunLibraryTool => {
            task_library::complete(bridge, pending, &terminal, now_monotonic_ms)
        }
        wire::TaskReducerEffectKind::RunMemoryTool => {
            task_memory::complete(bridge, pending, &terminal, now_monotonic_ms)
        }
        wire::TaskReducerEffectKind::ReconcileAction => {
            reconciliation::complete(bridge, pending, &terminal, now_monotonic_ms)
        }
        wire::TaskReducerEffectKind::PrepareDiscoveryTab => {
            discovery_tab::complete(bridge, pending, &terminal, now_monotonic_ms)
        }
        wire::TaskReducerEffectKind::RequestApproval
        | wire::TaskReducerEffectKind::RequestPermission
        | wire::TaskReducerEffectKind::RequestFieldValues => {
            bridge.pending_task_effects.remove(&effect_id);
            let operation_id = pending.operation.operation_id;
            match continue_reviewed_workflow(
                bridge,
                &TaskId::new(pending.task_id),
                now_monotonic_ms,
            ) {
                Ok(Some(next)) => next,
                // The unchanged binding is intentionally republished at a new
                // state sequence. It is the public readiness acknowledgement
                // that lets an answer already owned by the browser cross the
                // independent CoreSession pipe without racing this terminal.
                Ok(None) => response_after_change(
                    bridge,
                    response(
                        &operation_id,
                        wire::AdmissionStatus::Accepted as u8,
                        Vec::new(),
                    ),
                ),
                Err(()) => {
                    bridge.runtime = None;
                    response(
                        &operation_id,
                        wire::AdmissionStatus::CoreUnavailable as u8,
                        Vec::new(),
                    )
                }
            }
        }
        // A handover terminal is the person coming back or the window
        // closing, and either way the workflow continues from where the
        // reducer put it. The command that records which of the two happened
        // is submitted by the browser, not derived here: this bridge has no
        // way to know what the person did, and inventing a completion is
        // exactly the detector this product refuses to build.
        wire::TaskReducerEffectKind::AwaitHandover
        | wire::TaskReducerEffectKind::RevokeAuthority
        | wire::TaskReducerEffectKind::AwaitInFlightWork
        | wire::TaskReducerEffectKind::ReleaseTaskTabs
        | wire::TaskReducerEffectKind::GenerateArtifact
        | wire::TaskReducerEffectKind::ExportArtifact => {
            bridge.pending_task_effects.remove(&effect_id);
            let operation_id = pending.operation.operation_id;
            match continue_reviewed_workflow(
                bridge,
                &TaskId::new(pending.task_id),
                now_monotonic_ms,
            ) {
                Ok(Some(next)) => next,
                Ok(None) => response(
                    &operation_id,
                    wire::AdmissionStatus::Accepted as u8,
                    Vec::new(),
                ),
                Err(()) => {
                    bridge.runtime = None;
                    response(
                        &operation_id,
                        wire::AdmissionStatus::CoreUnavailable as u8,
                        Vec::new(),
                    )
                }
            }
        }
    }
}

pub(crate) use task_library::deliver_storage_completion as deliver_library_storage_completion;
pub(crate) use task_memory::deliver_storage_completion as deliver_memory_storage_completion;

fn submit_completion_command(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    command: Command,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    completion_submit::plain(bridge, pending, command, now_monotonic_ms)
}

fn terminal_matches(
    pending: &PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    generation: u64,
) -> bool {
    terminal.operation.operation_id == pending.operation.operation_id
        && terminal.operation.service_generation == generation
        && terminal.operation.task_revision == pending.operation.task_revision
        && terminal.operation.idempotency_key == pending.operation.idempotency_key
        && terminal.effect_id == pending.effect_id
        && terminal.task_id == pending.task_id
        && wire::TaskReducerEffectKind::from_wire(u32::from(terminal.kind)) == Some(pending.kind)
}
