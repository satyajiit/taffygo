// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generated account command/effect projections for the CXX boundary.

mod completion;
mod effect;

use core_runtime::wire;
use core_runtime::{AccountServiceError, AccountServiceStep};

use crate::ffi;
use crate::service_bridge_account_ffi::ffi as account_ffi;
use crate::service_bridge_runtime::{response, ServiceBridge};
use crate::service_bridge_status::response_after_change;

use self::completion::{completion_to_wire, wipe_bridge_entropy, wipe_generated_entropy};
use self::effect::effect_to_bridge;

#[allow(non_snake_case)]
pub(crate) fn SubmitAccount(
    bridge: &mut ServiceBridge,
    command: account_ffi::BridgeAccountCommand,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = command.operation.operation_id.clone();
    let Some(runtime) = bridge.runtime.as_mut() else {
        return response(&operation_id, 6, Vec::new());
    };
    runtime.set_utc_millis(now_utc_millis);
    let Some(command) = command_to_wire(command) else {
        return response(&operation_id, 5, Vec::new());
    };
    match runtime.submit_account_command(&command, now_monotonic_ms) {
        Ok(step) => step_response(bridge, step),
        Err(error) => response(&operation_id, error_status(error), Vec::new()),
    }
}

#[allow(non_snake_case)]
pub(crate) fn DeliverAccountCompletion(
    bridge: &mut ServiceBridge,
    mut completion: account_ffi::BridgeAccountCompletion,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> ffi::BridgeResponse {
    let operation_id = completion.operation.operation_id.clone();
    let Some(runtime) = bridge.runtime.as_mut() else {
        wipe_bridge_entropy(&mut completion);
        return response(&operation_id, 6, Vec::new());
    };
    runtime.set_utc_millis(now_utc_millis);
    let Some(mut result) = completion_to_wire(completion) else {
        return response(&operation_id, 5, Vec::new());
    };
    let delivered = runtime.deliver_account_effect_result(&result, now_monotonic_ms);
    wipe_generated_entropy(&mut result);
    match delivered {
        Ok(step) => step_response(bridge, step),
        Err(error) => response(&operation_id, error_status(error), Vec::new()),
    }
}

fn command_to_wire(value: account_ffi::BridgeAccountCommand) -> Option<wire::CoreServiceCommand> {
    let kind = wire::CoreServiceCommandKind::from_wire(u32::from(value.kind))?;
    let operation = wire::OperationEnvelope {
        operation_id: value.operation.operation_id,
        service_generation: value.operation.service_generation,
        task_revision: value.operation.task_revision,
        deadline_monotonic_ms: value.operation.deadline_monotonic_ms,
        idempotency_key: value.operation.idempotency_key,
    };
    let mut command = wire::CoreServiceCommand {
        operation,
        kind,
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
        supply_field_values: None,
        set_provider_credential_state: None,
        probe_provider_credential: None,
        set_provider_model_preference: None,
        probe_custom_endpoint: None,
        request_composer_completion: None,
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
    match kind {
        wire::CoreServiceCommandKind::StartAuth => {
            command.start_auth = Some(wire::StartAuthCommand {
                flow_id: value.flow_id,
                method: wire::AccountAuthMethod::from_wire(u32::from(value.auth_method))?,
                redirect_binding_id: value.redirect_binding_id,
                scopes: scopes_from_wire(value.scopes)?,
                issued_at_monotonic_ms: value.issued_at_monotonic_ms,
            });
        }
        wire::CoreServiceCommandKind::RequestEmailLink => {
            command.request_email_link = Some(wire::RequestEmailLinkCommand {
                flow_id: value.flow_id,
                email: value.email,
                redirect_binding_id: value.redirect_binding_id,
                scopes: scopes_from_wire(value.scopes)?,
                issued_at_monotonic_ms: value.issued_at_monotonic_ms,
            });
        }
        wire::CoreServiceCommandKind::SignOut => {
            command.sign_out = Some(wire::SignOutCommand {
                account_subject: value.has_account_subject.then_some(value.account_subject),
            });
        }
        wire::CoreServiceCommandKind::AuthCallback => {
            command.auth_callback = Some(wire::AuthCallbackCommand {
                flow_id: value.flow_id,
                redirect_binding_id: value.redirect_binding_id,
                returned_state: value.returned_state,
                status: wire::AuthCallbackStatus::from_wire(u32::from(value.auth_callback_status))?,
                authorization_code_handle: value
                    .has_authorization_code_handle
                    .then_some(value.authorization_code_handle),
            });
        }
        wire::CoreServiceCommandKind::AuthCredentialResult => {
            command.auth_credential_result = Some(wire::AuthCredentialResultCommand {
                flow_id: value.flow_id,
                method: wire::AccountAuthMethod::from_wire(u32::from(value.auth_method))?,
                credential_handle: value
                    .has_credential_handle
                    .then_some(value.credential_handle),
                status: wire::AuthCredentialStatus::from_wire(u32::from(
                    value.auth_credential_status,
                ))?,
            });
        }
        wire::CoreServiceCommandKind::StartTask
        | wire::CoreServiceCommandKind::CancelTask
        | wire::CoreServiceCommandKind::UserDecision
        | wire::CoreServiceCommandKind::PermissionResult
        | wire::CoreServiceCommandKind::CorrectWorkspaceFact
        | wire::CoreServiceCommandKind::ExcludeWorkspaceSource
        | wire::CoreServiceCommandKind::RequestWorkspaceExport
        | wire::CoreServiceCommandKind::SaveWorkspace
        | wire::CoreServiceCommandKind::RenameWorkspace
        | wire::CoreServiceCommandKind::DeleteWorkspace
        | wire::CoreServiceCommandKind::DiscardWorkspace
        | wire::CoreServiceCommandKind::SearchLibrary
        | wire::CoreServiceCommandKind::SaveLibraryFact
        | wire::CoreServiceCommandKind::RemoveLibraryEntry
        | wire::CoreServiceCommandKind::RequestLibraryExport
        | wire::CoreServiceCommandKind::SearchMemory
        | wire::CoreServiceCommandKind::UpsertMemory
        | wire::CoreServiceCommandKind::DeleteMemory
        | wire::CoreServiceCommandKind::AcceptTaskArtifact
        | wire::CoreServiceCommandKind::ExportTaskArtifact
        | wire::CoreServiceCommandKind::SetAssetDeliveryPolicy
        | wire::CoreServiceCommandKind::RequestAsset
        | wire::CoreServiceCommandKind::RemoveAsset
        | wire::CoreServiceCommandKind::SaveProviderCredential
        | wire::CoreServiceCommandKind::ForgetProviderCredential
        | wire::CoreServiceCommandKind::StartProviderAuth
        | wire::CoreServiceCommandKind::ProviderAuthCallback
        | wire::CoreServiceCommandKind::SaveCustomProvider
        | wire::CoreServiceCommandKind::RemoveCustomProvider
        | wire::CoreServiceCommandKind::CompleteHandover
        | wire::CoreServiceCommandKind::ExpireHandover
        | wire::CoreServiceCommandKind::SupplyUserInput
        | wire::CoreServiceCommandKind::FollowUp
        | wire::CoreServiceCommandKind::SetProviderCredentialState
        | wire::CoreServiceCommandKind::ProbeProviderCredential
        | wire::CoreServiceCommandKind::SupplyFieldValues
        | wire::CoreServiceCommandKind::SetProviderModelPreference
        | wire::CoreServiceCommandKind::ProbeCustomEndpoint
        | wire::CoreServiceCommandKind::RequestComposerCompletion
        | wire::CoreServiceCommandKind::CancelComposerCompletion
        | wire::CoreServiceCommandKind::PauseTask
        | wire::CoreServiceCommandKind::ResumeTask
        | wire::CoreServiceCommandKind::TakeOver
        | wire::CoreServiceCommandKind::SetAssistantConfiguration
        | wire::CoreServiceCommandKind::MutateSkill
        | wire::CoreServiceCommandKind::CancelProviderAuth
        | wire::CoreServiceCommandKind::ReplaceSavedDataSnapshot => return None,
    }
    command.has_valid_body().then_some(command)
}

fn scopes_from_wire(values: Vec<u8>) -> Option<Vec<wire::AccountScope>> {
    values
        .into_iter()
        .map(|value| wire::AccountScope::from_wire(u32::from(value)))
        .collect()
}

fn step_response(bridge: &mut ServiceBridge, step: AccountServiceStep) -> ffi::BridgeResponse {
    let AccountServiceStep {
        operation_id,
        admission,
        effects,
        state_changed,
    } = step;
    let status = admission as u8;
    let effects: Option<Vec<_>> = effects.into_iter().map(effect_to_bridge).collect();
    let Some(account_effects) = effects else {
        return response(&operation_id, 5, Vec::new());
    };
    let response = ffi::BridgeResponse {
        admission: ffi::BridgeAdmission {
            operation_id,
            status,
        },
        storage_effects: Vec::new(),
        workspace_effects: Vec::new(),
        account_effects,
        asset_effects: Vec::new(),
        probe_effects: Vec::new(),
        listing_effects: Vec::new(),
        endpoint_probe_effects: Vec::new(),
        task_answer_events: Vec::new(),
        states: Vec::new(),
    };
    if state_changed {
        response_after_change(bridge, response)
    } else {
        response
    }
}

fn error_status(error: AccountServiceError) -> u8 {
    match error {
        AccountServiceError::StaleGeneration => wire::AdmissionStatus::StaleGeneration as u8,
        AccountServiceError::DeadlineExceeded => wire::AdmissionStatus::DeadlineExceeded as u8,
        AccountServiceError::Backpressure => wire::AdmissionStatus::Backpressure as u8,
        AccountServiceError::InvalidCommand
        | AccountServiceError::PrivateProfile
        | AccountServiceError::DuplicateCompletion
        | AccountServiceError::InvalidCompletion
        | AccountServiceError::AccountMethodUnavailable
        | AccountServiceError::Domain(_)
        | AccountServiceError::Wire(_) => wire::AdmissionStatus::InvalidCommand as u8,
    }
}
