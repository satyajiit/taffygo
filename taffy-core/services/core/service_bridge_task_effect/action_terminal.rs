// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact terminal decoding for a dispatched browser action.

use core_runtime::wire;
use core_runtime::{ActionId, ActionResultCode, Command, DispatchId, MonotonicMillis, TaskId};

use super::{
    completion_submit, observation, task_download, task_store, task_tab, PendingTaskEffect,
};
use crate::ffi;
use crate::service_bridge_runtime::{note_refusal, response, ServiceBridge};
use crate::service_bridge_task::TypedTaskResult;

pub(super) fn complete(
    bridge: &mut ServiceBridge,
    pending: PendingTaskEffect,
    terminal: ffi::BridgeTaskTerminal,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let status = wire::TaskEffectCompletionStatus::from_wire(u32::from(terminal.status));
    let code = match status {
        Some(wire::TaskEffectCompletionStatus::Succeeded) => ActionResultCode::Verified,
        // A refusal that named its own code is that code. Collapsing every
        // refused effect into the one generic denial told the recovery table
        // "this will be decided the same way again", so a read of a page that
        // had moved under it — a redirect, a single-page app settling — was
        // never read again and the errand ended wherever it happened to be.
        Some(wire::TaskEffectCompletionStatus::Refused) => refused_code(&terminal),
        Some(wire::TaskEffectCompletionStatus::Unavailable) => ActionResultCode::Unsupported,
        Some(wire::TaskEffectCompletionStatus::Cancelled) => ActionResultCode::CancelledByUser,
        Some(wire::TaskEffectCompletionStatus::OutcomeUnknown) => ActionResultCode::OutcomeUnknown,
        Some(wire::TaskEffectCompletionStatus::ValueReferenceUnknown) => {
            ActionResultCode::ValueReferenceUnknown
        }
        None => return invalid(bridge, &pending, "terminal_status_unknown"),
    };
    let observation = match decode_observation(bridge, &pending, &terminal, code) {
        Ok(value) => value,
        // `decode_observation` records its own label at the site that refused.
        Err(()) => return invalid_unlabelled(&pending),
    };
    let discovered_source = match decode_discovered_source(&pending, &terminal, code) {
        Ok(value) => value,
        Err(label) => return invalid(bridge, &pending, label),
    };
    let task_tab_result = match task_tab::decode(&pending, &terminal, code) {
        Ok(value) => value,
        Err(()) => return invalid(bridge, &pending, "task_tab_terminal"),
    };
    let task_download_result = match task_download::decode(&pending, &terminal, code) {
        Ok(value) => value,
        Err(()) => return invalid(bridge, &pending, "task_download_terminal"),
    };
    let task_store_result = match task_store::decode(&pending, &terminal, code) {
        Ok(value) => value,
        Err(()) => return invalid(bridge, &pending, "task_store_terminal"),
    };
    // Each decoder answers only for its own operations, so at most one is
    // present; two would be one terminal wearing two effects' identities.
    let typed_result = match (task_tab_result, task_download_result, task_store_result) {
        (Some(result), None, None) => Some(TypedTaskResult::Tab(result)),
        (None, Some(result), None) => Some(TypedTaskResult::Download(result)),
        (None, None, Some(result)) => Some(TypedTaskResult::Store(result)),
        (None, None, None) => None,
        _ => return invalid(bridge, &pending, "typed_result_conflict"),
    };
    let command = Command::RecordActionOutcome {
        action_id: ActionId::new(pending.action_id.clone()),
        outcome: Box::new(core_runtime::ActionOutcome {
            code,
            dispatch_id: pending.dispatch_id.clone().map(DispatchId::new),
            observed_at: MonotonicMillis(now_monotonic_ms),
            observation,
            discovered_source,
        }),
    };
    bridge.pending_task_effects.remove(&pending.effect_id);
    completion_submit::with_typed_result(bridge, pending, command, now_monotonic_ms, typed_result)
}

/// The closed code a refused effect carries, or the bare policy refusal when
/// the browser named none. `Verified` is not a refusal and reads as the bare
/// code too.
fn refused_code(terminal: &ffi::BridgeTaskTerminal) -> ActionResultCode {
    if !terminal.has_denial_code {
        return ActionResultCode::DeniedByPolicy;
    }
    match wire::TaskActionResultCode::from_wire(u32::from(terminal.denial_code)) {
        Some(wire::TaskActionResultCode::Verified) | None => ActionResultCode::DeniedByPolicy,
        Some(code) => core_runtime::action_result_code_from_wire(code),
    }
}

/// Refuses the completion and names the branch that refused it.
///
/// The browser logs `LastRefusal` beside every `kInvalidCommand`, so a branch
/// that returns without recording a name leaves the previous command's label
/// standing and the line reads as a fault that happened somewhere else.
fn invalid(
    bridge: &mut ServiceBridge,
    pending: &PendingTaskEffect,
    label: &'static str,
) -> ffi::BridgeResponse {
    note_refusal(bridge, label);
    invalid_unlabelled(pending)
}

