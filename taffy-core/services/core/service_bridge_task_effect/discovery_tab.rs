// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact terminal decoding for the discovery-tab bootstrap of a Web errand.
//!
//! The browser either prepared the opaque blank tab the task searches from,
//! or answered that it will not. Both are durable facts about the task, and
//! neither may end the core.

use core_runtime::wire;
use core_runtime::{Command, TaskId};

use super::PendingTaskEffect;
use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_workflow::continue_reviewed_workflow;

pub(super) fn complete(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let carries_other_shape =
        terminal.has_discovered_source || terminal.has_observation || terminal.has_model_completion;
    let carries_tab = terminal.has_discovery_tab
        || !terminal.discovery_tab_id.is_empty()
        || !terminal.discovery_browser_session_id.is_empty();
    match wire::TaskEffectCompletionStatus::from_wire(u32::from(terminal.status)) {
        Some(wire::TaskEffectCompletionStatus::Succeeded)
            if terminal.has_discovery_tab
                && !terminal.discovery_tab_id.is_empty()
                && !terminal.discovery_browser_session_id.is_empty()
                && !carries_other_shape => {}
        // The browser answered, and its answer is that no discovery tab will
        // be prepared for this task: a durable fact about the task, not a
        // malformed terminal. The task is ended under one closed reason by the
        // walk (which admits the failure only once the task is running);
        // answering `InvalidCommand` here instead ended the whole core, on
        // every start, for a restored errand whose bootstrap ran before any
        // window was active.
        Some(
            wire::TaskEffectCompletionStatus::Refused
            | wire::TaskEffectCompletionStatus::Unavailable,
        ) if !carries_tab && !carries_other_shape => {
            return refuse(bridge, pending, now_monotonic_ms);
        }
        Some(_) | None => {
            return response(
                &pending.operation.operation_id,
                wire::AdmissionStatus::InvalidCommand as u8,
                Vec::new(),
            );
        }
    }
    let Ok(browser_session_id) =
        core_runtime::BrowserSessionId::new(terminal.discovery_browser_session_id.clone())
    else {
        return response(
            &pending.operation.operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    bridge.pending_task_effects.remove(&pending.effect_id);
    super::submit_completion_command(
        bridge,
        pending,
        Command::RecordDiscoveryTab {
            discovery_tab_id: core_runtime::TabId::new(terminal.discovery_tab_id.clone()),
            browser_session_id,
        },
        now_monotonic_ms,
    )
}

/// Turns a refused discovery bootstrap into the task's own failure.
///
/// The refusal is noted on the loop state and the walk is chained at once,
/// under the durable state that was published before the effect left: a
/// queued task takes its `ExecutorStarted` hop from here, and the walk after
/// that commit records `SourcesUnavailable`. Every hop is one status
/// publication, so the surface that started the task watches it end rather
/// than watching the core go away.
fn refuse(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let operation_id = pending.operation.operation_id.clone();
    bridge.pending_task_effects.remove(&pending.effect_id);
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    runtime.note_discovery_unavailable(&pending.task_id);
    // A completion's answer carries no state: the utility admits one beside a
    // task-effect completion only for a deferred surface, and refuses the core
    // otherwise. The walk chains the command that ends the task, and the state
    // that commit makes durable is published when its storage result lands,
    // the way every chained commit's is.
    match continue_reviewed_workflow(bridge, &TaskId::new(pending.task_id), now_monotonic_ms) {
        Ok(Some(chained)) => chained,
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
