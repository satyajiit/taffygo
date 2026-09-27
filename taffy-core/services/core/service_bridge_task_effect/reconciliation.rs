// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable action-journal reconciliation after a core-service restart.

use core_runtime::wire;
use core_runtime::{
    ActionId, ActionOutcome, ActionResultCode, Command, DispatchId, MonotonicMillis,
};

use super::{completion_submit, PendingTaskEffect};
use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};

pub(super) fn complete(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let Some(status) = wire::TaskEffectCompletionStatus::from_wire(u32::from(terminal.status))
    else {
        return invalid(&pending);
    };
    if terminal.action_id != pending.action_id || !has_reconciliation_shape(terminal, status) {
        return invalid(&pending);
    }

    let exact = terminal
        .has_reconciled_action_result
        .then_some(terminal.reconciled_action_result_code)
        .and_then(ActionResultCode::from_ordinal);
    if terminal.has_reconciled_action_result != exact.is_some() {
        return invalid(&pending);
    }

    bridge.pending_task_effects.remove(&pending.effect_id);
    let command = exact
        .filter(|code| definitive_without_lost_payload(*code, pending.action_operation))
        .and_then(|code| {
            pending
                .dispatch_id
                .clone()
                .map(|dispatch_id| Command::RecordActionOutcome {
                    action_id: ActionId::new(pending.action_id.clone()),
                    outcome: Box::new(ActionOutcome {
                        code,
                        dispatch_id: Some(DispatchId::new(dispatch_id)),
                        observed_at: MonotonicMillis(now_monotonic_ms),
                        observation: None,
                        discovered_source: None,
                    }),
                })
        })
        .unwrap_or(Command::RequestUserInput);
    completion_submit::plain(bridge, pending, command, now_monotonic_ms)
}

fn has_reconciliation_shape(
    terminal: &ffi::BridgeTaskTerminal,
    status: wire::TaskEffectCompletionStatus,
) -> bool {
    let carries_exact = terminal.has_reconciled_action_result;
    let status_matches = if carries_exact {
        status == wire::TaskEffectCompletionStatus::Succeeded
    } else {
        status == wire::TaskEffectCompletionStatus::OutcomeUnknown
            && terminal.reconciled_action_result_code == 0
    };
    status_matches
        && !terminal.has_observation
        && !terminal.has_media_observation
        && !terminal.has_media_attachment
        && !terminal.has_discovered_source
        && !terminal.has_discovery_tab
        && !terminal.has_task_tab_result
        && !terminal.has_task_download_result
        && !terminal.has_model_completion
        && !terminal.has_model_failure
        && !terminal.has_tool_output
        && !terminal.has_media_probe
}

fn definitive_without_lost_payload(
    code: ActionResultCode,
    operation: Option<wire::TaskActionOperationKind>,
) -> bool {
    if !code.is_terminal() || code.has_uncertain_side_effect() {
        return false;
    }
    code != ActionResultCode::Verified || operation.is_some_and(is_payload_free_operation)
}

/// Whether a verified outcome for this operation carries nothing the journal
/// could not reconstruct. The page actions answer with a result code and
/// nothing else; so do a reload and a stop, which leave the tab on the
/// document it was already on. Every other operation returns transient
/// evidence — an observation, a discovered source, a tab, a download — that
/// only the attempt itself held.
const fn is_payload_free_operation(operation: wire::TaskActionOperationKind) -> bool {
    matches!(
        operation,
        wire::TaskActionOperationKind::DomClick
            | wire::TaskActionOperationKind::DomFocus
            | wire::TaskActionOperationKind::DomScroll
            | wire::TaskActionOperationKind::FormFill
            | wire::TaskActionOperationKind::FormSelect
            | wire::TaskActionOperationKind::FormToggle
            | wire::TaskActionOperationKind::FormSubmit
            | wire::TaskActionOperationKind::Reload
            | wire::TaskActionOperationKind::StopLoading
    )
}

fn invalid(pending: &PendingTaskEffect) -> ffi::BridgeResponse {
    response(
        &pending.operation.operation_id,
        wire::AdmissionStatus::InvalidCommand as u8,
        Vec::new(),
    )
}
