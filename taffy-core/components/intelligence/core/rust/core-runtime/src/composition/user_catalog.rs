// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The person's own layer of the merged catalog (decision 0096 section 4).
//!
//! The merge has always had three layers and only two of them were ever
//! filled. A person could define their own provider, the plane stored it, the
//! roster drew it — and the third layer stayed empty, so there was never a
//! model under that provider to route to. This module is the projection that
//! fills it: every provider a person defined, every model their endpoint said
//! it has, and every model a connected provider listed for them, as one
//! catalog document the merge lays over the served overlay.
//!
//! It is a pure function of what the planes hold and it is rebuilt rather than
//! kept. A held copy is a copy that can be right about a provider the person
//! removed a moment ago, which is the staleness class decision 0049 was written
//! about; rebuilding costs one small document per command and cannot be wrong.
//!
//! Nothing here is decoded. The published catalog and a served overlay are
//! remote documents and go through the fail-closed decoder for exactly that
//! reason; what a person typed into their own device arrived through the typed
//! contract, already bounded field by field, and re-encoding it as JSON so it
//! could be parsed back would add a serializer and prove nothing. A **fetched**
//! listing is remote and does go through the decoder — see
//! [`crate::provider_listing`] — and arrives here already decoded.

use std::collections::BTreeMap;

use model_router::catalog::types::{
    AuthMethod, CatalogDocument, CatalogHeader, Model, ModelSource, Presentation, PriceBasis,
    PriceSnapshot, Provider, SUPPORTED_SCHEMA_VERSION,
};
use model_router::catalog::{InputModality, ModelRole, WireApi};
use model_router::money::Currency;
use model_router::thinking::ThinkingLevels;
use model_router::time::Timestamp;
use model_router::wire::{detect, ServerKind};

use crate::provider::{CustomModel, CustomProvider, ProviderWireApi};

/// The window the router is told about when the endpoint did not state one.
///
/// Zero is what the contract uses for "the endpoint said nothing", and zero
/// cannot travel into the catalog: the catalog reads a zero window as a model
/// that accepts nothing, refuses the entry for impossible limits, and every
/// request against it would be refused before it was made. So a number has to
/// be chosen, and the safe direction is small. A request planned against a
/// window this size fits on any of the five servers this product knows how to
/// talk to; a person whose model is bigger loses headroom they never asked for,
/// which is a smaller loss than a model they cannot use at all. The moment the
/// endpoint states a window, this value stops applying.
pub const UNSTATED_CONTEXT_WINDOW: u64 = 8_192;

/// The answer allowance the router is told about when none was stated.
///
/// Bounded by the window for the same reason the catalog is: an allowance
/// larger than the window is a pair no server can honour.
pub const UNSTATED_MAX_OUTPUT_TOKENS: u64 = 2_048;

/// The price snapshot version a person's own endpoint is filed under.
const OWN_MACHINE_SNAPSHOT: &str = "user-endpoint";

/// The catalog version the person's own layer declares.
const USER_LAYER_VERSION: &str = "user-layer";

/// Builds the person's own layer from what the two planes hold.
///
/// `listed` is already-decoded models from a fetched listing; they are merged
/// into the same document as the person's own providers because they are the
/// same kind of fact — models this device knows about that no published
/// catalog does — and because one document means one place every
/// `MergedCatalog::build` call site has to be told about.
///
/// The header is deliberately unremarkable. The merge never adopts a user
/// layer's header and never compares its generation time, so the time carried
/// here is [`Timestamp::floor`]: the value that cannot win an ordering against
/// a real one, which is the honest thing to say about a document that was
/// never generated anywhere.
pub fn user_catalog_document<'a>(
    baseline: &CatalogDocument,
    custom: impl Iterator<Item = &'a CustomProvider>,
    listed: impl Iterator<Item = &'a Model>,
) -> CatalogDocument {
    let currency = baseline_currency(baseline);
    let mut providers = Vec::new();
    let mut models: Vec<Model> = Vec::new();
    for entry in custom {
        providers.push(custom_provider_row(entry));
        let Some(currency) = currency else {
            continue;
        };
        let compat = detected_compat(entry.detected_server);
        models.extend(
            entry
                .models
                .iter()
                .filter_map(|model| custom_model_row(entry, model, currency, &compat)),
        );
    }
    models.extend(listed.cloned());
    CatalogDocument {
        header: CatalogHeader {
            schema_version: SUPPORTED_SCHEMA_VERSION,
            catalog_version: USER_LAYER_VERSION.to_owned(),
            generated_at: Timestamp::floor(),
        },
        providers,
        models,
    }
}

/// The currency the person's own layer prices in.
///
/// Borrowed from the compiled baseline rather than typed here. A task budget
/// refuses a price snapshot in a currency it was not set in, so a layer naming
/// its own currency would stop routing the moment the published catalog moved
/// to another one — and the failure would arrive as a budget refusal rather
/// than as two documents disagreeing about money. A baseline that prices
/// nothing has no currency to borrow, and a layer with no currency carries no
/// models rather than inventing one.
///
/// A fetched listing is priced in it too ([`crate::provider_listing`]): a
/// provider states a number and not a currency, and the only currency this
/// device can act in is the one its budgets were set in.
pub fn baseline_currency(baseline: &CatalogDocument) -> Option<Currency> {
    baseline.models.first().map(|model| model.cost.currency)
}

