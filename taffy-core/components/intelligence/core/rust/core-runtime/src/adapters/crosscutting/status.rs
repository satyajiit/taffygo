// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The status contributors, re-homed from the composition root.
//!
//! The first two bodies are the two `apply_*` methods the profile runtime
//! used to carry, unchanged, which is what proves the seam: with only those
//! two registered, the encoded status payload is byte-identical to the tree
//! before decision 0073. The provider roster joined at Core API 3.10 through
//! the same seam, because the provider plane is profile state the runtime's
//! own projection does not own.

use model_router::{CredentialState, ThinkingLevel};

use crate::account::AccountAuthMethod;
use crate::ports::{StatusContributionFacts, StatusContributorPort};
use crate::probe::{ProbeEndpoint, ProbeVerdict};
use model_router::catalog::{CatalogLayer, InputModality, ModelRole};
use model_router::wire::ServerKind;

use crate::provider::{
    CatalogModel, ProviderAuthMethod, ProviderModelPreference, ProviderOrigin,
    ProviderPresentation, ProviderRefusal, ProviderRefusalKind, StoredCredentialView,
};

/// Marks each account method the installation can start as available.
#[derive(Clone, Copy, Debug, Default)]
pub struct ProductionAccountMethods;

impl StatusContributorPort for ProductionAccountMethods {
    fn contribute(
        &self,
        facts: &StatusContributionFacts<'_>,
        status: &mut core_api_types::CoreStatus,
    ) {
        let Some(auth) = status.auth_state.as_mut() else {
            return;
        };
        auth.methods = [
            (
                AccountAuthMethod::Google,
                core_api_types::AuthProvider::Google,
            ),
            (
                AccountAuthMethod::EmailLink,
                core_api_types::AuthProvider::EmailLink,
            ),
            (
                AccountAuthMethod::Github,
                core_api_types::AuthProvider::Github,
            ),
            (
                AccountAuthMethod::Facebook,
                core_api_types::AuthProvider::Facebook,
            ),
        ]
        .into_iter()
        .map(|(method, provider)| core_api_types::AuthMethodView {
            provider,
            availability: if facts.available_account_methods.contains(&method) {
                core_api_types::AuthMethodAvailability::Available
            } else {
                core_api_types::AuthMethodAvailability::NotConfigured
            },
        })
        .collect();
    }
}

/// Shows the classified ask prompt on each task that is waiting on the person.
#[derive(Clone, Copy, Debug, Default)]
pub struct ProductionAskPrompts;

impl StatusContributorPort for ProductionAskPrompts {
    fn contribute(
        &self,
        facts: &StatusContributionFacts<'_>,
        status: &mut core_api_types::CoreStatus,
    ) {
        for task in &mut status.active_tasks {
            task.pending_ask_prompt = (task.phase == core_api_types::TaskPhase::WaitingForUser)
                .then(|| facts.ask_prompts.get(&task.task_id).cloned())
                .flatten();
        }
    }
}

/// Projects the provider plane's roster into the status payload.
///
/// A field-for-field map with one deliberate absence: a stored view whose
/// state claims `Absent` projects no stored record at all, because the wire
/// enum is closed at the three states a record can honestly hold and absence
/// travels as the optional record itself (Core API 3.10).
///
/// One field is not a map of anything on the plane's view: `model_count` was
/// counted eagerly from the same mutation's whole merged catalog, before the
/// flat model list was fitted to its budget (Core API 3.19). A publication
/// therefore copies a stable answer rather than repeating either operation.
#[derive(Clone, Copy, Debug, Default)]
pub struct ProductionProviderRoster;

impl StatusContributorPort for ProductionProviderRoster {
    fn contribute(
        &self,
        facts: &StatusContributionFacts<'_>,
        status: &mut core_api_types::CoreStatus,
    ) {
        status.provider_roster = facts
            .provider_status
            .roster()
            .map(|(view, model_count)| core_api_types::ProviderRosterEntry {
                provider_id: view.provider_id.as_str().to_owned(),
                display_name: view.display_name.clone(),
                origin: match view.origin {
                    ProviderOrigin::Catalog => core_api_types::ProviderOriginView::Catalog,
                    ProviderOrigin::Custom => core_api_types::ProviderOriginView::Custom,
                },
                auth_methods: view
                    .auth_methods
                    .iter()
                    .map(|method| auth_view(*method))
                    .collect(),
                stored: view.stored.as_ref().and_then(stored_view),
                signing_in: view.signing_in,
                enabled: view.enabled,
                endpoint_host: view.endpoint_host.clone(),
                endpoint_base: view.endpoint_base.clone(),
                last_refusal: view.last_refusal.map(refusal_view),
                configurable: view.configurable,
                subscription: view.subscription,
                endpoint_changed: view.endpoint_changed,
                // Disclosure of the host the guard refused, bounded here
                // because the served document chose it and a host that does
                // not fit the row's bound must not make the whole status
                // unencodable. The flag above is the fact and stays true
                // either way; only the name is withheld when it does not fit.
                refused_endpoint_host: view
                    .refused_endpoint_host
                    .as_deref()
                    .filter(|host| host.len() <= core_api_types::MAX_SOURCE_HOST_BYTES)
                    .map(str::to_owned),
                catalog_layer: layer_view(view.catalog_layer),
                selected_model_id: view
                    .preference
                    .model_id
                    .as_ref()
                    .map(|model_id| model_id.as_str().to_owned()),
                thinking: thinking_view(&view.preference),
                presentation: presentation_view(&view.presentation),
                model_count,
            })
            .collect();
    }
}

