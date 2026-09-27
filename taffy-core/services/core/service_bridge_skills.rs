// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable saved-skill mutation and exact completion publication.

use core_runtime::{wire, PreparedSkillMutation, SkillMutationError, SkillMutationPersistence};

use crate::ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

pub(crate) struct PendingSkillMutation {
    effect_id: String,
    service_generation: u64,
    deadline_monotonic_ms: u64,
    idempotency_key: String,
    prepared: PreparedSkillMutation,
}

/// An internally recorded completed task uses the same inactive install and
/// exact storage acknowledgment as a person-taught draft. This runs only
/// after the completed task state was successfully projected for publication.
pub(crate) fn stage_completed_flow(
    bridge: &mut ServiceBridge,
    task_id: &core_runtime::TaskId,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> Option<ffi::BridgeStorageEffect> {
    let prepared = bridge
        .runtime
        .as_ref()?
        .prepare_completed_flow(task_id, now_utc_millis)?;
    if bridge
        .pending_skill_mutations
        .values()
        .any(|pending| pending.prepared.skill_id() == prepared.skill_id())
    {
        return None;
    }
    let operation_id = format!("recorded-{}", prepared.skill_id());
    let operation = SkillEffectOperation {
        service_generation: bridge.generation.value(),
        deadline_monotonic_ms: now_monotonic_ms.checked_add(30_000)?,
        idempotency_key: operation_id.clone(),
    };
    let effect = storage_effect(&operation_id, &operation, &prepared);
    bridge.pending_skill_mutations.insert(
        operation_id,
        PendingSkillMutation {
            effect_id: effect.effect_id.clone(),
            service_generation: operation.service_generation,
            deadline_monotonic_ms: operation.deadline_monotonic_ms,
            idempotency_key: operation.idempotency_key,
            prepared,
        },
    );
    Some(effect)
}

#[allow(non_snake_case)]
pub(crate) fn SubmitSkillMutation(
    bridge: &mut ServiceBridge,
    command: ffi::BridgeSkillCommand,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    let operation_id = command.operation.operation_id.clone();
    if let Err(status) = validate_operation(
        &command.operation,
        bridge.generation.value(),
        now_monotonic_ms,
    ) {
        return response(&operation_id, status, Vec::new());
    }
    if bridge.pending_skill_mutations.contains_key(&operation_id) {
        return response(
            &operation_id,
            wire::AdmissionStatus::Duplicate as u8,
            Vec::new(),
        );
    }
    let Some((body, effect_operation)) = decode_command(command) else {
        return invalid(&operation_id);
    };
    if bridge
        .pending_skill_mutations
        .values()
        .any(|pending| pending.prepared.skill_id() == body.skill_id)
    {
        return response(
            &operation_id,
            wire::AdmissionStatus::Backpressure as u8,
            Vec::new(),
        );
    }
    let Some(runtime) = bridge.runtime.as_ref() else {
        return unavailable(&operation_id);
    };
    if runtime.private_profile() {
        return invalid(&operation_id);
    }
    let prepared = match runtime.prepare_skill_mutation(&body) {
        Ok(prepared) => prepared,
        Err(SkillMutationError::StaleVersion | SkillMutationError::UnknownSkill) => {
            return response(
                &operation_id,
                wire::AdmissionStatus::StaleRevision as u8,
                Vec::new(),
            )
        }
        Err(_) => return invalid(&operation_id),
    };
    let effect_id = operation_id.clone();
    let effect = storage_effect(&operation_id, &effect_operation, &prepared);
    bridge.pending_skill_mutations.insert(
        operation_id.clone(),
        PendingSkillMutation {
            effect_id,
            service_generation: bridge.generation.value(),
            deadline_monotonic_ms: effect.operation.deadline_monotonic_ms,
            idempotency_key: effect.operation.idempotency_key.clone(),
            prepared,
        },
    );
    response(
        &operation_id,
        wire::AdmissionStatus::Accepted as u8,
        vec![effect],
    )
}

struct SkillEffectOperation {
    service_generation: u64,
    deadline_monotonic_ms: u64,
    idempotency_key: String,
}

fn storage_effect(
    operation_id: &str,
    operation: &SkillEffectOperation,
    prepared: &PreparedSkillMutation,
) -> ffi::BridgeStorageEffect {
    let mut effect = empty_storage_effect(operation_id, prepared.skill_id(), operation);
    match prepared.persistence() {
        SkillMutationPersistence::Install(body) => {
            effect.operation_kind = wire::StorageOperation::InstallSkill as u8;
            effect.skill_version = body.version;
            effect.skill_origin = body.origin.clone();
            effect.skill_provenance = body.provenance as u8;
            effect.skill_definition.clone_from(&body.definition);
            effect.skill_step_count = body.step_count;
            effect.skill_changed_at_utc_ms = body.recorded_at_utc_ms;
        }
        SkillMutationPersistence::SetStatus(body) => {
            effect.operation_kind = wire::StorageOperation::SetSkillStatus as u8;
            effect.skill_version = body.version;
            effect.skill_status = body.status as u8;
            effect.skill_changed_at_utc_ms = body.changed_at_utc_ms;
        }
        SkillMutationPersistence::Forget(_) => {
            effect.operation_kind = wire::StorageOperation::ForgetSkill as u8;
        }
    }
    effect
}

fn empty_storage_effect(
    operation_id: &str,
    skill_id: &str,
    operation: &SkillEffectOperation,
) -> ffi::BridgeStorageEffect {
    ffi::BridgeStorageEffect {
        operation: ffi::BridgeOperation {
            operation_id: operation_id.to_owned(),
            service_generation: operation.service_generation,
            task_revision: 0,
            deadline_monotonic_ms: operation.deadline_monotonic_ms,
            idempotency_key: operation.idempotency_key.clone(),
        },
        effect_id: operation_id.to_owned(),
        operation_kind: 0,
        task_id: String::new(),
        expected_revision: 0,
        resulting_revision: 0,
        transaction_batch: Vec::new(),
        task_id_seed: [0; 32],
        workspace_id: String::new(),
        workspace_expected_revision: 0,
        workspace_resulting_revision: 0,
        workspace_snapshot: Vec::new(),
        skill_id: skill_id.to_owned(),
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
    }
}

pub(crate) fn deliver_skill_mutation_completion(
    bridge: &mut ServiceBridge,
    completion: ffi::BridgeStorageCompletion,
) -> ffi::BridgeResponse {
    let operation_id = completion.operation.operation_id.clone();
    let Some(pending) = bridge.pending_skill_mutations.get(&operation_id) else {
        return invalid(&operation_id);
    };
    if completion.operation.service_generation != pending.service_generation
        || completion.operation.task_revision != 0
        || completion.operation.deadline_monotonic_ms != pending.deadline_monotonic_ms
        || completion.operation.idempotency_key != pending.idempotency_key
        || completion.effect_id != pending.effect_id
    {
        return invalid(&operation_id);
    }
    let Some(status) = wire::EffectStatus::from_wire(u32::from(completion.status)) else {
        return invalid(&operation_id);
    };
    let Some(pending) = bridge.pending_skill_mutations.remove(&operation_id) else {
        return invalid(&operation_id);
    };
    if status != wire::EffectStatus::Completed {
        return response(
            &operation_id,
            wire::AdmissionStatus::StaleRevision as u8,
            Vec::new(),
        );
    }
    if completion.committed_revision != 0 {
        return withdraw_runtime(bridge, &operation_id);
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return unavailable(&operation_id);
    };
    if runtime.install_skill_mutation(pending.prepared).is_err() {
        return withdraw_runtime(bridge, &operation_id);
    }
    response_after_change(
        bridge,
        response(
            &operation_id,
            wire::AdmissionStatus::Accepted as u8,
            Vec::new(),
        ),
    )
}

fn decode_command(
    command: ffi::BridgeSkillCommand,
) -> Option<(wire::MutateSkillCommand, SkillEffectOperation)> {
    let operation = SkillEffectOperation {
        service_generation: command.operation.service_generation,
        deadline_monotonic_ms: command.operation.deadline_monotonic_ms,
        idempotency_key: command.operation.idempotency_key,
    };
    let body = wire::MutateSkillCommand {
        kind: wire::SkillMutationKind::from_wire(u32::from(command.kind))?,
        skill_id: command.skill_id,
        expected_version: command.expected_version,
        origin: command.origin,
        clauses: command
            .clauses
            .into_iter()
            .map(|clause| {
                Some(wire::SkillObservedClause {
                    kind: wire::SkillClauseKind::from_wire(u32::from(clause.kind))?,
                    role: clause.role,
                    detail: clause.detail,
                })
            })
            .collect::<Option<Vec<_>>>()?,
        steps: command
            .steps
            .into_iter()
            .map(|step| {
                Some(wire::SkillObservedStep {
                    verb: step.verb,
                    arguments: step
                        .arguments
                        .into_iter()
                        .map(|argument| {
                            if (!argument.has_public_address && !argument.public_address.is_empty())
                                || (!argument.has_semantic_target
                                    && (argument.semantic_role != 0
                                        || argument.semantic_phrase != 0))
                            {
                                return None;
                            }
                            Some(wire::SkillObservedArgument {
                                parameter: argument.parameter,
                                kind: wire::SkillArgumentKind::from_wire(u32::from(argument.kind))?,
                                value: argument.value,
                                purpose: argument.purpose,
                                public_address: argument
                                    .has_public_address
                                    .then_some(argument.public_address),
                                semantic_target: argument.has_semantic_target.then_some(
                                    wire::SkillSemanticTarget {
                                        role: argument.semantic_role,
                                        phrase: argument.semantic_phrase,
                                    },
                                ),
                            })
                        })
                        .collect::<Option<Vec<_>>>()?,
                    postcondition: step.postcondition,
                    has_fill: step.has_fill,
                    fill_purpose: step.fill_purpose,
                })
            })
            .collect::<Option<Vec<_>>>()?,
        admitted: command.admitted,
        enabled: command.enabled,
        recorded_at_epoch_ms: command.recorded_at_epoch_ms,
    };
    Some((body, operation))
}

fn validate_operation(
    operation: &crate::service_bridge_skills_ffi::ffi::BridgeSkillOperation,
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

fn invalid(operation_id: &str) -> ffi::BridgeResponse {
    response(
        operation_id,
        wire::AdmissionStatus::InvalidCommand as u8,
        Vec::new(),
    )
}

fn unavailable(operation_id: &str) -> ffi::BridgeResponse {
    response(
        operation_id,
        wire::AdmissionStatus::CoreUnavailable as u8,
        Vec::new(),
    )
}

fn withdraw_runtime(bridge: &mut ServiceBridge, operation_id: &str) -> ffi::BridgeResponse {
    bridge.runtime = None;
    response_after_change(bridge, unavailable(operation_id))
}
