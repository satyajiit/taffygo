// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Account method availability, and what a private profile refuses.

use std::rc::Rc;

use core_service_types as wire;

use super::configuration;
use crate::account::crypto::ReferenceSha256;
use crate::adapters::account::AccountServiceError;
use crate::codec::account_wire::decode_account_session;
use crate::composition::profile::create_profile_service_runtime;
use crate::contract::ServiceGeneration;
use crate::runtime::AccountRestoreError;

fn start_auth_command(
    operation_id: &str,
    method: wire::AccountAuthMethod,
) -> wire::CoreServiceCommand {
    wire::CoreServiceCommand {
        operation: wire::OperationEnvelope {
            operation_id: operation_id.to_owned(),
            service_generation: ServiceGeneration::INITIAL.value(),
            task_revision: 0,
            deadline_monotonic_ms: 10_000,
            idempotency_key: format!("{operation_id}-key"),
        },
        kind: wire::CoreServiceCommandKind::StartAuth,
        start_task: None,
        cancel_task: None,
        user_decision: None,
        auth_callback: None,
        permission_result: None,
        start_auth: Some(wire::StartAuthCommand {
            flow_id: format!("{operation_id}-flow"),
            method,
            redirect_binding_id: "primary-auth-callback".to_owned(),
            scopes: vec![wire::AccountScope::OpenId],
            issued_at_monotonic_ms: 1_000,
        }),
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
        set_provider_credential_state: None,
        probe_provider_credential: None,
    }
}

#[test]
fn method_availability_is_projected_and_enforced_before_protocol_admission() {
    let entropy = core::array::from_fn(|index| u8::try_from(index).unwrap_or_default());
    let mut runtime = create_profile_service_runtime(
        configuration(
            ServiceGeneration::INITIAL,
            entropy,
            1_000,
            false,
            vec![crate::account::AccountAuthMethod::Github],
        ),
        Rc::new(ReferenceSha256),
    )
    .unwrap_or_else(|_| unreachable!());
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    let methods = status.auth_state.unwrap_or_else(|| unreachable!()).methods;
    assert_eq!(methods.len(), 4);
    assert!(methods.iter().any(|method| {
        method.provider == core_api_types::AuthProvider::Github
            && method.availability == core_api_types::AuthMethodAvailability::Available
    }));
    assert!(methods.iter().any(|method| {
        method.provider == core_api_types::AuthProvider::Google
            && method.availability == core_api_types::AuthMethodAvailability::NotConfigured
    }));
    assert_eq!(
        runtime.submit_account_command(
            &start_auth_command("unavailable-google", wire::AccountAuthMethod::Google),
            1_000,
        ),
        Err(AccountServiceError::AccountMethodUnavailable)
    );
    assert!(runtime
        .submit_account_command(
            &start_auth_command("configured-github", wire::AccountAuthMethod::Github),
            1_000,
        )
        .is_ok());
}

#[test]
#[allow(clippy::too_many_lines)]
fn private_profile_refuses_account_restore_commands_and_completions() {
    let entropy = core::array::from_fn(|index| u8::try_from(index).unwrap_or_default());
    let mut runtime = create_profile_service_runtime(
        configuration(ServiceGeneration::INITIAL, entropy, 1_000, true, Vec::new()),
        Rc::new(ReferenceSha256),
    )
    .unwrap_or_else(|_| unreachable!());
    assert!(runtime.private_profile());
    let session = decode_account_session(&wire::AccountSessionHandle {
        session_handle: "session-handle-1".to_owned(),
        account_subject: "account-subject-1".to_owned(),
        expires_at_monotonic_ms: 10_000,
        rotation: 1,
        auth_method: wire::AccountAuthMethod::Github,
        email: None,
        display_name: None,
    })
    .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        runtime.restore_account_session(Some(session)),
        Err(AccountRestoreError::PrivateProfile)
    );

    let operation = wire::OperationEnvelope {
        operation_id: "private-account-operation".to_owned(),
        service_generation: ServiceGeneration::INITIAL.value(),
        task_revision: 0,
        deadline_monotonic_ms: 10_000,
        idempotency_key: "private-account-key".to_owned(),
    };
    let command = wire::CoreServiceCommand {
        operation: operation.clone(),
        kind: wire::CoreServiceCommandKind::StartAuth,
        start_task: None,
        cancel_task: None,
        user_decision: None,
        auth_callback: None,
        permission_result: None,
        start_auth: Some(wire::StartAuthCommand {
            flow_id: "private-flow-1".to_owned(),
            method: wire::AccountAuthMethod::Github,
            redirect_binding_id: "primary-auth-callback".to_owned(),
            scopes: vec![wire::AccountScope::OpenId],
            issued_at_monotonic_ms: 1_000,
        }),
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
        set_provider_credential_state: None,
        probe_provider_credential: None,
    };
    assert_eq!(
        runtime.submit_account_command(&command, 1_000),
        Err(AccountServiceError::PrivateProfile)
    );
    let completion = wire::EffectResult {
        operation,
        effect_id: "private-account-effect".to_owned(),
        status: wire::EffectStatus::Unavailable,
        kind: wire::EffectKind::NetworkRequest,
        storage: None,
        observation: None,
        model: None,
        network: None,
        browser_action: None,
        tool: None,
        secure_store: None,
        auth_surface: None,
        permission: None,
        asset_delivery: None,
        catalog: None,
        provider_listing: None,
        composer_completion: None,
        custom_endpoint_probe: None,
    };
    assert_eq!(
        runtime.deliver_account_effect_result(&completion, 1_000),
        Err(AccountServiceError::PrivateProfile)
    );
}
