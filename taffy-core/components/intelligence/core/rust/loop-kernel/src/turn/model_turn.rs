// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Composing one model turn inside the sandbox, and reading its reply there.
//!
//! Decision 0052 section 2 splits one model call across the process boundary:
//! this side decides *what* is asked and *of whom*, the browser process
//! performs the request because that is where the network stack and the
//! credential store are, and one typed completion comes back here to be read.
//!
//! Three properties are structural rather than promised.
//!
//! - **No secret can travel through this module.** The only credential-shaped
//!   value it handles is a [`CredentialHandle`] — an opaque browser
//!   secure-store reference — and `ModelRequestEffect` has no field a key could
//!   sit in. The core never composes the header the material travels in, and
//!   `core_model_effect_validation.cc` refuses every spelling of such a header
//!   on the other side, so "the credential reaches the provider one way" is a
//!   property of the pair rather than of either half.
//! - **The core names no address of its own.** Every endpoint a turn can carry
//!   was named by somebody else and is checked against that somebody on the
//!   other side. A catalog address is trimmed to an origin, because the closed
//!   wire family decides the path underneath it in the browser, from a
//!   compiled-in table — a core that could name a path could name any route on
//!   a host a person had already trusted with a key. An address a person typed
//!   for their own server travels whole, because the port and the base path
//!   are theirs, and the browser compares it byte for byte with what that
//!   person registered (decision 0096). The request says which of the two it
//!   claims, and the rule it claimed is the rule that answers it.
//! - **Reading a provider's bytes happens here, not there.** The browser holds
//!   every capability, so parsing untrusted JSON belongs in the sandbox (Rule
//!   of Two), and `ProfileModelBroker` hands the reply on unread.

use model_router::catalog::WireApi;
use model_router::money::{Currency, CurrencyError, Micros};
use model_router::route::{RouteCandidate, RoutePreference};
use model_router::{Route, RoutePlan, TaskBudget, TaskLedger};

use crate::context::LivePage;
use crate::digest::Sha256Port;
use crate::ports::{ModelRouterPort, ModelTurnFacts};
use crate::provider::{CredentialHandle, ProviderDirectory};

mod attempt;
mod body;
mod error;
mod reply;
mod routing;
mod schema;
mod shape;
mod style;

#[cfg(test)]
mod endpoint_tests;
#[cfg(test)]
mod tests;

pub use self::attempt::ComposedModelAttempt;
pub use self::error::ModelTurnError;
pub use self::reply::{
    read_model_reply, read_model_stream_terminal, ModelReplyReading, ModelReplyStream,
};
pub use self::style::ModelResponseStyle;

#[cfg(test)]
use self::body::{write_body, write_managed_body};
use self::body::{write_body_with_system, write_managed_body_with_system};

/// Tokens one answer may use.
///
/// Fixed rather than configurable, because the value bounds what a single turn
/// can cost and a bound a caller may raise is not a bound.
const ANSWER_TOKENS: u64 = 4096;

/// Bytes the browser may bring back from one call.
const MAX_COMPLETION_BYTES: u32 = 1024 * 1024;

/// The currency every compiled-in catalog price is quoted in.
const BUDGET_CURRENCY_CODE: &str = "USD";

/// Four bytes to a token. An estimate, and named as one: it feeds a cost
/// projection, never a claim about what a provider counted.
const BYTES_PER_TOKEN: u64 = 4;

/// Conservative context and spend allowance for one browser-produced image.
/// Provider usage remains the settlement authority after the call.
const MEDIA_INPUT_TOKEN_ESTIMATE: u64 = 32_768;

/// Domain separators for the two derived router identities.
///
/// `model-router`'s `TaskId` and `RequestId` are opaque sixteen-byte values,
/// and the identities this core holds are strings. They are therefore
/// *derived* rather than truncated: a digest of a domain-separated identity is
/// stable across a replay, which is what the ledger and the reply reader both
/// need, and it cannot collide by prefix the way a truncation can.
const ROUTER_TASK_DOMAIN: &[u8] = b"taffy/model-router-task/v1";
const ROUTER_REQUEST_DOMAIN: &[u8] = b"taffy/model-router-request/v1";

/// Domain separator for the managed route's canonical request identity.
///
/// Its own domain rather than a reuse of [`ROUTER_REQUEST_DOMAIN`], because
/// the two identities leave the device in different directions: the router's
/// stays inside this process, while this one is written into the request body
/// and becomes the worker's exactly-once metering key. Deriving both from the
/// same call identity under one domain would make them byte-equal, and a
/// value that crosses a trust boundary must not double as an internal key.
const MANAGED_REQUEST_DOMAIN: &[u8] = b"taffy/managed-request-id/v1";

