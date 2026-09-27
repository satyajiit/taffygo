// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The provider and model descriptors, as data.
//!
//! A provider is an identity: endpoints, auth methods, and the protocol family
//! its models speak. A model is a descriptor under a provider. Neither is code.
//! A new provider or a new model on an existing protocol family is a catalog
//! change; a new protocol family is model router code.
//!
//! What the descriptors deliberately omit is as load-bearing as what they
//! carry. There is no general capability matrix. Exactly one capability fact
//! is carried, [`Model::tool_calling`], and it is carried because it is a
//! *routing precondition* rather than a description: a model that cannot
//! answer with a tool call must not be handed tools, and the only alternative
//! to a field is a rule enforced where the catalog is authored. That rule used
//! to live only in the baseline generator, which sees the source and neither
//! of the two layers that merge over it — so a served overlay or a user
//! override could catalog a model for a task role and nothing would notice.
//! Anything that only changes how a request is encoded still lives in
//! [`Model::compat`], not in a new field.

use std::collections::BTreeMap;

use crate::ids::{ModelId, ModelKey, ProviderId};
use crate::money::{Currency, Micros};
use crate::thinking::ThinkingLevels;
use crate::time::Timestamp;

/// Catalog schema version this build understands.
///
/// An entry declaring anything else is dropped. Unknown versions fail closed at
/// entry granularity, never by refusing the whole document.
pub const SUPPORTED_SCHEMA_VERSION: u32 = 1;

/// A protocol family the model router can speak.
///
/// Adapters own request construction, streaming decode, thinking translation,
/// cache markers, and error normalization. This enumeration is closed: an
/// unrecognized value drops the entry rather than guessing a nearest match.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum WireApi {
    /// The messages family with typed content blocks and signed thinking.
    AnthropicMessages,
    /// The responses family.
    OpenAiResponses,
    /// The completions compatibility dialect most aggregators and local
    /// servers speak, refined per model by [`Model::compat`].
    OpenAiCompletions,
    /// The generative-language family.
    GoogleGenerativeLanguage,
    /// The responses family as the subscription endpoint speaks it.
    ///
    /// The same shape reached at a different address on a subscription
    /// credential, and not a spelling of [`Self::OpenAiResponses`] that the
    /// platform endpoint would accept: it refuses a stored response, and the
    /// subscription endpoint refuses one that is not. A vendor reachable both
    /// ways is still one catalog row — which of the two families is spoken is
    /// decided by the credential in play
    /// ([`Provider::wire_api_for`]), never by a second descriptor.
    OpenAiCodexResponses,
    /// The Cloud Code Assist family one subscription plan is served through.
    ///
    /// Not a spelling of [`Self::GoogleGenerativeLanguage`] that either
    /// endpoint would accept. The request that family writes is nested inside
    /// a routing envelope this one requires and the reply arrives wrapped in
    /// another, so a body built for one is refused by the other and a reply
    /// read by the wrong row is read as an empty answer rather than as an
    /// error. It is reached only with a subscription credential, which is why
    /// a person's own endpoint may never name it.
    GoogleCloudCodeAssist,
}

impl WireApi {
    /// Whether the family takes a named effort value rather than a token
    /// budget for its thinking control.
    ///
    /// Budget-mapped families convert a ladder level into a token budget and
    /// always reserve an answer allowance; effort-mapped families send the
    /// value the catalog supplies.
    pub fn is_effort_mapped(self) -> bool {
        match self {
            // Gemini 3 through Cloud Code Assist takes a named thinking level
            // rather than a token budget, which is the one place this family
            // differs from the generative-language row on a control the
            // catalog supplies rather than a field name.
            Self::OpenAiResponses
            | Self::OpenAiCompletions
            | Self::OpenAiCodexResponses
            | Self::GoogleCloudCodeAssist => true,
            Self::AnthropicMessages | Self::GoogleGenerativeLanguage => false,
        }
    }
}

/// How a provider proves who it is.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum AuthMethod {
    /// A stored secret sent as a header value.
    ApiKey,
    /// An authorization flow producing refreshable tokens.
    Oauth,
}

