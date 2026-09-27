// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Mutation totality for the eager provider status projection.

use model_router::catalog::CatalogLayer;
use model_router::{CredentialState, ModelId, ThinkingLevel};

use super::{endpoint, handle, id, name, oauth_vendor, plane, saved};
use crate::provider::{
    CatalogModel, CatalogProvider, ProviderAuthMethod, ProviderEffect, ProviderError,
    ProviderPresentation, ProviderProtocol, ProviderRefusal, ProviderRefusalKind, ProviderWireApi,
    SavedCustomProvider,
};

fn model(provider_id: &str, model_id: &str) -> CatalogModel {
    CatalogModel {
        provider_id: id(provider_id),
        model_id: ModelId::new(model_id).unwrap_or_else(|_| unreachable!()),
        display_name: model_id.to_owned(),
        context_window: 131_072,
        max_output_tokens: 8_192,
        reasoning: true,
        tool_calling: true,
        roles: Vec::new(),
        input_modalities: Vec::new(),
        thinking_levels: vec![ThinkingLevel::Low, ThinkingLevel::High],
    }
}

fn preference_plane() -> ProviderProtocol {
    ProviderProtocol::new(
        [CatalogProvider {
            provider_id: id("xai"),
            display_name: "xAI".to_owned(),
            auth_methods: vec![ProviderAuthMethod::ApiKey, ProviderAuthMethod::Oauth],
            enabled: true,
            layer: CatalogLayer::EmbeddedBaseline,
            configurable: true,
            subscription: false,
            endpoint_changed: false,
            refused_endpoint_host: None,
            presentation: ProviderPresentation::default(),
        }],
        [model("xai", "grok-status")],
    )
}

fn custom_saved(provider_id: &str) -> SavedCustomProvider {
    saved(
        id(provider_id),
        name("My gateway"),
        endpoint("https://gateway.example/v1"),
        ProviderWireApi::OpenAiCompletions,
        Some(handle("custom-handle")),
    )
}

fn assert_one_rebuild(mut protocol: ProviderProtocol, change: impl FnOnce(&mut ProviderProtocol)) {
    let before = protocol.status_projection().revision();
    change(&mut protocol);
    assert_eq!(
        protocol.status_projection().revision(),
        before.wrapping_add(1),
        "one accepted provider write must rebuild the eager projection once"
    );
}

#[test]
fn every_successful_provider_write_rebuilds_the_projection_once() {
    // Catalog replacement.
    assert_one_rebuild(plane(), |protocol| {
        protocol.replace_catalog([oauth_vendor("replacement")], []);
    });

    // Bootstrap restore.
    assert_one_rebuild(plane(), |protocol| protocol.restore([], []));

    // Credential save, forget, and state report.
    assert_one_rebuild(plane(), |protocol| {
        assert!(protocol
            .save_credential(
                id("anthropic"),
                ProviderAuthMethod::ApiKey,
                handle("save-handle"),
            )
            .is_ok());
    });
    let mut with_credential = plane();
    assert!(with_credential
        .save_credential(
            id("anthropic"),
            ProviderAuthMethod::ApiKey,
            handle("forget-handle"),
        )
        .is_ok());
    assert_one_rebuild(with_credential, |protocol| {
        assert!(protocol.forget_credential(&id("anthropic")).is_ok());
    });
    let mut with_credential = plane();
    assert!(with_credential
        .save_credential(
            id("anthropic"),
            ProviderAuthMethod::ApiKey,
            handle("state-handle"),
        )
        .is_ok());
    assert_one_rebuild(with_credential, |protocol| {
        assert!(protocol
            .set_credential_state(&id("anthropic"), CredentialState::NeedsSignIn)
            .is_ok());
    });

    // Provider refusal.
    assert_one_rebuild(plane(), |protocol| {
        protocol.record_refusal(
            &id("anthropic"),
            Some(ProviderRefusal {
                kind: ProviderRefusalKind::RateLimit,
                at_monotonic_ms: 7_000,
            }),
        );
    });

    // Sign-in admission and terminal.
    assert_one_rebuild(plane(), |protocol| {
        assert!(protocol
            .begin_sign_in(id("xai"), "flow-1".to_owned(), "binding-1".to_owned())
            .is_ok());
    });
    let mut with_flow = plane();
    assert!(with_flow
        .begin_sign_in(id("xai"), "flow-1".to_owned(), "binding-1".to_owned())
        .is_ok());
    assert_one_rebuild(with_flow, |protocol| {
        assert!(protocol.finish_sign_in("flow-1", "binding-1").is_ok());
    });

    // A person's provider save and remove.
    assert_one_rebuild(plane(), |protocol| {
        assert!(protocol
            .save_custom_provider(custom_saved("my-gateway"))
            .is_ok());
    });
    let mut with_custom = plane();
    assert!(with_custom
        .save_custom_provider(custom_saved("my-gateway"))
        .is_ok());
    assert_one_rebuild(with_custom, |protocol| {
        assert!(protocol.remove_custom_provider(&id("my-gateway")).is_ok());
    });

    // Standing model preference.
    assert_one_rebuild(preference_plane(), |protocol| {
        assert!(protocol
            .set_model_preference(&id("xai"), Some("grok-status"), Some(ThinkingLevel::High),)
            .is_ok());
    });
}

fn assert_refusal_reuses_projection(
    mut protocol: ProviderProtocol,
    change: impl FnOnce(&mut ProviderProtocol) -> Result<ProviderEffect, ProviderError>,
) {
    let before = protocol.status_projection().clone();
    assert!(change(&mut protocol).is_err());
    assert_eq!(
        protocol.status_projection(),
        &before,
        "a refused write changed or rebuilt status"
    );
}

#[test]
fn every_refused_provider_write_leaves_the_projection_untouched() {
    assert_refusal_reuses_projection(plane(), |protocol| {
        protocol.save_credential(
            id("anthropic"),
            ProviderAuthMethod::Oauth,
            handle("wrong-method"),
        )
    });
    assert_refusal_reuses_projection(plane(), |protocol| {
        protocol.forget_credential(&id("anthropic"))
    });
    assert_refusal_reuses_projection(plane(), |protocol| {
        protocol.set_credential_state(&id("anthropic"), CredentialState::NeedsSignIn)
    });
    assert_refusal_reuses_projection(plane(), |protocol| {
        protocol.begin_sign_in(
            id("switched-off-vendor"),
            "flow-off".to_owned(),
            "binding-off".to_owned(),
        )
    });
    assert_refusal_reuses_projection(plane(), |protocol| {
        protocol.finish_sign_in("missing-flow", "missing-binding")
    });
    assert_refusal_reuses_projection(plane(), |protocol| {
        protocol.save_custom_provider(custom_saved("anthropic"))
    });
    assert_refusal_reuses_projection(plane(), |protocol| {
        protocol.remove_custom_provider(&id("missing-custom"))
    });
    assert_refusal_reuses_projection(preference_plane(), |protocol| {
        protocol.set_model_preference(&id("missing-provider"), None, None)
    });
}

#[test]
fn unchanged_reads_reuse_one_projection_without_advancing_its_revision() {
    let protocol = preference_plane();
    let expected = protocol.status_projection().clone();
    for _ in 0..128 {
        assert_eq!(protocol.status_projection(), &expected);
        assert_eq!(protocol.roster().len(), 1);
    }
}
