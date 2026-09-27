// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One-command-at-a-time continuation of one task's durable work.
//!
//! The loop decisions live in the loop kernel's walk (decision 0072): a
//! refused model call settles first, loop-local calls settle on the
//! residency, and the scheduler — reviewed no-model sequence or assistant
//! decision table — is chosen from the task's consented route, never from an
//! empty answer of the other table. This file is transport around that walk:
//! it guards consequential work while allowing bounded bootstrap reads,
//! submits what the walk
//! chose, and rolls a staged ask back when admission refuses the submission.

use core_runtime::wire;
use core_runtime::TaskId;

use crate::ffi;
use crate::service_bridge_runtime::{note_refusal, ServiceBridge};
use crate::service_bridge_task::begin_submit;
use crate::service_bridge_trace as trace;

/// Starts exactly one next reducer command after the prior commit is durable.
///
/// `None` means there is nothing to start: the reducer is waiting or terminal,
/// and no refused model call is outstanding. The returned storage effect is
/// the only publication for a newly applied command; state and its effects
/// remain withheld until that storage commit completes. What a caller may
/// attach is the state that was durable *before* this command — the commit
/// that just landed — so a surface sees every hop of the walk; only the
/// command applied here is withheld.
pub(crate) fn continue_reviewed_workflow(
    bridge: &mut ServiceBridge,
    task_id: &TaskId,
    now_monotonic_ms: u64,
) -> Result<Option<ffi::BridgeResponse>, ()> {
    if bridge
        .pending_task_effects
        .values()
        .any(|pending| {
            pending.task_id == task_id.as_str() && !pending.is_bootstrap_observation(bridge)
        })
    {
        return Ok(None);
    }
    let service_generation = bridge.generation.value();
    let Some(runtime) = bridge.runtime.as_mut() else {
        note_refusal(bridge, "walk_runtime_missing");
        return Err(());
    };
    let advanced = runtime.advance_task_walk(task_id, service_generation, now_monotonic_ms);
    let planned = match advanced {
        None => {
            note_refusal(bridge, "walk_no_task");
            return Err(());
        }
        Some(Err(error)) => {
            note_refusal(bridge, walk_error_label(&error));
            return Err(());
        }
        Some(Ok(None)) => {
            trace::idle(task_id, "waiting");
            return Ok(None);
        }
        Some(Ok(Some(planned))) => planned,
    };
    let staged_ask = planned.staged_ask;
    let response = begin_submit(
        bridge,
        task_id.clone(),
        planned.envelope,
        planned.operation,
        now_monotonic_ms,
    );
    if staged_ask && response.admission.status != wire::AdmissionStatus::Accepted as u8 {
        if let Some(runtime) = bridge.runtime.as_mut() {
            runtime.drop_ask_prompt(task_id.as_str());
        }
    }
    let accepted = response.admission.status == wire::AdmissionStatus::Accepted as u8;
    match started(response) {
        Ok(response) => Ok(Some(response)),
        Err(()) => {
            // A refused admission was named by the submission itself; only
            // an accepted one with the wrong shape is this function's own.
            if accepted {
                note_refusal(bridge, "walk_response_shape");
            }
            Err(())
        }
    }
}

/// The content-free name of a walk that could not choose its next command.
fn walk_error_label(error: &core_runtime::WalkError) -> &'static str {
    match error {
        core_runtime::WalkError::SelectedProcedureUnavailable => "walk_procedure_unavailable",
        core_runtime::WalkError::Scheduler(inner) => inner.label(),
        core_runtime::WalkError::IdentifierTooLong => "walk_identifier_too_long",
        core_runtime::WalkError::DeadlineOverflow => "walk_deadline_overflow",
    }
}

/// One accepted command, published as its storage effect and nothing else.
fn started(response: ffi::BridgeResponse) -> Result<ffi::BridgeResponse, ()> {
    if response.admission.status != wire::AdmissionStatus::Accepted as u8
        || response.storage_effects.len() != 1
        || !response.states.is_empty()
    {
        return Err(());
    }
    Ok(response)
}
