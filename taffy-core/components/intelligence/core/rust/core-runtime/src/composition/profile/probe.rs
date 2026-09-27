// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The key probe's composition surface (decision 0083), and which of the two
//! endpoint authorities it claims (decision 0096 section 2).
//!
//! One command in, one bounded model effect out, one verdict back. Probing is
//! not routing: nothing here consults [`model_router::RouteSelector`], binds
//! a task, or touches the task ledger. The call is composed directly from
//! the catalog merge — the provider the person named, that provider's
//! cheapest enabled model, a fixed one-word prompt, a sixteen-token answer —
//! and its terminal is classified by [`crate::probe`] into the closed verdict
//! the status projection carries.
//!
//! The effect is `RetryClass::Never`, and deliberately: the credential
//! handle may be a one-shot transient minted for a pasted draft, and a retry
//! would need a second spend of a handle that no longer exists.

use model_router::wire::{write_request, Speaker, Turn, WireRequest};
use model_router::{MergedCatalog, ModelKey, ThinkingPlan};

use crate::probe::{
    ProbeOutcome, ProbeRefusal, ProbeVerdict, PROBE_ANSWER_TOKENS, PROBE_MAX_OUTPUT_BYTES,
};
use crate::provider::{CredentialHandle, ProviderId, ProviderRefusal, ProviderRefusalKind};
use crate::wire;

use super::provider::{endpoint_kind_of, wire_api_of};
use super::ProfileServiceRuntime;

/// The fixed probe prompt. Content-free by construction: it names nothing
/// about the person, the profile, or any page.
const PROBE_PROMPT: &str = "Reply with only the word ok.";

/// Why a probe command was refused before any effect existed.
///
/// Shared by both probes, because they share the flight this refuses on
/// behalf of. The endpoint probe of `super::endpoint_probe` reaches only
/// `InvalidCommand`, `ProbeInFlight` and `IdentityExhausted`: it looks nothing
/// up in the catalog and chooses no model, so the catalog refusal cannot be
/// its answer.
///
/// There is deliberately no member for "nothing to probe with". That is a
/// standing fact about the catalog rather than a fault in the asking, and it
/// is filed as the verdict [`ProbeVerdict::NoModelListed`] — see
/// [`ProfileServiceRuntime::submit_probe_command`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProbeCommandError {
    /// The command kind and its body disagree, or a field failed its bound.
    InvalidCommand,
    /// The named provider is not in the served catalog, or is switched off.
    UnknownProvider,
    /// One probe is already in flight; its verdict settles first.
    ProbeInFlight,
    /// This live incarnation has exhausted its attempt identity space.
    IdentityExhausted,
}