/// Projects the models a person picks from, flat and bounded (decision 0093).
///
/// One list across every provider rather than a list under each roster row.
/// The bound is the whole list's, which is the only place it can be: per
/// provider, one connected account with a long catalog could push the payload
/// past its limit and cost every other provider its place in the snapshot.
/// Past the bound the list is cut rather than the projection refused, because
/// a screen showing the models the catalog files is a working screen and a
/// withheld status is not.
///
/// **Which** models are cut is the part that had to change. The list arrives
/// in provider identity order, so cutting it at the bound spent the whole
/// budget on the identities that sort first and left every provider after them
/// with nothing — not a short roster, an empty one, indistinguishable on a
/// screen from a provider that genuinely carries no models. That was
/// theoretical while a custom endpoint saved one placeholder row. It stopped
/// being theoretical at Core API 3.18, which writes a probe's whole model list:
/// `MAX_CUSTOM_PROVIDERS` × `MAX_CUSTOM_MODEL_ENTRIES` is 1024 rows against a
/// 256-row surface, so a person with a few well-stocked endpoints could empty
/// the pickers of the rest without a word.
///
/// So the eager provider projection deals the budget out a round at a time,
/// one row per provider per round. Nobody is emptied while somebody else keeps
/// everything, the rows the cut takes are the tail rows of the longest lists,
/// and a provider carrying two models keeps both however long the aggregator
/// beside it is.
///
/// **How many** rows a provider lost is the roster's answer, not this list's.
/// Core API 3.19 puts `model_count` on `ProviderRosterEntry` — what the merged
/// catalog carries for that provider, counted before this deal spends
/// anything — so a surface subtracts the provider's rows in `provider_models`
/// from its `model_count` and knows exactly what is missing and whose.
/// `ProbeEndpointView` has kept the same pair for the same reason since 3.18
/// (decisions 0096 section 5 and 0098 section 4 both refuse collapsing the
/// two); the roster had no such pair until now, and the only signal was that a
/// list of exactly `MAX_PROVIDER_MODEL_ENTRIES` rows — what the deal always
/// spends when it cuts — might be short of somebody's models. That signal
/// still holds and is still checked, but it is no longer all there is.
#[derive(Clone, Copy, Debug, Default)]
pub struct ProductionProviderModels;

impl StatusContributorPort for ProductionProviderModels {
    fn contribute(
        &self,
        facts: &StatusContributionFacts<'_>,
        status: &mut core_api_types::CoreStatus,
    ) {
        status.provider_models = facts
            .provider_status
            .models()
            .iter()
            .map(model_view)
            .collect();
    }
}

/// One model, projected member for member with the contract's.
fn model_view(model: &CatalogModel) -> core_api_types::ProviderModelView {
    core_api_types::ProviderModelView {
        provider_id: model.provider_id.as_str().to_owned(),
        model_id: model.model_id.as_str().to_owned(),
        display_name: model.display_name.clone(),
        context_window: model.context_window,
        max_output_tokens: model.max_output_tokens,
        reasoning: model.reasoning,
        tool_calling: model.tool_calling,
        roles: model.roles.iter().map(|role| role_view(*role)).collect(),
        input_modalities: model
            .input_modalities
            .iter()
            .map(|modality| modality_view(*modality))
            .collect(),
        thinking_levels: model
            .thinking_levels
            .iter()
            .map(|level| thinking_level_view(*level))
            .collect(),
    }
}

/// The catalog's setup facts, absent when it carries none.
///
/// A row saying nothing projects no record at all rather than a record of
/// three absences: the difference matters to a surface, which draws a setup
/// section only when there is something in it.
fn presentation_view(
    presentation: &ProviderPresentation,
) -> Option<core_api_types::ProviderPresentationView> {
    if presentation.is_empty() {
        return None;
    }
    Some(core_api_types::ProviderPresentationView {
        key_prefix: presentation.key_prefix.clone(),
        get_key_url: presentation.get_key_url.clone(),
        docs_url: presentation.docs_url.clone(),
    })
}