/// The same refusal, for a branch that has already recorded its own name.
fn invalid_unlabelled(pending: &PendingTaskEffect) -> ffi::BridgeResponse {
    response(
        &pending.operation.operation_id,
        wire::AdmissionStatus::InvalidCommand as u8,
        Vec::new(),
    )
}

fn decode_observation(
    bridge: &mut ServiceBridge,
    pending: &PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    code: ActionResultCode,
) -> Result<Option<core_runtime::PageObservationEvidence>, ()> {
    match (code == ActionResultCode::Verified, terminal.has_observation) {
        (true, true) => {
            let grant = bridge
                .task_grants
                .get(&(pending.task_id.clone(), pending.action_id.clone()))
                .cloned();
            let generation = bridge.generation.value();
            let decoded = match (grant, pending.action_operation, bridge.runtime.as_mut()) {
                (Some(grant), Some(action_operation), Some(runtime)) => observation::decode(
                    runtime,
                    &TaskId::new(pending.task_id.clone()),
                    &ActionId::new(pending.action_id.clone()),
                    terminal,
                    generation,
                    &pending.tab_id,
                    &grant,
                    action_operation,
                ),
                (None, _, _) => Err("observation_grant_missing"),
                (_, None, _) => Err("observation_operation_missing"),
                (_, _, None) => Err("observation_runtime_missing"),
            };
            match decoded {
                Ok(evidence) => Ok(Some(evidence)),
                Err(label) => {
                    note_refusal(bridge, label);
                    Err(())
                }
            }
        }
        (true, false) | (false, false) => Ok(None),
        (false, true) => {
            note_refusal(bridge, "observation_on_a_refused_action");
            Err(())
        }
    }
}

fn decode_discovered_source(
    pending: &PendingTaskEffect,
    terminal: &ffi::BridgeTaskTerminal,
    code: ActionResultCode,
) -> Result<Option<core_runtime::ConsentedSource>, &'static str> {
    let fields_present = !terminal.discovered_source_id.is_empty()
        && !terminal.discovered_source_tab_id.is_empty()
        && !terminal.discovered_source_normalized_origin.is_empty();
    if terminal.has_discovered_source != fields_present
        || terminal.has_discovered_source_canonical_locator
            != !terminal.discovered_source_canonical_locator.is_empty()
        || terminal.discovered_source_canonical_locator.len() > wire::MAX_SOURCE_LOCATOR_BYTES
        || (!terminal.has_discovered_source
            && (!terminal.discovered_source_id.is_empty()
                || !terminal.discovered_source_tab_id.is_empty()
                || !terminal.discovered_source_normalized_origin.is_empty()
                || terminal.has_discovered_source_canonical_locator))
    {
        return Err("discovered_source_shape");
    }
    if !terminal.has_discovered_source {
        return Ok(None);
    }
    let same_tab = matches!(
        pending.action_operation,
        Some(
            wire::TaskActionOperationKind::Navigate
                | wire::TaskActionOperationKind::Search
                | wire::TaskActionOperationKind::HistoryBack
                | wire::TaskActionOperationKind::HistoryForward
                | wire::TaskActionOperationKind::Reload
                | wire::TaskActionOperationKind::LinkOpen
                | wire::TaskActionOperationKind::FormSubmit
        )
    );
    let new_tab = pending.action_operation == Some(wire::TaskActionOperationKind::TabsOpen);
    if code != ActionResultCode::Verified {
        return Err("discovered_source_on_a_refused_action");
    }
    // Only a move that lands somewhere may say where it landed. An observation
    // or a press carries no destination the browser could have declared, so a
    // source offered on one is an envelope no reader can check.
    if !same_tab && !new_tab {
        return Err("discovered_source_operation");
    }
    if (same_tab && terminal.discovered_source_tab_id != pending.tab_id)
        || (new_tab && terminal.discovered_source_tab_id == pending.tab_id)
    {
        return Err("discovered_source_tab");
    }
    if terminal.discovered_source_tab_id.len() > wire::MAX_IDENTIFIER_BYTES
        || terminal.discovered_source_normalized_origin.len() > wire::MAX_NORMALIZED_ORIGIN_BYTES
    {
        return Err("discovered_source_bytes");
    }
    let source_id = core_runtime::SourceId::parse(&terminal.discovered_source_id)
        .map_err(|_| "discovered_source_id")?;
    Ok(Some(core_runtime::ConsentedSource {
        source_id,
        tab_id: core_runtime::TabId::new(terminal.discovered_source_tab_id.clone()),
        normalized_origin: terminal.discovered_source_normalized_origin.clone(),
        // The public landing locator is part of the immutable browser-issued
        // source tuple. Dropping it would withdraw standing consent at the
        // next publication even when the tab remains on the admitted origin.
        canonical_locator: terminal
            .has_discovered_source_canonical_locator
            .then(|| terminal.discovered_source_canonical_locator.clone()),
    }))
}
