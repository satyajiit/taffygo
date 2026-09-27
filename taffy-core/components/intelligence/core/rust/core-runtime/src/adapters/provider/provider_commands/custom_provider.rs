// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one command that defines a person's own provider (decision 0096).
//!
//! Split from its siblings because it is the only provider command that
//! carries a list. Everything else on that seam is a handful of scalars; this
//! one arrives with the models a probe found behind an address and the runtime
//! it turned out to be, and each of those is a bounded value that has to be
//! parsed before any of it is written.
//!
//! Validation is complete before the plane is touched. A save that would be
//! refused halfway is a save that empties a provider's models and then fails,
//! and a person who retyped an address would find the one they had gone.

use model_router::wire::ServerKind;
use model_router::ModelId;

use crate::provider::{
    CredentialHandle, CustomModel, ProviderDisplayName, ProviderError, ProviderId,
    ProviderProtocol, SavedCustomProvider,
};

use super::{domain, wire_api, ProviderServiceError, ProviderServiceStep};

/// Applies one save of a person's own provider.
pub(super) fn save_custom_provider(
    plane: &mut ProviderProtocol,
    command: &core_service_types::CoreServiceCommand,
) -> Result<ProviderServiceStep, ProviderServiceError> {
    let body = command
        .save_custom_provider
        .as_ref()
        .ok_or(ProviderServiceError::MissingBody)?;
    let credential = body
        .credential_handle
        .as_ref()
        .map(|handle| CredentialHandle::new(handle.as_str()))
        .transpose()
        .map_err(|error| domain(error.into()))?;
    let saved = SavedCustomProvider {
        provider_id: ProviderId::new(body.provider_id.as_str()).map_err(domain)?,
        display_name: ProviderDisplayName::new(body.display_name.as_str()).map_err(domain)?,
        // The person's rule, never the catalog's. `Endpoint::new` is what a
        // served document is held to and it requires an https origin, so a
        // save of `http://localhost:11434/v1` was refused here before any
        // other part of decision 0096 got a chance — the browser had already
        // accepted the address and written it to the register.
        endpoint: model_router::catalog::Endpoint::user_base_url(body.endpoint.as_str())
            .map_err(|_| domain(ProviderError::InvalidEndpoint))?,
        wire_api: wire_api(body.wire_api)?,
        credential,
        models: custom_models(&body.models)?,
        detected_server: body
            .detected_server
            .as_ref()
            .map(|detected| server_kind(detected.server_kind)),
    };
    let effect = plane.save_custom_provider(saved).map_err(domain)?;
    Ok(ProviderServiceStep::from_effect(&effect))
}

/// Every model the save carried, parsed into the plane's own value type.
///
/// A model whose identity or name fails its bound refuses the whole save
/// rather than being dropped from it. The list is not remote input being
/// salvaged — it is one answer a probe already gave about one address, and
/// silently saving a shorter version of it would leave a person looking at a
/// provider missing the model they set it up for, with nothing said.
fn custom_models(
    specs: &[core_service_types::CustomModelSpec],
) -> Result<Vec<CustomModel>, ProviderServiceError> {
    specs
        .iter()
        .map(|spec| {
            if spec.display_name.is_empty()
                || spec.display_name.len() > core_service_types::MAX_MODEL_DISPLAY_NAME_BYTES
                || spec.model_id.len() > core_service_types::MAX_MODEL_ID_BYTES
            {
                return Err(ProviderServiceError::MissingBody);
            }
            Ok(CustomModel {
                model_id: ModelId::new(spec.model_id.as_str())
                    .map_err(|_| ProviderServiceError::MissingBody)?,
                display_name: spec.display_name.clone(),
                context_window: spec.context_window,
                max_output_tokens: spec.max_output_tokens,
                reasoning: spec.reasoning,
                tool_calling: spec.tool_calling,
            })
        })
        .collect()
}

/// The router's spelling of one detected server, member for member.
///
/// Total and exhaustive with no catch-all, so a sixth member added to either
/// side fails to compile here rather than being read as whichever kind was
/// written first — which would send a body in a dialect the server refuses.
const fn server_kind(kind: core_service_types::ServerKind) -> ServerKind {
    match kind {
        core_service_types::ServerKind::OpenaiCompatible => ServerKind::OpenAiCompatible,
        core_service_types::ServerKind::Ollama => ServerKind::Ollama,
        core_service_types::ServerKind::LmStudio => ServerKind::LmStudio,
        core_service_types::ServerKind::Vllm => ServerKind::Vllm,
        core_service_types::ServerKind::LlamaCpp => ServerKind::LlamaCpp,
    }
}