impl ProfileServiceRuntime {
    /// Composes the one bounded probe effect for a submitted probe command.
    ///
    /// `Ok(Some(effect))` is the flight. Everything is validated before it is
    /// claimed, and it is released again if body writing refuses after it —
    /// the browser never saw an effect, so nothing is in flight anywhere.
    ///
    /// `Ok(None)` is an accepted command with nothing to send. A provider that
    /// lists no enabled model — one that serves its own list, before a
    /// credential is saved and the list fetched (decision 0098 section 1) —
    /// has no call to make, and the answer is filed straight onto the sheet
    /// as [`ProbeVerdict::NoModelListed`], stamped with `now_monotonic_ms`,
    /// rather than returned as a refusal. It was a refusal once, and the
    /// bridge collapsed it into `InvalidCommand`, which a surface says as
    /// "could not run right now": a permanent state about the catalog was
    /// being said as a transient one about the command, for every provider
    /// whose list is served. The caller publishes on `Ok(None)` exactly as on
    /// `Ok(Some(_))`, because a verdict is surface state and an unpublished
    /// one is invisible rather than wrong. The stamp has to be the browser's
    /// real clock: the Android coordinator tells a fresh verdict from the row
    /// it held before by inequality, and two filings with one timestamp would
    /// be one row.
    pub fn submit_probe_command(
        &mut self,
        command: &wire::CoreServiceCommand,
        now_monotonic_ms: u64,
    ) -> Result<Option<wire::EffectEnvelope>, ProbeCommandError> {
        let body = command
            .probe_provider_credential
            .as_ref()
            .ok_or(ProbeCommandError::InvalidCommand)?;
        let provider_id = ProviderId::new(body.provider_id.as_str())
            .map_err(|_| ProbeCommandError::InvalidCommand)?;
        let handle = CredentialHandle::new(body.credential_handle.as_str())
            .map_err(|_| ProbeCommandError::InvalidCommand)?;

        // The person's own layer is in the merge, so a probe can be aimed at a
        // provider they run themselves and find a model to probe with. It is
        // also why the effect below derives which endpoint rule it claims
        // rather than naming one: this lookup is exactly what puts an address
        // the person typed within reach of this command.
        let merged = self.merged_catalog();
        let catalog_provider_id = model_router::ProviderId::new(provider_id.as_str())
            .map_err(|_| ProbeCommandError::InvalidCommand)?;
        let provider = merged
            .provider(&catalog_provider_id)
            .ok_or(ProbeCommandError::UnknownProvider)?;
        if !provider.enabled {
            return Err(ProbeCommandError::UnknownProvider);
        }

        // The cheapest enabled model, by the published input+output rate: a
        // probe spends the person's own money, so it spends the least the
        // catalog offers. The merge iterates in stable key order, so a tie
        // keeps the first model id and the choice is deterministic.
        let Some(resolved) = cheapest_model_key(&merged, &catalog_provider_id)
            .and_then(|model_key| merged.resolve(&model_key).ok())
        else {
            // Nothing to send, so nothing to claim: no flight, no identity,
            // no effect. The verdict is the answer, and it stands until a
            // listing gives this provider a model.
            self.probes.file_without_flight(
                &provider_id,
                ProbeVerdict::NoModelListed,
                now_monotonic_ms,
            );
            return Ok(None);
        };

        let effect_id = match self.probes.begin(&provider_id) {
            Ok(effect_id) => effect_id,
            Err(ProbeRefusal::ProbeInFlight) => return Err(ProbeCommandError::ProbeInFlight),
            Err(ProbeRefusal::IdentityExhausted) => {
                return Err(ProbeCommandError::IdentityExhausted)
            }
        };

        let mut request_body = String::new();
        let written = write_request(
            resolved.wire_api,
            &WireRequest {
                model_id: resolved.model.model_id.as_str(),
                tool_calling: false,
                system: None,
                system_preamble: None,
                credential_method: None,
                compat: None,
                turns: &[Turn::Said {
                    speaker: Speaker::User,
                    text: &[PROBE_PROMPT],
                }],
                tools: &[],
                tool_cache_retention: model_router::request::CacheRetention::None,
                conversation_key: None,
                thinking: &ThinkingPlan::Disabled,
                answer_tokens: PROBE_ANSWER_TOKENS,
                stream: false,
            },
            &mut request_body,
        );
        if written.is_err() {
            self.probes.abandon(&effect_id);
            return Err(ProbeCommandError::InvalidCommand);
        }

        Ok(Some(wire::EffectEnvelope {
            operation: command.operation.clone(),
            effect_id,
            kind: wire::EffectKind::ModelRequest,
            // A retry would need a second spend of a handle that may no
            // longer exist, and a probe that runs twice bills twice for one
            // verdict.
            retry_class: wire::RetryClass::Never,
            storage_commit: None,
            page_observation: None,
            model_request: Some(wire::ModelRequestEffect {
                route_id: "probe".to_owned(),
                model_id: resolved.model.model_id.as_str().to_owned(),
                // The prompt is a fixed string this file owns; nothing from
                // the person or a page can reach the body.
                disclosure: wire::DisclosureClass::ContentFree,
                request_body: request_body.into_bytes(),
                max_output_bytes: PROBE_MAX_OUTPUT_BYTES,
                // Empty on purpose: no task owns this call, and the browser
                // expects exactly that of a probe.
                task_id: String::new(),
                provider_id: provider_id.as_str().to_owned(),
                wire_api: wire_api_of(resolved.wire_api),
                endpoint: resolved.endpoint.as_str().to_owned(),
                credential_handle: Some(handle.as_str().to_owned()),
                static_headers: Vec::new(),
                probe: true,
                // Whichever layer named the address above. `ProbeCustomEndpoint`
                // is a different command with a different rule — it proves an
                // *unsaved* address, and the identity it carries since Core API
                // 3.18 is a draft the save will reuse rather than a row this
                // merge holds — but that says nothing about this one, which
                // names a provider and finds it in a merge the person's own
                // layer is part of. Nothing refuses that pairing: the browser's
                // command factory bounds the identity's shape and no more, and
                // the lookup above is against the whole merge on purpose. So a
                // key probe aimed at a server somebody runs is composable
                // today, and it must ask for the rule that can answer it.
                endpoint_kind: endpoint_kind_of(resolved.endpoint_layer),
                media_attachment_handle: None,
                media_attachment_mime_type: None,
                not_before_monotonic_ms: 0,
            }),
            network_request: None,
            browser_action: None,
            tool_job: None,
            secure_store: None,
            auth_surface: None,
            permission_request: None,
            asset_delivery: None,
            catalog_fetch: None,
            provider_listing_fetch: None,
            composer_completion: None,
            custom_endpoint_probe: None,
        }))
    }

