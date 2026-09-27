// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The custom-endpoint probe, end to end inside the core.

use std::rc::Rc;

use core_service_types as wire;

use crate::account::crypto::ReferenceSha256;
use crate::composition::profile::probe::ProbeCommandError;
use crate::composition::profile::{
    create_profile_service_runtime, ProfileRuntimeConfiguration, ProfileServiceRuntime,
};
use crate::contract::ServiceGeneration;

use super::ENDPOINT_PROBE_MAX_RESPONSE_BYTES;

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

fn command(provider_id: &str, endpoint: &str) -> wire::CoreServiceCommand {
    wire::CoreServiceCommand {
        operation: wire::OperationEnvelope {
            operation_id: "endpoint-probe-1".to_owned(),
            service_generation: ServiceGeneration::INITIAL.value(),
            task_revision: 0,
            deadline_monotonic_ms: 10_000,
            idempotency_key: "endpoint-probe-1-key".to_owned(),
        },
        kind: wire::CoreServiceCommandKind::ProbeCustomEndpoint,
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
        probe_custom_endpoint: Some(wire::ProbeCustomEndpointCommand {
            endpoint: endpoint.to_owned(),
            wire_api: wire::ProviderWireApi::OpenAiCompletions,
            credential_handle: None,
            provider_id: provider_id.to_owned(),
        }),
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

fn reached_result(provider_id: &str, model_count: u32) -> wire::CustomEndpointProbeResult {
    wire::CustomEndpointProbeResult {
        provider_id: provider_id.to_owned(),
        reached: true,
        detected_server: Some(wire::DetectedServer {
            server_kind: wire::ServerKind::Ollama,
        }),
        model_count,
        models: vec![wire::CustomModelSpec {
            model_id: "qwen3-30b".to_owned(),
            display_name: "Qwen3 30B".to_owned(),
            context_window: 32_768,
            max_output_tokens: 8_192,
            reasoning: false,
            tool_calling: true,
        }],
        proved_base: Some("http://workstation.local:11434/v1".to_owned()),
    }
}

#[test]
fn a_probe_names_the_address_the_person_typed_and_the_draft_it_reports_under() {
    let mut runtime = runtime();
    let effect = runtime
        .submit_custom_endpoint_probe_command(&command(
            "my-gateway",
            "http://workstation.local:11434",
        ))
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(effect.kind, wire::EffectKind::ProbeCustomEndpoint);
    assert_eq!(effect.retry_class, wire::RetryClass::Never);
    assert_eq!(
        effect.effect_id,
        "custom-endpoint-probe-my-gateway-browser-session-1-1-1"
    );
    let body = effect
        .custom_endpoint_probe
        .as_ref()
        .unwrap_or_else(|| unreachable!("the effect carries its own body"));
    assert_eq!(body.provider_id, "my-gateway");
    // The port survives, which is the whole of decision 0096: a probe aimed at
    // an origin would prove an address the person did not type.
    assert_eq!(body.endpoint, "http://workstation.local:11434");
    assert_eq!(body.max_response_bytes, ENDPOINT_PROBE_MAX_RESPONSE_BYTES);
}

#[test]
fn a_verdict_lands_on_the_draft_identity_and_carries_what_the_server_said() {
    let mut runtime = runtime();
    let effect = runtime
        .submit_custom_endpoint_probe_command(&command(
            "my-gateway",
            "http://workstation.local:11434",
        ))
        .unwrap_or_else(|_| unreachable!());
    assert!(runtime.deliver_custom_endpoint_probe_result(
        &effect.effect_id,
        // Fifty named and one carried: the count is the server's answer and the
        // list is what survived, and a surface reading the list's length would
        // tell a person their endpoint offers one model.
        &reached_result("my-gateway", 50),
        480_000,
    ));

    let probes = runtime.probes.display();
    let row = probes.first().unwrap_or_else(|| unreachable!());
    assert_eq!(row.provider_id, "my-gateway");
    assert_eq!(row.verdict, crate::probe::ProbeVerdict::EndpointReached);
    let endpoint = row
        .endpoint
        .as_ref()
        .unwrap_or_else(|| unreachable!("a reached endpoint reports its shape"));
    assert_eq!(endpoint.model_count, 50);
    assert_eq!(endpoint.models.len(), 1);
    assert_eq!(
        endpoint.proved_base.as_deref(),
        Some("http://workstation.local:11434/v1")
    );
}

#[test]
fn an_endpoint_nothing_answered_at_files_a_verdict_and_no_shape() {
    let mut runtime = runtime();
    let effect = runtime
        .submit_custom_endpoint_probe_command(&command("my-gateway", "http://nothing.local:9"))
        .unwrap_or_else(|_| unreachable!());
    let mut result = reached_result("my-gateway", 0);
    result.reached = false;
    assert!(runtime.deliver_custom_endpoint_probe_result(&effect.effect_id, &result, 480_000));

    let probes = runtime.probes.display();
    let row = probes.first().unwrap_or_else(|| unreachable!());
    assert_eq!(row.verdict, crate::probe::ProbeVerdict::Network);
    assert!(
        row.endpoint.is_none(),
        "there is nothing to describe about an endpoint that answered nothing"
    );
}

#[test]
fn the_reserved_wire_families_and_a_second_flight_are_refused() {
    let mut runtime = runtime();
    let mut managed = command("my-gateway", "http://workstation.local:11434");
    if let Some(body) = managed.probe_custom_endpoint.as_mut() {
        body.wire_api = wire::ProviderWireApi::Managed;
    }
    assert_eq!(
        runtime.submit_custom_endpoint_probe_command(&managed),
        Err(ProbeCommandError::InvalidCommand)
    );

    // An address with a query is not one this product will route to, and the
    // refusal is the address rule's rather than a bound of this file's.
    assert_eq!(
        runtime.submit_custom_endpoint_probe_command(&command(
            "my-gateway",
            "http://workstation.local:11434/v1?key=1"
        )),
        Err(ProbeCommandError::InvalidCommand)
    );

    let _first = runtime
        .submit_custom_endpoint_probe_command(&command(
            "my-gateway",
            "http://workstation.local:11434",
        ))
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        runtime.submit_custom_endpoint_probe_command(&command(
            "other-gateway",
            "http://workstation.local:11434"
        )),
        Err(ProbeCommandError::ProbeInFlight),
        "one flight covers both probe kinds, so two verdicts cannot land in either order"
    );
}