/// Where a provider's model list comes from.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ModelSource {
    /// The catalog carries the model list.
    StaticCatalog,
    /// The model list is an authenticated remote call whose result refreshes
    /// into the same on-device cache under the same parsing rules.
    DynamicListing,
}

/// The vendor-neutral role a model may serve.
///
/// The router addresses models by role, never by provider name, so per-role
/// selection stays configuration of the one assistant.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ModelRole {
    /// Planning, analysis, and result synthesis.
    PrimaryReasoning,
    /// Low-latency page triage and extraction assistance.
    FastBrowsing,
    /// The separately permitted targeted image path.
    Vision,
    /// Retrieval support, cataloged only where an approved retrieval design
    /// exists.
    Embedding,
}

impl ModelRole {
    /// Whether filling this role means being handed the tool vocabulary.
    ///
    /// Every role but embedding is task work, and task work is driven by tool
    /// calls. The match is exhaustive with no catch-all so that adding a role
    /// is a compile error here rather than a role that silently inherits
    /// whichever answer happened to be the default — which is the difference
    /// between deciding the tool question and forgetting it.
    pub fn requires_tool_calling(self) -> bool {
        match self {
            Self::PrimaryReasoning | Self::FastBrowsing | Self::Vision => true,
            Self::Embedding => false,
        }
    }
}

/// What a model accepts as input.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum InputModality {
    /// Text.
    Text,
    /// Images.
    Image,
}

/// One long-context pricing tier.
///
/// Tiers are selected by total input size and are strictly ascending by
/// [`LongContextTier::min_input_tokens`]; validation refuses any other order so
/// tier selection is a total function.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct LongContextTier {
    /// Smallest total input size this tier applies to.
    pub min_input_tokens: u64,
    /// Input rate in micro-units per million tokens.
    pub input_micros_per_million: u64,
    /// Output rate in micro-units per million tokens.
    pub output_micros_per_million: u64,
    /// Cache-read rate in micro-units per million tokens.
    pub cache_read_micros_per_million: u64,
    /// Cache-write rate in micro-units per million tokens.
    pub cache_write_micros_per_million: u64,
}

/// The four rates that price one request.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Rates {
    /// Input rate in micro-units per million tokens.
    pub input: u64,
    /// Output rate in micro-units per million tokens.
    pub output: u64,
    /// Cache-read rate in micro-units per million tokens.
    pub cache_read: u64,
    /// Cache-write rate in micro-units per million tokens.
    pub cache_write: u64,
}

/// Whether a price is billed by the provider or imputed.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum PriceBasis {
    /// The provider meters and bills this usage.
    Metered,
    /// Access is backed by a user plan, so the provider bills nothing per
    /// request. The catalog carries equivalent rates so usage accounting and
    /// budget enforcement still value the request, and every record says the
    /// value was imputed.
    Implied,
}

/// A versioned price snapshot.
///
/// A price change is a new snapshot and triggers a recompute; nothing here
/// drifts silently.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct PriceSnapshot {
    /// Version of this snapshot, as published.
    pub snapshot_version: String,
    /// Currency of every rate below.
    pub currency: Currency,
    /// Whether the rates are billed or imputed.
    pub basis: PriceBasis,
    /// Input rate in micro-units per million tokens.
    pub input_micros_per_million: u64,
    /// Output rate in micro-units per million tokens.
    pub output_micros_per_million: u64,
    /// Cache-read rate in micro-units per million tokens.
    pub cache_read_micros_per_million: u64,
    /// Cache-write rate in micro-units per million tokens.
    pub cache_write_micros_per_million: u64,
    /// Tiers selected by total input size, ascending.
    #[serde(default)]
    pub long_context_tiers: Vec<LongContextTier>,
}

impl PriceSnapshot {
    /// The rates that apply to a request whose total input is `input_tokens`.
    ///
    /// The highest tier whose threshold the input reaches wins; below every
    /// threshold the base rates apply.
    pub fn rates_for(&self, input_tokens: u64) -> Rates {
        let mut rates = Rates {
            input: self.input_micros_per_million,
            output: self.output_micros_per_million,
            cache_read: self.cache_read_micros_per_million,
            cache_write: self.cache_write_micros_per_million,
        };
        for tier in &self.long_context_tiers {
            if input_tokens >= tier.min_input_tokens {
                rates = Rates {
                    input: tier.input_micros_per_million,
                    output: tier.output_micros_per_million,
                    cache_read: tier.cache_read_micros_per_million,
                    cache_write: tier.cache_write_micros_per_million,
                };
            }
        }
        rates
    }

