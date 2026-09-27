// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the provider plane holds and what it asks the browser to do.
//!
//! `CredentialState` and `Endpoint` are taken from `model-router` rather than
//! restated. They are portable value types with one authoritative definition,
//! and the router is the component that must agree with this plane about both:
//! a second spelling of "usable" would be a correspondence held by review.

use model_router::catalog::CatalogLayer;
use model_router::catalog::Endpoint;
use model_router::catalog::{InputModality, ModelRole};
use model_router::wire::ServerKind;
use model_router::{CredentialState, ModelId, ModelKey, ThinkingLevel};

use super::identity::{CredentialHandle, ProviderDisplayName, ProviderId};

/// How a person authenticates to one provider.
///
/// Mirrors the contract enumeration member for member; the conversion lives in
/// the production adapter so this module stays free of wire types.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ProviderAuthMethod {
    /// A key the person holds and supplies.
    ApiKey,
    /// A subscription the person signs in to.
    Oauth,
}

/// The closed request family one provider endpoint speaks.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ProviderWireApi {
    /// Anthropic's messages API.
    AnthropicMessages,
    /// `OpenAI`'s responses API.
    OpenAiResponses,
    /// The chat-completions shape several providers accept.
    OpenAiCompletions,
    /// Google's generative language API.
    GoogleGenerativeLanguage,
}

/// Where a provider came from.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ProviderOrigin {
    /// Shipped in the compiled baseline catalog.
    Catalog,
    /// Defined by the person on this device.
    Custom,
}

/// One model a person's own endpoint said it has (decision 0096 section 4).
///
/// The four numbers and flags are exactly what the endpoint answered, zero
/// included. The contract says a zero window or a zero allowance means the
/// endpoint did not state the value, which is a different fact from a small
/// one, and a plane that filled in a number here would lose the difference for
/// good. What zero becomes for routing is decided once, where the person's
/// layer of the merged catalog is built, and it is decided there because that
/// is the only place a wrong answer can be seen beside every other layer's.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CustomModel {
    /// The identity the endpoint files it under, and the identity a request
    /// names.
    pub model_id: ModelId,
    /// The name to show, as the endpoint gave it.
    pub display_name: String,
    /// Total context window in tokens; zero when the endpoint did not say.
    pub context_window: u32,
    /// Largest answer allowance in tokens; zero when the endpoint did not say.
    pub max_output_tokens: u32,
    /// Whether the model produces thinking output.
    pub reasoning: bool,
    /// Whether the model can be handed tools and answer with a tool call.
    pub tool_calling: bool,
}

/// One of a person's own providers.
///
/// The endpoint, the wire family, the credential, the models and the runtime
/// behind it are one value, because they are written by one command. A provider
/// that existed configured but unauthenticated would be a state a person never
/// asked for and could not see; a provider filed with nothing to route to would
/// be one they could see and not use, which is the same defect wearing a fuller
/// screen (decision 0096 section 4).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CustomProvider {
    /// The identity this provider is keyed by.
    pub provider_id: ProviderId,
    /// The name shown for it.
    pub display_name: ProviderDisplayName,
    /// The endpoint the person supplied, held to the rule for an address a
    /// person typed rather than to the catalog's (decision 0096 section 2).
    pub endpoint: Endpoint,
    /// The request family that endpoint speaks.
    pub wire_api: ProviderWireApi,
    /// The opaque handle for its key, absent when the endpoint needs none.
    pub credential: Option<CredentialHandle>,
    /// The models the endpoint listed when the person saved it, in the order
    /// it listed them. Empty is an answer — the address is right and nothing
    /// is loaded behind it — and it is said as one rather than as a failure.
    pub models: Vec<CustomModel>,
    /// What the endpoint turned out to be, absent when nothing was detected.
    ///
    /// The contract carries this inside a one-field record because mojom has
    /// no optional enumeration, and "nothing was detected" must be the absent
    /// record rather than a member meaning "no member". On this side an
    /// `Option` says the same thing natively, so the wrapper is unwrapped at
    /// the seam and never restated here. The kind itself is the router's own
    /// [`ServerKind`], not a third spelling of the same five servers: the
    /// detection exists to pick the dialect overrides that kind needs, and the
    /// table that holds them is the router's.
    pub detected_server: Option<ServerKind>,
}