/// Domain separator for the name a provider is told this conversation has.
///
/// Its own domain for the same reason [`MANAGED_REQUEST_DOMAIN`] has one, and
/// derived from the **task** rather than from the call: a conversation name
/// that changed every turn would name a different conversation each time,
/// which is the opposite of what it is for. Every turn of one task therefore
/// produces the same value, and two tasks never produce the same one.
///
/// The raw task identity is not sent. It is a durable internal key that
/// appears in effect identities and in the ledger, and a value that crosses to
/// a vendor must not double as one (decision 0111 section 3).
const CONVERSATION_KEY_DOMAIN: &[u8] = b"taffy/provider-conversation/v1";

/// What one composed turn is, beside the effect the browser is handed.
///
/// The two extra numbers are what reading the reply needs and the effect does
/// not carry: an overflow verdict is a comparison against the window the model
/// actually has and the allowance this request actually asked for. The page is
/// the projection this turn was built from, so a reply that designates a
/// handle resolves against the table that issued it.
/// The vocabulary one reply will arrive in.
///
/// Two members rather than an optional field, because the two readings need
/// different facts: a family reply is read by its dialect row, and a managed
/// reply is read against the canonical identity the body was written with.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ReplyWire {
    /// One of the four provider families, spoken directly.
    Family(WireApi),
    /// The managed canonical schema, spoken by the product's own worker.
    Managed {
        /// The canonical request identity the body carried, kept so the
        /// reader can refuse a response naming some other request.
        request_id: String,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub struct ComposedModelTurn {
    /// The effect the browser performs.
    pub request: core_service_types::ModelRequestEffect,
    /// The vocabulary the reply will be in.
    pub wire: ReplyWire,
    /// The chosen model's context window, in tokens.
    pub context_window: u64,
    /// The answer allowance this request asked for, in tokens.
    pub answer_tokens: u64,
    /// Request-ready direct-route substitutes in router order.
    ///
    /// The list is bounded by `model-router` and absent for managed or media
    /// calls. Keeping exact bodies here prevents a retry from re-reading a
    /// catalog, page, credential register, or assistant configuration.
    pub failover: std::collections::VecDeque<ComposedModelAttempt>,
    /// Depth of this transient call: zero for an ordinary turn and one for
    /// the single nested pass. It is never written to the task journal.
    pub nested_depth: u32,
    /// The projection this turn showed, and the numbers it issued.
    pub page: task_engine::TurnPage,
}

/// Composes one model request for `call_id`.
///
/// `page` is the live observation, if any. Handles are issued into a clone
/// and committed only when this returns `Ok`, so a refused composition does
/// not spend numbers on a request that never left (decision 0070).
pub fn compose_model_turn(
    router: &mut dyn ModelRouterPort,
    providers: &dyn ProviderDirectory,
    digest: &dyn Sha256Port,
    facts: &ModelTurnFacts,
    call_id: &str,
    page: &mut LivePage,
    person_answer: Option<&str>,
) -> Result<ComposedModelTurn, ModelTurnError> {
    compose_model_turn_with_configuration(
        router,
        providers,
        digest,
        facts,
        call_id,
        page,
        person_answer,
        None,
        ModelResponseStyle::default(),
    )
}

/// Composes a turn after a profile configuration has only narrowed the task's
/// reviewed tools and selected bounded response-style prose.
#[allow(clippy::too_many_arguments)]
pub fn compose_configured_model_turn(
    router: &mut dyn ModelRouterPort,
    providers: &dyn ProviderDirectory,
    digest: &dyn Sha256Port,
    facts: &ModelTurnFacts,
    call_id: &str,
    page: &mut LivePage,
    person_answer: Option<&str>,
    configured_tool_allowlist: &[String],
    style: ModelResponseStyle,
) -> Result<ComposedModelTurn, ModelTurnError> {
    compose_model_turn_with_configuration(
        router,
        providers,
        digest,
        facts,
        call_id,
        page,
        person_answer,
        Some(configured_tool_allowlist),
        style,
    )
}

#[allow(clippy::too_many_arguments)]
fn compose_model_turn_with_configuration(
    router: &mut dyn ModelRouterPort,
    providers: &dyn ProviderDirectory,
    digest: &dyn Sha256Port,
    facts: &ModelTurnFacts,
    call_id: &str,
    page: &mut LivePage,
    person_answer: Option<&str>,
    configured_tool_allowlist: Option<&[String]>,
    style: ModelResponseStyle,
) -> Result<ComposedModelTurn, ModelTurnError> {
    if facts.attempts_started > 1 {
        return Err(ModelTurnError::SubattemptPlanLost);
    }
    let preference = route_preference(facts.provider_route_id.as_deref())?;
    let reviewed = task_engine::EffectiveToolSet::for_template(
        facts.template_id,
        facts.milestone,
        &facts.tool_allowlist,
    );
    // `Option::map_or` evaluates its default eagerly. Using it here cloned
    // both registry vectors even when a configured allowlist was present and
    // the clone was immediately discarded. Move the reviewed set on the
    // ordinary path and allocate only for the real narrowing operation.
    let configured = match configured_tool_allowlist {
        Some(allowlist) => reviewed.narrow_by(allowlist),
        None => reviewed,
    };
    let tools = configured.with_activated(&facts.activated);
    let system_instruction = style.system_instruction_for(facts.template_id, &tools);
    let system_instruction = system_instruction.as_str();
    let empty_page_tab = empty_page_tab(facts)?;
    let discovery_tab = discovery_tab(facts)?;
    let mut preview = page.preview_for_empty_tab(empty_page_tab.as_ref(), &facts.persons_pages);
    let page_bytes = preview.text.as_ref().map_or(0, String::len);
    let carries_page_content = preview.carries_page_content;
    let page_context = routing::PageRouteContext {
        page,
        text_bytes: page_bytes,
        carries_page_content: preview.carries_page_content,
        carries_media_attachment: preview.media_attachment.is_some(),
    };
    let plan = route_plan(router, digest, facts, preference, &tools, &page_context)?;
    let managed_request_id = match plan.route {
        Route::Managed => Some(managed_request_id(digest, call_id)?),
        Route::ByoDirect => None,
    };
    // Derived on every turn rather than only where a family names a place for
    // it, because the failover candidates may not speak the family the primary
    // does and a value derived per candidate would be a second derivation to
    // keep in step. It costs one digest of a value already in hand.
    let conversation_key = conversation_key(digest, &facts.task_id)?;
    let person_line = person_answer.or(facts.nested_goal.as_deref());
    let nested_depth = u32::from(person_answer.is_none() && facts.nested_goal.is_some());
    let primary = attempt::compose(
        router,
        providers,
        plan.route,
        &plan.primary,
        &facts.transcript,
        &tools,
        &facts.task_id,
        call_id,
        preview.text.as_deref(),
        person_line,
        preview.media_attachment.as_ref(),
        system_instruction,
        managed_request_id.as_deref(),
        &conversation_key,
        carries_page_content,
    )?;
    // A media handle is one-use and the managed worker owns its own provider
    // ladder. Only an ordinary direct call can safely retain client-side
    // substitutes. Invalid substitutes are skipped; they never invalidate the
    // already-complete primary plan.
    let failover = if plan.route == Route::ByoDirect && preview.media_attachment.is_none() {
        plan.failover
            .iter()
            .filter_map(|candidate| {
                attempt::compose(
                    router,
                    providers,
                    plan.route,
                    candidate,
                    &facts.transcript,
                    &tools,
                    &facts.task_id,
                    call_id,
                    preview.text.as_deref(),
                    person_line,
                    None,
                    system_instruction,
                    None,
                    &conversation_key,
                    carries_page_content,
                )
                .ok()
            })
            .collect()
    } else {
        std::collections::VecDeque::new()
    };
    // The one-use attachment is represented by the request handle, never by
    // the page residency retained for reading the reply.
    drop(preview.media_attachment.take());
    let projection_digest = projection_digest(digest, preview.text.as_deref())?;
    let handles = preview.handles.clone();
    let turn_page = preview
        .into_turn_page(projection_digest)
        .with_discovery_tab(discovery_tab);
    let turn = ComposedModelTurn {
        request: primary.request,
        wire: primary.wire,
        context_window: primary.context_window,
        answer_tokens: primary.answer_tokens,
        failover,
        nested_depth,
        page: turn_page,
    };
    let turn = shape::validate(turn, call_id)?;
    page.commit_handles(handles);
    Ok(turn)
}

/// The tab a turn with no page projection names.
///
/// Validated to the same shape the browser issues, because the value becomes
/// the tab of every call this turn makes that designates no node.
fn empty_page_tab(
    facts: &ModelTurnFacts,
) -> Result<Option<bip_types::identity::TabId>, ModelTurnError> {
    task_owned_tab(facts.empty_page_tab_id.as_deref())
}

/// The task's own blank tab, when its consent still holds discovery authority.
///
/// Validated the same way and for the same reason: a search this turn makes
/// carries it into a canonical intent, and an empty or malformed identifier
/// there ends the walk rather than the call (decision 0224).
fn discovery_tab(
    facts: &ModelTurnFacts,
) -> Result<Option<bip_types::identity::TabId>, ModelTurnError> {
    task_owned_tab(facts.discovery_tab_id.as_deref())
}

fn task_owned_tab(
    value: Option<&str>,
) -> Result<Option<bip_types::identity::TabId>, ModelTurnError> {
    value
        .map(|value| {
            if value.is_empty()
                || value.len() > task_engine::MAX_OBSERVATION_IDENTIFIER_BYTES
                || !value
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'_'))
            {
                return Err(ModelTurnError::Overflow);
            }
            Ok(bip_types::identity::TabId::new(value))
        })
        .transpose()
}

