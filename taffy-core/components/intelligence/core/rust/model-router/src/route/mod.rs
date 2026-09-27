// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Route selection: which placement a request is eligible for, and what the
//! user is told about it.
//!
//! Two routes are decided and both are here. Requests on the direct route go
//! from the device straight to the user's chosen provider, on a credential that
//! never leaves the device. Requests on the managed route go device, then the
//! product's edge worker, then the gateway, then a pinned provider. A third
//! route — generation on the device itself — is planned behind its own open
//! decision ([OD-037]) and is deliberately absent from [`Route`]: a variant
//! here would read as an implementation that does not exist.
//!
//! The rules this module exists to make unbreakable:
//!
//! - **A route never changes under a failure.** Every refusal is a refusal.
//!   There is no path from a failed credential to the managed route, because a
//!   silent one would move the user's page content across a boundary they chose
//!   not to cross.
//! - **Failover stays inside one disclosure class.** Candidates are filtered to
//!   the primary's class before the plan is built, so substituting a model can
//!   never substitute a data-handling story.
//! - **Nothing carries a secret.** A candidate carries a
//!   [`CredentialRef`][crate::credential::CredentialRef] and the header names
//!   the adapter will fill. The value is fetched at request-build time and
//!   never enters a plan, a disclosure, or a report.
//! - **A server somebody runs may need no key, and a vendor's API always
//!   does.** Those are two rules, written out separately and neither expressed
//!   in terms of the other, chosen between by the layer of the merge that named
//!   the address (decision 0096 section 2). Ollama out of the box wants no
//!   credential, so refusing a candidate for want of one would make the
//!   commonest self-hosted server the one thing selection cannot reach — while
//!   a catalog provider with nothing stored stays refused, because there is no
//!   such thing as reaching a vendor's API with nothing. Relaxing either rule
//!   leaves the other exactly where it was, which is the property that lets the
//!   catalog one stay as narrow as it is.
//! - **Prohibited material is refused before anything else.** It is neither
//!   model-transmittable nor persistable, on any route.
//! - **A model is never handed tools it cannot answer with.** A call that
//!   carries a tool vocabulary refuses a model whose descriptor says it cannot
//!   call tools, rather than sending the call without its tools and reading
//!   the resulting prose as a plan.
//! - **Ordering is total.** Candidates come from the assistant's per-role
//!   preference first and catalog key order after, so two devices with the same
//!   snapshot fail over to the same model in the same order.
//!
//! [OD-037]: ../../../../../../../../docs/open-decisions.md
//!
//! # How this module is laid out
//!
//! The selector is here; everything it decides *about* is a sibling module.
//!
//! | Module | Owns |
//! |---|---|
//! | [`request`] | The placement enumeration and what one call asks for |
//! | [`policy`] | Standing configuration: entitlement, preference, endpoint classification |
//! | [`disclosure`] | What leaves the device, to whom, and what the user is told |
//! | [`refusal`] | Every way selection says no |
//! | [`plan`] | The primary and its ordered substitutes |
//! | [`secret`] | Header names, which are protocol detail and never secret |

mod disclosure;
mod plan;
mod policy;
mod refusal;
mod request;
mod secret;

use std::collections::BTreeSet;

pub use self::disclosure::{Disclosure, DisclosureClass, EgressStatement, Recipient};
pub use self::plan::{RouteCandidate, RoutePlan};
pub use self::policy::{EndpointClassifier, ManagedEntitlement, ModelPolicy, NoLocalEndpoints};
pub use self::refusal::RouteRefusal;
pub use self::request::{Route, RoutePreference, RouteRequest};
pub use self::secret::{declared_methods, secret_header_names};

use crate::catalog::{CatalogLayer, Endpoint, MergedCatalog, Provider, ResolvedModel, WireApi};
use crate::cost::{CostAmount, TaskLedger, TokenUsage};
use crate::credential::{
    AuthAttachment, AuthType, CredentialDirectory, CredentialRef, CredentialState,
};
use crate::ids::ModelKey;
use crate::request::ContextManifest;
use crate::thinking::plan as plan_thinking;
use crate::wire::managed::{tools_travel_at, MANAGED_SCHEMA_VERSION};

