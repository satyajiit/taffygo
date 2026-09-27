// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Settlement of definitive direct-provider failures.
//!
//! Chromium supplies transport facts; `core-runtime` owns the retry verdict.
//! Keeping the boundary here makes the terminal path a small translation:
//! either dispatch one exact next attempt or durably settle the logical turn.

use core_runtime::{wire, ModelFailureOutcome, TaskId, TurnGap};

use crate::ffi;
use crate::service_bridge_runtime::ServiceBridge;

use super::{invalid, record_gap};
use crate::service_bridge_task_effect::{completion_submit, PendingTaskEffect};

pub(super) fn complete_definitive_failure(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let status = wire::TaskEffectCompletionStatus::from_wire(u32::from(terminal.status));
    if !matches!(
        status,
        Some(
            wire::TaskEffectCompletionStatus::Refused
                | wire::TaskEffectCompletionStatus::Unavailable
        )
    ) || terminal.has_model_completion
        || terminal.model_completion_streamed
        || pending.stream_sequence != 0
        || (!terminal.has_model_retry_after && terminal.model_retry_after_millis != 0)
    {
        return invalid(&pending);
    }
    let Some(class) = model_error_from_wire(terminal.model_failure_class) else {
        return invalid(&pending);
    };
    let retry_after = terminal
        .has_model_retry_after
        .then_some(terminal.model_retry_after_millis);
    let task_id = TaskId::new(pending.task_id.clone());
    let Some(runtime) = bridge.runtime.as_mut() else {
        return invalid(&pending);
    };
    let outcome = runtime.plan_model_failure(
        &task_id,
        &pending.call_id,
        &terminal.model_completion_model_id,
        class,
        retry_after,
    );
    match outcome {
        ModelFailureOutcome::Invalid => invalid(&pending),
        ModelFailureOutcome::Gap(gap) => record_gap(bridge, pending, gap, now_monotonic_ms),
        ModelFailureOutcome::Dispatch(dispatch) => {
            dispatch_next_attempt(bridge, pending, &task_id, *dispatch, now_monotonic_ms)
        }
    }
}

fn dispatch_next_attempt(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    task_id: &TaskId,
    mut dispatch: core_runtime::ModelAttemptDispatch,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let not_before = if dispatch.delay_millis == 0 {
        0
    } else {
        let Some(value) = now_monotonic_ms.checked_add(dispatch.delay_millis) else {
            if let Some(runtime) = bridge.runtime.as_mut() {
                runtime.drop_held_turn(task_id);
            }
            return record_gap(bridge, pending, TurnGap::Unavailable, now_monotonic_ms);
        };
        value
    };
    if not_before >= pending.operation.deadline_monotonic_ms && not_before != 0 {
        if let Some(runtime) = bridge.runtime.as_mut() {
            runtime.drop_held_turn(task_id);
        }
        return record_gap(bridge, pending, TurnGap::Unavailable, now_monotonic_ms);
    }
    dispatch.request.not_before_monotonic_ms = not_before;
    let submitted = completion_submit::model_attempt(bridge, pending, dispatch, now_monotonic_ms);
    if !matches!(
        submitted.admission.status,
        value if value == wire::AdmissionStatus::Accepted as u8
            || value == wire::AdmissionStatus::Duplicate as u8
    ) {
        bridge.runtime = None;
    }
    submitted
}

fn model_error_from_wire(value: u8) -> Option<core_runtime::ModelErrorClass> {
    match wire::ModelErrorClass::from_wire(u32::from(value)) {
        Some(wire::ModelErrorClass::Auth) => Some(core_runtime::ModelErrorClass::Auth),
        Some(wire::ModelErrorClass::Quota) => Some(core_runtime::ModelErrorClass::Quota),
        Some(wire::ModelErrorClass::Overloaded) => Some(core_runtime::ModelErrorClass::Overloaded),
        Some(wire::ModelErrorClass::InvalidRequest) => {
            Some(core_runtime::ModelErrorClass::InvalidRequest)
        }
        Some(wire::ModelErrorClass::Network) => Some(core_runtime::ModelErrorClass::Network),
        Some(wire::ModelErrorClass::Overflow) => Some(core_runtime::ModelErrorClass::Overflow),
        Some(wire::ModelErrorClass::Canceled) => Some(core_runtime::ModelErrorClass::Canceled),
        Some(wire::ModelErrorClass::Unknown) => Some(core_runtime::ModelErrorClass::Unknown),
        None => None,
    }
}
