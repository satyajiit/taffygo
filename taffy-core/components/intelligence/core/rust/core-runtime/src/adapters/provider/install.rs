// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Installing what the provider plane holds into the router.
//!
//! This is the join the two halves were missing. `model-router` has carried a
//! credential directory, a three-layer catalog and an endpoint registry since
//! it was written, and nothing ever filled any of them: the directory started
//! empty, stayed empty, and every direct route was refused for want of a
//! credential — the same refusal a provider nobody configured gets. The
//! provider plane has the facts; this module hands them over.

use std::collections::BTreeMap;

use model_router::catalog::{Endpoint, ModelRole};
use model_router::credential::{AuthType, CredentialRef};
use model_router::route::ModelPolicy;
use model_router::ModelKey;

use crate::ports::ModelRouterPort;
use crate::provider::{ProviderAuthMethod, ProviderCredential, ProviderProtocol};

/// The router's spelling of one authentication method.
pub const fn router_auth_type(method: ProviderAuthMethod) -> AuthType {
    match method {
        ProviderAuthMethod::ApiKey => AuthType::ApiKey,
        ProviderAuthMethod::Oauth => AuthType::Oauth,
    }
}

/// One credential as metadata the router may hold.
///
/// `CredentialRef` has no field that could carry a secret, which is why this
/// conversion is total: there is nothing to leave out.
pub fn credential_metadata(credential: &ProviderCredential) -> CredentialRef {
    CredentialRef {
        provider_id: credential.provider_id.as_router().clone(),
        auth_type: router_auth_type(credential.auth_method),
        state: credential.state,
        subscription_backed: credential.auth_method == ProviderAuthMethod::Oauth,
        created_at: None,
        rotated_at: None,
    }
}

/// Every endpoint a person runs themselves.
pub fn local_endpoints(plane: &ProviderProtocol) -> Vec<Endpoint> {
    plane.custom_endpoints()
}

/// The per-role candidate order the person's own choices come to (decision
/// 0093).
///
/// A pinned model leads every role the catalog files it for, and nothing else
/// is stated: the rest of the order is the catalog's, which is where the
/// router takes it from when a role has no preference. Two pins that serve one
/// role are ordered by their provider's identity, so a device with two chosen
/// models fails over between them the same way on every run.
///
/// This is the seam decision 0082 named and left open. The empty policy that
/// stood here until now was honest while nothing could express a choice; it
/// stops being honest the moment something can.
pub fn model_policy(plane: &ProviderProtocol) -> ModelPolicy {
    let mut ordered: BTreeMap<ModelRole, Vec<ModelKey>> = BTreeMap::new();
    for model in plane.pinned_models() {
        for role in &model.roles {
            ordered.entry(*role).or_default().push(model.key());
        }
    }
    let mut policy = ModelPolicy::new();
    for (role, order) in ordered {
        policy.set(role, order);
    }
    policy
}

/// Installs everything the plane holds into one router.
///
/// Called after every change rather than at bootstrap alone. Routing is a pure
/// function of what is installed, so a router that was not told about a change
/// keeps answering from the state it was built with, and the person sees a
/// credential they saved refused as missing.
///
/// The policy is derived here rather than held, so it is recomputed by every
/// caller of this function — every accepted provider command and every
/// accepted catalog refresh — and a pin the refresh dropped stops leading a
/// role in the same call that dropped it.
pub fn install_provider_state(plane: &ProviderProtocol, router: &mut dyn ModelRouterPort) {
    router.replace_credentials(plane.credentials().map(credential_metadata).collect());
    router.replace_local_endpoints(local_endpoints(plane));
    router.set_policy(model_policy(plane));
}

#[cfg(test)]
mod tests {
    use model_router::catalog::CatalogLayer;
    use model_router::catalog::{InputModality, ModelRole};
    use model_router::credential::CredentialState;
    use model_router::money::{Currency, Micros};
    use model_router::request::{ContextManifest, DataSensitivity, RequestPurpose};
    use model_router::route::{DisclosureClass, RoutePreference, RouteRequest};
    use model_router::{RouteRefusal, TaskBudget, TaskId, TaskLedger, ThinkingLevel};

    use super::*;
    use crate::adapters::model::ProductionModelRouter;
    use crate::provider::{CatalogProvider, CredentialHandle, ProviderId, ProviderPresentation};

    fn plane_over(provider: &str) -> ProviderProtocol {
        ProviderProtocol::new(
            [CatalogProvider {
                presentation: ProviderPresentation::default(),
                provider_id: ProviderId::new(provider).expect("valid"),
                display_name: provider.to_owned(),
                auth_methods: vec![ProviderAuthMethod::ApiKey],
                enabled: true,
                layer: CatalogLayer::EmbeddedBaseline,
                configurable: true,
                subscription: false,
                endpoint_changed: false,
                refused_endpoint_host: None,
            }],
            [],
        )
    }

    fn request() -> RouteRequest {
        RouteRequest {
            task_id: TaskId::from_bytes([1; 16]),
            role: ModelRole::PrimaryReasoning,
            preference: RoutePreference::ByoDirect,
            purpose: RequestPurpose::Planning,
            context: ContextManifest {
                source_ids: Vec::new(),
                classes: vec![DataSensitivity::Public],
                item_count: 1,
                estimated_input_tokens: 100,
            },
            required_modalities: vec![InputModality::Text],
            // Planning is tool-driven, so this is the precondition a real
            // planning call carries.
            requires_tool_calling: true,
            thinking: ThinkingLevel::Medium,
            answer_tokens: 500,
            estimated_output_tokens: 200,
            pinned_model: None,
        }
    }