    /// Judges one delivered probe terminal on the ordered core sequence.
    ///
    /// Returns whether a verdict was filed — the caller republishes status
    /// exactly then, because the verdict is surface state and an unpublished
    /// state change is invisible rather than wrong. A result naming no
    /// in-flight probe files nothing and publishes nothing.
    pub fn deliver_probe_result(
        &mut self,
        effect_id: &str,
        outcome: ProbeOutcome,
        now_monotonic_ms: u64,
    ) -> bool {
        let Some((provider_id, verdict)) =
            self.probes.deliver(effect_id, outcome, now_monotonic_ms)
        else {
            return false;
        };
        // The probe is the one place the core holds a provider identity and
        // the vendor's own HTTP status together, so it is the one place a
        // refusal the composer's quota banner can read is derivable today. A
        // task turn's terminal carries the same status and no provider, which
        // is the seam the next stage closes.
        if let Ok(identity) = ProviderId::new(provider_id.as_str()) {
            match verdict {
                ProbeVerdict::RateLimit => {
                    self.record_refusal(
                        &identity,
                        ProviderRefusalKind::RateLimit,
                        now_monotonic_ms,
                    );
                }
                ProbeVerdict::Billing => {
                    self.record_refusal(&identity, ProviderRefusalKind::Billing, now_monotonic_ms);
                }
                ProbeVerdict::Overloaded => {
                    self.record_refusal(
                        &identity,
                        ProviderRefusalKind::Overloaded,
                        now_monotonic_ms,
                    );
                }
                // The vendor spent on this call, so whatever it last refused
                // with is over and the banner comes down.
                ProbeVerdict::Usable => self.providers.record_refusal(&identity, None),
                // None of these says anything about spending: a wrong key, an
                // address that answered nothing, a catalog defect, a probe that
                // ran out of time, a provider with nothing to probe. What
                // stands, stands. (`NoModelListed` never arrives here — it is
                // filed without a flight — but the match is exhaustive on
                // purpose, so a new member is a decision rather than a
                // fall-through.)
                ProbeVerdict::Auth
                | ProbeVerdict::Timeout
                | ProbeVerdict::Network
                | ProbeVerdict::ModelNotFound
                | ProbeVerdict::Unknown
                | ProbeVerdict::EndpointReached
                | ProbeVerdict::NoModelListed => {}
            }
        }
        true
    }

    fn record_refusal(
        &mut self,
        provider_id: &ProviderId,
        kind: ProviderRefusalKind,
        at_monotonic_ms: u64,
    ) {
        self.providers.record_refusal(
            provider_id,
            Some(ProviderRefusal {
                kind,
                at_monotonic_ms,
            }),
        );
    }
}

fn cheapest_model_key(
    catalog: &MergedCatalog,
    provider_id: &model_router::ProviderId,
) -> Option<ModelKey> {
    catalog
        .models()
        .filter(|entry| entry.model.provider_id == *provider_id && entry.model.enabled)
        .map(|entry| {
            let rate = entry
                .model
                .cost
                .input_micros_per_million
                .saturating_add(entry.model.cost.output_micros_per_million);
            (rate, entry.model.key())
        })
        .min_by_key(|(rate, _)| *rate)
        .map(|(_, key)| key)
}
