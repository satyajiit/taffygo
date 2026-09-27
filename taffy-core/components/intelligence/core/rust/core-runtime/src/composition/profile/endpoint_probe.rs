// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The custom-endpoint probe's composition surface (decisions 0096 and 0098).
//!
//! Its sibling `probe` proves a *credential* by one bounded completion against
//! a provider the catalog already carries. This one proves an *address*: what
//! answers at something a person typed, before anything is saved under it. The
//! two share one flight and one row on the status projection, and nothing
//! else — there is no catalog to look a provider up in here, because the whole
//! point is that no provider is defined yet.
//!
//! So the core composes no model call for it. The work is the browser's
//! prober, which tries the OpenAI-shaped listing and then the native ones and
//! reports what it found; the core's part is the identity the verdict is filed
//! under, the single flight, and the bound on what may come back.
//!
//! Why the identity comes from the surface rather than from here: the save
//! that follows reuses it, so a person who probes an address and then saves it
//! gets one row rather than two, and a verdict that arrived while they were
//! still typing is already filed under the row the save will create. Minting
//! one here would make the core's spelling authoritative for a provider that
//! does not exist yet, and the save would then have to be told about it.

use model_router::catalog::Endpoint;

use crate::probe::{EndpointProbeOutcome, ProbeEndpoint, ProbeRefusal};
use crate::provider::{CredentialHandle, CustomModel, ProviderId};
use crate::wire;

use super::probe::ProbeCommandError;
use super::ProfileServiceRuntime;

/// The response cap the probe effect carries, in bytes.
///
/// The listing bound rather than a number of this file's own: what comes back
/// is a model listing, and a second opinion about how big one may be would be
/// a second bound to keep in step with the transport's.
pub const ENDPOINT_PROBE_MAX_RESPONSE_BYTES: u32 = {
    assert!(wire::MAX_PROVIDER_LISTING_BYTES <= u32::MAX as usize);
    #[allow(clippy::cast_possible_truncation)]
    {
        wire::MAX_PROVIDER_LISTING_BYTES as u32
    }
};

impl ProfileServiceRuntime {
    /// Composes the one bounded endpoint probe for a submitted command.
    ///
    /// Every field is parsed into its bounded type before the flight is
    /// claimed, so a refused command leaves nothing in flight. The address is
    /// held to the rule for something a person typed rather than to the
    /// catalog's (decision 0096 section 2): a port and a base path are theirs,
    /// and plain http reaches a machine on the desk in front of them. Which
    /// addresses plain http may actually reach is the browser's answer and is
    /// deliberately not asked here.
    pub fn submit_custom_endpoint_probe_command(
        &mut self,
        command: &wire::CoreServiceCommand,
    ) -> Result<wire::EffectEnvelope, ProbeCommandError> {
        let body = command
            .probe_custom_endpoint
            .as_ref()
            .ok_or(ProbeCommandError::InvalidCommand)?;
        let provider_id = ProviderId::new(body.provider_id.as_str())
            .map_err(|_| ProbeCommandError::InvalidCommand)?;
        let endpoint = Endpoint::user_base_url(body.endpoint.as_str())
            .map_err(|_| ProbeCommandError::InvalidCommand)?;
        // The three reserved families are refused rather than probed. The
        // managed wire is spoken only to the product's own edge worker at its
        // compiled origin, and the other two are vendors' subscription
        // endpoints; an address a person typed is none of them, so a command
        // claiming one is malformed rather than unlucky. A `matches!` is not
        // exhaustive, so this list does not grow by itself — a family added
        // without a line here is one a person may claim at any address they
        // type, and nothing compiled says so.
        if matches!(
            body.wire_api,
            wire::ProviderWireApi::Managed
                | wire::ProviderWireApi::OpenAiCodexResponses
                | wire::ProviderWireApi::GoogleCloudCodeAssist
        ) {
            return Err(ProbeCommandError::InvalidCommand);
        }
        let handle = match body.credential_handle.as_deref() {
            None => None,
            Some(raw) => {
                Some(CredentialHandle::new(raw).map_err(|_| ProbeCommandError::InvalidCommand)?)
            }
        };

        let effect_id = match self.probes.begin_endpoint(&provider_id) {
            Ok(effect_id) => effect_id,
            Err(ProbeRefusal::ProbeInFlight) => return Err(ProbeCommandError::ProbeInFlight),
            Err(ProbeRefusal::IdentityExhausted) => {
                return Err(ProbeCommandError::IdentityExhausted)
            }
        };

        Ok(wire::EffectEnvelope {
            operation: command.operation.clone(),
            effect_id,
            kind: wire::EffectKind::ProbeCustomEndpoint,
            // A retry would need a second spend of a handle that may have been
            // minted for one send, and a person who is still typing an address
            // will ask again themselves.
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
            composer_completion: None,
            custom_endpoint_probe: Some(wire::CustomEndpointProbeEffect {
                provider_id: provider_id.as_str().to_owned(),
                endpoint: endpoint.as_str().to_owned(),
                wire_api: body.wire_api,
                credential_handle: handle.map(|held| held.as_str().to_owned()),
                max_response_bytes: ENDPOINT_PROBE_MAX_RESPONSE_BYTES,
            }),
        })
    }

