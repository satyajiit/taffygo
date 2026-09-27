// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Which address a composer suggestion may be spent on.
//!
//! The sibling tests in `composition::profile` already pin the shape of the
//! effect and the flight it claims. What is pinned here is the one question
//! that decides whether any effect exists at all: not "is a credential held"
//! but "can a request to this address be routed", which is the same question
//! `model_router`'s `own_endpoint_auth` and `catalog_auth` answer one layer
//! further in — and which the two files answer separately on purpose.

use std::rc::Rc;

use core_service_types as wire;

use crate::account::crypto::ReferenceSha256;
use crate::composition::profile::{
    create_profile_service_runtime, ProfileRuntimeConfiguration, ProfileServiceRuntime,
};
use crate::contract::ServiceGeneration;

use super::CompletionCommandError;

/// The address the person typed, port and base path included.
const OWN_ENDPOINT: &str = "http://workstation.local:11434/v1";

/// The identity their provider is filed under.
const OWN_PROVIDER: &str = "my-gateway";

fn runtime() -> ProfileServiceRuntime {
    let entropy = core::array::from_fn(|index| u8::try_from(index).unwrap_or_default());
    create_profile_service_runtime(
        ProfileRuntimeConfiguration {
            generation: ServiceGeneration::INITIAL,
            generation_capability_entropy: entropy,
            initial_utc_millis: 1_000,
            private_profile: false,
            browser_profile_id: "profile-1".to_owned(),
            browser_session_id: "browser-session-1".to_owned(),
            available_account_methods: Vec::new(),
            skills: Vec::new(),
            recall: Vec::new(),
            assistant_configuration: None,
        },
        Rc::new(ReferenceSha256),
    )
    .unwrap_or_else(|_| unreachable!())
}