    /// A ledger whose budget cannot be the reason a route is refused.
    fn ledger() -> TaskLedger {
        TaskLedger::new(
            TaskId::from_bytes([7; 16]),
            TaskBudget {
                currency: Currency::new("USD").expect("currency"),
                max_micros: Micros::new(10_000_000),
                max_calls: 32,
                max_retries: 4,
            },
        )
    }

    /// The provider the compiled baseline files a direct reasoning route under.
    fn refused_provider(router: &mut ProductionModelRouter) -> String {
        match router.route(&request(), &ledger()) {
            Err(RouteRefusal::CredentialMissing { provider_id }) => provider_id.as_str().to_owned(),
            other => panic!("expected a missing credential before install, got {other:?}"),
        }
    }

    #[test]
    fn a_direct_route_is_refused_until_a_credential_is_installed() {
        // This is the defect the whole plane exists to close: a person saves a
        // key, the key is genuinely on the disk, and the router answers
        // CredentialMissing because nothing ever told it.
        let mut router =
            ProductionModelRouter::from_embedded_catalog().expect("the compiled catalog");
        let provider = refused_provider(&mut router);

        let mut plane = plane_over(&provider);
        plane
            .save_credential(
                ProviderId::new(&provider).expect("valid"),
                ProviderAuthMethod::ApiKey,
                CredentialHandle::new("handle-1").expect("valid"),
            )
            .expect("the baseline offers a key for this provider");
        install_provider_state(&plane, &mut router);

        let plan = router
            .route(&request(), &ledger())
            .expect("the credential is installed, so the route resolves");
        assert_eq!(
            plan.disclosure_class(),
            DisclosureClass::ApiKeyDirect,
            "a key the person supplied is metered access on the direct route"
        );
    }

    #[test]
    fn removing_a_credential_refuses_the_route_again() {
        // replace_credentials replaces rather than merges, so a credential the
        // person removed has to stop routing. A merge would leave it behind and
        // the route would keep working after they revoked the key.
        let mut router =
            ProductionModelRouter::from_embedded_catalog().expect("the compiled catalog");
        let provider = refused_provider(&mut router);
        let id = ProviderId::new(&provider).expect("valid");

        let mut plane = plane_over(&provider);
        plane
            .save_credential(
                id.clone(),
                ProviderAuthMethod::ApiKey,
                CredentialHandle::new("handle-1").expect("valid"),
            )
            .expect("saved");
        install_provider_state(&plane, &mut router);
        assert!(router.route(&request(), &ledger()).is_ok());

        plane.forget_credential(&id).expect("held one");
        install_provider_state(&plane, &mut router);
        assert!(
            matches!(
                router.route(&request(), &ledger()),
                Err(RouteRefusal::CredentialMissing { .. })
            ),
            "a revoked key must stop routing"
        );
    }

    #[test]
    fn a_credential_needing_attention_is_reported_as_such_rather_than_as_missing() {
        // The two refusals send a person to different places: one says set a
        // key up, the other says the one you have needs renewing. Flattening
        // them would send somebody to re-enter a key that is already correct.
        let mut router =
            ProductionModelRouter::from_embedded_catalog().expect("the compiled catalog");
        let provider = refused_provider(&mut router);
        let id = ProviderId::new(&provider).expect("valid");

        let mut plane = plane_over(&provider);
        plane
            .save_credential(
                id.clone(),
                ProviderAuthMethod::ApiKey,
                CredentialHandle::new("handle-1").expect("valid"),
            )
            .expect("saved");
        let mut entries: Vec<_> = plane.credentials().map(credential_metadata).collect();
        for entry in &mut entries {
            entry.state = CredentialState::NeedsSignIn;
        }
        router.replace_credentials(entries);

        assert!(matches!(
            router.route(&request(), &ledger()),
            Err(RouteRefusal::CredentialNeedsAttention {
                state: CredentialState::NeedsSignIn,
                ..
            })
        ));
    }

    #[test]
    fn a_persons_own_endpoints_reach_the_router() {
        use model_router::catalog::Endpoint as CatalogEndpoint;

        use crate::provider::{ProviderDisplayName, ProviderWireApi, SavedCustomProvider};

        let mut plane = plane_over("anthropic");
        plane
            .save_custom_provider(SavedCustomProvider {
                provider_id: ProviderId::new("my-gateway").expect("valid"),
                display_name: ProviderDisplayName::new("My gateway").expect("valid"),
                endpoint: CatalogEndpoint::new("https://gateway.example/v1").expect("valid"),
                wire_api: ProviderWireApi::OpenAiCompletions,
                credential: None,
                models: Vec::new(),
                detected_server: None,
            })
            .expect("defined");
        assert_eq!(
            local_endpoints(&plane),
            vec![CatalogEndpoint::new("https://gateway.example/v1").expect("valid")]
        );
    }

    #[test]
    fn a_subscription_credential_is_marked_plan_backed_for_the_router() {
        let credential = ProviderCredential {
            provider_id: ProviderId::new("xai").expect("valid"),
            auth_method: ProviderAuthMethod::Oauth,
            handle: CredentialHandle::new("handle-1").expect("valid"),
            state: CredentialState::Usable,
        };
        let metadata = credential_metadata(&credential);
        assert!(metadata.subscription_backed);
        assert_eq!(metadata.auth_type, AuthType::Oauth);
        assert_eq!(metadata.provider_id.as_str(), "xai");
    }
}
