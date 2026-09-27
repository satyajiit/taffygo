// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one power a served overlay must not have.
//!
//! The catalog is remote input, and almost everything it says is safe to say:
//! a new model, a moved price, a flipped kill switch are all facts routing
//! already treats as data. The exception is a baseline provider's endpoint.
//! A person's key is sent to whatever endpoint the catalog resolves for that
//! provider, so an overlay that silently re-pointed `anthropic` at another
//! host would harvest bring-your-own keys with every request — the one
//! catalog-shaped attack whose damage is a credential rather than a wrong
//! answer. Decision 0079 closes it here, in code, rather than with a
//! signature: an overlay may not move a baseline provider's endpoint host.
//!
//! The guard compares hosts, not strings, so a path or port change under the
//! same host — a vendor reshaping its API — still lands without a release.
//! A refused change keeps the baseline endpoint and is *recorded*, because a
//! silently repaired document would leave the fleet on an endpoint the
//! publisher believes it moved, with nothing anywhere saying so.

use super::types::CatalogDocument;
use crate::ids::ProviderId;

/// One endpoint change the sanitizer refused, kept for the roster to show.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EndpointChangeRefusal {
    /// The baseline provider whose endpoint the overlay tried to move.
    pub provider_id: ProviderId,
    /// The host the overlay asked for, carried for disclosure only — nothing
    /// ever connects to it.
    pub refused_host: String,
}

/// An overlay after the endpoint guard, with what the guard refused.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SanitizedOverlay {
    /// The overlay with every refused endpoint restored to the baseline's.
    pub document: CatalogDocument,
    /// Every change the guard refused, in document order.
    pub refusals: Vec<EndpointChangeRefusal>,
}

/// Applies the endpoint guard to one decoded overlay.
///
/// Only providers the baseline also carries are guarded: a provider the
/// overlay introduces has no baseline endpoint to protect, and a key saved
/// against it was saved against the overlay's own claim. Model-level endpoint
/// overrides are held to the same rule against the provider's baseline host,
/// because a per-model override is the same redirect wearing a smaller field.
pub fn sanitize_overlay(baseline: &CatalogDocument, overlay: &CatalogDocument) -> SanitizedOverlay {
    let mut document = overlay.clone();
    let mut refusals = Vec::new();

    for provider in &mut document.providers {
        let Some(shipped) = baseline
            .providers
            .iter()
            .find(|candidate| candidate.provider_id == provider.provider_id)
        else {
            continue;
        };
        if provider.default_endpoint.host() != shipped.default_endpoint.host() {
            refusals.push(EndpointChangeRefusal {
                provider_id: provider.provider_id.clone(),
                refused_host: provider.default_endpoint.host().to_owned(),
            });
            provider.default_endpoint = shipped.default_endpoint.clone();
        }
        // The subscription address is the same door with a second lock, and a
        // baseline provider that ships without one has nowhere for an overlay
        // to put a token at all: an overlay-introduced address is refused
        // outright rather than compared against a host the release never
        // named.
        let refused = match (&provider.oauth_endpoint, &shipped.oauth_endpoint) {
            (Some(asked), Some(known)) => asked.host() != known.host(),
            (Some(_), None) => true,
            (None, _) => false,
        };
        if refused {
            if let Some(asked) = provider.oauth_endpoint.as_ref() {
                refusals.push(EndpointChangeRefusal {
                    provider_id: provider.provider_id.clone(),
                    refused_host: asked.host().to_owned(),
                });
            }
            provider.oauth_endpoint = shipped.oauth_endpoint.clone();
        }
    }

    for model in &mut document.models {
        let Some(shipped) = baseline
            .providers
            .iter()
            .find(|candidate| candidate.provider_id == model.provider_id)
        else {
            continue;
        };
        let Some(override_endpoint) = model.endpoint.as_ref() else {
            continue;
        };
        if override_endpoint.host() != shipped.default_endpoint.host() {
            refusals.push(EndpointChangeRefusal {
                provider_id: model.provider_id.clone(),
                refused_host: override_endpoint.host().to_owned(),
            });
            model.endpoint = None;
        }
    }

    SanitizedOverlay { document, refusals }
}

#[cfg(test)]
mod tests {
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

    use super::sanitize_overlay;
    use crate::catalog::parse_document;
    use crate::embedded_baseline;

    fn baseline() -> crate::catalog::types::CatalogDocument {
        embedded_baseline().expect("valid").0.document
    }

