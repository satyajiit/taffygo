// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact initial-consent task admission behind the CXX bridge.

use core_runtime::wire;
use core_runtime::{decode_start_task, Deadline, EffectRequest, OperationEnvelope, OperationId};

use crate::ffi;
use crate::service_bridge_runtime::{note_refusal, response, ServiceBridge};

#[allow(non_snake_case)]
pub(crate) fn SubmitStartTask(
    bridge: &mut ServiceBridge,
    command: ffi::BridgeStartTask,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = command.operation.operation_id.clone();
    let Some(runtime) = bridge.runtime.as_mut() else {
        return refuse(bridge, &operation_id, CORE_UNAVAILABLE, "start_runtime_missing");
    };
    runtime.set_utc_millis(now_utc_millis);
    let Some((wire_operation, mut wire_start)) = decode_bridge_start(command) else {
        return refuse(bridge, &operation_id, INVALID_COMMAND, "start_bridge_projection");
    };
    if runtime.prepare_skill_start(&mut wire_start).is_err() {
        return refuse(bridge, &operation_id, INVALID_COMMAND, "start_skill_preparation");
    }
    if runtime
        .core()
        .prepare_library_refresh_start(&mut wire_start)
        .is_err()
    {
        return refuse(bridge, &operation_id, INVALID_COMMAND, "start_library_refresh");
    }
    let load = match decode_start_task(&wire_start, &wire_operation) {
        Ok(load) => load,
        Err(error) => return refuse(bridge, &operation_id, INVALID_COMMAND, error.label()),
    };
    let Ok(runtime_operation_id) = OperationId::new(wire_operation.operation_id.clone()) else {
        return refuse(bridge, &operation_id, INVALID_COMMAND, "start_operation_id");
    };
    let task_id = wire_start.task_id.clone();
    let opened = runtime.core_mut().begin_open_task(
        load,
        runtime_operation_id,
        Deadline::from_millis(wire_operation.deadline_monotonic_ms),
        now_monotonic_ms,
        now_utc_millis,
    );
    let opened = match opened {
        Ok(opened) => opened,
        Err(error) => return refuse(bridge, &operation_id, INVALID_COMMAND, error.label()),
    };
    let Some(effect) = storage_effect(opened.commit) else {
        return refuse(bridge, &operation_id, INVALID_COMMAND, "start_storage_effect_shape");
    };
    bridge
        .pending_opens
        .insert(effect.operation.operation_id.clone(), task_id);
    response(&operation_id, 0, vec![effect])
}

/// The admission statuses this file answers, by their generated wire values.
const INVALID_COMMAND: u8 = wire::AdmissionStatus::InvalidCommand as u8;
const CORE_UNAVAILABLE: u8 = wire::AdmissionStatus::CoreUnavailable as u8;

/// Refuses one start and records which branch did it.
///
/// Every refusal here reaches a person as one sentence — "Taffy could not read
/// this request" — because the wire carries a status and nothing else. The
/// label is compiled in, names the branch rather than anything from the
/// command, and is what the browser prints beside the status it already logs.
/// A start refused without one is a defect this function exists to make hard:
/// there is no other way out of `SubmitStartTask`.
fn refuse(
    bridge: &mut ServiceBridge,
    operation_id: &str,
    status: u8,
    label: &'static str,
) -> ffi::BridgeResponse {
    note_refusal(bridge, label);
    response(operation_id, status, Vec::new())
}