fn route_plan(
    router: &mut dyn ModelRouterPort,
    digest: &dyn Sha256Port,
    facts: &ModelTurnFacts,
    preference: RoutePreference,
    tools: &task_engine::EffectiveToolSet,
    page_context: &routing::PageRouteContext<'_>,
) -> Result<RoutePlan, ModelTurnError> {
    let task_id =
        model_router::TaskId::from_bytes(derive_id(digest, ROUTER_TASK_DOMAIN, &facts.task_id)?);
    let request = routing::request(facts, task_id, preference, tools, page_context)?;
    let plan = router
        .route(&request, &ledger(task_id)?)
        .map_err(ModelTurnError::Route)?;
    if plan.crosses_disclosure_boundary() {
        return Err(ModelTurnError::NoDisclosedRoute);
    }
    Ok(plan)
}

fn route_preference(route_id: Option<&str>) -> Result<RoutePreference, ModelTurnError> {
    match route_id {
        Some("direct_user_key") => Ok(RoutePreference::ByoDirect),
        Some("managed_service") => Ok(RoutePreference::Managed),
        Some(task_engine::REVIEWED_NO_MODEL_ROUTE_ID) => Err(ModelTurnError::NoModelRoute),
        None | Some(_) => Err(ModelTurnError::NoDisclosedRoute),
    }
}