    /// Prices a token count at the input rate, for estimates.
    pub fn price_input(&self, tokens: u64) -> Option<Micros> {
        Micros::for_tokens(self.rates_for(tokens).input, tokens)
    }
}

/// A provider entry.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct Provider {
    /// Stable key, and the key the credential record is filed under.
    pub provider_id: ProviderId,
    /// Name a surface may render.
    pub display_name: String,
    /// Default protocol family; a model may override it.
    pub wire_api: WireApi,
    /// Default endpoint; a model may override it.
    pub default_endpoint: Endpoint,
    /// The family this vendor speaks to a subscription credential, when that
    /// is not the family a key reaches.
    ///
    /// Decision 0029 section 2 allows one descriptor per vendor and derives
    /// the class from the credential, so a vendor reachable by both a key and
    /// a subscription is one row with two answers rather than two rows. Absent
    /// is the ordinary case and means the vendor speaks one family however it
    /// is authenticated.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub oauth_wire_api: Option<WireApi>,
    /// The endpoint a subscription credential is spent at, when that is not
    /// the endpoint a key is spent at.
    ///
    /// Read only for a credential whose method is
    /// [`AuthMethod::Oauth`]; a key never reaches it, which is what keeps one
    /// row from sending a key somewhere the vendor's key endpoint is not.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub oauth_endpoint: Option<Endpoint>,
    /// Accepted auth methods, at least one.
    ///
    /// A keyless local server still declares [`AuthMethod::ApiKey`] with an
    /// empty resolution, so "is this provider configured?" has one answer
    /// across every provider class.
    pub auth_methods: Vec<AuthMethod>,
    /// Whether authorized access is backed by a user plan.
    pub subscription: bool,
    /// Non-secret extra headers.
    #[serde(default)]
    pub static_headers: BTreeMap<String, String>,
    /// Where the model list comes from.
    pub model_source: ModelSource,
    /// Kill switch. An absent or unreadable value drops the entry.
    pub enabled: bool,
    /// The few setup facts a surface cannot derive (decision 0094).
    #[serde(default)]
    pub presentation: Presentation,
    /// Schema version of this entry.
    pub schema_version: u32,
}

impl Provider {
    /// The family this provider speaks to a credential of `method`.
    ///
    /// A method the row says nothing extra about reaches the default, which is
    /// what keeps a vendor with one family answering the same way however it
    /// is authenticated. The argument is the *credential registry's* answer,
    /// never a guess made from the shape of a stored token: a token's prefix
    /// is a convention the vendor may change, and reading a request family out
    /// of one would send a subscription body to a key endpoint the first time
    /// it did.
    pub fn wire_api_for(&self, method: AuthMethod) -> WireApi {
        match method {
            AuthMethod::Oauth => self.oauth_wire_api.unwrap_or(self.wire_api),
            AuthMethod::ApiKey => self.wire_api,
        }
    }

    /// The endpoint a credential of `method` is spent at.
    pub fn endpoint_for(&self, method: AuthMethod) -> &Endpoint {
        match method {
            AuthMethod::Oauth => self
                .oauth_endpoint
                .as_ref()
                .unwrap_or(&self.default_endpoint),
            AuthMethod::ApiKey => &self.default_endpoint,
        }
    }
}

/// What a setup surface needs and cannot work out for itself.
///
/// Behavioural rather than decorative: a form that knows the prefix can say a
/// pasted string does not look like this vendor's key before spending a
/// bounded completion proving it. Deliberately no prose — what to try when a
/// key is refused is a sentence, and a served sentence is one nobody can
/// translate in a product where every user-visible string is externalized.
#[derive(Clone, Debug, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct Presentation {
    /// What this vendor's keys begin with.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub key_prefix: Option<String>,
    /// Where a person creates a key. `https` only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub get_key_url: Option<String>,
    /// Where the vendor documents this endpoint. `https` only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub docs_url: Option<String>,
}