/// One of a person's own providers, as a catalog row.
fn custom_provider_row(entry: &CustomProvider) -> Provider {
    Provider {
        provider_id: entry.provider_id.as_router().clone(),
        display_name: entry.display_name.as_str().to_owned(),
        wire_api: catalog_wire_api(entry.wire_api),
        default_endpoint: entry.endpoint.clone(),
        // A person's own endpoint is one address reached one way. There is no
        // second address and no subscription flow behind it, so neither
        // override is stated rather than being stated as a copy of the first.
        oauth_wire_api: None,
        oauth_endpoint: None,
        // Key only, and a keyless local server still declares it with nothing
        // filed against it, so "is this provider configured?" has one answer
        // across every provider class.
        auth_methods: vec![AuthMethod::ApiKey],
        // Not a claim about a plan, and it never reaches the disclosure that
        // asks about one: that question is only put to an OAuth credential at
        // a non-local address, and a person's own endpoint is neither. What
        // this field settles here is the catalog's own invariant that an
        // imputed price must have something standing behind the access rather
        // than a per-request bill — which is exactly the arrangement a machine
        // somebody already owns is.
        subscription: true,
        static_headers: BTreeMap::new(),
        // The models arrived with the save (decision 0096 section 4). Nothing
        // fetches them again, so this row's list is the whole list.
        model_source: ModelSource::StaticCatalog,
        // A person's own provider has no kill switch. Nobody publishes one for
        // a server they run themselves, and a switch nobody can flip is a
        // field that can only ever be wrong.
        enabled: true,
        // Nobody publishes a key prefix or a documentation page for a server
        // somebody runs at home, so there is nothing here that would not be
        // invented.
        presentation: Presentation::default(),
        schema_version: SUPPORTED_SCHEMA_VERSION,
    }
}

/// One model a person's endpoint reported, as a catalog row.
///
/// `None` drops the model. There are exactly two ways to reach it and both are
/// the endpoint describing something that cannot be routed to: a model it says
/// cannot call tools, and a pair of limits it stated that contradict each
/// other. Neither is repaired here — a repaired entry is an entry nobody
/// published, and the catalog's own validator says the same thing about the
/// documents it reads.
fn custom_model_row(
    provider: &CustomProvider,
    model: &CustomModel,
    currency: Currency,
    compat: &BTreeMap<String, String>,
) -> Option<Model> {
    // A probe reports no modality, so the image role is not asked for here
    // and the row below stays text-only. The two go together.
    let roles = roles_for(model.reasoning, model.tool_calling, /*vision=*/ false)?;
    let (context_window, max_output_tokens) = limits_for(
        match u64::from(model.context_window) {
            0 => UNSTATED_CONTEXT_WINDOW,
            stated => stated,
        },
        u64::from(model.max_output_tokens),
    )?;
    Some(Model {
        model_id: model.model_id.clone(),
        provider_id: provider.provider_id.as_router().clone(),
        // The provider row already says both, and a per-model copy is a second
        // place they could disagree. An endpoint that needs a different address
        // per model is a second provider.
        wire_api: None,
        endpoint: None,
        display_name: model.display_name.clone(),
        roles,
        // Nothing in the probe reports an image path, and a modality nobody
        // reported is not one to claim: the vision role is refused above for
        // the same reason.
        input_modalities: vec![InputModality::Text],
        reasoning: model.reasoning,
        tool_calling: model.tool_calling,
        // Every rung through HIGH takes the adapter's own mapping, which is
        // what an entry that says nothing about the ladder means. The endpoint
        // said nothing about it, so that is what it says here too.
        thinking_levels: ThinkingLevels::adapter_defaults(),
        context_window,
        max_output_tokens,
        cost: own_machine_price(currency),
        compat: compat.clone(),
        sampling_defaults: BTreeMap::new(),
        enabled: true,
        schema_version: SUPPORTED_SCHEMA_VERSION,
    })
}

