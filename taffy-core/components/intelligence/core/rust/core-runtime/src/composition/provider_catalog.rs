// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The compiled catalog's providers, as the provider plane's own facts.
//!
//! The plane needs three things about each shipped provider: the identity it
//! is filed under, the methods it offers, and whether the baseline ships it
//! switched on. All three already exist in the merged catalog, so this module
//! reads them across rather than restating them — decisions 0015 and 0029 both
//! forbid a second per-vendor table, and a second one here would decide
//! "reserved identity" and "offers OAuth" separately from routing with nothing
//! comparing the two.

use model_router::catalog::merge::ProviderEntry;
use model_router::catalog::{AuthMethod, CatalogLayer, MergedCatalog};

use crate::provider::{
    CatalogModel, CatalogProvider, ProviderAuthMethod, ProviderError, ProviderId,
    ProviderPresentation,
};

use crate::adapters::model::ProductionModelRouter;

/// The router's spelling of one authentication method, in the plane's terms.
const fn plane_auth_method(method: AuthMethod) -> ProviderAuthMethod {
    match method {
        AuthMethod::ApiKey => ProviderAuthMethod::ApiKey,
        AuthMethod::Oauth => ProviderAuthMethod::Oauth,
    }
}

/// The vendors whose subscription sign-in this binary carries (decision 0081).
///
/// Compiled here because `configurable` is this layer's answer, and a served
/// row must not claim what the binary cannot do: an overlay that grants
/// `OAUTH` to a vendor with no compiled flow gets a row that is carried and
/// explained, never offered. The same vendors appear twice more in the
/// product — the Android flow map (`ProviderSignInFlows`) carries each flow's
/// shape and acknowledgement obligation, and the browser's pinned
/// configuration carries its origins and its client identity — each layer
/// owning its own facet.
///
/// These three agreeing is not enough, and this comment used to say it was.
/// The catalog is a fourth table and it is the one that decides whether a row
/// is offered at all: with no `OAUTH` on the row, `configurable` above is
/// false however complete the flow is, and nothing anywhere reports it. That
/// is not hypothetical — these three named seven vendors and agreed exactly
/// while the catalog offered `OAUTH` to one, so five finished flows were
/// unreachable and every gate was green. Adding or enabling a vendor is a
/// change to all four, `check_vendor_agreement.py` on the `catalog` lane
/// compares all four, and the test at the bottom of this file makes the same
/// rule executable against the catalog this build embeds.
const SIGN_IN_VENDORS: [&str; 6] = [
    "anthropic",
    "github-copilot",
    "kimi-coding",
    "openai",
    "openrouter",
    "xai",
];

/// Whether this binary can run a subscription sign-in for the provider.
fn sign_in_flow_compiled(provider_id: &str) -> bool {
    SIGN_IN_VENDORS.contains(&provider_id)
}

/// Every catalog provider, ready for `ProviderProtocol::new`.
///
/// A refusal rather than a skip. The plane's alphabet is the narrower of the
/// two sides (the Android store's `[a-z0-9][a-z0-9-]{0,63}`), so a catalog
/// identity it will not accept is an identity no credential can ever be filed
/// under. Dropping that row would leave a provider the router still routes to
/// and the plane has never heard of: a person would save a key against it, the
/// save would be refused as `UnknownProvider`, and nothing would say why.
/// Failing the profile build instead makes the disagreement a named error at
/// the one moment somebody can act on it.
pub fn catalog_providers(
    router: &ProductionModelRouter,
) -> Result<Vec<CatalogProvider>, ProviderError> {
    catalog_providers_from(router.catalog_providers())
}