/// An otherwise-empty command, for a kind to fill in.
fn command(kind: wire::CoreServiceCommandKind, name: &str) -> wire::CoreServiceCommand {
    wire::CoreServiceCommand {
        operation: wire::OperationEnvelope {
            operation_id: name.to_owned(),
            service_generation: ServiceGeneration::INITIAL.value(),
            task_revision: 0,
            deadline_monotonic_ms: 10_000,
            idempotency_key: format!("{name}-key"),
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
        set_provider_credential_state: None,
        probe_provider_credential: None,
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
    }
}

/// One model a person's endpoint reported.
///
/// Tool calling is on because a model that cannot call tools serves no catalog
/// role at all and never reaches the merge; the two zeros are what the contract
/// carries when the server stated no limits.
fn model(model_id: &str) -> wire::CustomModelSpec {
    wire::CustomModelSpec {
        model_id: model_id.to_owned(),
        display_name: "A local model".to_owned(),
        context_window: 0,
        max_output_tokens: 0,
        reasoning: false,
        tool_calling: true,
    }
}

/// The save of a person's own endpoint, with or without a key behind it.
fn save_own_endpoint(
    credential_handle: Option<&str>,
    models: Vec<wire::CustomModelSpec>,
) -> wire::CoreServiceCommand {
    let mut command = command(
        wire::CoreServiceCommandKind::SaveCustomProvider,
        "save-own-1",
    );
    command.save_custom_provider = Some(wire::SaveCustomProviderCommand {
        provider_id: OWN_PROVIDER.to_owned(),
        display_name: "My workstation".to_owned(),
        endpoint: OWN_ENDPOINT.to_owned(),
        wire_api: wire::ProviderWireApi::OpenAiCompletions,
        credential_handle: credential_handle.map(str::to_owned),
        models,
        detected_server: Some(wire::DetectedServer {
            server_kind: wire::ServerKind::Ollama,
        }),
    });
    command
}

/// The browser's report that a stored credential is in some state.
fn report_state(state: wire::ProviderCredentialState) -> wire::CoreServiceCommand {
    let mut command = command(
        wire::CoreServiceCommandKind::SetProviderCredentialState,
        "credential-state-1",
    );
    command.set_provider_credential_state = Some(wire::SetProviderCredentialStateCommand {
        provider_id: OWN_PROVIDER.to_owned(),
        state,
        available_model_ids: Vec::new(),
    });
    command
}

/// The person's standing pin for their own provider.
fn pin(model_id: &str) -> wire::CoreServiceCommand {
    let mut command = command(
        wire::CoreServiceCommandKind::SetProviderModelPreference,
        "pin-1",
    );
    command.set_provider_model_preference = Some(wire::SetProviderModelPreferenceCommand {
        provider_id: OWN_PROVIDER.to_owned(),
        model_id: Some(model_id.to_owned()),
        thinking: None,
    });
    command
}

/// One keystroke's worth of composer text.
fn composer(request_id: &str) -> wire::CoreServiceCommand {
    let mut command = command(
        wire::CoreServiceCommandKind::RequestComposerCompletion,
        "composer-1",
    );
    command.request_composer_completion = Some(wire::RequestComposerCompletionCommand {
        request_id: request_id.to_owned(),
        prefix: "Compare the ".to_owned(),
        suffix: None,
    });
    command
}

/// The model request one accepted suggestion composed.
fn model_request(
    runtime: &mut ProfileServiceRuntime,
    request_id: &str,
) -> wire::ModelRequestEffect {
    runtime
        .submit_composer_completion_command(&composer(request_id))
        .unwrap_or_else(|_| unreachable!("the suggestion is composed"))
        .effect
        .model_request
        .unwrap_or_else(|| unreachable!("a model request carries its own body"))
}

#[test]
fn a_server_that_needs_no_key_is_the_address_a_suggestion_is_spent_on() {
    // The ordinary self-hosted case: a plain Ollama, saved with no credential
    // handle because it asks for none. Nothing is filed against it, so a
    // selection that asked "which credentials are usable" found none and drew
    // no ghost text for the life of the profile, with nothing said.
    let mut runtime = runtime();
    runtime
        .submit_provider_command(&save_own_endpoint(None, vec![model("alpha-model")]))
        .unwrap_or_else(|_| unreachable!("a person's own endpoint saves"));

    let request = model_request(&mut runtime, "request-1");
    assert_eq!(request.provider_id, OWN_PROVIDER);
    assert_eq!(request.model_id, "alpha-model");
    assert_eq!(request.endpoint, OWN_ENDPOINT);
    assert_eq!(
        request.credential_handle, None,
        "an endpoint that needs no key is reached with none, not with an empty one"
    );
    // The effect is judged by the rule for an address a person registered, so
    // the browser recognizes it against its register rather than comparing it
    // to a published one it has no entry for.
    assert_eq!(request.endpoint_kind, wire::ModelEndpointKind::UserBaseUrl);
}

#[test]
fn a_key_that_stopped_working_refuses_rather_than_going_out_unauthenticated() {
    // The security-relevant half. The same endpoint, saved *with* a key, whose
    // credential the browser then reports is no longer usable. There is still a
    // record, so this is not the keyless case and must never be read as one:
    // admitting it would turn "your key stopped working" into "your words are
    // going to your server with nothing attached", a downgrade with no refusal
    // anywhere to notice it.
    let mut runtime = runtime();
    runtime
        .submit_provider_command(&save_own_endpoint(
            Some("handle-own-1"),
            vec![model("alpha-model")],
        ))
        .unwrap_or_else(|_| unreachable!("a person's own endpoint saves"));

    // While it works, it is spent — the same address, one state earlier.
    assert_eq!(
        model_request(&mut runtime, "request-1").credential_handle,
        Some("handle-own-1".to_owned())
    );

    for state in [
        wire::ProviderCredentialState::NeedsSignIn,
        wire::ProviderCredentialState::RefreshFailed,
    ] {
        runtime
            .submit_provider_command(&report_state(state))
            .unwrap_or_else(|_| unreachable!("the browser reports on a record that exists"));
        assert_eq!(
            runtime
                .submit_composer_completion_command(&composer("request-2"))
                .err(),
            Some(CompletionCommandError::NoUsableCredential),
            "a credential that exists and has stopped working is refused, never dropped"
        );
    }
}

#[test]
fn a_published_address_with_nothing_filed_against_it_is_still_refused() {
    // The other rule, unchanged. Every catalog provider declares a method and
    // every method is a credential, so a suggestion to one of those addresses
    // with nothing attached is a round trip spent learning what the device
    // already knew. A profile that has configured nothing at all reaches this
    // on every keystroke, and the compiled baseline is full of providers.
    let mut runtime = runtime();
    assert_eq!(
        runtime
            .submit_composer_completion_command(&composer("request-1"))
            .err(),
        Some(CompletionCommandError::NoUsableCredential)
    );

    // And it stays refused once a person's own endpoint is the thing that is
    // reachable: the suggestion goes there, never to a published address the
    // profile holds no key for.
    runtime
        .submit_provider_command(&save_own_endpoint(None, vec![model("alpha-model")]))
        .unwrap_or_else(|_| unreachable!("a person's own endpoint saves"));
    assert_eq!(
        model_request(&mut runtime, "request-2").provider_id,
        OWN_PROVIDER
    );
}

#[test]
fn a_standing_pin_wins_over_the_cheapest_model_on_the_same_endpoint() {
    // Two models on one keyless endpoint, both free, so the cheapest sweep
    // settles on whichever the catalog names first. The pin has to beat it, or
    // a suggestion arrives in a different voice than the person's answers do.
    let mut runtime = runtime();
    runtime
        .submit_provider_command(&save_own_endpoint(
            None,
            vec![model("alpha-model"), model("zeta-model")],
        ))
        .unwrap_or_else(|_| unreachable!("a person's own endpoint saves"));
    assert_eq!(
        model_request(&mut runtime, "request-1").model_id,
        "alpha-model",
        "with no pin, the first of two equally free models"
    );

    runtime
        .submit_provider_command(&pin("zeta-model"))
        .unwrap_or_else(|_| unreachable!("the model is one the endpoint reported"));
    let request = model_request(&mut runtime, "request-2");
    assert_eq!(request.model_id, "zeta-model");
    assert_eq!(
        request.credential_handle, None,
        "the pinned path reaches a keyless endpoint too, not only the cheapest one"
    );
}