fn decode_bridge_start(
    value: ffi::BridgeStartTask,
) -> Option<(wire::OperationEnvelope, wire::StartTaskCommand)> {
    let kind = wire::TaskKind::from_wire(u32::from(value.kind))?;
    let control_mode = wire::TaskControlMode::from_wire(u32::from(value.control_mode))?;
    let milestone = wire::TaskMilestone::from_wire(u32::from(value.milestone))?;
    let budgets = value
        .budgets
        .into_iter()
        .map(|budget| {
            Some(wire::TaskBudget {
                kind: wire::TaskBudgetKind::from_wire(u32::from(budget.kind))?,
                limit: budget.limit,
            })
        })
        .collect::<Option<Vec<_>>>()?;
    let consent_provider_route =
        wire::TaskProviderRoute::from_wire(u32::from(value.consent_provider_route))?;
    let builtin_skill = if value.has_builtin_skill {
        Some(wire::BuiltinSkillReference {
            skill_id: wire::BuiltinSkillId::from_wire(u32::from(value.builtin_skill_id))?,
            version: value.builtin_skill_version,
        })
    } else {
        None
    };
    let consent_sources = value
        .consent_sources
        .into_iter()
        .map(|source| wire::TaskConsentSource {
            source_id: source.source_id,
            tab_id: source.tab_id,
            normalized_origin: source.normalized_origin,
            canonical_locator: source
                .has_canonical_locator
                .then_some(source.canonical_locator),
        })
        .collect();
    let operation = wire::OperationEnvelope {
        operation_id: value.operation.operation_id,
        service_generation: value.operation.service_generation,
        task_revision: value.operation.task_revision,
        deadline_monotonic_ms: value.operation.deadline_monotonic_ms,
        idempotency_key: value.operation.idempotency_key,
    };
    let start = wire::StartTaskCommand {
        task_id: value.task_id,
        workspace_id: value.has_workspace_id.then_some(value.workspace_id),
        browser_profile_id: value.browser_profile_id,
        kind,
        goal: value.goal,
        control_mode,
        provider_route_id: value
            .has_provider_route_id
            .then_some(value.provider_route_id),
        assistant_config_version: value.assistant_config_version,
        policy_version: value.policy_version,
        skill_version_id: value.has_skill_version_id.then_some(value.skill_version_id),
        builtin_skill,
        tool_allowlist: value.tool_allowlist,
        milestone,
        budgets,
        has_task_deadline: value.has_task_deadline,
        task_deadline_monotonic_ms: value.task_deadline_monotonic_ms,
        task_deadline_utc_ms: value.task_deadline_utc_ms,
        predecessor_task_id: value
            .has_predecessor_task_id
            .then_some(value.predecessor_task_id),
        trace_id: value.trace_id,
        task_id_seed: value.task_id_seed,
        template_id: wire::TaskTemplateId::from_wire(u32::from(value.template_id))?,
        consent_preview: wire::TaskConsentPreview {
            sources: consent_sources,
            source_discovery_enabled: value.source_discovery_enabled,
            new_source_cap: value.new_source_cap,
            provider_route: consent_provider_route,
        },
        initial_consent_receipt_id: value.initial_consent_receipt_id,
        browser_session_id: value.browser_session_id,
        library_refresh: value
            .has_library_refresh
            .then_some(wire::LibraryRefreshRequest {
                preview_id: value.library_refresh_preview_id,
                library_revision: value.library_refresh_library_revision,
                collection_id: value.library_refresh_collection_id,
                source_workspace_revision: value.library_refresh_workspace_revision,
                sources: Vec::new(),
            }),
    };
    Some((operation, start))
}

fn storage_effect(envelope: OperationEnvelope<EffectRequest>) -> Option<ffi::BridgeStorageEffect> {
    let EffectRequest::Storage(storage) = envelope.body else {
        return None;
    };
    let wire = storage.to_wire();
    let workspace = wire.workspace;
    Some(ffi::BridgeStorageEffect {
        operation: ffi::BridgeOperation {
            operation_id: envelope.operation_id.as_str().to_owned(),
            service_generation: envelope.service_generation.value(),
            task_revision: envelope.task_revision,
            deadline_monotonic_ms: envelope.deadline.as_millis(),
            idempotency_key: envelope.idempotency_key.as_str().to_owned(),
        },
        effect_id: envelope.operation_id.as_str().to_owned(),
        operation_kind: wire::StorageOperation::AppendTaskCommit as u8,
        task_id: wire.task_id,
        expected_revision: wire.expected_revision,
        resulting_revision: wire.resulting_revision,
        transaction_batch: wire.transaction_batch,
        task_id_seed: wire.task_id_seed,
        workspace_id: workspace
            .as_ref()
            .map_or_else(String::new, |value| value.workspace_id.clone()),
        workspace_expected_revision: workspace
            .as_ref()
            .map_or(0, |value| value.expected_revision),
        workspace_resulting_revision: workspace
            .as_ref()
            .map_or(0, |value| value.resulting_revision),
        workspace_snapshot: workspace.map_or_else(Vec::new, |value| value.snapshot),
        skill_id: String::new(),
        skill_version: 0,
        skill_origin: String::new(),
        skill_provenance: 0,
        skill_status: 0,
        skill_definition: Vec::new(),
        skill_step_count: 0,
        skill_changed_at_utc_ms: 0,
        skill_task_id: String::new(),
        skill_run_outcome: 0,
        skill_ran_at_utc_ms: 0,
        configuration_disabled_abilities: Vec::new(),
        configuration_preset: 0,
        configuration_pace: 0,
        configuration_length: 0,
        configuration_check_in: 0,
    })
}
