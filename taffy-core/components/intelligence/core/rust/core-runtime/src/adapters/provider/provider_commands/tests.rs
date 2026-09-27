// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What each provider command does, and what each one refuses.
//!
//! This file holds the fixtures and nothing else; every test lives in one
//! submodule named for the subject it exercises, so a new test has one place
//! to go. A builder here is shared by more than one of them, which is why it
//! stays at this level rather than moving down with its first caller.

use core_service_types as wire;
use model_router::catalog::CatalogLayer;

use crate::provider::{CatalogProvider, ProviderId, ProviderPresentation};

use super::*;

mod command_refusals;
mod credentials;
mod custom_providers;
mod state_reports;
mod subscription_sign_in;

/// The catalog identity every test here saves against.
const CATALOG_ID: &str = "anthropic";
/// An identity the catalog does not ship, so a person may take it.
const OWN_ID: &str = "my-gateway";

fn plane() -> ProviderProtocol {
    ProviderProtocol::new(
        [
            CatalogProvider {
                provider_id: ProviderId::new(CATALOG_ID).expect("valid"),
                display_name: "Anthropic".to_owned(),
                auth_methods: vec![ProviderAuthMethod::ApiKey],
                enabled: true,
                layer: CatalogLayer::EmbeddedBaseline,
                configurable: true,
                subscription: false,
                endpoint_changed: false,
                refused_endpoint_host: None,
                presentation: ProviderPresentation::default(),
            },
            CatalogProvider {
                provider_id: ProviderId::new("plan-only").expect("valid"),
                display_name: "Plan only".to_owned(),
                auth_methods: vec![ProviderAuthMethod::Oauth],
                enabled: true,
                layer: CatalogLayer::EmbeddedBaseline,
                configurable: true,
                subscription: false,
                endpoint_changed: false,
                refused_endpoint_host: None,
                presentation: ProviderPresentation::default(),
            },
            CatalogProvider {
                provider_id: ProviderId::new("switched-off").expect("valid"),
                display_name: "Switched off".to_owned(),
                auth_methods: vec![ProviderAuthMethod::ApiKey],
                enabled: false,
                layer: CatalogLayer::EmbeddedBaseline,
                configurable: true,
                subscription: false,
                endpoint_changed: false,
                refused_endpoint_host: None,
                presentation: ProviderPresentation::default(),
            },
        ],
        [],
    )
}

fn command(kind: wire::CoreServiceCommandKind) -> wire::CoreServiceCommand {
    wire::CoreServiceCommand {
        operation: wire::OperationEnvelope {
            operation_id: "operation-1".to_owned(),
            service_generation: 1,
            task_revision: 0,
            deadline_monotonic_ms: 10_000,
            idempotency_key: "idempotency-1".to_owned(),
        },
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

fn save(provider_id: &str, handle: &str) -> wire::CoreServiceCommand {
    let mut value = command(wire::CoreServiceCommandKind::SaveProviderCredential);
    value.save_provider_credential = Some(wire::SaveProviderCredentialCommand {
        provider_id: provider_id.to_owned(),
        auth_method: wire::ProviderAuthMethod::ApiKey,
        credential_handle: handle.to_owned(),
    });
    value
}

fn forget(provider_id: &str) -> wire::CoreServiceCommand {
    let mut value = command(wire::CoreServiceCommandKind::ForgetProviderCredential);
    value.forget_provider_credential = Some(wire::ForgetProviderCredentialCommand {
        provider_id: provider_id.to_owned(),
    });
    value
}

fn save_custom(
    provider_id: &str,
    endpoint: &str,
    handle: Option<&str>,
) -> wire::CoreServiceCommand {
    let mut value = command(wire::CoreServiceCommandKind::SaveCustomProvider);
    value.save_custom_provider = Some(wire::SaveCustomProviderCommand {
        provider_id: provider_id.to_owned(),
        display_name: "My gateway".to_owned(),
        endpoint: endpoint.to_owned(),
        wire_api: wire::ProviderWireApi::OpenAiCompletions,
        credential_handle: handle.map(str::to_owned),
        models: vec![wire::CustomModelSpec {
            model_id: "gateway-large".to_owned(),
            display_name: "Gateway Large".to_owned(),
            context_window: 131_072,
            max_output_tokens: 8_192,
            reasoning: false,
            tool_calling: true,
        }],
        detected_server: None,
    });
    value
}

fn remove_custom(provider_id: &str) -> wire::CoreServiceCommand {
    let mut value = command(wire::CoreServiceCommandKind::RemoveCustomProvider);
    value.remove_custom_provider = Some(wire::RemoveCustomProviderCommand {
        provider_id: provider_id.to_owned(),
    });
    value
}

fn set_state(provider_id: &str, state: wire::ProviderCredentialState) -> wire::CoreServiceCommand {
    let mut value = command(wire::CoreServiceCommandKind::SetProviderCredentialState);
    value.set_provider_credential_state = Some(wire::SetProviderCredentialStateCommand {
        available_model_ids: Vec::new(),
        provider_id: provider_id.to_owned(),
        state,
    });
    value
}

fn start_auth(provider_id: &str) -> wire::CoreServiceCommand {
    let mut value = command(wire::CoreServiceCommandKind::StartProviderAuth);
    value.start_provider_auth = Some(wire::StartProviderAuthCommand {
        flow_id: "flow-1".to_owned(),
        provider_id: provider_id.to_owned(),
        redirect_binding_id: "binding-1".to_owned(),
        issued_at_monotonic_ms: 1,
    });
    value
}

fn cancel_auth(flow_id: &str) -> wire::CoreServiceCommand {
    let mut value = command(wire::CoreServiceCommandKind::CancelProviderAuth);
    value.cancel_provider_auth = Some(wire::CancelProviderAuthCommand {
        flow_id: flow_id.to_owned(),
    });
    value
}

fn auth_callback(
    flow_id: &str,
    binding: &str,
    status: wire::AuthCallbackStatus,
    code_handle: Option<&str>,
) -> wire::CoreServiceCommand {
    let mut value = command(wire::CoreServiceCommandKind::ProviderAuthCallback);
    value.provider_auth_callback = Some(wire::ProviderAuthCallbackCommand {
        flow_id: flow_id.to_owned(),
        redirect_binding_id: binding.to_owned(),
        returned_state: "c3RhdGU".to_owned(),
        status,
        authorization_code_handle: code_handle.map(str::to_owned),
    });
    value
}