/// What the managed wire says about a tool-carrying call at `schema_version`.
///
/// The wire's own answer, read here rather than restated: the predicate and
/// the constant behind it live beside [`MANAGED_SCHEMA_VERSION`] in
/// [`crate::wire::managed`], and the writer's own refusal reads the same one
/// (decision 0092). Two independent refusals are deliberate — the last place
/// that can say no must not rely on the first having said it — but two
/// independent *opinions* about which builds carry tools would be a device
/// that refuses a call its own writer would have written, or the reverse.
///
/// A free function rather than a method, so a version below the one that first
/// carried tools can be asked the same question a build below it would ask.
fn managed_tool_verdict(
    schema_version: u64,
    requires_tool_calling: bool,
) -> Result<(), RouteRefusal> {
    if requires_tool_calling && !tools_travel_at(schema_version) {
        return Err(RouteRefusal::ManagedToolCallingUnsupported);
    }
    Ok(())
}

/// Whether a tool-carrying call may be written on `wire_api`.
///
/// A family that seals its reasoning replays a tool loop only alongside the
/// sealed item that preceded each call, and the durable transcript carries no
/// reasoning — see [`RouteRefusal::ToolLoopCarriageMissing`] for what the
/// endpoint does about a broken pairing and why it is invisible.
///
/// Read off the dialect rather than from a list of family names, so a fifth
/// family that seals its reasoning is covered by the rule the day it is added
/// rather than the day somebody remembers this function. A prose call is
/// served on every family: the refusal is about the loop, never the route.
///
/// A free function for the same reason [`managed_tool_verdict`] is one — the
/// rule can be asked directly, by a test that holds no catalog.
fn tool_loop_carriage_verdict(
    wire_api: WireApi,
    requires_tool_calling: bool,
) -> Result<(), RouteRefusal> {
    if requires_tool_calling && crate::wire::seals_reasoning(wire_api) {
        return Err(RouteRefusal::ToolLoopCarriageMissing { wire_api });
    }
    Ok(())
}

/// The router.
pub struct RouteSelector<'a> {
    catalog: &'a MergedCatalog,
    credentials: &'a dyn CredentialDirectory,
    entitlement: &'a ManagedEntitlement,
    policy: &'a ModelPolicy,
    classifier: &'a dyn EndpointClassifier,
}

impl core::fmt::Debug for RouteSelector<'_> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("RouteSelector")
            .field("catalog_version", &self.catalog.header().catalog_version)
            .field("managed_available", &self.entitlement.available)
            .finish_non_exhaustive()
    }
}

impl<'a> RouteSelector<'a> {
    /// Builds a selector over one catalog snapshot and one device state.
    pub fn new(
        catalog: &'a MergedCatalog,
        credentials: &'a dyn CredentialDirectory,
        entitlement: &'a ManagedEntitlement,
        policy: &'a ModelPolicy,
        classifier: &'a dyn EndpointClassifier,
    ) -> Self {
        Self {
            catalog,
            credentials,
            entitlement,
            policy,
            classifier,
        }
    }