/// The same mapping over any provider iterator, for the refresh path.
///
/// A merged catalog after an accepted overlay is the same authority the
/// bootstrap read, one generation later; deriving the plane's rows from it
/// through this one function is what keeps "reserved identity" and "offers
/// OAuth" a single answer before and after a refresh.
///
/// A person's own provider is skipped. It reaches the merge because the router
/// has to resolve models under it, but the plane already holds it as the
/// person's own record: readmitting it here as a catalog row would put its
/// identity in the set the catalog reserves, and the next save of the very
/// same provider would be refused for taking an identity the catalog
/// defines — its own.
pub fn catalog_providers_from<'a>(
    providers: impl Iterator<Item = &'a ProviderEntry>,
) -> Result<Vec<CatalogProvider>, ProviderError> {
    providers
        .filter(|entry| entry.layer != CatalogLayer::UserOverride)
        .map(|entry| {
            let provider = &entry.provider;
            let auth_methods: Vec<ProviderAuthMethod> = provider
                .auth_methods
                .iter()
                .copied()
                .map(plane_auth_method)
                .collect();
            Ok(CatalogProvider {
                presentation: ProviderPresentation {
                    key_prefix: provider.presentation.key_prefix.clone(),
                    get_key_url: provider.presentation.get_key_url.clone(),
                    docs_url: provider.presentation.docs_url.clone(),
                },
                provider_id: ProviderId::new(provider.provider_id.as_str())?,
                display_name: provider.display_name.clone(),
                // A row is actionable only through a method this binary can
                // enter: a pasted key always can, and OAUTH exactly when a
                // compiled sign-in flow exists for the vendor. A row that
                // fails both is carried and explained, never offered —
                // remote data must not claim what the binary cannot do.
                configurable: auth_methods.contains(&ProviderAuthMethod::ApiKey)
                    || (auth_methods.contains(&ProviderAuthMethod::Oauth)
                        && sign_in_flow_compiled(provider.provider_id.as_str())),
                // Carried, never derived. `auth_methods` says a sign-in exists;
                // only the catalog says whether what it signs in to is a plan.
                subscription: provider.subscription,
                auth_methods,
                enabled: provider.enabled,
                layer: entry.layer,
                // Always false and always absent now, and that is the
                // finished state rather than a stub. These two facts disclosed
                // a served endpoint move the guard refused (decision 0115),
                // and decision 0200 removed the served catalog, so there is no
                // move to refuse: the only address a request can reach is one
                // this build compiled or the person typed. They stay declared
                // because the contract's `ProviderRosterEntry` still carries
                // them, and a frozen wire field leaves only at the reset.
                endpoint_changed: false,
                refused_endpoint_host: None,
            })
        })
        .collect()
}

/// Every model a person may choose between, ready for the provider plane.
///
/// Only what the merged catalog resolves: a model behind either kill switch is
/// one no route will reach, so offering it would be a choice routing would
/// refuse, and pinning it would file a name that cannot be spent. The order is
/// the merge's own — provider identity, then model identity — which is what
/// makes the flat status projection the same list on two devices holding one
/// snapshot.
pub fn catalog_models(router: &ProductionModelRouter) -> Result<Vec<CatalogModel>, ProviderError> {
    catalog_models_from(router.catalog())
}