    /// Files one delivered endpoint probe on the ordered core sequence.
    ///
    /// Returns whether a verdict was filed — the caller republishes status
    /// exactly then, because a verdict nothing published is invisible rather
    /// than wrong. A result naming no in-flight probe files nothing.
    pub fn deliver_custom_endpoint_probe_result(
        &mut self,
        effect_id: &str,
        result: &wire::CustomEndpointProbeResult,
        now_monotonic_ms: u64,
    ) -> bool {
        let outcome = EndpointProbeOutcome {
            reached: result.reached,
            endpoint: result.reached.then(|| ProbeEndpoint {
                // An endpoint that named no runtime is the compatible one,
                // which is the contract's own word for it rather than a guess.
                server_kind: result
                    .detected_server
                    .as_ref()
                    .map_or(model_router::wire::ServerKind::OpenAiCompatible, |held| {
                        server_kind(held.server_kind)
                    }),
                model_count: result.model_count,
                models: result.models.iter().filter_map(custom_model).collect(),
                proved_base: result.proved_base.clone(),
            }),
        };
        self.probes
            .deliver_endpoint(effect_id, outcome, now_monotonic_ms)
            .is_some()
    }
}

/// The router's spelling of one runtime, member for member with the contract's.
const fn server_kind(kind: wire::ServerKind) -> model_router::wire::ServerKind {
    match kind {
        wire::ServerKind::OpenaiCompatible => model_router::wire::ServerKind::OpenAiCompatible,
        wire::ServerKind::Ollama => model_router::wire::ServerKind::Ollama,
        wire::ServerKind::LmStudio => model_router::wire::ServerKind::LmStudio,
        wire::ServerKind::Vllm => model_router::wire::ServerKind::Vllm,
        wire::ServerKind::LlamaCpp => model_router::wire::ServerKind::LlamaCpp,
    }
}

/// One reported model, or nothing when its identity fails the bound.
///
/// A model whose identity a request could not name is dropped rather than
/// carried: it would show in the setup screen's list and refuse every call.
/// The count beside the list is what the server said, so dropping one here
/// does not quietly change the number a person is shown.
fn custom_model(spec: &wire::CustomModelSpec) -> Option<CustomModel> {
    if spec.display_name.is_empty()
        || spec.display_name.len() > wire::MAX_MODEL_DISPLAY_NAME_BYTES
        || spec.model_id.len() > wire::MAX_MODEL_ID_BYTES
    {
        return None;
    }
    Some(CustomModel {
        model_id: model_router::ModelId::new(spec.model_id.as_str()).ok()?,
        display_name: spec.display_name.clone(),
        context_window: spec.context_window,
        max_output_tokens: spec.max_output_tokens,
        reasoning: spec.reasoning,
        tool_calling: spec.tool_calling,
    })
}

#[cfg(test)]
mod tests;
