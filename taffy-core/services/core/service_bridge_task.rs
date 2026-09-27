// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Durable task-command admission for the CXX boundary.

use core_runtime::wire;
use core_runtime::{Command, CommandEnvelope, IdempotencyKey, OperationId, TaskId, TraceId};

pub(crate) use crate::service_bridge_task_completion::deliver_task_completion;
pub(crate) use crate::service_bridge_task_submit::{
    begin_submit, begin_submit_typed, TypedTaskResult,
};

use crate::ffi;
use crate::service_bridge_runtime::{response, PendingTaskCancellation, ServiceBridge};
use crate::service_bridge_task_command::{command_envelope, decode_command};
use crate::service_bridge_task_support::{
    settlement_identities, valid_identifier, validate_operation,
};

const INTERNAL_SETTLEMENT_DEADLINE_MS: u64 = 30_000;

#[allow(non_snake_case)]
pub(crate) fn SubmitTask(
    bridge: &mut ServiceBridge,
    command: ffi::BridgeTaskCommand,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = command.operation.operation_id.clone();
    let generation = bridge.generation.value();
    let mut operation = wire::OperationEnvelope {
        operation_id: command.operation.operation_id.clone(),
        service_generation: command.operation.service_generation,
        task_revision: command.operation.task_revision,
        deadline_monotonic_ms: command.operation.deadline_monotonic_ms,
        idempotency_key: command.operation.idempotency_key.clone(),
    };
    if let Err(status) = validate_operation(&operation, generation, now_monotonic_ms) {
        return response(&operation_id, status, Vec::new());
    }
    let Ok(runtime_operation_id) = OperationId::new(operation_id.clone()) else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let task_id = TaskId::new(command.task_id.clone());
    if let Some(pending) = bridge
        .pending_task_cancellations
        .values()
        .find(|pending| pending.operation.operation_id == operation_id)
    {
        let status = if pending.operation.idempotency_key == operation.idempotency_key
            && pending.task_id == task_id
        {
            wire::AdmissionStatus::Duplicate
        } else {
            wire::AdmissionStatus::InvalidCommand
        };
        return response(&operation_id, status as u8, Vec::new());
    }
    if bridge.pending_submits.contains_key(&operation_id) {
        return response(
            &operation_id,
            wire::AdmissionStatus::Duplicate as u8,
            Vec::new(),
        );
    }
    if bridge
        .pending_task_cancellations
        .contains_key(task_id.as_str())
    {
        return response(
            &operation_id,
            wire::AdmissionStatus::Backpressure as u8,
            Vec::new(),
        );
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(
            &operation_id,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    runtime.set_utc_millis(now_utc_millis);
    if runtime.core().operation_is_terminal(&runtime_operation_id) {
        return response(
            &operation_id,
            wire::AdmissionStatus::Duplicate as u8,
            Vec::new(),
        );
    }
    let commit_in_flight = runtime.core().commit_in_flight(&task_id).unwrap_or(false);
    let task_revision = match runtime.core().task(&task_id) {
        Some(task) => task.revision(),
        None => {
            return response(
                &operation_id,
                wire::AdmissionStatus::InvalidCommand as u8,
                Vec::new(),
            )
        }
    };
    // A surface asks to stop one task by identity; it never supplies a reducer
    // revision. The browser resolves the newest state it has published, but the
    // service intentionally dispatches the next effect before that state so a
    // fast effect can advance the reducer again before the person's stop
    // arrives. Waiting for another publication cannot solve that race: the
    // effect being stopped may itself be what prevents another publication.
    //
    // Bind that one monotonic control atomically to the revision this ordered
    // core owns. This is not a general stale-command retry. The submitted
    // operation must already be valid for this generation, its revision must be
    // non-zero and strictly older, and decoding against the current task must
    // prove both a well-formed user cancellation and a currently exposed Stop
    // control. Future revisions and every other command retain exact-revision
    // admission. Only the local revision is replaced: the browser-minted
    // operation and idempotency identities remain the identities of the one
    // person action.
    let rebound_cancel = if task_revision != operation.task_revision {
        let is_older_user_cancel = operation.task_revision < task_revision
            && wire::CoreServiceCommandKind::from_wire(u32::from(command.kind))
                == Some(wire::CoreServiceCommandKind::CancelTask)
            && wire::CancelReason::from_wire(u32::from(command.cancel_reason))
                == Some(wire::CancelReason::User)
            && valid_identifier(&command.trace_id);
        let decoded = is_older_user_cancel.then(|| {
            decode_command(
                runtime,
                &task_id,
                task_revision,
                &command,
                now_monotonic_ms,
                now_utc_millis,
            )
        });
        let Some(Some(Command::CancelTask)) = decoded else {
            return response(
                &operation_id,
                wire::AdmissionStatus::StaleRevision as u8,
                Vec::new(),
            );
        };
        operation.task_revision = task_revision;
        Some(Command::CancelTask)
    } else {
        None
    };
    let task_command = rebound_cancel.or_else(|| {
        decode_command(
            runtime,
            &task_id,
            task_revision,
            &command,
            now_monotonic_ms,
            now_utc_millis,
        )
    });
    let Some(task_command) = task_command else {
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let defer_cancel = commit_in_flight && matches!(&task_command, Command::CancelTask);
    // Two commands stage a line for the next compose and journal nothing of
    // it: the answer to a question Taffy asked, and a follow-up question the
    // person asked of a finished task (decision 0137). They differ in the
    // prefix the model reads and in whether an ask prompt is open to drop.
    let staged_answer = matches!(task_command, Command::SupplyUserInput);
    let staged_ask = staged_answer || matches!(task_command, Command::FollowUp);
    if staged_ask {
        let classified = if staged_answer {
            core_runtime::classify_person_answer(&command.answer)
        } else {
            core_runtime::classify_follow_up_question(&command.answer)
        };
        let Ok(line) = classified else {
            return response(
                &operation_id,
                wire::AdmissionStatus::InvalidCommand as u8,
                Vec::new(),
            );
        };
        runtime.stage_person_answer(task_id.as_str(), line);
        if staged_answer {
            runtime.drop_ask_prompt(task_id.as_str());
        }
    }
    let Some(envelope) = command_envelope(&operation, command.trace_id, task_command) else {
        if staged_ask {
            runtime.drop_person_answer(task_id.as_str());
        }
        return response(
            &operation_id,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let task_id_text = task_id.as_str().to_owned();
    if defer_cancel {
        bridge.pending_task_cancellations.insert(
            task_id_text,
            PendingTaskCancellation {
                task_id,
                envelope,
                operation,
            },
        );
        return response(
            &operation_id,
            wire::AdmissionStatus::Accepted as u8,
            Vec::new(),
        );
    }
    let response = begin_submit(bridge, task_id, envelope, operation, now_monotonic_ms);
    if staged_ask && response.admission.status != wire::AdmissionStatus::Accepted as u8 {
        if let Some(runtime) = bridge.runtime.as_mut() {
            runtime.drop_person_answer(&task_id_text);
        }
    }
    response
}

#[allow(non_snake_case)]
pub(crate) fn CompleteTaskSettlement(
    bridge: &mut ServiceBridge,
    settlement: ffi::BridgeTaskSettlement,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    if settlement.service_generation != bridge.generation.value()
        || settlement.task_id.is_empty()
        || settlement.task_id.len() > wire::MAX_IDENTIFIER_BYTES
        || settlement.task_revision == 0
    {
        return response("", wire::AdmissionStatus::InvalidCommand as u8, Vec::new());
    }
    let task_id = TaskId::new(settlement.task_id);
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response("", wire::AdmissionStatus::CoreUnavailable as u8, Vec::new());
    };
    runtime.set_utc_millis(now_utc_millis);
    let Some(task) = runtime.core().task(&task_id) else {
        return response("", wire::AdmissionStatus::InvalidCommand as u8, Vec::new());
    };
    if task.revision() != settlement.task_revision {
        return response("", wire::AdmissionStatus::StaleRevision as u8, Vec::new());
    }
    let Ok(facts) = task.view_facts() else {
        return response("", wire::AdmissionStatus::CoreUnavailable as u8, Vec::new());
    };
    let Some(kind) = wire::TaskSettlementKind::from_wire(u32::from(settlement.kind)) else {
        return response("", wire::AdmissionStatus::InvalidCommand as u8, Vec::new());
    };
    let command = match (kind, facts.state) {
        (wire::TaskSettlementKind::Pause, core_runtime::TaskState::Pausing) => {
            Command::PauseSettled
        }
        (wire::TaskSettlementKind::Cancel, core_runtime::TaskState::Cancelling) => {
            Command::CancelSettled
        }
        _ => return response("", wire::AdmissionStatus::StaleRevision as u8, Vec::new()),
    };
    let identities = settlement_identities(task_id.as_str(), settlement.task_revision, kind);
    let Some((operation_id, idempotency_key, trace_id)) = identities else {
        return response("", wire::AdmissionStatus::CoreUnavailable as u8, Vec::new());
    };
    let deadline = now_monotonic_ms.saturating_add(INTERNAL_SETTLEMENT_DEADLINE_MS);
    let operation = wire::OperationEnvelope {
        operation_id,
        service_generation: bridge.generation.value(),
        task_revision: settlement.task_revision,
        deadline_monotonic_ms: deadline,
        idempotency_key: idempotency_key.clone(),
    };
    let envelope = CommandEnvelope::new(
        IdempotencyKey::new(idempotency_key),
        settlement.task_revision,
        TraceId::new(trace_id),
        command,
    );
    let task_id_text = task_id.as_str().to_owned();
    let response = begin_submit(bridge, task_id, envelope, operation, now_monotonic_ms);
    if response.admission.status == wire::AdmissionStatus::Accepted as u8 {
        crate::service_bridge_task_effect::acknowledge_settlement_effect(
            bridge,
            &task_id_text,
            settlement.task_revision,
        );
    }
    response
}
