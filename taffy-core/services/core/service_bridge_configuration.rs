// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable one-assistant configuration mutation and publication.

use core_runtime::wire;

use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

pub(crate) struct PendingAssistantConfiguration {
    pub(crate) effect_id: String,
    pub(crate) service_generation: u64,
    pub(crate) deadline_monotonic_ms: u64,
    pub(crate) idempotency_key: String,
    pub(crate) expected_revision: u64,
    pub(crate) configuration: core_runtime::AssistantConfiguration,
}

#[allow(non_snake_case)]
pub(crate) fn SubmitAssistantConfiguration(
    bridge: &mut ServiceBridge,
    command: ffi::BridgeAssistantConfigurationCommand,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let operation_id = command.operation.operation_id.clone();
    let status = validate_operation(
        &command.operation,
        bridge.generation.value(),
        now_monotonic_ms,
    );
    if let Err(status) = status {
        return response(&operation_id, status, Vec::new());
    }
    if bridge
        .pending_assistant_configurations
        .contains_key(&operation_id)
    {
        return response(
            &operation_id,
            wire::AdmissionStatus::Duplicate as u8,
            Vec::new(),
        );
    }
    let Some(runtime) = bridge.runtime.as_ref() else {
        return response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    let Some(body) = decode_body(&command) else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let pending = match runtime.prepare_assistant_configuration(&body) {
        Ok(value) => value,
        Err(core_runtime::AssistantConfigurationError::StaleRevision) => {
            return response(
                &operation_id,
                wire::AdmissionStatus::StaleRevision as u8,
                Vec::new(),
            );
        }
        Err(_) => {
            return response(
                &operation_id,
                wire::AdmissionStatus::InvalidCommand as u8,
                Vec::new(),
            );
        }
    };
    let effect_id = operation_id.clone();
    let persist = pending.persist_body();
    let effect = ffi::BridgeStorageEffect {
        operation: ffi::BridgeOperation {
            operation_id: operation_id.clone(),
            service_generation: command.operation.service_generation,
            task_revision: 0,
            deadline_monotonic_ms: command.operation.deadline_monotonic_ms,
            idempotency_key: command.operation.idempotency_key.clone(),
        },
        effect_id: effect_id.clone(),
        operation_kind: wire::StorageOperation::SetAssistantConfiguration as u8,
        task_id: String::new(),
        expected_revision: body.expected_revision,
        resulting_revision: pending.revision(),
        transaction_batch: Vec::new(),
        task_id_seed: [0; 32],
        workspace_id: String::new(),
        workspace_expected_revision: 0,
        workspace_resulting_revision: 0,
        workspace_snapshot: Vec::new(),
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
        configuration_disabled_abilities: persist
            .disabled_abilities
            .iter()
            .map(|ability| *ability as u8)
            .collect(),
        configuration_preset: persist.preset as u8,
        configuration_pace: persist.pace,
        configuration_length: persist.length,
        configuration_check_in: persist.check_in,
    };
    bridge.pending_assistant_configurations.insert(
        operation_id.clone(),
        PendingAssistantConfiguration {
            effect_id,
            service_generation: command.operation.service_generation,
            deadline_monotonic_ms: command.operation.deadline_monotonic_ms,
            idempotency_key: command.operation.idempotency_key,
            expected_revision: body.expected_revision,
            configuration: pending,
        },
    );
    response(
        &operation_id,
        wire::AdmissionStatus::Accepted as u8,
        vec![effect],
    )
}

pub(crate) fn deliver_assistant_configuration_completion(
    bridge: &mut ServiceBridge,
    completion: ffi::BridgeStorageCompletion,
) -> ffi::BridgeResponse {
    let operation_id = completion.operation.operation_id.clone();
    let Some(pending) = bridge.pending_assistant_configurations.get(&operation_id) else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    if completion.operation.service_generation != pending.service_generation
        || completion.operation.task_revision != 0
        || completion.operation.deadline_monotonic_ms != pending.deadline_monotonic_ms
        || completion.operation.idempotency_key != pending.idempotency_key
        || completion.effect_id != pending.effect_id
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
    let Some(pending) = bridge
        .pending_assistant_configurations
        .remove(&operation_id)
    else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    if status != wire::EffectStatus::Completed {
        return response(
            &operation_id,
            wire::AdmissionStatus::StaleRevision as u8,
            Vec::new(),
        );
    }
    if completion.committed_revision != pending.configuration.revision() {
        bridge.runtime = None;
        let unavailable = response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
        return response_after_change(bridge, unavailable);
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    if runtime.assistant_configuration().revision() != pending.expected_revision {
        bridge.runtime = None;
        let unavailable = response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
        return response_after_change(bridge, unavailable);
    }
    runtime.install_assistant_configuration(pending.configuration);
    let accepted = response(
        &operation_id,
        wire::AdmissionStatus::Accepted as u8,
        Vec::new(),
    );
    response_after_change(bridge, accepted)
}

fn validate_operation(
    operation: &ffi::BridgeConfigurationOperation,
    generation: u64,
    now_monotonic_ms: u64,
) -> Result<(), u8> {
    if operation.service_generation != generation {
        return Err(wire::AdmissionStatus::StaleGeneration as u8);
    }
    if operation.task_revision != 0
        || operation.operation_id.is_empty()
        || operation.operation_id.len() > wire::MAX_OPERATION_ID_BYTES
        || operation.idempotency_key.is_empty()
        || operation.idempotency_key.len() > wire::MAX_IDEMPOTENCY_KEY_BYTES
    {
        return Err(wire::AdmissionStatus::InvalidCommand as u8);
    }
    if now_monotonic_ms >= operation.deadline_monotonic_ms {
        return Err(wire::AdmissionStatus::DeadlineExceeded as u8);
    }
    Ok(())
}

fn decode_body(
    command: &ffi::BridgeAssistantConfigurationCommand,
) -> Option<wire::SetAssistantConfigurationCommand> {
    let disabled_abilities = command
        .disabled_abilities
        .iter()
        .copied()
        .map(|ability| wire::AssistantAbility::from_wire(u32::from(ability)))
        .collect::<Option<Vec<_>>>()?;
    Some(wire::SetAssistantConfigurationCommand {
        expected_revision: command.expected_revision,
        disabled_abilities,
        preset: wire::PersonalityPreset::from_wire(u32::from(command.preset))?,
        pace: command.pace,
        length: command.length,
        check_in: command.check_in,
    })
}