    /// Selects a route, a primary model, and its substitutes.
    pub fn select(
        &self,
        request: &RouteRequest,
        ledger: &TaskLedger,
    ) -> Result<RoutePlan, RouteRefusal> {
        if request.context.contains_prohibited() {
            return Err(RouteRefusal::ProhibitedMaterial);
        }
        let route = request.preference.route();
        let keys = self.ordered_keys(request)?;
        let mut candidates = Vec::new();
        let mut first_refusal: Option<RouteRefusal> = None;
        for key in keys {
            match self.build_candidate(&key, request, route) {
                Ok(candidate) => candidates.push(candidate),
                Err(refusal) => {
                    if first_refusal.is_none() {
                        first_refusal = Some(refusal);
                    }
                }
            }
        }
        let mut candidates = candidates.into_iter();
        let Some(primary) = candidates.next() else {
            return Err(first_refusal.unwrap_or(RouteRefusal::NoCandidate {
                role: request.role,
                route,
            }));
        };
        let class = primary.disclosure.class;
        let failover: Vec<RouteCandidate> = if request.pinned_model.is_some() {
            Vec::new()
        } else {
            candidates
                .filter(|candidate| candidate.disclosure.class == class)
                .take(crate::defaults::MAX_FAILOVER_CANDIDATES)
                .collect()
        };
        let estimate = self.estimate(&primary.model, request, ledger)?;
        self.check_quota(route, estimate)?;
        ledger
            .admit(crate::cost::CallKind::Initial, estimate)
            .map_err(RouteRefusal::Budget)?;
        Ok(RoutePlan {
            route,
            role: request.role,
            primary,
            failover,
            estimate,
        })
    }

    /// The candidate keys to consider, in order.
    fn ordered_keys(&self, request: &RouteRequest) -> Result<Vec<ModelKey>, RouteRefusal> {
        if let Some(pinned) = &request.pinned_model {
            self.catalog.resolve(pinned).map_err(RouteRefusal::Lookup)?;
            return Ok(vec![pinned.clone()]);
        }
        let mut keys: Vec<ModelKey> = Vec::new();
        let mut seen = BTreeSet::new();
        for preferred in self.policy.preferred(request.role) {
            if self.catalog.resolve(preferred).is_ok() && seen.insert(preferred.clone()) {
                keys.push(preferred.clone());
            }
        }
        for resolved in self.catalog.candidates_for_role(request.role) {
            let key = resolved.model.key();
            if seen.insert(key.clone()) {
                keys.push(key);
            }
        }
        Ok(keys)
    }

    fn build_candidate(
        &self,
        key: &ModelKey,
        request: &RouteRequest,
        route: Route,
    ) -> Result<RouteCandidate, RouteRefusal> {
        let resolved = self.catalog.resolve(key).map_err(RouteRefusal::Lookup)?;
        if !resolved.model.serves(request.role) {
            return Err(RouteRefusal::NoCandidate {
                role: request.role,
                route,
            });
        }
        for modality in &request.required_modalities {
            if !resolved.model.accepts(*modality) {
                return Err(RouteRefusal::ModalityUnsupported {
                    model: key.clone(),
                    modality: *modality,
                });
            }
        }
        if request.requires_tool_calling && !resolved.model.tool_calling {
            return Err(RouteRefusal::ToolCallingUnsupported { model: key.clone() });
        }
        // The credential decides which of a vendor's two ways in this call
        // takes, before anything is built from either. Decision 0029 section 2
        // keeps a vendor reachable both ways to one descriptor, so "which
        // family, at which address" is a question about the credential rather
        // than about the catalog — and reading the registry first is what
        // keeps a subscription request off the endpoint reserved for keys.
        let credential = match route {
            Route::ByoDirect => Some(self.credentials.credential(&resolved.provider.provider_id)),
            Route::Managed => None,
        };
        let method = credential.as_ref().map(|held| held.auth_type.method());
        let wire_api = method.map_or(resolved.wire_api, |held| resolved.wire_api_for(held));
        let endpoint = method.map_or(resolved.endpoint, |held| resolved.endpoint_for(held));
        // Asked after the credential has decided the family, because that is
        // when the family is known: the same model on the same row is one
        // wire on a key and another on a subscription, and only one of the two
        // has this problem. `tool_loop_carriage_verdict` holds the rule.
        tool_loop_carriage_verdict(wire_api, request.requires_tool_calling)?;
        // Held one credential is exactly the direct route, so the option is the
        // placement rather than a second copy of it. Which of the two direct
        // rules then answers is decided by the layer that named the address —
        // the fact the merge already recorded, never the address itself, which
        // would be a guess (decision 0096 section 2).
        let auth = if let Some(credential) = credential {
            match resolved.endpoint_layer {
                CatalogLayer::EmbeddedBaseline | CatalogLayer::RemoteOverlay => {
                    Some(catalog_auth(resolved.provider, wire_api, credential)?)
                }
                CatalogLayer::UserOverride => {
                    own_endpoint_auth(resolved.provider, wire_api, credential)?
                }
            }
        } else {
            self.check_managed(key, request.requires_tool_calling)?;
            None
        };
        let thinking = plan_thinking(
            resolved.wire_api,
            &resolved.model.thinking_levels,
            request.thinking,
            request.answer_tokens,
            resolved.model.max_output_tokens,
        )
        .map_err(RouteRefusal::Thinking)?;
        let class = self.class_of(route, resolved.provider, auth.as_ref(), endpoint);
        Ok(RouteCandidate {
            model: key.clone(),
            wire_api,
            endpoint: endpoint.clone(),
            endpoint_layer: resolved.endpoint_layer,
            thinking,
            tool_calling: resolved.model.tool_calling,
            disclosure: self.disclosure(
                route,
                class,
                &resolved,
                endpoint,
                auth.as_ref(),
                &request.context,
            ),
            auth,
        })
    }

