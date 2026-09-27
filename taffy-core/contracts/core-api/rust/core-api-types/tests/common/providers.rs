// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The provider roster, its probes and its models, as one fixture each.

use core_api_types::{
    CatalogLayerView, CustomModelSpecView, InputModalityView, ModelRoleView, ProbeEndpointView,
    ProviderAuthMethodView, ProviderCredentialStateView, ProviderModelView, ProviderOriginView,
    ProviderPresentationView, ProviderProbeVerdictView, ProviderProbeView,
    ProviderRefusalStateView, ProviderRefusalView, ProviderRosterEntry, ServerKindView,
    StoredCredentialView, ThinkingLevelView, ThinkingPreferenceView,
};

pub(super) fn probes_fixture() -> Vec<ProviderProbeView> {
    vec![
        ProviderProbeView {
            provider_id: "anthropic".to_owned(),
            verdict: ProviderProbeVerdictView::Usable,
            at_monotonic_ms: 480_000,
            endpoint: None,
        },
        // Filed without a flight (Core API 3.40): a provider that serves
        // its own list, probed before any listing, has nothing to probe
        // with. Not a judgement of the key.
        ProviderProbeView {
            provider_id: "openrouter".to_owned(),
            verdict: ProviderProbeVerdictView::NoModelListed,
            at_monotonic_ms: 484_000,
            endpoint: None,
        },
        ProviderProbeView {
            provider_id: "my-gateway".to_owned(),
            verdict: ProviderProbeVerdictView::EndpointReached,
            at_monotonic_ms: 492_000,
            endpoint: Some(ProbeEndpointView {
                server_kind: ServerKindView::Ollama,
                // The count and the list are kept apart deliberately: the
                // count is what the server named and the list is what
                // survived the bound (Core API 3.18).
                model_count: 3,
                models: vec![
                    CustomModelSpecView {
                        model_id: "qwen3-30b".to_owned(),
                        display_name: "Qwen3 30B".to_owned(),
                        context_window: 32_768,
                        max_output_tokens: 8_192,
                        reasoning: false,
                        tool_calling: true,
                    },
                    CustomModelSpecView {
                        model_id: "llama3.1-8b".to_owned(),
                        display_name: "Llama 3.1 8B".to_owned(),
                        context_window: 131_072,
                        max_output_tokens: 4_096,
                        reasoning: false,
                        tool_calling: false,
                    },
                    CustomModelSpecView {
                        model_id: "nomic-embed-text".to_owned(),
                        display_name: "Nomic Embed Text".to_owned(),
                        context_window: 8_192,
                        max_output_tokens: 0,
                        reasoning: false,
                        tool_calling: false,
                    },
                ],
                proved_base: Some("https://gateway.example.invalid/v1".to_owned()),
            }),
        },
    ]
}

pub(super) fn models_fixture() -> Vec<ProviderModelView> {
    vec![
        ProviderModelView {
            provider_id: "anthropic".to_owned(),
            model_id: "claude-sonnet-4-5".to_owned(),
            display_name: "Claude Sonnet 4.5".to_owned(),
            context_window: 200_000,
            max_output_tokens: 64_000,
            reasoning: true,
            tool_calling: true,
            roles: vec![ModelRoleView::PrimaryReasoning, ModelRoleView::FastBrowsing],
            input_modalities: vec![InputModalityView::Text, InputModalityView::Image],
            thinking_levels: vec![
                ThinkingLevelView::Off,
                ThinkingLevelView::Low,
                ThinkingLevelView::Medium,
                ThinkingLevelView::High,
            ],
        },
        ProviderModelView {
            provider_id: "xai".to_owned(),
            model_id: "grok-4".to_owned(),
            display_name: "Grok 4".to_owned(),
            context_window: 131_072,
            max_output_tokens: 32_768,
            reasoning: true,
            tool_calling: true,
            roles: vec![ModelRoleView::PrimaryReasoning],
            input_modalities: vec![InputModalityView::Text],
            thinking_levels: Vec::new(),
        },
        ProviderModelView {
            provider_id: "my-gateway".to_owned(),
            model_id: "qwen3-30b".to_owned(),
            display_name: "Qwen3 30B".to_owned(),
            context_window: 32_768,
            max_output_tokens: 8_192,
            reasoning: false,
            tool_calling: true,
            roles: vec![ModelRoleView::PrimaryReasoning, ModelRoleView::FastBrowsing],
            input_modalities: vec![InputModalityView::Text],
            thinking_levels: Vec::new(),
        },
    ]
}