impl Presentation {
    /// Whether the catalog said anything at all.
    pub const fn is_empty(&self) -> bool {
        self.key_prefix.is_none() && self.get_key_url.is_none() && self.docs_url.is_none()
    }
}

/// A model entry.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct Model {
    /// Provider-scoped stable id.
    pub model_id: ModelId,
    /// Owning provider.
    pub provider_id: ProviderId,
    /// Protocol family, overriding the provider default when present.
    #[serde(default)]
    pub wire_api: Option<WireApi>,
    /// Endpoint, overriding the provider default when present.
    #[serde(default)]
    pub endpoint: Option<Endpoint>,
    /// Name a surface may render.
    pub display_name: String,
    /// Roles this model may serve.
    pub roles: Vec<ModelRole>,
    /// Accepted input modalities.
    pub input_modalities: Vec<InputModality>,
    /// Whether the model produces thinking output.
    pub reasoning: bool,
    /// Whether the model can be handed tools and answer with a tool call.
    ///
    /// **This field carries no `serde(default)`, and must never be given
    /// one.** The catalog is remote input, and a record that omits the field
    /// is a record nobody finished — which is a different fact from either
    /// answer. Defaulting it to `false` would quietly withdraw a working model
    /// from every task role; defaulting it to `true` would let the router hand
    /// tools to a model that answers a tool call as prose, and the symptom of
    /// that is a task that loops producing plausible text and never acts. The
    /// decoder therefore drops an entry that does not say
    /// ([`decode`][crate::catalog::decode]), which is the same fail-closed
    /// treatment the kill switch gets.
    pub tool_calling: bool,
    /// Per-level mapping onto the internal ladder.
    #[serde(default)]
    pub thinking_levels: ThinkingLevels,
    /// Total context window in tokens.
    pub context_window: u64,
    /// Largest answer allowance in tokens.
    pub max_output_tokens: u64,
    /// Price snapshot.
    pub cost: PriceSnapshot,
    /// Dialect overrides the adapter reads verbatim.
    ///
    /// Values are opaque to the router: a dialect quirk becomes catalog data
    /// first and adapter code only when genuinely new behavior is required.
    #[serde(default)]
    pub compat: BTreeMap<String, String>,
    /// Sampling defaults the adapter reads verbatim.
    #[serde(default)]
    pub sampling_defaults: BTreeMap<String, String>,
    /// Kill switch. An absent or unreadable value drops the entry.
    pub enabled: bool,
    /// Schema version of this entry.
    pub schema_version: u32,
}

impl Model {
    /// The key that names this model.
    pub fn key(&self) -> ModelKey {
        ModelKey::new(self.provider_id.clone(), self.model_id.clone())
    }

    /// Whether the model is cataloged for `role`.
    pub fn serves(&self, role: ModelRole) -> bool {
        self.roles.contains(&role)
    }

    /// Whether the model accepts `modality`.
    pub fn accepts(&self, modality: InputModality) -> bool {
        self.input_modalities.contains(&modality)
    }
}

/// Why an endpoint a catalog document named was refused.
///
/// The catalog vocabulary and only that. An address a person supplied is
/// refused in [`UserEndpointError`]'s words instead, because it is a different
/// question asked of an address a different authority named.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EndpointError {
    /// The value was not an absolute `https` URL.
    NotHttps,
    /// The value had no host.
    NoHost,
    /// The value was longer than the accepted bound.
    TooLong,
}

impl core::fmt::Display for EndpointError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        let text = match self {
            Self::NotHttps => "endpoint is not an absolute https URL",
            Self::NoHost => "endpoint has no host",
            Self::TooLong => "endpoint is longer than the accepted bound",
        };
        f.write_str(text)
    }
}

