// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One durable reducer submission, with optional typed browser-owned result.

use core_runtime::wire;
use core_runtime::{
    BeginSubmit, CommandEnvelope, Deadline, OperationId, TaskId, TaskResultSubmission,
};

use crate::ffi;
use crate::service_bridge_runtime::{note_refusal, response, ServiceBridge};
use crate::service_bridge_task_support::{storage_effect, submit_error_label, submit_error_status};
use crate::service_bridge_trace as trace;

/// The one typed browser-owned result a settlement may carry beside its
/// outcome. Each is staged behind the same storage commit as the outcome and
/// reaches the transcript only when that commit lands.
pub(crate) enum TypedTaskResult {
    Tab(core_runtime::TaskTabActionResult),
    Download(core_runtime::TaskDownloadActionResult),
    MediaProbe(core_runtime::MediaProbeTranscriptOutcome),
    LibrarySearch(core_runtime::TaskLibrarySearchTranscriptOutcome),
    MemorySearch(core_runtime::TaskMemorySearchTranscriptOutcome),
    Store(core_runtime::TaskStoreActionResult),
}

pub(crate) fn begin_submit(
    bridge: &mut ServiceBridge,
    task_id: TaskId,
    envelope: CommandEnvelope,
    operation: wire::OperationEnvelope,
    now_monotonic_ms: u64,
) -> ffi::BridgeResponse {
    begin_submit_typed(bridge, task_id, envelope, operation, now_monotonic_ms, None)
}

pub(crate) fn begin_submit_typed(
    bridge: &mut ServiceBridge,
    task_id: TaskId,
    envelope: CommandEnvelope,
    operation: wire::OperationEnvelope,
    now_monotonic_ms: u64,
    typed_result: Option<TypedTaskResult>,
) -> ffi::BridgeResponse {
    // Every command a task applies passes here, whoever chose it — the walk,
    // an effect's terminal, or a person's control — so this is the one place
    // the trace can see the whole of a task's history as it is made.
    let step = trace::step(&task_id, &envelope);
    let response = submit(
        bridge,
        task_id,
        envelope,
        operation,
        now_monotonic_ms,
        typed_result,
    );
    trace::submitted(step, &admission_label(response.admission.status));
    response
}

/// The admission a trace line names. A refusal's branch is logged by whoever
/// answers `CoreUnavailable` or `InvalidCommand`, beside `LastRefusal`.
fn admission_label(status: u8) -> String {
    match status {
        s if s == wire::AdmissionStatus::Accepted as u8 => "accepted".to_owned(),
        s if s == wire::AdmissionStatus::Duplicate as u8 => "duplicate".to_owned(),
        s if s == wire::AdmissionStatus::CoreUnavailable as u8 => "core_unavailable".to_owned(),
        s if s == wire::AdmissionStatus::InvalidCommand as u8 => "invalid_command".to_owned(),
        other => format!("refused:{other}"),
    }
}

fn submit(
    bridge: &mut ServiceBridge,
    task_id: TaskId,
    envelope: CommandEnvelope,
    operation: wire::OperationEnvelope,
    now_monotonic_ms: u64,
    typed_result: Option<TypedTaskResult>,
) -> ffi::BridgeResponse {
    let operation_id_text = operation.operation_id.clone();
    let Some(runtime) = bridge.runtime.as_mut() else {
        note_refusal(bridge, "submit_runtime_missing");
        return response(
            &operation_id_text,
            wire::AdmissionStatus::CoreUnavailable as u8,
            Vec::new(),
        );
    };
    let Ok(operation_id) = OperationId::new(operation.operation_id.clone()) else {
        return response(
            &operation_id_text,
            wire::AdmissionStatus::InvalidCommand as u8,
            Vec::new(),
        );
    };
    let now_utc_millis = runtime.utc_millis();
    let deadline = Deadline::from_millis(operation.deadline_monotonic_ms);
    let submitted = match typed_result {
        Some(TypedTaskResult::Tab(result)) => runtime.core_mut().begin_submit_with_task_tab_result(
            &task_id,
            &result,
            TaskResultSubmission {
                command: envelope,
                operation_id,
                deadline,
                now_monotonic_millis: now_monotonic_ms,
                now_utc_millis,
            },
        ),
        Some(TypedTaskResult::Download(result)) => {
            runtime.core_mut().begin_submit_with_task_download_result(
                &task_id,
                &result,
                TaskResultSubmission {
                    command: envelope,
                    operation_id,
                    deadline,
                    now_monotonic_millis: now_monotonic_ms,
                    now_utc_millis,
                },
            )
        }
        Some(TypedTaskResult::MediaProbe(result)) => {
            runtime.core_mut().begin_submit_with_media_probe_result(
                &task_id,
                result,
                TaskResultSubmission {
                    command: envelope,
                    operation_id,
                    deadline,
                    now_monotonic_millis: now_monotonic_ms,
                    now_utc_millis,
                },
            )
        }
        Some(TypedTaskResult::LibrarySearch(result)) => runtime
            .core_mut()
            .begin_submit_with_task_library_search_result(
                &task_id,
                result,
                TaskResultSubmission {
                    command: envelope,
                    operation_id,
                    deadline,
                    now_monotonic_millis: now_monotonic_ms,
                    now_utc_millis,
                },
            ),
        Some(TypedTaskResult::MemorySearch(result)) => runtime
            .core_mut()
            .begin_submit_with_task_memory_search_result(
                &task_id,
                result,
                TaskResultSubmission {
                    command: envelope,
                    operation_id,
                    deadline,
                    now_monotonic_millis: now_monotonic_ms,
                    now_utc_millis,
                },
            ),
        Some(TypedTaskResult::Store(result)) => {
            runtime.core_mut().begin_submit_with_task_store_result(
                &task_id,
                &result,
                TaskResultSubmission {
                    command: envelope,
                    operation_id,
                    deadline,
                    now_monotonic_millis: now_monotonic_ms,
                    now_utc_millis,
                },
            )
        }
        None => runtime.core_mut().begin_submit(
            &task_id,
            envelope,
            operation_id,
            deadline,
            now_monotonic_ms,
            now_utc_millis,
        ),
    };
    match submitted {
        Ok(BeginSubmit::Duplicate(_)) => response(
            &operation_id_text,
            wire::AdmissionStatus::Duplicate as u8,
            Vec::new(),
        ),
        Ok(BeginSubmit::AwaitingCommit(commit)) => {
            let Some(effect) = storage_effect(*commit) else {
                note_refusal(bridge, "submit_storage_effect_shape");
                return response(
                    &operation_id_text,
                    wire::AdmissionStatus::CoreUnavailable as u8,
                    Vec::new(),
                );
            };
            bridge
                .pending_submits
                .insert(operation_id_text.clone(), task_id.as_str().to_owned());
            response(
                &operation_id_text,
                wire::AdmissionStatus::Accepted as u8,
                vec![effect],
            )
        }
        Err(error) => {
            note_refusal(bridge, submit_error_label(&error));
            response(&operation_id_text, submit_error_status(&error), Vec::new())
        }
    }
}