/// The rung a person asked for, absent when nobody asked.
///
/// The record is what carries the difference: no record is "Taffy decides",
/// and a record naming `OFF` is a person asking for no thinking phase.
fn thinking_view(
    preference: &ProviderModelPreference,
) -> Option<core_api_types::ThinkingPreferenceView> {
    preference
        .thinking
        .map(|level| core_api_types::ThinkingPreferenceView {
            level: thinking_level_view(level),
        })
}

const fn thinking_level_view(level: ThinkingLevel) -> core_api_types::ThinkingLevelView {
    match level {
        ThinkingLevel::Off => core_api_types::ThinkingLevelView::Off,
        ThinkingLevel::Minimal => core_api_types::ThinkingLevelView::Minimal,
        ThinkingLevel::Low => core_api_types::ThinkingLevelView::Low,
        ThinkingLevel::Medium => core_api_types::ThinkingLevelView::Medium,
        ThinkingLevel::High => core_api_types::ThinkingLevelView::High,
        ThinkingLevel::XHigh => core_api_types::ThinkingLevelView::Xhigh,
        ThinkingLevel::Max => core_api_types::ThinkingLevelView::Max,
    }
}

const fn role_view(role: ModelRole) -> core_api_types::ModelRoleView {
    match role {
        ModelRole::PrimaryReasoning => core_api_types::ModelRoleView::PrimaryReasoning,
        ModelRole::FastBrowsing => core_api_types::ModelRoleView::FastBrowsing,
        ModelRole::Vision => core_api_types::ModelRoleView::Vision,
        ModelRole::Embedding => core_api_types::ModelRoleView::Embedding,
    }
}

const fn modality_view(modality: InputModality) -> core_api_types::InputModalityView {
    match modality {
        InputModality::Text => core_api_types::InputModalityView::Text,
        InputModality::Image => core_api_types::InputModalityView::Image,
    }
}

/// Projects the held entitlement summary into the auth state.
///
/// The runtime's own projection leaves `entitlement` absent, because the
/// summary is profile state — it lives with the entitlement refresh protocol
/// and dies with the signed-in session — and this contributor is its one
/// writer (decision 0073). No auth state, nothing to decorate: an entitlement
/// is the signed-in account's answer, and a payload with no auth block has no
/// account to say it about.
#[derive(Clone, Copy, Debug, Default)]
pub struct ProductionEntitlementView;

impl StatusContributorPort for ProductionEntitlementView {
    fn contribute(
        &self,
        facts: &StatusContributionFacts<'_>,
        status: &mut core_api_types::CoreStatus,
    ) {
        let Some(auth) = status.auth_state.as_mut() else {
            return;
        };
        auth.entitlement = facts
            .entitlement
            .map(|display| core_api_types::EntitlementView {
                plan_id: display.plan_id.clone(),
                credits_granted: display.credits_granted,
                credits_remaining: display.credits_remaining,
                next_renewal_epoch_seconds: display.next_renewal_epoch_seconds,
                valid_until_epoch_seconds: display.valid_until_epoch_seconds,
            });
    }
}

/// Projects the latest probe verdict per provider into the status.
///
/// The runtime's own projection leaves `provider_probes` empty, because the
/// verdicts are profile state — they live with the probe protocol and this
/// contributor is their one writer (decision 0083). Unlike the entitlement,
/// a verdict needs no auth block: a person probes a key signed out.
#[derive(Clone, Copy, Debug, Default)]
pub struct ProductionProviderProbes;

impl StatusContributorPort for ProductionProviderProbes {
    fn contribute(
        &self,
        facts: &StatusContributionFacts<'_>,
        status: &mut core_api_types::CoreStatus,
    ) {
        status.provider_probes = facts
            .provider_probes
            .iter()
            .map(|display| core_api_types::ProviderProbeView {
                provider_id: display.provider_id.clone(),
                verdict: probe_verdict_view(display.verdict),
                at_monotonic_ms: display.at_monotonic_ms,
                // A key probe proves a credential, not a server: it sends one
                // bounded completion to the provider the person named
                // (decision 0083) and learns nothing about what kind of
                // endpoint answered or how many models it lists. Only an
                // endpoint probe files a shape, which is why this is an
                // absence rather than a record of absences.
                endpoint: display.endpoint.as_ref().map(endpoint_view),
            })
            .collect();
    }
}

