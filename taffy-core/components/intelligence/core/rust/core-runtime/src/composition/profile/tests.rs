// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Host tests for the canonical profile composition.
//!
//! One subject to a file. This module holds only what more than one subject
//! needs: the runtime under test and the commands every subject sends.

mod account;
mod build;
mod completion;
mod discovery_workspace;
mod entitlement;
mod listing;
mod local_latency;
mod page_invalidation;
mod probe;
mod provider;
mod recording;
mod research_result;
mod status_budget;

use std::rc::Rc;

use core_service_types as wire;

use super::{create_profile_service_runtime, ProfileRuntimeConfiguration};
use crate::account::crypto::ReferenceSha256;
use crate::contract::ServiceGeneration;

/// Every provider the compiled baseline ships, counted from the baseline.
///
/// Read rather than written down: what these tests are about is that the
/// roster is the catalog's whole list, and a literal here would have to be
/// edited by whoever adds a provider — which is the one person who would
/// edit it without noticing they were changing what the test claims.
fn compiled_provider_count() -> usize {
    model_router::embedded_baseline()
        .unwrap_or_else(|_| unreachable!())
        .0
        .document
        .providers
        .len()
}

fn all_account_methods() -> Vec<crate::account::AccountAuthMethod> {
    vec![
        crate::account::AccountAuthMethod::Google,
        crate::account::AccountAuthMethod::EmailLink,
        crate::account::AccountAuthMethod::Github,
        crate::account::AccountAuthMethod::Facebook,
    ]
}

fn configuration(
    generation: ServiceGeneration,
    entropy: [u8; 32],
    initial_utc_millis: u64,
    private_profile: bool,
    available_account_methods: Vec<crate::account::AccountAuthMethod>,
) -> ProfileRuntimeConfiguration {
    ProfileRuntimeConfiguration {
        generation,
        generation_capability_entropy: entropy,
        initial_utc_millis,
        private_profile,
        browser_profile_id: if private_profile {
            "private-profile-1".to_owned()
        } else {
            "profile-1".to_owned()
        },
        browser_session_id: "browser-session-1".to_owned(),
        available_account_methods,
        skills: Vec::new(),
        recall: Vec::new(),
        assistant_configuration: None,
    }
}

fn built_runtime() -> super::ProfileServiceRuntime {
    let entropy = core::array::from_fn(|index| u8::try_from(index).unwrap_or_default());
    create_profile_service_runtime(
        configuration(
            ServiceGeneration::INITIAL,
            entropy,
            1_000,
            false,
            all_account_methods(),
        ),
        Rc::new(ReferenceSha256),
    )
    .unwrap_or_else(|_| unreachable!())
}

fn save_provider_credential_command(provider_id: &str, handle: &str) -> wire::CoreServiceCommand {
    wire::CoreServiceCommand {
        operation: wire::OperationEnvelope {
            operation_id: "save-provider-credential-1".to_owned(),
            service_generation: ServiceGeneration::INITIAL.value(),
            task_revision: 0,
            deadline_monotonic_ms: 10_000,
            idempotency_key: "save-provider-credential-1-key".to_owned(),
        },
        kind: wire::CoreServiceCommandKind::SaveProviderCredential,
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
        save_provider_credential: Some(wire::SaveProviderCredentialCommand {
            provider_id: provider_id.to_owned(),
            auth_method: wire::ProviderAuthMethod::ApiKey,
            credential_handle: handle.to_owned(),
        }),
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

fn probe_command(provider_id: &str, handle: &str) -> wire::CoreServiceCommand {
    let mut command = save_provider_credential_command(provider_id, handle);
    command.operation.operation_id = "probe-provider-1".to_owned();
    command.operation.idempotency_key = "probe-provider-1-key".to_owned();
    command.kind = wire::CoreServiceCommandKind::ProbeProviderCredential;
    command.save_provider_credential = None;
    command.probe_provider_credential = Some(wire::ProbeProviderCredentialCommand {
        provider_id: provider_id.to_owned(),
        credential_handle: handle.to_owned(),
    });
    command
}

fn composer_command(request_id: &str, prefix: &str) -> wire::CoreServiceCommand {
    let mut command = save_provider_credential_command("anthropic", "unused");
    command.operation.operation_id = "composer-1".to_owned();
    command.operation.idempotency_key = "composer-1-key".to_owned();
    command.kind = wire::CoreServiceCommandKind::RequestComposerCompletion;
    command.save_provider_credential = None;
    command.request_composer_completion = Some(wire::RequestComposerCompletionCommand {
        request_id: request_id.to_owned(),
        prefix: prefix.to_owned(),
        suffix: None,
    });
    command
}