    fn check_managed(
        &self,
        key: &ModelKey,
        requires_tool_calling: bool,
    ) -> Result<(), RouteRefusal> {
        if !self.entitlement.available {
            return Err(RouteRefusal::ManagedUnavailable);
        }
        // Before the entitlement lookup: the wire's limit holds for every
        // entitled model equally, so naming a model would misstate the reason.
        managed_tool_verdict(MANAGED_SCHEMA_VERSION, requires_tool_calling)?;
        if !self.entitlement.entitled_models.contains(key) {
            return Err(RouteRefusal::ModelNotEntitled { model: key.clone() });
        }
        Ok(())
    }

    fn check_quota(&self, route: Route, estimate: CostAmount) -> Result<(), RouteRefusal> {
        if route != Route::Managed {
            return Ok(());
        }
        let out_of_money = self
            .entitlement
            .remaining_micros
            .is_some_and(|remaining| remaining < estimate.micros);
        let out_of_calls = self
            .entitlement
            .remaining_calls
            .is_some_and(|remaining| remaining == 0);
        if out_of_money || out_of_calls {
            return Err(RouteRefusal::QuotaExhausted {
                remaining_micros: self.entitlement.remaining_micros,
                remaining_calls: self.entitlement.remaining_calls,
            });
        }
        Ok(())
    }

    fn estimate(
        &self,
        key: &ModelKey,
        request: &RouteRequest,
        ledger: &TaskLedger,
    ) -> Result<CostAmount, RouteRefusal> {
        let resolved = self.catalog.resolve(key).map_err(RouteRefusal::Lookup)?;
        let usage = TokenUsage {
            input: request.context.estimated_input_tokens,
            output: request.estimated_output_tokens,
            cache_read: 0,
            cache_write: 0,
        };
        ledger
            .estimate(&resolved.model.cost, usage)
            .map_err(RouteRefusal::Budget)
    }

    fn class_of(
        &self,
        route: Route,
        provider: &Provider,
        auth: Option<&AuthAttachment>,
        endpoint: &Endpoint,
    ) -> DisclosureClass {
        if route == Route::Managed {
            return DisclosureClass::Managed;
        }
        if self.classifier.is_local_endpoint(endpoint) {
            return DisclosureClass::LocalEndpointDirect;
        }
        // Nothing attached is itself an answer, and on this route it is only
        // ever one thing: a catalog candidate is refused before it reaches here
        // without a credential, so a direct call carrying none came from the
        // person's own layer. The two classes below each name what stands
        // behind the access — a plan, or a key — and neither is true of a
        // request that carries neither, so answering with one of them would put
        // a sentence about a credential in front of somebody whose page went
        // out without one.
        let Some(attachment) = auth else {
            return DisclosureClass::LocalEndpointDirect;
        };
        if attachment.credential.auth_type == AuthType::Oauth && provider.subscription {
            DisclosureClass::SubscriptionDirect
        } else {
            DisclosureClass::ApiKeyDirect
        }
    }

