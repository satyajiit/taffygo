// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The browser-driven sweep of operations whose deadlines have arrived.
//!
//! Every deadline in this process is browser-owned and monotonic, and nothing
//! below this boundary may read a clock. The runtime therefore records a
//! deadline when it stages an effect and can only notice one when a call
//! arrives carrying a later `now`. Every other entry point in this bridge is
//! such a call, which is why an expiry appears to work on a busy profile — and
//! why it never fires on the one profile that needs it, whose single
//! outstanding storage commit is the reason nothing else is arriving.
//!
//! This is that missing call. It carries no payload because it asks nothing:
//! the time is the whole of the input, exactly as it is the last argument of
//! every command beside it.

use core_runtime::wire;
use core_runtime::{CancellationReason, OperationId};

use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

/// Returns the earliest pending browser-owned monotonic deadline, or zero.
///
/// Zero is not a valid registered deadline: registration refuses work whose
/// deadline has already arrived, and the browser clock is non-negative. It is
/// therefore the compact CXX spelling of `None`, keeping scheduling metadata
/// out of every response record while Rust remains the single authority.
#[allow(non_snake_case)]
pub(crate) fn NextOperationDeadline(bridge: &ServiceBridge) -> u64 {
    bridge
        .runtime
        .as_ref()
        .and_then(|runtime| runtime.core().next_operation_deadline_monotonic_ms())
        .unwrap_or(0)
}

/// Ends every profile operation whose browser-owned deadline has arrived.
///
/// The terminals travel the road a browser-delivered failure already travels.
/// A pending operation in this build is always one storage commit, and a
/// commit that fails is not publishable: `begin_submit` applied the command in
/// memory and deliberately withholds the state until the commit is durable, so
/// the runtime is now a revision ahead of storage and has marked the task for
/// replay. The browser-answered failure path (`complete_open_task` returning
/// `NotOpened`, `complete_commit` returning `RecoveryRequired`) drops the same
/// bookkeeping and publishes nothing for the same reason. An expiry that
/// published a state would be the one terminal the browser could tell apart.
#[allow(non_snake_case)]
pub(crate) fn ExpireDueOperations(
    bridge: &mut ServiceBridge,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response("", wire::AdmissionStatus::CoreUnavailable as u8, Vec::new());
    };
    let terminals = runtime.core_mut().expire_due(now_monotonic_ms);
    for terminal in &terminals {
        // The runtime has already dropped its own half of this operation. What
        // is left is the correlation this file keeps so a later completion can
        // find its task; leaving it would let a completion the browser sends
        // after the deadline be routed to a commit that no longer exists.
        let operation_id = terminal.completion.operation_id.as_str();
        bridge.pending_opens.remove(operation_id);
        bridge.pending_submits.remove(operation_id);
        bridge.pending_model_attempts.remove(operation_id);
        bridge
            .pending_task_cancellations
            .remove(terminal.task_id.as_str());
    }
    response("", wire::AdmissionStatus::Accepted as u8, Vec::new())
}

/// Cancels one in-flight operation and publishes the state the cancellation
/// left behind. Every refusal names why and publishes nothing, because a
/// refused cancel changed nothing; a performed one goes through the single
/// publication seam like every other runtime mutation, so the projection a
/// surface is holding never outlives the operation it was drawn from.
#[allow(non_snake_case)]
pub(crate) fn CancelOperation(
    bridge: &mut ServiceBridge,
    operation: ffi::BridgeOperation,
) -> ffi::BridgeResponse {
    let operation_id_text = operation.operation_id.clone();
    if operation.service_generation != bridge.generation.value() {
        return response(
            &operation_id_text,
            wire::AdmissionStatus::StaleGeneration as u8,
            Vec::new(),
        );
    }
    let Ok(operation_id) = OperationId::new(operation_id_text.clone()) else {
        return response(
            &operation_id_text,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let queued_task = bridge
        .pending_task_cancellations
        .iter()
        .find_map(|(task_id, pending)| {
            (pending.operation.operation_id == operation_id_text).then(|| task_id.clone())
        });
    if let Some(task_id) = queued_task {
        bridge.pending_task_cancellations.remove(&task_id);
        return response(
            &operation_id_text,
            wire::AdmissionStatus::Accepted as u8,
            Vec::new(),
        );
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(
            &operation_id_text,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    if runtime
        .cancel_operation(&operation_id, CancellationReason::User)
        .is_err()
    {
        return response(
            &operation_id_text,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    }
    bridge.pending_opens.remove(&operation_id_text);
    bridge.pending_submits.remove(&operation_id_text);
    bridge.pending_model_attempts.remove(&operation_id_text);
    bridge
        .pending_task_effects
        .retain(|_, effect| effect.operation.operation_id != operation_id_text);
    response_after_change(
        bridge,
        response(
            &operation_id_text,
            wire::AdmissionStatus::Accepted as u8,
            Vec::new(),
        ),
    )
}
