// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exact conversion and validation for flat composer bridge records.

use core_runtime::wire;

use crate::service_bridge_composer_ffi::ffi as composer_ffi;

/// The flat CXX record, back into the generated command it stands for.
///
/// The bounds are applied here as well as inside the composition, because
/// this is where a value a surface typed enters the core and the contract's
/// limits are the product's rather than the caller's. A prefix that is only
/// whitespace is refused by the composition rather than here: what counts as
/// nothing to continue is a question about the model call, not about the
/// seam.
pub(super) fn command_to_wire(
    value: composer_ffi::BridgeComposerCommand,
) -> Option<wire::CoreServiceCommand> {
    if value.request_id.is_empty()
        || value.request_id.len() > wire::MAX_IDENTIFIER_BYTES
        || value.prefix.is_empty()
        || value.prefix.len() > wire::MAX_COMPOSER_PREFIX_BYTES
        || value.suffix.len() > wire::MAX_COMPOSER_SUFFIX_BYTES
        || (!value.has_suffix && !value.suffix.is_empty())
    {
        return None;
    }
    let command = wire::CoreServiceCommand {
        operation: operation_to_wire(value.operation),
        kind: wire::CoreServiceCommandKind::RequestComposerCompletion,
        start_task: None,
        cancel_task: None,
        user_decision: None,
        auth_callback: None,
        permission_result: None,
        start_auth: None,
        request_email_link: None,
        sign_out: None,
        auth_credential_result: None,
        correct_workspace_fact: None,
        exclude_workspace_source: None,
        request_workspace_export: None,
        set_asset_delivery_policy: None,
        request_asset: None,
        remove_asset: None,
        save_provider_credential: None,
        forget_provider_credential: None,
        start_provider_auth: None,
        provider_auth_callback: None,
        save_custom_provider: None,
        remove_custom_provider: None,
        complete_handover: None,
        expire_handover: None,
        supply_user_input: None,
        follow_up: None,
        set_provider_credential_state: None,
        probe_provider_credential: None,
        supply_field_values: None,
        set_provider_model_preference: None,
        probe_custom_endpoint: None,
        request_composer_completion: Some(wire::RequestComposerCompletionCommand {
            request_id: value.request_id,
            prefix: value.prefix,
            suffix: value.has_suffix.then_some(value.suffix),
        }),
        cancel_composer_completion: None,
        pause_task: None,
        resume_task: None,
        take_over: None,
        set_assistant_configuration: None,
        save_workspace: None,

        rename_workspace: None,

        delete_workspace: None,

        discard_workspace: None,
        search_library: None,
        save_library_fact: None,
        remove_library_entry: None,
        request_library_export: None,
        search_memory: None,
        upsert_memory: None,
        delete_memory: None,
        accept_task_artifact: None,
        export_task_artifact: None,
        replace_saved_data_snapshot: None,
        mutate_skill: None,
        cancel_provider_auth: None,
    };
    command.has_valid_body().then_some(command)
}

/// The flat withdrawal record, back into the generated command it stands for.
///
/// Only the request identity, bounded here as well as inside the composition
/// for the same reason the submission's fields are: this is where a value a
/// surface typed enters the core, and the contract's limits are the product's
/// rather than the caller's.
pub(super) fn cancel_to_wire(
    value: composer_ffi::BridgeComposerCancel,
) -> Option<wire::CoreServiceCommand> {
    if value.request_id.is_empty() || value.request_id.len() > wire::MAX_IDENTIFIER_BYTES {
        return None;
    }
    let command = wire::CoreServiceCommand {
        operation: operation_to_wire(value.operation),
        kind: wire::CoreServiceCommandKind::CancelComposerCompletion,
        start_task: None,
        cancel_task: None,
        user_decision: None,
        auth_callback: None,
        permission_result: None,
        start_auth: None,
        request_email_link: None,
        sign_out: None,
        auth_credential_result: None,
        correct_workspace_fact: None,
        exclude_workspace_source: None,
        request_workspace_export: None,
        set_asset_delivery_policy: None,
        request_asset: None,
        remove_asset: None,
        save_provider_credential: None,
        forget_provider_credential: None,
        start_provider_auth: None,
        provider_auth_callback: None,
        save_custom_provider: None,
        remove_custom_provider: None,
        complete_handover: None,
        expire_handover: None,
        supply_user_input: None,
        follow_up: None,
        set_provider_credential_state: None,
        probe_provider_credential: None,
        supply_field_values: None,
        set_provider_model_preference: None,
        probe_custom_endpoint: None,
        request_composer_completion: None,
        cancel_composer_completion: Some(wire::CancelComposerCompletionCommand {
            request_id: value.request_id,
        }),
        pause_task: None,
        resume_task: None,
        take_over: None,
        set_assistant_configuration: None,
        save_workspace: None,

        rename_workspace: None,

        delete_workspace: None,

        discard_workspace: None,
        search_library: None,
        save_library_fact: None,
        remove_library_entry: None,
        request_library_export: None,
        search_memory: None,
        upsert_memory: None,
        delete_memory: None,
        accept_task_artifact: None,
        export_task_artifact: None,
        replace_saved_data_snapshot: None,
        mutate_skill: None,
        cancel_provider_auth: None,
    };
    command.has_valid_body().then_some(command)
}

/// The composer bridge's mirror of the operation record, into the one wire
/// envelope. Field for field, because the mirror exists only to break a
/// header cycle between generated cxx bridges, never to differ.
pub(super) fn operation_to_wire(
    value: composer_ffi::BridgeComposerOperation,
) -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: value.operation_id,
        service_generation: value.service_generation,
        task_revision: value.task_revision,
        deadline_monotonic_ms: value.deadline_monotonic_ms,
        idempotency_key: value.idempotency_key,
    }
}

pub(super) fn operation_from_wire(
    value: wire::OperationEnvelope,
) -> composer_ffi::BridgeComposerOperation {
    composer_ffi::BridgeComposerOperation {
        operation_id: value.operation_id,
        service_generation: value.service_generation,
        task_revision: value.task_revision,
        deadline_monotonic_ms: value.deadline_monotonic_ms,
        idempotency_key: value.idempotency_key,
    }
}

/// The operation envelope, checked before the composition is asked anything.
///
/// The same shape the provider bridge uses, and for the same reason: a
/// composer suggestion belongs to no task, so a non-zero task revision is a
/// caller that built the envelope wrong rather than a stale one.
pub(super) fn validate_operation(
    operation: &composer_ffi::BridgeComposerOperation,
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