/// What this plane knows about one provider's credential.
///
/// The handle is the whole of it. No field here can hold key material, and
/// that is a property of the type rather than of the code that fills it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProviderCredential {
    /// The provider this credential authenticates.
    pub provider_id: ProviderId,
    /// The method the handle was minted for.
    pub auth_method: ProviderAuthMethod,
    /// The opaque browser secure-store handle.
    pub handle: CredentialHandle,
    /// Whether a request may proceed on it.
    pub state: CredentialState,
}

/// One model the merged catalog carries under a provider.
///
/// The picking facts and nothing else: what a person chooses between and what
/// a surface says about the choice. Price, endpoint and dialect stay in the
/// catalog, because a screen that offers a model does not price it and the
/// plane must not become a second copy of the catalog.
///
/// `thinking_levels` is the model's own ladder as
/// [`ThinkingLevels::selectable`][model_router::thinking::ThinkingLevels::selectable]
/// answers it — exactly the rungs it supports, ascending. It is read from the
/// catalog rather than assumed, and a model that supports one rung reports
/// one.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CatalogModel {
    /// The provider this model belongs to.
    pub provider_id: ProviderId,
    /// The provider-scoped identity, exactly as the catalog files it.
    pub model_id: ModelId,
    /// The name to show.
    pub display_name: String,
    /// Total context window in tokens.
    pub context_window: u64,
    /// Largest answer allowance in tokens.
    pub max_output_tokens: u64,
    /// Whether the model produces thinking output.
    pub reasoning: bool,
    /// Whether the model can be handed tools and answer with a tool call.
    pub tool_calling: bool,
    /// The roles the catalog files it for.
    pub roles: Vec<ModelRole>,
    /// What it accepts as input.
    pub input_modalities: Vec<InputModality>,
    /// The rungs it supports, ascending.
    pub thinking_levels: Vec<ThinkingLevel>,
}

impl CatalogModel {
    /// The key that names this model to the router.
    pub fn key(&self) -> ModelKey {
        ModelKey::new(self.provider_id.as_router().clone(), self.model_id.clone())
    }
}

/// One provider's standing model choice (decision 0093).
///
/// Both fields are the choice as it should now stand rather than a change to
/// apply, which is why they are one value written by one command: separately
/// written, a snapshot could show a model this person never chose beside a
/// thinking level they did.
///
/// Absence is a state in both halves and it is the same state in both — nobody
/// chose, so Taffy decides. `OFF` is a person asking for no thinking phase,
/// which is a different answer, and the two never collapse because the level
/// travels inside an optional record rather than as a rung meaning no rung.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ProviderModelPreference {
    /// The model the person pinned, always one the catalog still carries.
    pub model_id: Option<ModelId>,
    /// The rung the person asked for.
    pub thinking: Option<ThinkingLevel>,
}

impl ProviderModelPreference {
    /// Whether this record states any choice at all.
    pub const fn states_a_choice(&self) -> bool {
        self.model_id.is_some() || self.thinking.is_some()
    }
}

/// The few setup facts a surface cannot derive (decision 0094).
///
/// Every field is optional because the catalog may say nothing, and a surface
/// told nothing draws nothing rather than an empty row. Prose is deliberately
/// absent: it is written in the product's own strings, in the person's own
/// language.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ProviderPresentation {
    /// What this vendor's keys begin with.
    pub key_prefix: Option<String>,
    /// Where a person creates a key.
    pub get_key_url: Option<String>,
    /// Where the vendor documents this endpoint.
    pub docs_url: Option<String>,
}

impl ProviderPresentation {
    /// Whether the catalog said anything at all.
    pub const fn is_empty(&self) -> bool {
        self.key_prefix.is_none() && self.get_key_url.is_none() && self.docs_url.is_none()
    }
}

/// What a vendor last refused a request with, when the credential itself is
/// fine.
///
/// Closed at three because these are the three a surface can say something
/// useful about, and because a credential state cannot say any of them:
/// `Usable`, `NeedsSignIn` and `RefreshFailed` are all statements about the
/// key, and "this key works and the vendor is refusing to spend it" is not.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProviderRefusalKind {
    /// The vendor is rate limiting this account.
    RateLimit,
    /// The account behind the credential cannot pay.
    Billing,
    /// The vendor is temporarily unable to serve the request.
    Overloaded,
}

/// One vendor refusal and when it was reached.
///
/// The pair travels together and is absent together: a timestamp with no
/// refusal has no honest value, and a zero beside an absent refusal would read
/// as the start of the process.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProviderRefusal {
    /// What the vendor refused with.
    pub kind: ProviderRefusalKind,
    /// When it was reached, browser monotonic milliseconds.
    pub at_monotonic_ms: u64,
}