/// Why an address a person supplied for their own server was refused.
///
/// A separate list from [`EndpointError`], and separate is the point: the two
/// constructors return different error types, so widening one into the other
/// is a change to every signature that names it rather than a deleted line.
/// Decision 0096 section 2 requires exactly that, and the browser already
/// honours it literally — `CheckCatalogProviderEndpoint` and
/// `CheckCustomProviderEndpoint` are two functions that never call one
/// another, with two refusal vocabularies of their own.
///
/// What is deliberately *not* here is the cleartext rule. Decision 0096
/// section 3 admits plain http only to a literal local address, and that
/// judgement belongs to the browser: it holds the register, it owns the
/// network stack that canonicalizes a host before any range is consulted, and
/// it refuses a model request whose address is not byte-identical to a string
/// the person registered. So the core cannot name a host or a path nobody
/// typed however lax it is here, and a second address policy written in this
/// crate could only ever be a second opinion — one that breaks a server
/// somebody actually runs on the day it disagrees with the authority.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UserEndpointError {
    /// The value was longer than the accepted bound.
    TooLong,
    /// The value began with neither `https://` nor `http://`, or named no
    /// host after the scheme.
    NotAnAddress,
    /// The authority carried a `user:password@` prefix.
    CarriesCredentials,
    /// The value carried a query.
    CarriesQuery,
    /// The value carried a fragment.
    CarriesFragment,
}

impl core::fmt::Display for UserEndpointError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        let text = match self {
            Self::TooLong => "the address is longer than the accepted bound",
            Self::NotAnAddress => "the address names no host over http or https",
            Self::CarriesCredentials => "the address carries a user name and password",
            Self::CarriesQuery => "the address carries a query",
            Self::CarriesFragment => "the address carries a fragment",
        };
        f.write_str(text)
    }
}

/// Longest endpoint either rule accepts, in bytes.
///
/// One number for both, because it is the contract's bound rather than either
/// rule's opinion: `MAX_PROVIDER_ENDPOINT_BYTES` is what a command may carry,
/// and an address longer than that never reaches this crate to be judged.
pub const MAX_ENDPOINT_LEN: usize = 512;

/// A validated base URL.
///
/// One type and two constructors, and which one made a value records which
/// authority named the address rather than how strict somebody felt.
/// [`Endpoint::new`] is the catalog rule: a served document may name an
/// `https` origin and nothing else. [`Endpoint::user_base_url`] is the rule
/// for the one address a person typed for a server they run themselves, where
/// the port and the base path are theirs and plain http reaches a machine on
/// the desk in front of them (decision 0096).
///
/// Neither resolves a name. Whether a host is private or loopback — and
/// therefore whether a provider is a local endpoint — is answered by the host
/// that owns the network stack, not by string inspection in the model router.
///
/// Deserialization goes through [`Endpoint::new`], which is right because the
/// only documents this crate decodes are catalog documents. A person's own
/// address is built from the typed contract and never round-trips through
/// serde; a path that made it do so would find `http://` refused on the way
/// back in, and would have to say which of the two rules it meant to apply.
#[derive(
    Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, serde::Serialize, serde::Deserialize,
)]
#[serde(try_from = "String", into = "String")]
pub struct Endpoint(String);

impl Endpoint {
    /// Validates and wraps a base URL a catalog document named.
    ///
    /// This rule does not move. A served document may only ever name an https
    /// origin, and the endpoint guard of decision 0080 — which refuses an
    /// overlay that repoints a shipped provider — is written on top of that
    /// staying true. Relaxing it to admit a person's own server would relax
    /// what a *published* document may say about a vendor's address, which is
    /// the one catalog-shaped attack whose damage is a credential.
    pub fn new(raw: &str) -> Result<Self, EndpointError> {
        if raw.len() > MAX_ENDPOINT_LEN {
            return Err(EndpointError::TooLong);
        }
        let rest = raw
            .strip_prefix("https://")
            .ok_or(EndpointError::NotHttps)?;
        let host = rest.split('/').next().unwrap_or("");
        if host.is_empty() || host.contains('@') {
            return Err(EndpointError::NoHost);
        }
        Ok(Self(raw.to_owned()))
    }