    fn disclosure(
        &self,
        route: Route,
        class: DisclosureClass,
        resolved: &ResolvedModel<'_>,
        endpoint: &Endpoint,
        auth: Option<&AuthAttachment>,
        context: &ContextManifest,
    ) -> Disclosure {
        // The endpoint the credential in play actually reaches, not the row's
        // default: a person told their page went to one host while it went to
        // another has been told something untrue, and this is the sentence
        // they are shown.
        let provider_recipient = Recipient::Provider {
            display_name: resolved.provider.display_name.clone(),
            host: endpoint.host().to_owned(),
        };
        let recipients = match route {
            Route::ByoDirect => vec![provider_recipient],
            Route::Managed => vec![
                Recipient::EdgeWorker {
                    host: self.entitlement.worker_host.clone(),
                },
                Recipient::Gateway {
                    host: self.entitlement.gateway_host.clone(),
                },
                provider_recipient,
            ],
        };
        let mut classes = context.classes.clone();
        classes.sort_unstable();
        classes.dedup();
        Disclosure {
            route,
            class,
            provider_display_name: resolved.provider.display_name.clone(),
            model_display_name: resolved.model.display_name.clone(),
            egress: EgressStatement {
                recipients,
                classes,
                source_count: u32::try_from(context.source_ids.len()).unwrap_or(u32::MAX),
            },
            price_basis: resolved.model.cost.basis,
            price_snapshot_version: resolved.model.cost.snapshot_version.clone(),
            credential: auth.map(|attachment| attachment.credential.clone()),
        }
    }
}

/// The credential a call to a **catalog** address goes out on, and the headers
/// that carry it.
///
/// A free function, and it takes the credential rather than fetching one: the
/// caller already read the registry to decide which of a vendor's two ways in
/// this call takes, and a second read could answer differently — leaving a
/// request built for one family authenticated for the other.
///
/// Nothing stored is a refusal and stays one. A published document names a
/// vendor's API; every catalog provider declares a method, every method is a
/// credential, and a request sent to one of those addresses with none is a
/// round trip spent learning what the device already knew. This is the rule
/// [`own_endpoint_auth`] is deliberately not written in terms of.
fn catalog_auth(
    provider: &Provider,
    wire_api: WireApi,
    credential: CredentialRef,
) -> Result<AuthAttachment, RouteRefusal> {
    match credential.state {
        CredentialState::Absent => {
            return Err(RouteRefusal::CredentialMissing {
                provider_id: provider.provider_id.clone(),
            })
        }
        CredentialState::NeedsSignIn | CredentialState::RefreshFailed => {
            return Err(RouteRefusal::CredentialNeedsAttention {
                provider_id: provider.provider_id.clone(),
                state: credential.state,
            })
        }
        CredentialState::Usable => {}
    }
    if !provider
        .auth_methods
        .contains(&credential.auth_type.method())
    {
        return Err(RouteRefusal::AuthMethodUnsupported {
            provider_id: provider.provider_id.clone(),
            auth_type: credential.auth_type,
        });
    }
    Ok(AuthAttachment {
        secret_header_names: secret_header_names(wire_api, credential.auth_type),
        static_header_names: provider.static_headers.keys().cloned().collect(),
        credential,
    })
}