/// The registry facts about one stored credential, as a surface should draw
/// them.
///
/// The value exists only while a record is stored, so a state, a method and a
/// plan-backing can never describe nothing — the same shape the Core API's
/// `StoredCredentialView` carries, established here so the projection is a
/// field-for-field map rather than a place where absence is re-derived.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StoredCredentialView {
    /// The method the stored credential was minted for.
    pub auth_method: ProviderAuthMethod,
    /// Whether a request may proceed on it. Never `Absent`: an absent record
    /// projects no view at all.
    pub state: CredentialState,
    /// Whether access is backed by a plan rather than metered billing. Read
    /// from the credential in use, never from a stored flag (decisions 0015
    /// and 0029).
    pub subscription_backed: bool,
}

/// One provider as a surface should draw it.
#[derive(Clone, Debug, PartialEq, Eq)]
// Four independent yes/no facts, not a state machine wearing flags: a row can
// be signing in while disabled, or unconfigurable with its endpoint refused.
// Folding any two into an enum would invent a relationship the plane does not
// have.
#[allow(clippy::struct_excessive_bools)]
pub struct ProviderView {
    /// The identity.
    pub provider_id: ProviderId,
    /// The name to show.
    pub display_name: String,
    /// Catalog or the person's own.
    pub origin: ProviderOrigin,
    /// Every method this provider offers, in catalog order. A person's own
    /// provider offers a key.
    pub auth_methods: Vec<ProviderAuthMethod>,
    /// The stored credential's registry facts, absent when nothing is stored.
    pub stored: Option<StoredCredentialView>,
    /// Whether a sign-in flow is currently admitted and pending.
    pub signing_in: bool,
    /// The catalog kill switch. A disabled provider is still listed so a
    /// surface can say it was switched off rather than hiding it; a person's
    /// own provider has no switch and is always on.
    pub enabled: bool,
    /// The endpoint's host, for a person's own provider only. Never a path or
    /// a query, because this value exists to be shown.
    pub endpoint_host: Option<String>,
    /// The whole base address of a person's own provider, absent for a catalog
    /// row. Beside the host rather than instead of it: the host is what a
    /// disclosure line may show, and this is what an edit screen has to put
    /// back in the field a person typed it into.
    pub endpoint_base: Option<String>,
    /// What this provider last refused a request with while its credential was
    /// usable, absent when there has been none.
    pub last_refusal: Option<ProviderRefusal>,
    /// Whether this build can act on the row: at least one declared method
    /// has a working entry path in this binary.
    pub configurable: bool,
    /// Whether OAUTH on this row means a plan, as the catalog states it.
    pub subscription: bool,
    /// The served catalog asked to move this provider's endpoint host and
    /// the guard refused. A surface states it and pauses key entry.
    pub endpoint_changed: bool,
    /// The host it asked for, present exactly while `endpoint_changed` is
    /// true. Disclosure only: a surface may name what it did not connect to,
    /// and nothing ever connects to it.
    pub refused_endpoint_host: Option<String>,
    /// Which layer supplied this row: compiled, served, or the person's own.
    pub catalog_layer: CatalogLayer,
    /// The standing choice for this provider, absent when nobody made one.
    pub preference: ProviderModelPreference,
    /// The catalog's setup facts for this vendor, empty when it carries none
    /// and always empty for a person's own provider — nobody publishes a key
    /// prefix for a server they run themselves.
    pub presentation: ProviderPresentation,
}

/// What the plane asks the browser to do after a change.
///
/// There is exactly one, and it needs no contract vocabulary beyond what the
/// account plane already established. A provider sign-in would need more —
/// `OAuthSurfaceRequest` is typed on `AccountAuthMethod` and every member of
/// `AccountNetworkOperation` names an account operation, so a provider flow
/// cannot fill either without sibling records. Whether those records should
/// exist at all is OD-103, which decision 0049 deliberately left open, so this
/// enumeration stops where the decided part stops.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ProviderEffect {
    /// Nothing for the browser to do; the change was recorded and no handle
    /// was orphaned by it.
    None,
    /// A handle this plane no longer references. The browser owns the material
    /// behind it and must release it, or the store keeps a key for a provider
    /// the person removed.
    ReleaseHandle(CredentialHandle),
}

impl ProviderEffect {
    /// The handle to release, if this effect releases one.
    pub fn released_handle(&self) -> Option<&CredentialHandle> {
        match self {
            Self::None => None,
            Self::ReleaseHandle(handle) => Some(handle),
        }
    }
}