/// The roles a model with these two capability facts may serve, or `None` when
/// it may serve none.
///
/// One rule for both halves of the person's own layer — an endpoint they typed
/// and an aggregator they connected — because the question is the same one and
/// two answers to it would be two products. Written down, because nothing a
/// probe or a listing reports states it:
///
/// - A model that cannot call tools serves no role at all. This is not a
///   choice made here — every role but embedding is task work, task work is
///   driven by tool calls, and the catalog's own validator refuses the pair
///   (`TaskRoleWithoutToolCalling`). Filing such a model under the embedding
///   role to give it *a* role would put a chat model in retrieval candidate
///   lists, so it is dropped instead. The place to change this is the
///   invariant, not this function.
/// - A model that calls tools and thinks is the one to plan with, and only
///   that: the fast-browsing rung is latency-sensitive and a thinking phase is
///   the opposite of low latency.
/// - A model that calls tools and does not think serves both rungs. It has to
///   serve the reasoning rung as well, because a person whose only server has
///   one such model must still be able to run a task — planning with no
///   thinking phase is exactly what the bottom of the ladder is for.
///
/// - A model that accepts images serves the image role *as well*. It is added
///   rather than substituted, because accepting a picture does not stop a
///   model planning: the two facts are independent and a model that has both
///   is offered for both. The caller must state the modality it read; a caller
///   that read none passes `false` and the role is not given, which is the
///   behaviour every caller had before there was anything to read.
///
/// The image role travels with `InputModality::Image` on the row it is written
/// onto and cannot be given without it — the catalog's own validator refuses
/// that pair (`VisionRoleWithoutImageInput`), so a caller that gives this
/// function `true` and then writes a text-only modality list has written a
/// document the decoder throws away.
pub fn roles_for(reasoning: bool, tool_calling: bool, vision: bool) -> Option<Vec<ModelRole>> {
    if !tool_calling {
        return None;
    }
    let mut roles = if reasoning {
        vec![ModelRole::PrimaryReasoning]
    } else {
        vec![ModelRole::PrimaryReasoning, ModelRole::FastBrowsing]
    };
    if vision {
        roles.push(ModelRole::Vision);
    }
    Some(roles)
}

/// The window and answer allowance the router is told about.
///
/// A zero allowance is one nobody stated and takes the standing default,
/// bounded by whatever window applies. A pair that was stated and contradicts
/// itself — an allowance larger than the window — drops the model, because
/// there is no reading of it that is true and clamping one half would publish
/// a limit nobody stated. The window is already resolved by the caller, which
/// is the half that knows whether zero meant silence.
pub fn limits_for(context_window: u64, max_output_tokens: u64) -> Option<(u64, u64)> {
    let max_output_tokens = match max_output_tokens {
        0 => UNSTATED_MAX_OUTPUT_TOKENS.min(context_window),
        stated => stated,
    };
    if max_output_tokens > context_window || context_window == 0 {
        return None;
    }
    Some((context_window, max_output_tokens))
}

/// What a person's own machine costs per request.
///
/// Zero, and imputed rather than metered. The distinction is the whole point:
/// `IMPLIED` is the catalog's word for "the provider bills nothing per request
/// and the value here was imputed", which is exactly true of a server somebody
/// already paid for — while `METERED` at zero would be this product asserting
/// that a meter exists and reads nothing. A guessed number would be worse than
/// either, because it would feed a spend cap and the cap would enforce a
/// fiction. Every usage record made against this snapshot says the value was
/// imputed, and the ledger totals it separately for that reason.
fn own_machine_price(currency: Currency) -> PriceSnapshot {
    PriceSnapshot {
        snapshot_version: OWN_MACHINE_SNAPSHOT.to_owned(),
        currency,
        basis: PriceBasis::Implied,
        input_micros_per_million: 0,
        output_micros_per_million: 0,
        cache_read_micros_per_million: 0,
        cache_write_micros_per_million: 0,
        long_context_tiers: Vec::new(),
    }
}

/// The dialect overrides a detected server needs, as catalog data.
///
/// [`detect`] is the router's own table and the only one: a second copy here
/// would be two answers to "what does an Ollama endpoint call its answer
/// allowance", and the first request written under the wrong one is a 400
/// nobody can read. What travels is the three scalar overrides, which is
/// everything the detected base states for all five servers today, and
/// everything a map can carry — the two list-valued fields are slices by
/// design and stay the caller's.
///
/// An endpoint that detected as nothing carries no overrides. Absence is the
/// answer, not a sixth server whose row happens to be the platform's.
fn detected_compat(detected: Option<ServerKind>) -> BTreeMap<String, String> {
    let mut overrides = BTreeMap::new();
    let Some(kind) = detected else {
        return overrides;
    };
    let compat = detect(kind);
    overrides.insert(
        "answer_tokens_field".to_owned(),
        compat.answer_tokens_key.to_owned(),
    );
    overrides.insert(
        "reports_finish_reason".to_owned(),
        compat.reports_finish_reason.to_string(),
    );
    overrides.insert(
        "accepts_reasoning_effort".to_owned(),
        compat.accepts_reasoning_effort.to_string(),
    );
    overrides
}

/// The catalog's spelling of one wire family.
///
/// Total, because the plane's own enumeration holds neither reserved family:
/// the managed wire is the product's own envelope and the Codex responses wire
/// is one vendor's subscription endpoint, and a person's own endpoint is
/// neither.
const fn catalog_wire_api(api: ProviderWireApi) -> WireApi {
    match api {
        ProviderWireApi::AnthropicMessages => WireApi::AnthropicMessages,
        ProviderWireApi::OpenAiResponses => WireApi::OpenAiResponses,
        ProviderWireApi::OpenAiCompletions => WireApi::OpenAiCompletions,
        ProviderWireApi::GoogleGenerativeLanguage => WireApi::GoogleGenerativeLanguage,
    }
}

#[cfg(test)]
mod tests;
