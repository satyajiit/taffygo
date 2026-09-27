// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One request-ready candidate inside a logical model turn.

use model_router::route::RouteCandidate;
use model_router::Route;

use crate::context::{MediaAttachment, TaskTranscript};
use crate::ports::ModelRouterPort;
use crate::provider::ProviderDirectory;

use super::{
    credential_handle, shape, wire_api, ModelTurnError, ReplyWire, ANSWER_TOKENS,
    MAX_COMPLETION_BYTES,
};

/// The candidate-specific facts needed to dispatch and read one attempt.
#[derive(Clone, Debug, PartialEq)]
pub struct ComposedModelAttempt {
    pub request: core_service_types::ModelRequestEffect,
    pub wire: ReplyWire,
    pub context_window: u64,
    pub answer_tokens: u64,
}

/// Composes one candidate without re-reading routing or page state.
#[allow(clippy::too_many_arguments)]
pub(super) fn compose(
    router: &dyn ModelRouterPort,
    providers: &dyn ProviderDirectory,
    route: Route,
    candidate: &RouteCandidate,
    transcript: &TaskTranscript,
    tools: &task_engine::EffectiveToolSet,
    task_id: &str,
    call_id: &str,
    page: Option<&str>,
    person_line: Option<&str>,
    media: Option<&MediaAttachment>,
    system_instruction: &str,
    managed_request_id: Option<&str>,
    conversation_key: &str,
    carries_page_content: bool,
) -> Result<ComposedModelAttempt, ModelTurnError> {
    let context_window = router
        .context_window(&candidate.model)
        .ok_or(ModelTurnError::Overflow)?;
    let credential = credential_handle(providers, candidate)?;
    let body = match (route, managed_request_id) {
        (Route::Managed, Some(request_id)) => super::write_managed_body_with_system(
            candidate,
            transcript,
            tools,
            request_id,
            page,
            person_line,
            media,
            system_instruction,
        )?,
        (Route::ByoDirect, None) => super::write_body_with_system(
            candidate,
            transcript,
            tools,
            page,
            person_line,
            media,
            system_instruction,
            conversation_key,
        )?,
        (Route::Managed, None) | (Route::ByoDirect, Some(_)) => {
            return Err(ModelTurnError::NoDisclosedRoute);
        }
    };
    let (endpoint, endpoint_kind) = match route {
        Route::Managed => (
            shape::managed_endpoint(candidate)?,
            core_service_types::ModelEndpointKind::CatalogOrigin,
        ),
        Route::ByoDirect => shape::direct_endpoint(candidate)?,
    };
    let (media_attachment_handle, media_attachment_mime_type) =
        media.map_or((None, None), |attachment| {
            (
                Some(attachment.handle.clone()),
                Some(attachment.mime_type.clone()),
            )
        });
    let attempt = ComposedModelAttempt {
        request: core_service_types::ModelRequestEffect {
            route_id: match route {
                Route::ByoDirect => "direct_user_key",
                Route::Managed => "managed_service",
            }
            .to_owned(),
            model_id: candidate.model.model_id.as_str().to_owned(),
            disclosure: super::disclosure_class(carries_page_content),
            request_body: body.into_bytes(),
            max_output_bytes: MAX_COMPLETION_BYTES,
            task_id: task_id.to_owned(),
            provider_id: candidate.model.provider_id.as_str().to_owned(),
            wire_api: match route {
                Route::Managed => core_service_types::ProviderWireApi::Managed,
                Route::ByoDirect => wire_api(candidate.wire_api),
            },
            endpoint,
            credential_handle: credential.map(|handle| handle.as_str().to_owned()),
            static_headers: family_headers(candidate, conversation_key),
            probe: false,
            endpoint_kind,
            media_attachment_handle,
            media_attachment_mime_type,
            not_before_monotonic_ms: 0,
        },
        wire: match managed_request_id {
            Some(request_id) => ReplyWire::Managed {
                request_id: request_id.to_owned(),
            },
            None => ReplyWire::Family(candidate.wire_api),
        },
        context_window,
        answer_tokens: ANSWER_TOKENS,
    };
    shape::validate_attempt(attempt, call_id)
}

/// The headers the browser cannot compose for itself, and nothing on the rest.
///
/// A header is the browser's whenever it is fixed for a whole family — those
/// are composed there, beside the credential, in one table. This function is
/// for the two that are not fixed: one varies per conversation, and one varies
/// per model inside a single family, and neither is something a per-family
/// route table can say.
fn family_headers(
    candidate: &RouteCandidate,
    conversation_key: &str,
) -> Vec<core_service_types::ModelStaticHeader> {
    match candidate.wire_api {
        // The subscription responses endpoint correlates a conversation by a
        // header as well as by the body field, and its proxy drops a name it
        // does not recognise before anything downstream sees it — so the
        // hyphen in `session-id` is load-bearing, and an underscored spelling
        // passes every check this side makes and then arrives nowhere
        // (decision 0111 section 3).
        //
        // Two names for one value, which is what the borrowed tool sends. They
        // are not redundant to the endpoint: one correlates the conversation
        // and one correlates the request, and a proxy that logs by the second
        // while the edge routes by the first sees the same conversation either
        // way.
        model_router::catalog::WireApi::OpenAiCodexResponses => vec![
            core_service_types::ModelStaticHeader {
                name: "session-id".to_owned(),
                value: conversation_key.to_owned(),
            },
            core_service_types::ModelStaticHeader {
                name: "x-client-request-id".to_owned(),
                value: conversation_key.to_owned(),
            },
        ],
        model_router::catalog::WireApi::GoogleCloudCodeAssist => claude_thinking_headers(candidate),
        model_router::catalog::WireApi::AnthropicMessages
        | model_router::catalog::WireApi::OpenAiResponses
        | model_router::catalog::WireApi::OpenAiCompletions
        | model_router::catalog::WireApi::GoogleGenerativeLanguage => Vec::new(),
    }
}

/// The interleaved-thinking opt-in, on the models of one family that need it.
///
/// It cannot be a family header, which is what makes it the core's: this
/// endpoint fronts three makers' models under one family, the models of one of
/// them require the opt-in when they reason, and the models of another must
/// never see it. A table row is per family and has nowhere to say that.
///
/// Read off the model id rather than off a catalog flag, and that is the weak
/// part of it. The flag says whether a model reasons, not whose model it is,
/// and the vendor prefix is the only thing on hand that answers the second
/// question. It is stated here rather than hidden because it is the one place
/// this file inspects an id for meaning: an id spelled some other way for the
/// same maker silently loses the opt-in, and what a person then sees is a
/// model that answers without thinking rather than an error.
fn claude_thinking_headers(
    candidate: &RouteCandidate,
) -> Vec<core_service_types::ModelStaticHeader> {
    if !candidate.model.model_id.as_str().starts_with("claude-")
        || matches!(
            candidate.thinking,
            model_router::thinking::ThinkingPlan::Disabled
        )
    {
        return Vec::new();
    }
    vec![core_service_types::ModelStaticHeader {
        name: "anthropic-beta".to_owned(),
        value: "interleaved-thinking-2025-05-14".to_owned(),
    }]
}
