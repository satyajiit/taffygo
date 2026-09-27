// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A person's own endpoint: what it routes, and which authority it claims.

use core_service_types as wire;

use super::{
    built_runtime, compiled_provider_count, composer_command, probe_command,
    save_provider_credential_command,
};

// --- which authority a composed effect claims (decision 0096 section 2) --
//
// Both composers in this directory resolve their model out of a merge the
// person's own layer is part of, and both used to write `CATALOG_ORIGIN` on
// whatever came back. For a server somebody runs that is a request asking
// the browser to compare an address against a catalog copy that has never
// held it — so the one endpoint decision 0096 exists to make reachable was
// refused by the rule it claimed rather than by anything true about it.
// Each case below has a published-address twin, because two rules chosen
// between by one fact are only two rules while both are exercised.

/// The identity a person's own server is filed under here.
const OWN_SERVER_ID: &str = "my-server";

/// The address decision 0096 was written for, with the scheme, port and
/// base path a server on somebody's desk actually has.
const OWN_SERVER_ADDRESS: &str = "http://localhost:11434/v1";

/// The one model that server reports.
const OWN_SERVER_MODEL: &str = "llama-4";

/// One save of a person's own provider, with a key filed against it.
///
/// A key rather than none, because both composers look for a *usable
/// credential* before they compose anything: the suggestion because
/// decision 0097 section 2 spends nothing but the person's own, and the
/// probe because proving a key is what it is for. Whether an endpoint may
/// be reached with no key at all is route selection's question and is
/// settled in `model-router`; what these three are about is the address,
/// so the credential here is present and unremarkable.
fn save_own_server_command(handle: &str) -> wire::CoreServiceCommand {
    let mut command = save_provider_credential_command(OWN_SERVER_ID, handle);
    command.operation.operation_id = "save-custom-1".to_owned();
    command.operation.idempotency_key = "save-custom-1-key".to_owned();
    command.kind = wire::CoreServiceCommandKind::SaveCustomProvider;
    command.save_provider_credential = None;
    command.save_custom_provider = Some(wire::SaveCustomProviderCommand {
        provider_id: OWN_SERVER_ID.to_owned(),
        display_name: "My server".to_owned(),
        endpoint: OWN_SERVER_ADDRESS.to_owned(),
        wire_api: wire::ProviderWireApi::OpenAiCompletions,
        credential_handle: Some(handle.to_owned()),
        // Zero for both limits is what an endpoint that stated neither
        // says, and a model that calls tools without a thinking phase
        // serves the fast-browsing rung a suggestion is asked for.
        models: vec![wire::CustomModelSpec {
            model_id: OWN_SERVER_MODEL.to_owned(),
            display_name: "Llama 4".to_owned(),
            context_window: 0,
            max_output_tokens: 0,
            reasoning: false,
            tool_calling: true,
        }],
        detected_server: Some(wire::DetectedServer {
            server_kind: wire::ServerKind::Ollama,
        }),
    });
    command
}

/// The model request one composed effect carries.
fn model_request(effect: &wire::EffectEnvelope) -> &wire::ModelRequestEffect {
    effect
        .model_request
        .as_ref()
        .unwrap_or_else(|| unreachable!("a composed model effect carries a model request"))
}

#[test]
fn a_suggestion_spent_on_their_own_server_claims_the_register_and_not_the_catalog() {
    let mut runtime = built_runtime();
    runtime
        .submit_provider_command(&save_own_server_command("handle-own"))
        .unwrap_or_else(|_| unreachable!("a person's own provider is savable"));

    // Their key is the only usable one on the profile and their model is
    // the cheapest thing behind it, so the suggestion goes to their machine.
    let plan = runtime
        .submit_composer_completion_command(&composer_command("request-1", "Compare the "))
        .unwrap_or_else(|_| unreachable!("one usable credential is enough for a suggestion"));
    let request = model_request(&plan.effect);

    assert_eq!(request.provider_id, OWN_SERVER_ID);
    assert_eq!(request.model_id, OWN_SERVER_MODEL);
    assert_eq!(
        request.endpoint, OWN_SERVER_ADDRESS,
        "whole: a dropped port is a different server and a dropped path is its root"
    );
    assert_eq!(
        request.endpoint_kind,
        wire::ModelEndpointKind::UserBaseUrl,
        "the register is the only rule the browser holds this address under"
    );
}

#[test]
fn a_probe_of_their_own_server_claims_the_register_and_not_the_catalog() {
    // `ProbeCustomEndpoint` proves an address nothing has saved yet and
    // carries no provider identity, so it is not this command and cannot
    // stand in for it. This one names a provider, and the lookup that finds
    // it is against the whole merge — the person's own layer included.
    let mut runtime = built_runtime();
    runtime
        .submit_provider_command(&save_own_server_command("handle-own"))
        .unwrap_or_else(|_| unreachable!("a person's own provider is savable"));

    let effect = runtime
        .submit_probe_command(&probe_command(OWN_SERVER_ID, "handle-own"), 1_000)
        .unwrap_or_else(|_| unreachable!("a saved provider is probeable, whoever named it"))
        .unwrap_or_else(|| unreachable!("a listed model composes a flight"));
    let request = model_request(&effect);

    assert_eq!(request.provider_id, OWN_SERVER_ID);
    assert_eq!(request.model_id, OWN_SERVER_MODEL);
    assert_eq!(request.endpoint, OWN_SERVER_ADDRESS);
    assert_eq!(
        request.endpoint_kind,
        wire::ModelEndpointKind::UserBaseUrl,
        "a probe of their own server asks for the rule that can answer it"
    );
}