/// The same mapping over any merged snapshot, for the refresh path.
///
/// Every layer's models come through, the person's own included. That is what
/// makes a model on a server somebody runs themselves selectable and pinnable
/// through the same path as a published one — the provider row is the plane's
/// own record and does not come back, but the models under it have to, or the
/// picker offers nothing beneath a provider the person just set up.
pub fn catalog_models_from(catalog: &MergedCatalog) -> Result<Vec<CatalogModel>, ProviderError> {
    catalog
        .models()
        .filter(|entry| catalog.resolve(&entry.model.key()).is_ok())
        .map(|entry| {
            let model = &entry.model;
            Ok(CatalogModel {
                provider_id: ProviderId::new(model.provider_id.as_str())?,
                model_id: model.model_id.clone(),
                display_name: model.display_name.clone(),
                context_window: model.context_window,
                max_output_tokens: model.max_output_tokens,
                reasoning: model.reasoning,
                tool_calling: model.tool_calling,
                roles: model.roles.clone(),
                input_modalities: model.input_modalities.clone(),
                // The catalog's own answer about this model's ladder, never a
                // guess: an entry with no map supports every rung through HIGH
                // through the adapter's default, and one that maps a rung to
                // null does not support it at all.
                thinking_levels: model.thinking_levels.selectable(),
            })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_compiled_provider_reaches_the_plane() {
        // The compiled baseline is the one catalog this build ships, so this
        // is the real check rather than a fixture: if a provider is ever added
        // with an identity the Android store cannot hold, this fails here
        // instead of at the last hop on a device.
        let router = ProductionModelRouter::from_embedded_catalog().expect("the compiled catalog");
        let plane = catalog_providers(&router).expect("every compiled identity is acceptable");
        assert_eq!(
            plane.len(),
            router.catalog_providers().count(),
            "a provider was dropped between the catalog and the plane"
        );
        assert!(
            !plane.is_empty(),
            "the compiled baseline ships at least one provider"
        );
    }

    #[test]
    fn the_plane_agrees_with_the_catalog_about_methods_and_the_kill_switch() {
        let router = ProductionModelRouter::from_embedded_catalog().expect("the compiled catalog");
        let plane = catalog_providers(&router).expect("acceptable");
        for (entry, projected) in router.catalog_providers().zip(plane.iter()) {
            let provider = &entry.provider;
            assert_eq!(
                projected.provider_id.as_str(),
                provider.provider_id.as_str()
            );
            assert_eq!(projected.enabled, provider.enabled);
            assert_eq!(projected.auth_methods.len(), provider.auth_methods.len());
            for (offered, method) in provider
                .auth_methods
                .iter()
                .zip(projected.auth_methods.iter())
            {
                assert_eq!(plane_auth_method(*offered), *method);
            }
            // A row is actionable when a pasted key can enter it, or when a
            // compiled sign-in flow serves its OAUTH offer (decision 0081).
            assert_eq!(
                projected.configurable,
                projected.auth_methods.contains(&ProviderAuthMethod::ApiKey)
                    || (projected.auth_methods.contains(&ProviderAuthMethod::Oauth)
                        && sign_in_flow_compiled(projected.provider_id.as_str())),
            );
            assert!(
                projected.configurable,
                "every baseline vendor is actionable through at least one method"
            );
            assert!(!projected.endpoint_changed);
            assert!(projected.refused_endpoint_host.is_none());
            assert_eq!(
                projected.layer,
                model_router::catalog::CatalogLayer::EmbeddedBaseline
            );
        }
    }

    #[test]
    fn every_compiled_sign_in_has_a_catalog_row_that_offers_it() {
        // The half of the doc comment above that was a claim rather than a
        // check. This table and two more agreed exactly about seven vendors
        // while the catalog offered OAUTH to one of them, so five complete
        // sign-in flows were unreachable and nothing said so: `configurable`
        // is an *and*, and a row with no OAUTH makes the compiled flow
        // irrelevant rather than pending. `check_vendor_agreement.py` on the
        // `catalog` lane compares all four tables as source text; this is the
        // same rule where it is executable, against the catalog this build
        // actually embeds.
        //
        // Note what it does not assert: that a sign-in can *run*. Decision
        // 0081 gates a vendor by a dated terms review and a client identity in
        // the browser's own table, and no row carries a date today. This
        // asserts only that the catalog is not the thing standing in the way.
        let router = ProductionModelRouter::from_embedded_catalog().expect("the compiled catalog");
        let plane = catalog_providers(&router).expect("acceptable");
        for vendor in SIGN_IN_VENDORS {
            let row = plane
                .iter()
                .find(|projected| projected.provider_id.as_str() == vendor)
                .unwrap_or_else(|| {
                    panic!("{vendor} has a compiled sign-in flow and no catalog row at all")
                });
            assert!(
                row.auth_methods.contains(&ProviderAuthMethod::Oauth),
                "{vendor} has a compiled sign-in flow and its catalog row does not offer OAUTH, \
                 so nothing can reach the flow"
            );
        }
        // And the mirror, which fails in a person's hands rather than here: a
        // row offering a sign-in this binary cannot run would draw a control
        // that cannot start.
        for row in &plane {
            if row.auth_methods.contains(&ProviderAuthMethod::Oauth) {
                assert!(
                    sign_in_flow_compiled(row.provider_id.as_str()),
                    "{} offers OAUTH and this binary compiles no flow for it",
                    row.provider_id.as_str()
                );
            }
        }
    }
}