const fn wire_api(api: WireApi) -> core_service_types::ProviderWireApi {
    match api {
        WireApi::AnthropicMessages => core_service_types::ProviderWireApi::AnthropicMessages,
        WireApi::OpenAiResponses => core_service_types::ProviderWireApi::OpenAiResponses,
        WireApi::OpenAiCompletions => core_service_types::ProviderWireApi::OpenAiCompletions,
        WireApi::GoogleGenerativeLanguage => {
            core_service_types::ProviderWireApi::GoogleGenerativeLanguage
        }
        WireApi::OpenAiCodexResponses => core_service_types::ProviderWireApi::OpenAiCodexResponses,
        WireApi::GoogleCloudCodeAssist => {
            core_service_types::ProviderWireApi::GoogleCloudCodeAssist
        }
    }
}

/// The handle for the chosen candidate's provider, when the call needs one.
///
/// `None` is a complete answer for a candidate carrying no auth attachment — a
/// managed route, or an endpoint the person runs themselves. A candidate that
/// does need one and has none is refused rather than sent unauthenticated,
/// which would spend a round trip to learn something the device already knows.
fn credential_handle<'a>(
    providers: &'a dyn ProviderDirectory,
    candidate: &RouteCandidate,
) -> Result<Option<&'a CredentialHandle>, ModelTurnError> {
    if candidate.auth.is_none() {
        return Ok(None);
    }
    providers
        .usable_credential(&candidate.model.provider_id)
        .map(Some)
        .ok_or(ModelTurnError::CredentialMissing)
}

/// `PAGE_CONTENT` only when page-authored bytes leave (decision 0070).
const fn disclosure_class(carries_page_content: bool) -> core_service_types::DisclosureClass {
    if carries_page_content {
        core_service_types::DisclosureClass::PageContent
    } else {
        core_service_types::DisclosureClass::UserSelectedContent
    }
}

fn projection_digest(
    digest: &dyn Sha256Port,
    text: Option<&str>,
) -> Result<[u8; 32], ModelTurnError> {
    digest
        .sha256(text.unwrap_or("").as_bytes())
        .map_err(|crate::digest::DigestError::Unavailable| ModelTurnError::DigestUnavailable)
}