pub(super) fn roster_fixture() -> Vec<ProviderRosterEntry> {
    vec![
        ProviderRosterEntry {
            provider_id: "anthropic".to_owned(),
            display_name: "Anthropic".to_owned(),
            origin: ProviderOriginView::Catalog,
            auth_methods: vec![ProviderAuthMethodView::ApiKey],
            stored: Some(StoredCredentialView {
                auth_method: ProviderAuthMethodView::ApiKey,
                state: ProviderCredentialStateView::Usable,
                subscription_backed: false,
                account_label: None,
                plan_label: None,
            }),
            signing_in: false,
            enabled: true,
            endpoint_host: None,
            endpoint_base: None,
            last_refusal: Some(ProviderRefusalStateView {
                refusal: ProviderRefusalView::RateLimit,
                at_monotonic_ms: 486_000,
            }),
            configurable: true,
            subscription: false,
            endpoint_changed: false,
            refused_endpoint_host: None,
            catalog_layer: CatalogLayerView::EmbeddedBaseline,
            selected_model_id: Some("claude-sonnet-4-5".to_owned()),
            thinking: Some(ThinkingPreferenceView {
                level: ThinkingLevelView::High,
            }),
            presentation: Some(ProviderPresentationView {
                key_prefix: Some("sk-ant-".to_owned()),
                get_key_url: Some("https://console.anthropic.com/settings/keys".to_owned()),
                docs_url: None,
            }),
            // What the merged catalog carries, counted before the flat
            // list was fitted to its budget (Core API 3.19). This snapshot
            // is far under the budget, so nothing was cut and each count
            // is exactly that provider's rows in `provider_models`.
            model_count: 1,
        },
        ProviderRosterEntry {
            provider_id: "xai".to_owned(),
            display_name: "xAI".to_owned(),
            origin: ProviderOriginView::Catalog,
            auth_methods: vec![
                ProviderAuthMethodView::ApiKey,
                ProviderAuthMethodView::Oauth,
            ],
            stored: Some(StoredCredentialView {
                auth_method: ProviderAuthMethodView::Oauth,
                state: ProviderCredentialStateView::NeedsSignIn,
                subscription_backed: true,
                account_label: Some("person@example.invalid".to_owned()),
                plan_label: Some("SuperGrok".to_owned()),
            }),
            signing_in: false,
            enabled: true,
            endpoint_host: None,
            endpoint_base: None,
            last_refusal: None,
            configurable: true,
            subscription: true,
            endpoint_changed: true,
            // Present exactly while the refusal stands (Core API 3.40):
            // the host the served document asked for, for disclosure.
            refused_endpoint_host: Some("api.grok.example.invalid".to_owned()),
            catalog_layer: CatalogLayerView::RemoteOverlay,
            selected_model_id: None,
            thinking: None,
            presentation: Some(ProviderPresentationView {
                key_prefix: Some("xai-".to_owned()),
                get_key_url: None,
                docs_url: None,
            }),
            model_count: 1,
        },
        ProviderRosterEntry {
            provider_id: "my-gateway".to_owned(),
            display_name: "My gateway".to_owned(),
            origin: ProviderOriginView::Custom,
            auth_methods: vec![ProviderAuthMethodView::ApiKey],
            stored: None,
            signing_in: false,
            enabled: true,
            endpoint_host: Some("gateway.example.invalid".to_owned()),
            endpoint_base: Some("https://gateway.example.invalid/v1".to_owned()),
            last_refusal: None,
            configurable: true,
            subscription: false,
            endpoint_changed: false,
            refused_endpoint_host: None,
            catalog_layer: CatalogLayerView::UserOverride,
            selected_model_id: None,
            thinking: None,
            presentation: None,
            model_count: 1,
        },
    ]
}