/// What one endpoint probe read, projected for a setup surface.
///
/// The count and the list stay apart across the seam. The count is what the
/// server named; the list is what survived `MAX_CUSTOM_MODEL_ENTRIES`, and the
/// list is cut here rather than the projection refused, because a screen
/// showing the first thirty-two models is a working screen.
fn endpoint_view(endpoint: &ProbeEndpoint) -> core_api_types::ProbeEndpointView {
    core_api_types::ProbeEndpointView {
        server_kind: server_kind_view(endpoint.server_kind),
        model_count: endpoint.model_count,
        models: endpoint
            .models
            .iter()
            .take(core_api_types::MAX_CUSTOM_MODEL_ENTRIES)
            .map(|model| core_api_types::CustomModelSpecView {
                model_id: model.model_id.as_str().to_owned(),
                display_name: model.display_name.clone(),
                context_window: model.context_window,
                max_output_tokens: model.max_output_tokens,
                reasoning: model.reasoning,
                tool_calling: model.tool_calling,
            })
            .collect(),
        proved_base: endpoint.proved_base.clone(),
    }
}

const fn server_kind_view(kind: ServerKind) -> core_api_types::ServerKindView {
    match kind {
        ServerKind::OpenAiCompatible => core_api_types::ServerKindView::OpenaiCompatible,
        ServerKind::Ollama => core_api_types::ServerKindView::Ollama,
        ServerKind::LmStudio => core_api_types::ServerKindView::LmStudio,
        ServerKind::Vllm => core_api_types::ServerKindView::Vllm,
        ServerKind::LlamaCpp => core_api_types::ServerKindView::LlamaCpp,
    }
}

const fn probe_verdict_view(verdict: ProbeVerdict) -> core_api_types::ProviderProbeVerdictView {
    match verdict {
        ProbeVerdict::Usable => core_api_types::ProviderProbeVerdictView::Usable,
        ProbeVerdict::Auth => core_api_types::ProviderProbeVerdictView::Auth,
        ProbeVerdict::Billing => core_api_types::ProviderProbeVerdictView::Billing,
        ProbeVerdict::RateLimit => core_api_types::ProviderProbeVerdictView::RateLimit,
        ProbeVerdict::Overloaded => core_api_types::ProviderProbeVerdictView::Overloaded,
        ProbeVerdict::Timeout => core_api_types::ProviderProbeVerdictView::Timeout,
        ProbeVerdict::Network => core_api_types::ProviderProbeVerdictView::Network,
        ProbeVerdict::ModelNotFound => core_api_types::ProviderProbeVerdictView::ModelNotFound,
        ProbeVerdict::Unknown => core_api_types::ProviderProbeVerdictView::Unknown,
        ProbeVerdict::EndpointReached => core_api_types::ProviderProbeVerdictView::EndpointReached,
        ProbeVerdict::NoModelListed => core_api_types::ProviderProbeVerdictView::NoModelListed,
    }
}

/// The last vendor refusal, projected member for member with the contract's.
const fn refusal_view(refusal: ProviderRefusal) -> core_api_types::ProviderRefusalStateView {
    let kind = match refusal.kind {
        ProviderRefusalKind::RateLimit => core_api_types::ProviderRefusalView::RateLimit,
        ProviderRefusalKind::Billing => core_api_types::ProviderRefusalView::Billing,
        ProviderRefusalKind::Overloaded => core_api_types::ProviderRefusalView::Overloaded,
    };
    core_api_types::ProviderRefusalStateView {
        refusal: kind,
        at_monotonic_ms: refusal.at_monotonic_ms,
    }
}

const fn layer_view(layer: CatalogLayer) -> core_api_types::CatalogLayerView {
    match layer {
        CatalogLayer::EmbeddedBaseline => core_api_types::CatalogLayerView::EmbeddedBaseline,
        CatalogLayer::RemoteOverlay => core_api_types::CatalogLayerView::RemoteOverlay,
        CatalogLayer::UserOverride => core_api_types::CatalogLayerView::UserOverride,
    }
}

const fn auth_view(method: ProviderAuthMethod) -> core_api_types::ProviderAuthMethodView {
    match method {
        ProviderAuthMethod::ApiKey => core_api_types::ProviderAuthMethodView::ApiKey,
        ProviderAuthMethod::Oauth => core_api_types::ProviderAuthMethodView::Oauth,
    }
}

fn stored_view(stored: &StoredCredentialView) -> Option<core_api_types::StoredCredentialView> {
    let state = match stored.state {
        CredentialState::Absent => return None,
        CredentialState::Usable => core_api_types::ProviderCredentialStateView::Usable,
        CredentialState::NeedsSignIn => core_api_types::ProviderCredentialStateView::NeedsSignIn,
        CredentialState::RefreshFailed => {
            core_api_types::ProviderCredentialStateView::RefreshFailed
        }
    };
    Some(core_api_types::StoredCredentialView {
        auth_method: auth_view(stored.auth_method),
        state,
        subscription_backed: stored.subscription_backed,
        // The vendor's own words about the account behind a credential are
        // read from a sign-in the browser runs, and nothing reports them
        // across this seam yet. Absent is the contract's own meaning for "the
        // vendor said nothing", so a surface draws no account line rather than
        // an empty one.
        account_label: None,
        plan_label: None,
    })
}

#[cfg(test)]
mod tests;