    /// Validates and wraps the address a person supplied for their own server.
    ///
    /// A bound rather than a gate, and sized on purpose. The security decision
    /// is the browser's: decision 0096 section 1 puts the register in the
    /// browser process and checks a model request against it by byte equality,
    /// so nothing this constructor admits can make the core name a host or a
    /// path a person did not type. Too strict here and a person's own server
    /// is unreachable for a reason no surface can explain, because the address
    /// the browser accepted and registered was thrown away by the core before
    /// anything else looked at it; too lax and nothing is lost.
    ///
    /// So what is checked is the cheap structure that is true whatever the
    /// address policy turns out to be — a length, a scheme this product speaks,
    /// a host, and none of the three things a base URL may not carry. Ports and
    /// paths are allowed, and carrying them is the whole point of decision
    /// 0096: a server at `http://192.168.1.9:11434/v1` has both, and today
    /// loses both.
    ///
    /// Which addresses plain http may reach is not asked here and must not be.
    /// That is `custom_provider_endpoint_policy.cc`'s answer, made with GURL
    /// and `net::IPAddress` on a canonicalized host — the tools that get
    /// `0x7f.1` and `[::ffff:169.254.169.254]` right, and tools this crate does
    /// not have.
    pub fn user_base_url(raw: &str) -> Result<Self, UserEndpointError> {
        if raw.len() > MAX_ENDPOINT_LEN {
            return Err(UserEndpointError::TooLong);
        }
        let rest = raw
            .strip_prefix("https://")
            .or_else(|| raw.strip_prefix("http://"))
            .ok_or(UserEndpointError::NotAnAddress)?;
        // The authority ends at whichever of the three delimiters comes first,
        // so that a query typed straight onto the host is read as a query
        // rather than as part of the name — which is the order the browser
        // answers in, and the order that tells a person the one thing they
        // have to fix.
        let authority = rest.split(['/', '?', '#']).next().unwrap_or("");
        if authority.is_empty() {
            return Err(UserEndpointError::NotAnAddress);
        }
        if authority.contains('@') {
            return Err(UserEndpointError::CarriesCredentials);
        }
        if rest.contains('?') {
            return Err(UserEndpointError::CarriesQuery);
        }
        if rest.contains('#') {
            return Err(UserEndpointError::CarriesFragment);
        }
        Ok(Self(raw.to_owned()))
    }

    /// The full base URL.
    pub fn as_str(&self) -> &str {
        &self.0
    }

    /// The host, for disclosure text that must not leak a path or a query.
    ///
    /// Both schemes are stripped, because an endpoint is no longer https by
    /// construction. Reading only `https://` left an address a person typed
    /// answering `http:` — not their host, not even a host, in the roster row
    /// that names their own server and in the sentence that says where their
    /// page went.
    ///
    /// The authority still ends at the first `/` and nowhere else. An address
    /// a person supplied cannot carry a query or a fragment — the constructor
    /// above refuses both — so widening the split would change nothing for
    /// them, and it would change something for a served document: decision
    /// 0080's guard compares two of these answers, and a query glued to a
    /// baseline provider's host is a spelling it currently refuses.
    pub fn host(&self) -> &str {
        self.0
            .strip_prefix("https://")
            .or_else(|| self.0.strip_prefix("http://"))
            .unwrap_or(&self.0)
            .split('/')
            .next()
            .unwrap_or("")
    }
}

impl TryFrom<String> for Endpoint {
    type Error = EndpointError;

    fn try_from(raw: String) -> Result<Self, EndpointError> {
        Self::new(&raw)
    }
}

impl From<Endpoint> for String {
    fn from(value: Endpoint) -> Self {
        value.0
    }
}

impl core::fmt::Display for Endpoint {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(&self.0)
    }
}

/// The header of a catalog document.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct CatalogHeader {
    /// Schema version of the document itself.
    pub schema_version: u32,
    /// Publisher-assigned version of this catalog.
    pub catalog_version: String,
    /// When the document was generated.
    ///
    /// The merge compares this across layers: an overlay that is not newer
    /// than the embedded baseline is ignored, so a freshly built release never
    /// downgrades itself to a staler served catalog.
    pub generated_at: Timestamp,
}

/// A whole catalog document.
#[derive(Clone, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct CatalogDocument {
    /// Document header.
    #[serde(flatten)]
    pub header: CatalogHeader,
    /// Provider entries.
    pub providers: Vec<Provider>,
    /// Model entries.
    pub models: Vec<Model>,
}