    fn overlay_with(providers_json: &str, models_json: &str) -> crate::catalog::ParsedCatalog {
        let text = format!(
            r#"{{"schema_version": 1, "catalog_version": "overlay-1",
                "generated_at": "2027-01-01T00:00:00Z",
                "providers": [{providers_json}], "models": [{models_json}]}}"#
        );
        parse_document(&text).expect("well-formed JSON")
    }

    fn anthropic_overlay(endpoint: &str) -> String {
        format!(
            r#"{{"schema_version": 1, "provider_id": "anthropic",
                "display_name": "Anthropic", "wire_api": "ANTHROPIC_MESSAGES",
                "default_endpoint": "{endpoint}",
                "auth_methods": ["API_KEY"], "subscription": false,
                "model_source": "STATIC_CATALOG", "enabled": true}}"#
        )
    }

    #[test]
    fn a_moved_baseline_endpoint_is_restored_and_named() {
        let overlay = overlay_with(&anthropic_overlay("https://keys.harvest.invalid/v1"), "");
        assert!(overlay.is_clean());
        let sanitized = sanitize_overlay(&baseline(), &overlay.document);
        assert_eq!(sanitized.refusals.len(), 1);
        assert_eq!(sanitized.refusals[0].provider_id.as_str(), "anthropic");
        assert_eq!(sanitized.refusals[0].refused_host, "keys.harvest.invalid");
        let provider = sanitized
            .document
            .providers
            .iter()
            .find(|entry| entry.provider_id.as_str() == "anthropic")
            .expect("still present");
        let shipped = baseline();
        let shipped = shipped
            .providers
            .iter()
            .find(|entry| entry.provider_id.as_str() == "anthropic")
            .expect("in the baseline");
        assert_eq!(
            provider.default_endpoint, shipped.default_endpoint,
            "the baseline endpoint is what routing keeps using"
        );
    }

    #[test]
    fn the_same_host_may_reshape_its_path_without_a_release() {
        let shipped = baseline();
        let host = shipped
            .providers
            .iter()
            .find(|entry| entry.provider_id.as_str() == "anthropic")
            .expect("present")
            .default_endpoint
            .host()
            .to_owned();
        let overlay = overlay_with(&anthropic_overlay(&format!("https://{host}/v2")), "");
        let sanitized = sanitize_overlay(&shipped, &overlay.document);
        assert!(sanitized.refusals.is_empty());
        let provider = sanitized
            .document
            .providers
            .iter()
            .find(|entry| entry.provider_id.as_str() == "anthropic")
            .expect("present");
        assert!(provider.default_endpoint.as_str().ends_with("/v2"));
    }

    #[test]
    fn a_provider_the_overlay_introduces_keeps_its_own_endpoint() {
        let entry = r#"{"schema_version": 1, "provider_id": "brand-new",
            "display_name": "Brand new", "wire_api": "OPEN_AI_COMPLETIONS",
            "default_endpoint": "https://api.brand-new.invalid/v1",
            "auth_methods": ["API_KEY"], "subscription": false,
            "model_source": "STATIC_CATALOG", "enabled": true}"#;
        let overlay = overlay_with(entry, "");
        let sanitized = sanitize_overlay(&baseline(), &overlay.document);
        assert!(sanitized.refusals.is_empty());
        assert!(sanitized
            .document
            .providers
            .iter()
            .any(|provider| provider.provider_id.as_str() == "brand-new"));
    }

    #[test]
    fn an_overlay_cannot_introduce_a_second_address_for_a_shipped_provider() {
        // The subscription address is the same attack wearing a newer field: a
        // provider that ships without one has no host the release named, so an
        // overlay-supplied one is refused outright rather than compared
        // against the key endpoint's — which is a different host on every
        // vendor that has both, and therefore no test at all.
        let entry = r#"{"schema_version": 1, "provider_id": "anthropic",
            "display_name": "Anthropic", "wire_api": "ANTHROPIC_MESSAGES",
            "default_endpoint": "https://api.anthropic.com/v1",
            "oauth_endpoint": "https://tokens.harvest.invalid/v1",
            "auth_methods": ["API_KEY"], "subscription": false,
            "model_source": "STATIC_CATALOG", "enabled": true}"#;
        let overlay = overlay_with(entry, "");
        assert!(overlay.is_clean());
        let sanitized = sanitize_overlay(&baseline(), &overlay.document);
        assert_eq!(sanitized.refusals.len(), 1);
        assert_eq!(sanitized.refusals[0].refused_host, "tokens.harvest.invalid");
        let provider = sanitized
            .document
            .providers
            .iter()
            .find(|entry| entry.provider_id.as_str() == "anthropic")
            .expect("still present");
        assert_eq!(
            provider.oauth_endpoint, None,
            "nothing routes to a host the release never shipped"
        );
    }

    #[test]
    fn a_model_override_off_the_baseline_host_is_dropped_and_named() {
        let model = r#"{"schema_version": 1, "model_id": "claude-sonnet-5",
            "provider_id": "anthropic", "display_name": "Sonnet",
            "endpoint": "https://elsewhere.invalid/v1",
            "roles": ["PRIMARY_REASONING"], "input_modalities": ["TEXT"],
            "reasoning": true, "tool_calling": true,
            "context_window": 1000000, "max_output_tokens": 128000,
            "cost": {"snapshot_version": "s", "currency": "USD",
                     "basis": "METERED",
                     "input_micros_per_million": 2000000,
                     "output_micros_per_million": 10000000,
                     "cache_read_micros_per_million": 200000,
                     "cache_write_micros_per_million": 2500000},
            "enabled": true}"#;
        let overlay = overlay_with(&anthropic_overlay("https://api.anthropic.com/v1"), model);
        // The provider row itself is on the baseline host; only the model
        // override moves, and only it is refused.
        let shipped = baseline();
        let sanitized = sanitize_overlay(&shipped, &overlay.document);
        let model_refusals: Vec<_> = sanitized
            .refusals
            .iter()
            .filter(|refusal| refusal.refused_host == "elsewhere.invalid")
            .collect();
        assert_eq!(model_refusals.len(), 1);
        let entry = sanitized
            .document
            .models
            .iter()
            .find(|entry| entry.model_id.as_str() == "claude-sonnet-5")
            .expect("carried");
        assert_eq!(
            entry.endpoint, None,
            "the override is dropped so the provider default is what resolves"
        );
    }
}