/// A ledger wide enough not to be a second budget.
///
/// The task's own budgets belong to the reducer and are charged there, before
/// the effect leaves — `Reducer::on_request_model_turn` charges
/// `MaxModelRequests` when the call is *requested*. This ledger exists only
/// because route selection takes one, and carrying a second set of numbers
/// here would be two places that decide whether a task may spend.
fn ledger(task_id: model_router::TaskId) -> Result<TaskLedger, ModelTurnError> {
    Ok(TaskLedger::new(
        task_id,
        TaskBudget {
            // The one currency every catalog price snapshot is quoted in. A
            // budget in another currency is refused by the ledger rather than
            // converted, so naming it here is naming the only value that can
            // agree with the catalog this build compiles in. A code this
            // module wrote itself cannot fail to parse; a ledger with a zero
            // ceiling would refuse every call, which is the fail-closed answer
            // if it ever did.
            currency: Currency::new(BUDGET_CURRENCY_CODE)
                .map_err(|CurrencyError| ModelTurnError::Overflow)?,
            max_micros: Micros::new(u64::MAX),
            max_calls: u32::MAX,
            max_retries: u32::MAX,
        },
    ))
}

/// The identity of the request that carries one call.
///
/// Public because reading the reply needs the same value the request was sent
/// under, and deriving it twice from the same call identity is what keeps the
/// two halves naming one request without a field to carry it between them.
pub fn model_request_id(
    digest: &dyn Sha256Port,
    call_id: &str,
) -> Result<model_router::ids::RequestId, ModelTurnError> {
    derive_id(digest, ROUTER_REQUEST_DOMAIN, call_id).map(model_router::ids::RequestId::from_bytes)
}

/// The canonical identity of the managed request that carries one call.
///
/// Derived from the same durable call identity as [`model_request_id`], under
/// its own domain, and rendered as a lowercase hyphenated UUID because that is
/// the one spelling the worker's validator admits. The version and variant
/// bits are set so the value is a well-formed version-4 UUID; the rest is the
/// digest's, which is what makes a kernel retry of the same call mint the same
/// id — the property the worker's exactly-once metering is keyed on.
pub fn managed_request_id(
    digest: &dyn Sha256Port,
    call_id: &str,
) -> Result<String, ModelTurnError> {
    let bytes = derive_id(digest, MANAGED_REQUEST_DOMAIN, call_id)?;
    let [b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15] = bytes;
    let b6 = (b6 & 0x0f) | 0x40;
    let b8 = (b8 & 0x3f) | 0x80;
    Ok(format!(
        "{b0:02x}{b1:02x}{b2:02x}{b3:02x}-{b4:02x}{b5:02x}-{b6:02x}{b7:02x}-{b8:02x}{b9:02x}-{b10:02x}{b11:02x}{b12:02x}{b13:02x}{b14:02x}{b15:02x}"
    ))
}

/// The name a provider is told this conversation has.
///
/// Thirty-two lowercase hex characters, which is inside every bound a vendor
/// publishes for such a field by construction rather than by clamping — a
/// clamp is a length that can be exceeded, and a derived width cannot be.
pub fn conversation_key(digest: &dyn Sha256Port, task_id: &str) -> Result<String, ModelTurnError> {
    let bytes = derive_id(digest, CONVERSATION_KEY_DOMAIN, task_id)?;
    let mut rendered = String::with_capacity(bytes.len().saturating_mul(2));
    for byte in bytes {
        use core::fmt::Write as _;
        // Infallible into a `String`; the router's lint set forbids unwrap
        // here, and a formatting error has nowhere to go but the same
        // overflow the derivation already reports.
        write!(rendered, "{byte:02x}").map_err(|_| ModelTurnError::Overflow)?;
    }
    Ok(rendered)
}

fn derive_id(
    digest: &dyn Sha256Port,
    domain: &[u8],
    value: &str,
) -> Result<[u8; 16], ModelTurnError> {
    let length = u32::try_from(value.len()).map_err(|_| ModelTurnError::Overflow)?;
    let mut material =
        Vec::with_capacity(domain.len().saturating_add(value.len()).saturating_add(4));
    material.extend_from_slice(domain);
    material.extend_from_slice(&length.to_be_bytes());
    material.extend_from_slice(value.as_bytes());
    let bytes = digest
        .sha256(&material)
        .map_err(|crate::digest::DigestError::Unavailable| ModelTurnError::DigestUnavailable)?;
    bytes
        .get(..16)
        .and_then(|head| <[u8; 16]>::try_from(head).ok())
        .ok_or(ModelTurnError::Overflow)
}
