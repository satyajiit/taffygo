// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The composer suggestion's composition surface (decision 0097).
//!
//! One command in, one bounded model effect out, one push back. Like the key
//! probe beside it this is not routing: nothing here consults
//! [`model_router::RouteSelector`], binds a task, or touches the task ledger.
//! Unlike the probe it carries the person's own words, so the disclosure is
//! what the person selected rather than content-free, and the choice of what
//! to send it to is theirs rather than the cheapest thing available.
//!
//! The managed route is unreachable from here by construction rather than by a
//! check: a suggestion is composed against an address this profile can reach
//! **directly** — a stored provider credential, or an endpoint the person
//! registered themselves that needs none — and the managed route is neither of
//! those. It is entered with a token the browser mints per call (decision
//! 0082), so it leaves nothing filed against it, and no layer of the catalog
//! carries it as an address somebody typed. So a profile whose only configured
//! route is the managed one produces no suggestion and says so, which is
//! exactly what decision 0097 requires and would be easy to get wrong with an
//! allowlist.

use model_router::catalog::{CatalogLayer, ModelRole};
use model_router::wire::{write_request, Speaker, Turn, WireRequest};
use model_router::{CredentialState, MergedCatalog, ModelKey, ThinkingPlan};

use crate::completion::{suggestion_from, COMPLETION_ANSWER_TOKENS, COMPLETION_MAX_OUTPUT_BYTES};
use crate::wire;

use super::provider::{endpoint_kind_of, wire_api_of};
use super::ProfileServiceRuntime;

/// What Taffy is asked to do with the text, stated once so the request the
/// provider sees is not a fragment with no instruction attached.
///
/// It names nothing about the person, the profile, or any page — everything
/// specific to this request is the person's own composer text, which travels
/// in the turn below and is disclosed as theirs.
const COMPOSER_SYSTEM: &str = concat!(
    "Continue the person's unfinished sentence. Reply with only the continuation, ",
    "with no preamble, no quotation marks and no explanation. Reply with nothing at ",
    "all if the sentence is already complete."
);

/// Why a composer completion was refused before any effect existed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompletionCommandError {
    /// The command kind and its body disagree, or a field failed its bound.
    InvalidCommand,
    /// No provider this profile could route a suggestion to.
    ///
    /// The ordinary case, not a failure: a profile on the managed route alone
    /// reaches it on every keystroke, and the surface draws nothing.
    ///
    /// The name says credential and the rule is wider than one — an endpoint
    /// the person registered themselves is routable with nothing filed against
    /// it, which is what `completion_auth` below decides. The name stays
    /// because it crosses the cxx bridge in
    /// `taffy-core/services/core/service_bridge_composer.rs`, which is named by
    /// GN and by nothing Cargo builds, so renaming it here would compile
    /// cleanly on a host and break the product build.
    NoUsableCredential,
    /// This live incarnation exhausted its effect-identity ordinal.
    IdentityExhausted,
}

/// What one withdrawal stopped.
///
/// A record with one field rather than a bare `Option`, because the caller's
/// contract is the same as a submission's — dispatch nothing, and stop what is
/// named — and two shapes for one instruction is how the browser ends up with
/// two ways to stop an effect.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ComposerCancellation {
    /// The dispatch the browser must stop, absent when nothing was running.
    pub withdrawn_effect_id: Option<String>,
}

/// One suggestion effect, and the one it displaced.
#[derive(Clone, Debug)]
pub struct ComposerCompletionPlan {
    /// The bounded model request to dispatch.
    pub effect: wire::EffectEnvelope,
    /// The effect a newer request displaced, which the caller cancels.
    pub superseded_effect_id: Option<String>,
}

