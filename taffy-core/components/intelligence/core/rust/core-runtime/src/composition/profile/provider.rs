// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The provider plane's composition surface (decisions 0049 and 0096).
//!
//! One typed command in, one applied write out, and everything that reads the
//! result told about it before the call returns. Split from the profile's own
//! file for the reason its siblings were: the catalog, the probe, the composer
//! suggestion and the provider registry are four subjects that share a runtime
//! and nothing else.

use model_router::catalog::{CatalogLayer, WireApi};

use crate::adapters::provider::{
    apply_provider_command, ProviderServiceError, ProviderServiceStep,
};
use crate::wire;

use super::ProfileServiceRuntime;

impl ProfileServiceRuntime {
    /// Admits one typed provider command and re-installs what routing reads.
    ///
    /// The install is not bookkeeping and it is not conditional on anything but
    /// success. Routing is a pure function of the credential directory, the
    /// endpoint registry and the merged catalog, so a change the router was not
    /// told about leaves it answering `CredentialMissing` for a key that is
    /// genuinely on the disk — the exact defect decision 0049 was written
    /// about, with no log, no assertion and no failing test, because every
    /// component is behaving as written.
    ///
    /// The merge is rebuilt and not only the directory, because a saved
    /// provider now arrives with its models (decision 0096 section 4) and those
    /// models are the person's own layer of the catalog. A save that installed
    /// the credential and not the layer would file an endpoint with four models
    /// behind it that nothing could route to until the next restart — the same
    /// staleness, one layer further in.
    ///
    /// A refusal installs nothing, because a refusal changed nothing: the
    /// plane's methods take `&mut self` and return before writing when they
    /// refuse, so there is no partial write for an install to publish.
    pub fn submit_provider_command(
        &mut self,
        command: &wire::CoreServiceCommand,
    ) -> Result<ProviderServiceStep, ProviderServiceError> {
        let step = apply_provider_command(&mut self.providers, command)?;
        // A listing was fetched with a credential and describes that account's
        // models. When the credential goes the list stops being about anybody,
        // so it goes in the same write rather than being left to offer models
        // the next request has nothing to reach them with.
        self.forget_unconnected_listings();
        self.reinstall_catalog_state();
        Ok(step)
    }

    /// The provider roster a surface draws.
    pub fn provider_roster(&self) -> Vec<crate::provider::ProviderView> {
        self.providers.roster()
    }
}

/// The contract's spelling of one wire family.
///
/// One definition for every effect this crate composes that names a provider —
/// a probe, a composer suggestion, a listing fetch. The catalog's own
/// enumeration deliberately lacks `Managed`, because a served document may not
/// claim the product's reserved dialect, so this projection is total and no
/// composed effect can name a family a person's credential does not reach.
pub(super) const fn wire_api_of(api: WireApi) -> wire::ProviderWireApi {
    match api {
        WireApi::AnthropicMessages => wire::ProviderWireApi::AnthropicMessages,
        WireApi::OpenAiResponses => wire::ProviderWireApi::OpenAiResponses,
        WireApi::OpenAiCompletions => wire::ProviderWireApi::OpenAiCompletions,
        WireApi::GoogleGenerativeLanguage => wire::ProviderWireApi::GoogleGenerativeLanguage,
        WireApi::OpenAiCodexResponses => wire::ProviderWireApi::OpenAiCodexResponses,
        WireApi::GoogleCloudCodeAssist => wire::ProviderWireApi::GoogleCloudCodeAssist,
    }
}

/// Which of the browser's two endpoint rules an effect is claiming, read off
/// the layer of the merge that named the address (decision 0096 section 2).
///
/// Derived and never stamped. Both composers in this directory resolve their
/// model out of a merge the person's own layer is part of, so which authority
/// named the address is a fact already on the resolved model, and a constant
/// written in its place is a request that asks to be judged by the wrong rule.
/// The browser refuses the mismatch either way — what a stamp costs is the
/// refusal's meaning: it arrives naming the transport rather than saying that
/// a person's own server was reached at an address nobody registered.
///
/// The same derivation `loop-kernel`'s `shape::direct_endpoint` makes for a
/// task turn. Three composers, one rule, because a request composed under one
/// rule and announced under the other is refused by the rule it claimed — and
/// that is a refusal nobody could have intended.
pub(super) const fn endpoint_kind_of(layer: CatalogLayer) -> wire::ModelEndpointKind {
    match layer {
        // A published document named this address, so the browser holds it too
        // and compares the two.
        CatalogLayer::EmbeddedBaseline | CatalogLayer::RemoteOverlay => {
            wire::ModelEndpointKind::CatalogOrigin
        }
        // The person typed this one, and the browser recognizes it against its
        // register rather than judging it.
        CatalogLayer::UserOverride => wire::ModelEndpointKind::UserBaseUrl,
    }
}