#[test]
fn a_probe_of_a_published_provider_still_claims_the_catalog_rule() {
    // The twin. One command, one composer, and the rule it asks for follows
    // the address rather than the command — so making a person's own server
    // reachable leaves a vendor's API judged exactly as it was.
    let mut runtime = built_runtime();
    let effect = runtime
        .submit_probe_command(&probe_command("anthropic", "transient-handle-1"), 1_000)
        .unwrap_or_else(|_| unreachable!("the compiled baseline ships this provider"))
        .unwrap_or_else(|| unreachable!("a listed model composes a flight"));
    let request = model_request(&effect);

    assert!(request.endpoint.starts_with("https://"));
    assert_eq!(
        request.endpoint_kind,
        wire::ModelEndpointKind::CatalogOrigin
    );
}

/// One save of a person's own provider, carrying `models`.
fn save_custom_provider_command(
    provider_id: &str,
    models: Vec<wire::CustomModelSpec>,
) -> wire::CoreServiceCommand {
    let mut command = save_provider_credential_command(provider_id, "handle-1");
    command.kind = wire::CoreServiceCommandKind::SaveCustomProvider;
    command.save_provider_credential = None;
    command.save_custom_provider = Some(wire::SaveCustomProviderCommand {
        provider_id: provider_id.to_owned(),
        display_name: "My gateway".to_owned(),
        endpoint: "https://gateway.example/v1".to_owned(),
        wire_api: wire::ProviderWireApi::OpenAiCompletions,
        credential_handle: Some("handle-1".to_owned()),
        models,
        detected_server: Some(wire::DetectedServer {
            server_kind: wire::ServerKind::Ollama,
        }),
    });
    command
}

fn remove_custom_provider_command(provider_id: &str) -> wire::CoreServiceCommand {
    let mut command = save_provider_credential_command(provider_id, "handle-1");
    command.kind = wire::CoreServiceCommandKind::RemoveCustomProvider;
    command.save_provider_credential = None;
    command.remove_custom_provider = Some(wire::RemoveCustomProviderCommand {
        provider_id: provider_id.to_owned(),
    });
    command
}

fn gateway_model() -> wire::CustomModelSpec {
    wire::CustomModelSpec {
        model_id: "gateway-large".to_owned(),
        display_name: "Gateway Large".to_owned(),
        context_window: 131_072,
        max_output_tokens: 8_192,
        reasoning: false,
        tool_calling: true,
    }
}

#[test]
fn a_saved_endpoint_is_routable_in_the_same_call_that_saved_it() {
    // The defect this closes: a person saves an endpoint with models behind
    // it, everything is stored correctly, and nothing can route to any of
    // them until the browser is restarted, because the merge the router
    // reads was never rebuilt.
    let mut runtime = built_runtime();
    assert!(runtime
        .submit_provider_command(&save_custom_provider_command(
            "my-gateway",
            vec![gateway_model()]
        ))
        .is_ok());
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(status.provider_roster.len(), compiled_provider_count() + 1);
    let model = status
        .provider_models
        .iter()
        .find(|entry| entry.provider_id == "my-gateway")
        .unwrap_or_else(|| unreachable!("the person's own model reaches the picker"));
    assert_eq!(model.model_id, "gateway-large");
    assert_eq!(model.context_window, 131_072);
    assert!(model.tool_calling);
}

#[test]
fn removing_a_saved_endpoint_takes_its_models_with_it() {
    let mut runtime = built_runtime();
    assert!(runtime
        .submit_provider_command(&save_custom_provider_command(
            "my-gateway",
            vec![gateway_model()]
        ))
        .is_ok());
    assert!(runtime
        .submit_provider_command(&remove_custom_provider_command("my-gateway"))
        .is_ok());
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(status.provider_roster.len(), compiled_provider_count());
    assert!(
        !status
            .provider_models
            .iter()
            .any(|entry| entry.provider_id == "my-gateway"),
        "a model behind an endpoint nobody has any more is one nothing can reach"
    );
}

#[test]
fn a_person_may_correct_the_endpoint_they_just_saved() {
    // The rebuild puts the person's own provider into the merge, and the
    // merge is where the plane's reserved identities come from. If the
    // custom row came back as a catalog row, this second save would be
    // refused for taking an identity the catalog defines — its own.
    let mut runtime = built_runtime();
    assert!(runtime
        .submit_provider_command(&save_custom_provider_command(
            "my-gateway",
            vec![gateway_model()]
        ))
        .is_ok());
    assert!(
        runtime
            .submit_provider_command(&save_custom_provider_command(
                "my-gateway",
                vec![gateway_model()]
            ))
            .is_ok(),
        "a person at the keyboard correcting a typo must not be refused"
    );
}