/// The credential a call to an address **the person supplied** goes out on,
/// when that address has one.
///
/// `None` is a complete answer and it is the ordinary one. A person's own
/// endpoint is reached with a key or with nothing, and which of the two is what
/// they said when they saved it: `SaveCustomProviderCommand`'s credential
/// handle is optional, "absent for an endpoint that needs no credential", and
/// Ollama out of the box is the endpoint that needs none. So nothing filed
/// against a provider they defined is their configuration rather than a gap in
/// it, and refusing it would make the single most common self-hosted server the
/// one address selection cannot reach.
///
/// Written out rather than deferring to [`catalog_auth`] for the states the two
/// happen to agree on. Agreeing today is not the property this needs: the point
/// of two rules is that relaxing one leaves the other standing, and here the
/// direction that matters is the one nobody would see. A stored credential that
/// has stopped working must never be read as "this endpoint needs none",
/// because that turns *your key stopped working* into *your pages are going to
/// your server unauthenticated* — a downgrade with no refusal to notice and no
/// sentence to read. So a credential that exists and is not usable is refused
/// here exactly as a vendor's is, and only an absent one admits.
fn own_endpoint_auth(
    provider: &Provider,
    wire_api: WireApi,
    credential: CredentialRef,
) -> Result<Option<AuthAttachment>, RouteRefusal> {
    match credential.state {
        CredentialState::Absent => return Ok(None),
        CredentialState::NeedsSignIn | CredentialState::RefreshFailed => {
            return Err(RouteRefusal::CredentialNeedsAttention {
                provider_id: provider.provider_id.clone(),
                state: credential.state,
            })
        }
        CredentialState::Usable => {}
    }
    if !provider
        .auth_methods
        .contains(&credential.auth_type.method())
    {
        return Err(RouteRefusal::AuthMethodUnsupported {
            provider_id: provider.provider_id.clone(),
            auth_type: credential.auth_type,
        });
    }
    Ok(Some(AuthAttachment {
        secret_header_names: secret_header_names(wire_api, credential.auth_type),
        static_header_names: provider.static_headers.keys().cloned().collect(),
        credential,
    }))
}

#[cfg(test)]
mod tests {
    use super::{managed_tool_verdict, tool_loop_carriage_verdict, RouteRefusal, WireApi};
    use crate::wire::managed::{MANAGED_SCHEMA_VERSION, MANAGED_TOOL_SCHEMA_VERSION};

    /// The version below the one that first carried tools, named rather than
    /// written as a number so this keeps meaning "below" after a bump.
    const BELOW: u64 = MANAGED_TOOL_SCHEMA_VERSION - 1;

    #[test]
    fn a_build_below_the_tool_version_refuses_a_tool_carrying_call_by_the_wire() {
        assert_eq!(
            managed_tool_verdict(BELOW, true),
            Err(RouteRefusal::ManagedToolCallingUnsupported)
        );
        // A prose call is served on every version: the refusal is about the
        // tools, never about the route.
        assert_eq!(managed_tool_verdict(BELOW, false), Ok(()));
        assert_eq!(managed_tool_verdict(MANAGED_SCHEMA_VERSION, true), Ok(()));
    }

    #[test]
    fn a_family_that_seals_its_reasoning_refuses_a_tool_loop_and_serves_prose() {
        // The subscription responses family pairs each replayed tool call with
        // the sealed reasoning item before it, and nothing durable here keeps
        // that item. Refused by name rather than sent, because the endpoint
        // answers a broken pairing by closing the connection before headers —
        // which reads as a stream that never started.
        assert_eq!(
            tool_loop_carriage_verdict(WireApi::OpenAiCodexResponses, true),
            Err(RouteRefusal::ToolLoopCarriageMissing {
                wire_api: WireApi::OpenAiCodexResponses
            })
        );
        // Prose is served on the same family: the refusal is about the loop.
        assert_eq!(
            tool_loop_carriage_verdict(WireApi::OpenAiCodexResponses, false),
            Ok(())
        );
        // And it is the carriage that decides, not the vendor: the platform
        // responses family is the same shape at a different address and asks
        // for no sealed reasoning, so a tool loop is written there as before.
        for api in [
            WireApi::OpenAiResponses,
            WireApi::OpenAiCompletions,
            WireApi::AnthropicMessages,
            WireApi::GoogleGenerativeLanguage,
        ] {
            assert_eq!(tool_loop_carriage_verdict(api, true), Ok(()), "{api:?}");
        }
    }
}