impl ProfileServiceRuntime {
    /// Composes the one bounded suggestion effect for a submitted command.
    ///
    /// Everything is validated before the flight is claimed, and the flight is
    /// released again if body writing refuses after it — the browser never saw
    /// an effect, so nothing is in flight anywhere.
    pub fn submit_composer_completion_command(
        &mut self,
        command: &wire::CoreServiceCommand,
    ) -> Result<ComposerCompletionPlan, CompletionCommandError> {
        let body = command
            .request_composer_completion
            .as_ref()
            .ok_or(CompletionCommandError::InvalidCommand)?;
        if body.request_id.is_empty() || body.request_id.len() > wire::MAX_IDENTIFIER_BYTES {
            return Err(CompletionCommandError::InvalidCommand);
        }
        if body.prefix.trim().is_empty() || body.prefix.len() > wire::MAX_COMPOSER_PREFIX_BYTES {
            return Err(CompletionCommandError::InvalidCommand);
        }
        if body
            .suffix
            .as_ref()
            .is_some_and(|suffix| suffix.len() > wire::MAX_COMPOSER_SUFFIX_BYTES)
        {
            return Err(CompletionCommandError::InvalidCommand);
        }

        // The person's own layer is in the merge, so a suggestion can be spent
        // on a model behind an endpoint they run themselves.
        let merged = self.merged_catalog();
        let chosen = self
            .choose_completion_model(&merged)
            .ok_or(CompletionCommandError::NoUsableCredential)?;
        let resolved = merged
            .resolve(&chosen.model_key)
            .map_err(|_| CompletionCommandError::NoUsableCredential)?;

        let flight = self
            .completions
            .begin(body.request_id.as_str())
            .map_err(|_| CompletionCommandError::IdentityExhausted)?;

        let mut request_body = String::new();
        let prefix = body.prefix.as_str();
        // The suffix travels as a second sentence of the same turn rather than
        // as a second turn: a turn is something somebody said, and the text
        // after the caret was not said to Taffy, it is where the person's own
        // sentence goes next.
        let turn_text: Vec<&str> = match body.suffix.as_deref() {
            Some(after) if !after.is_empty() => vec![prefix, after],
            _ => vec![prefix],
        };
        let written = write_request(
            resolved.wire_api,
            &WireRequest {
                model_id: resolved.model.model_id.as_str(),
                tool_calling: false,
                system: Some(COMPOSER_SYSTEM),
                system_preamble: None,
                credential_method: None,
                compat: None,
                turns: &[Turn::Said {
                    speaker: Speaker::User,
                    text: &turn_text,
                }],
                tools: &[],
                tool_cache_retention: model_router::request::CacheRetention::None,
                // No thinking phase at any rung. A person who wanted the model
                // to think about this asked a question instead of typing one.
                conversation_key: None,
                thinking: &ThinkingPlan::Disabled,
                answer_tokens: COMPLETION_ANSWER_TOKENS,
                stream: false,
            },
            &mut request_body,
        );
        if written.is_err() {
            self.completions.abandon(&flight.effect_id);
            return Err(CompletionCommandError::InvalidCommand);
        }

        Ok(ComposerCompletionPlan {
            effect: wire::EffectEnvelope {
                operation: command.operation.clone(),
                effect_id: flight.effect_id,
                kind: wire::EffectKind::ModelRequest,
                // A suggestion the person has already typed past is worth
                // nothing, so a retry would spend money on an answer that
                // would be discarded on arrival.
                retry_class: wire::RetryClass::Never,
                storage_commit: None,
                page_observation: None,
                model_request: Some(wire::ModelRequestEffect {
                    route_id: "composer".to_owned(),
                    model_id: resolved.model.model_id.as_str().to_owned(),
                    // The person's own words, and nothing else: no page, no
                    // history, no workspace fact, no other conversation.
                    disclosure: wire::DisclosureClass::UserSelectedContent,
                    request_body: request_body.into_bytes(),
                    max_output_bytes: COMPLETION_MAX_OUTPUT_BYTES,
                    // Empty on purpose: no task owns this call, which is what
                    // makes it unjournalled and unauditable by construction
                    // rather than by omission.
                    task_id: String::new(),
                    provider_id: chosen.model_key.provider_id.as_str().to_owned(),
                    wire_api: wire_api_of(resolved.wire_api),
                    endpoint: resolved.endpoint.as_str().to_owned(),
                    // Absent for an endpoint that needs none, which is what a
                    // plain local server is. The contract already admits it,
                    // and the choice below is what decides that this address
                    // may be reached that way.
                    credential_handle: chosen.credential_handle,
                    static_headers: Vec::new(),
                    probe: false,
                    // Whichever layer named the address above, so the two
                    // cannot say different things about one request. The
                    // choice below reads the same fact to decide what the
                    // request may carry, so a constant here would compose a
                    // suggestion for a server the person runs themselves and
                    // announce it as an address the browser holds an entry for.
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
            },
            superseded_effect_id: flight.superseded_effect_id,
        })
    }

    /// Withdraws one composer suggestion the surface has stopped waiting for.
    ///
    /// The core's half of decision 0097 section 3. It releases the flight and
    /// names the dispatch the browser must stop; the browser's half is the
    /// withdrawal it already performs for a superseded request, which is why
    /// this returns the same shape a submission does rather than inventing a
    /// second channel for the same instruction.
    ///
    /// An identity nothing is in flight for withdraws nothing and is accepted:
    /// the answer may already have arrived, and refusing would make a surface
    /// that cancelled a moment too late look like one that sent nonsense.
    pub fn submit_composer_cancel_command(
        &mut self,
        command: &wire::CoreServiceCommand,
    ) -> Result<ComposerCancellation, CompletionCommandError> {
        let body = command
            .cancel_composer_completion
            .as_ref()
            .ok_or(CompletionCommandError::InvalidCommand)?;
        if body.request_id.is_empty() || body.request_id.len() > wire::MAX_IDENTIFIER_BYTES {
            return Err(CompletionCommandError::InvalidCommand);
        }
        Ok(ComposerCancellation {
            withdrawn_effect_id: self.completions.withdraw(body.request_id.as_str()),
        })
    }

    /// Turns one delivered suggestion terminal into the effect that carries it
    /// to a surface.
    ///
    /// `None` means there is nothing to deliver: the answer was for a request
    /// the person has typed past, or the model offered nothing usable. Both
    /// are ordinary, and neither is reported as a failure.
    pub fn deliver_composer_completion_result(
        &mut self,
        effect_id: &str,
        completion: &[u8],
        operation: &wire::OperationEnvelope,
    ) -> Option<wire::EffectEnvelope> {
        let request_id = self.completions.settle(effect_id)?;
        let text = suggestion_from(completion)?;
        Some(wire::EffectEnvelope {
            operation: operation.clone(),
            effect_id: format!("composer-push-{request_id}"),
            kind: wire::EffectKind::DeliverComposerCompletion,
            retry_class: wire::RetryClass::Never,
            storage_commit: None,
            page_observation: None,
            model_request: None,
            network_request: None,
            browser_action: None,
            tool_job: None,
            secure_store: None,
            auth_surface: None,
            permission_request: None,
            asset_delivery: None,
            catalog_fetch: None,
            provider_listing_fetch: None,
            composer_completion: Some(wire::ComposerCompletionEffect {
                request_id,
                text: Some(text),
            }),
            custom_endpoint_probe: None,
        })
    }

    /// Records what became of one suggestion push (decision 0097).
    ///
    /// `delivered` is not an argument, and that is the decision rather than an
    /// omission. The browser is required to say whether any surface was still
    /// watching, so that a suggestion computed and never shown is
    /// distinguishable from one never computed — but a suggestion is
    /// journalled nowhere and charged to nothing (decision 0097 section 1), so
    /// there is no record here for the answer to enter and no balance for it
    /// to move. Taking it would be bookkeeping kept for its own sake.
    ///
    /// What the terminal is for is the flight, and this settles it. A push
    /// answers a request either way, so a flight still held for that request
    /// is released whether or not anybody saw the words.
    pub fn record_composer_completion_delivery(&mut self, request_id: &str) {
        self.completions.release(request_id);
    }

    /// The model a suggestion is spent on, if any.
    ///
    /// The person's standing pin wins wherever they made one, because a
    /// suggestion that came from a different model than their answers is a
    /// suggestion in a different voice. Failing that, the cheapest model
    /// cataloged for fast browsing — the rung this call belongs on — and only
    /// then the cheapest enabled model at all, so a provider whose catalog
    /// names no fast model is still usable.
    fn choose_completion_model(&self, merged: &MergedCatalog) -> Option<ChosenModel> {
        self.pinned_completion_model(merged)
            .or_else(|| self.cheapest_completion_model(merged))
    }

    /// The person's standing pin, when one of them can be reached.
    ///
    /// In provider-identity order, so one snapshot and one set of pins choose
    /// the same model on two devices. A pin the merge no longer resolves, or
    /// one on an address this profile cannot reach, is passed over rather than
    /// refusing the suggestion — the person may have pinned more than one
    /// provider, and the cheapest-model fallback stands behind all of them.
    ///
    /// Each pin is already scoped to the provider it was made against, which is
    /// what makes this safe: a model identity is only unique under its provider
    /// — several of them publish the same open-weights model under the same
    /// name — so a search by identity alone would send this person's own key to
    /// a provider they did not pin, on a model that happens to share a name
    /// with the one they did.
    fn pinned_completion_model(&self, merged: &MergedCatalog) -> Option<ChosenModel> {
        for pinned in self.providers.pinned_models() {
            let key = ModelKey::new(
                pinned.provider_id.as_router().clone(),
                pinned.model_id.clone(),
            );
            let Ok(resolved) = merged.resolve(&key) else {
                continue;
            };
            let Ok(credential_handle) =
                self.completion_auth(&key.provider_id, resolved.endpoint_layer)
            else {
                continue;
            };
            return Some(ChosenModel {
                credential_handle,
                model_key: key,
            });
        }
        None
    }

    /// The cheapest model this profile can reach, a fast one first.
    ///
    /// Two passes rather than one scored sweep, so "a fast model, whatever it
    /// costs" is never beaten by "a cheap model that is not one".
    fn cheapest_completion_model(&self, merged: &MergedCatalog) -> Option<ChosenModel> {
        for fast_only in [true, false] {
            let mut cheapest: Option<(u64, ChosenModel)> = None;
            for entry in merged.models() {
                let key = entry.model.key();
                // Both kill switches in one question: the merge refuses to
                // resolve a model or a provider that is switched off, and a
                // model it will not resolve is one no request could be built
                // against anyway.
                let Ok(resolved) = merged.resolve(&key) else {
                    continue;
                };
                if fast_only && !resolved.model.roles.contains(&ModelRole::FastBrowsing) {
                    continue;
                }
                let Ok(credential_handle) =
                    self.completion_auth(&key.provider_id, resolved.endpoint_layer)
                else {
                    continue;
                };
                let rate = resolved
                    .model
                    .cost
                    .input_micros_per_million
                    .saturating_add(resolved.model.cost.output_micros_per_million);
                if cheapest.as_ref().is_none_or(|(held, _)| rate < *held) {
                    cheapest = Some((
                        rate,
                        ChosenModel {
                            credential_handle,
                            model_key: key,
                        },
                    ));
                }
            }
            if let Some((_, chosen)) = cheapest {
                return Some(chosen);
            }
        }
        None
    }

    /// What a suggestion to one address goes out carrying, or a refusal.
    ///
    /// The question is whether a request to this address can be **routed**,
    /// which is not the same as whether a credential is held — and the one
    /// address where the two differ is the ordinary one. A person's own
    /// endpoint is reached with a key or with nothing, and which of the two is
    /// what they said when they saved it: `SaveCustomProviderCommand`'s
    /// credential handle is optional, and a save carrying none removes the
    /// record rather than filing an empty one. So nothing on file against an
    /// address they typed is their configuration rather than a gap in it, and
    /// asking for a credential there would leave the composer silent for the
    /// life of the profile on a plain Ollama — the single most common server
    /// anybody runs themselves, and one of the things this product is for.
    ///
    /// A published address keeps the other rule. Every catalog provider
    /// declares a method, every method is a credential, and a suggestion sent
    /// to one of those addresses with nothing attached is a round trip spent
    /// learning what the device already knew.
    ///
    /// Both refuse a credential that exists and has stopped working, and both
    /// say so themselves rather than one deferring to the other. Written out
    /// for the reason `model_router`'s `own_endpoint_auth` is written out
    /// beside its `catalog_auth`: the point of having two rules is that
    /// relaxing one leaves the other standing, and the direction that matters
    /// is the one nobody would see. Reading a `NeedsSignIn` credential as "this
    /// endpoint needs none" would turn *your key stopped working* into *your
    /// words are going to your server unauthenticated* — a downgrade with no
    /// refusal to notice and no sentence to read. The two files agree by both
    /// being right, never by sharing a line.
    ///
    /// Which of the rules applies is read off the layer that named the address,
    /// the fact the merge already recorded — never the address itself, which
    /// would be a guess (decision 0096 section 2). It is the same fact
    /// [`endpoint_kind_of`] stamps on the composed effect, so a request cannot
    /// be built under one rule and announced under the other.
    fn completion_auth(
        &self,
        provider_id: &model_router::ProviderId,
        layer: CatalogLayer,
    ) -> Result<Option<String>, CompletionUnreachable> {
        let filed = self
            .providers
            .credentials()
            .find(|credential| credential.provider_id.as_router() == provider_id);
        let handle = filed.map(|credential| credential.handle.as_str().to_owned());
        // No record at all is the absence the router's own registry answers
        // with, so the two halves below read one vocabulary rather than one
        // reading a state and the other reading an `Option`.
        let state = filed.map_or(CredentialState::Absent, |credential| credential.state);
        match layer {
            CatalogLayer::EmbeddedBaseline | CatalogLayer::RemoteOverlay => match state {
                CredentialState::Usable => Ok(handle),
                CredentialState::Absent
                | CredentialState::NeedsSignIn
                | CredentialState::RefreshFailed => Err(CompletionUnreachable),
            },
            CatalogLayer::UserOverride => match state {
                CredentialState::Usable => Ok(handle),
                CredentialState::Absent => Ok(None),
                CredentialState::NeedsSignIn | CredentialState::RefreshFailed => {
                    Err(CompletionUnreachable)
                }
            },
        }
    }
}

/// No suggestion may be composed against this address.
///
/// A named refusal rather than a second `Option` wrapped around the answer:
/// "reachable carrying nothing" and "not reachable at all" are the two absences
/// this file must never let collapse into one, and an option of an option is
/// how they collapse.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct CompletionUnreachable;

/// One model, and what a request to it goes out carrying.
///
/// The provider is not a third field. It is the first half of the model key,
/// and a copy beside it would be a second place the two could disagree about
/// which address this suggestion is for.
struct ChosenModel {
    /// The stored handle the request is spent on, absent for an address that
    /// needs none.
    credential_handle: Option<String>,
    model_key: ModelKey,
}

#[cfg(test)]
mod tests;