#[cfg(test)]
mod tests {
    use super::{server_kind, ProviderProtocol};
    use crate::adapters::provider::apply_provider_command;
    use crate::provider::{CatalogProvider, ProviderAuthMethod, ProviderId, ProviderPresentation};
    use core_service_types as wire;
    use model_router::catalog::CatalogLayer;
    use model_router::wire::ServerKind;

    fn plane() -> ProviderProtocol {
        ProviderProtocol::new(
            [CatalogProvider {
                provider_id: ProviderId::new("anthropic").expect("valid"),
                display_name: "Anthropic".to_owned(),
                auth_methods: vec![ProviderAuthMethod::ApiKey],
                enabled: true,
                layer: CatalogLayer::EmbeddedBaseline,
                configurable: true,
                subscription: false,
                endpoint_changed: false,
                refused_endpoint_host: None,
                presentation: ProviderPresentation::default(),
            }],
            [],
        )
    }

    fn save(models: Vec<wire::CustomModelSpec>) -> wire::CoreServiceCommand {
        let mut command = wire::CoreServiceCommand {
            operation: wire::OperationEnvelope {
                operation_id: "save-custom-1".to_owned(),
                service_generation: 1,
                task_revision: 0,
                deadline_monotonic_ms: 0,
                idempotency_key: "save-custom-1-key".to_owned(),
            },
            kind: wire::CoreServiceCommandKind::SaveCustomProvider,
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
        };
        command.save_custom_provider = Some(wire::SaveCustomProviderCommand {
            provider_id: "my-gateway".to_owned(),
            display_name: "My gateway".to_owned(),
            endpoint: "https://gateway.example/v1".to_owned(),
            wire_api: wire::ProviderWireApi::OpenAiCompletions,
            credential_handle: None,
            models,
            detected_server: Some(wire::DetectedServer {
                server_kind: wire::ServerKind::LlamaCpp,
            }),
        });
        command
    }

    fn spec(model_id: &str) -> wire::CustomModelSpec {
        wire::CustomModelSpec {
            model_id: model_id.to_owned(),
            display_name: "A model".to_owned(),
            context_window: 0,
            max_output_tokens: 0,
            reasoning: true,
            tool_calling: true,
        }
    }

    #[test]
    fn a_save_carries_its_models_and_its_runtime_across_the_seam() {
        let mut plane = plane();
        apply_provider_command(&mut plane, &save(vec![spec("gateway-large")]))
            .expect("every field parses");
        let entry = plane.custom_providers().next().expect("one provider");
        assert_eq!(entry.models.len(), 1);
        assert_eq!(entry.detected_server, Some(ServerKind::LlamaCpp));
        assert_eq!(
            entry.models.first().expect("one").context_window,
            0,
            "a zero the endpoint reported crosses as the silence it is"
        );
    }

    #[test]
    fn a_server_on_the_desk_is_saved_with_its_scheme_port_and_path_intact() {
        // The address decision 0096 was written for, and the one the save
        // refused until it stopped asking the catalog's question. A person who
        // runs a model server on the machine in front of them types this, and
        // all three of the scheme, the port and the base path have to survive
        // — the browser appends only the operation beneath what it holds.
        let mut plane = plane();
        let mut command = save(vec![spec("gateway-large")]);
        if let Some(body) = command.save_custom_provider.as_mut() {
            body.endpoint = "http://localhost:11434/v1".to_owned();
        }
        apply_provider_command(&mut plane, &command).expect("a server somebody runs");
        let entry = plane.custom_providers().next().expect("one provider");
        assert_eq!(entry.endpoint.as_str(), "http://localhost:11434/v1");
        assert_eq!(
            entry.endpoint.host(),
            "localhost:11434",
            "the roster row names the host, not the scheme in front of it"
        );
    }

    #[test]
    fn a_model_identity_the_catalog_cannot_hold_refuses_the_whole_save() {
        let mut plane = plane();
        assert!(
            apply_provider_command(&mut plane, &save(vec![spec("a model with spaces")])).is_err()
        );
        assert_eq!(
            plane.custom_providers().count(),
            0,
            "a save refused for one model files no provider at all"
        );
    }

    #[test]
    fn the_five_detected_servers_map_member_for_member() {
        for (contract, router) in [
            (
                wire::ServerKind::OpenaiCompatible,
                ServerKind::OpenAiCompatible,
            ),
            (wire::ServerKind::Ollama, ServerKind::Ollama),
            (wire::ServerKind::LmStudio, ServerKind::LmStudio),
            (wire::ServerKind::Vllm, ServerKind::Vllm),
            (wire::ServerKind::LlamaCpp, ServerKind::LlamaCpp),
        ] {
            assert_eq!(server_kind(contract), router);
        }
    }
}
