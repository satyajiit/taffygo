// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Provider-independent assembly of one model request body.

use model_router::catalog::{CatalogLayer, WireApi};
use model_router::request::CacheRetention;
use model_router::route::RouteCandidate;
use model_router::wire::{
    write_managed_media_request, write_managed_request, ManagedRequest, WireMediaAttachment,
    WireRequest,
};

use crate::context::{MediaAttachment, TaskTranscript};

#[cfg(test)]
use super::style::SYSTEM_INSTRUCTION;
use super::{schema, ModelTurnError, ANSWER_TOKENS};

/// Writes the managed canonical body for one turn of `transcript`.
///
/// The same borrowed staging as [`write_body`], and now the same tool
/// vocabulary: schema version 3 carries tools in this product's own shape,
/// asks for the canonical response stream, and the Worker translates both
/// directions per upstream (decision 0092).
#[cfg(test)]
pub(super) fn write_managed_body(
    candidate: &RouteCandidate,
    transcript: &TaskTranscript,
    tools: &task_engine::EffectiveToolSet,
    request_id: &str,
    page: Option<&str>,
    person_answer: Option<&str>,
) -> Result<String, ModelTurnError> {
    write_managed_body_with_system(
        candidate,
        transcript,
        tools,
        request_id,
        page,
        person_answer,
        None,
        SYSTEM_INSTRUCTION,
    )
}

#[allow(clippy::too_many_arguments)]
pub(super) fn write_managed_body_with_system(
    candidate: &RouteCandidate,
    transcript: &TaskTranscript,
    tools: &task_engine::EffectiveToolSet,
    request_id: &str,
    page: Option<&str>,
    person_answer: Option<&str>,
    media: Option<&MediaAttachment>,
    system_instruction: &str,
) -> Result<String, ModelTurnError> {
    // A model with no tool-calling dialect cannot receive these declarations.
    // Avoid constructing a tree of parameter schemas only to discard it.
    let declarations = if candidate.tool_calling {
        schema::declarations(tools).ok_or(ModelTurnError::ToolSchemaUnavailable)?
    } else {
        Vec::new()
    };
    let views = transcript.views(page, person_answer);
    let turns = views.turns();
    let request = ManagedRequest {
        request_id,
        model_id: candidate.model.model_id.as_str(),
        system: Some(system_instruction),
        turns: &turns,
        tools: declarations.as_slice(),
        thinking: &candidate.thinking,
        answer_tokens: ANSWER_TOKENS,
    };
    let mut body = String::new();
    match media.map(wire_media) {
        Some(media) => write_managed_media_request(&request, media, &mut body),
        None => write_managed_request(&request, &mut body),
    }
    .map_err(ModelTurnError::ManagedBody)?;
    Ok(body)
}

/// Writes the direct-provider body for one turn of `transcript`.
#[cfg(test)]
pub(super) fn write_body(
    candidate: &RouteCandidate,
    transcript: &TaskTranscript,
    tools: &task_engine::EffectiveToolSet,
    page: Option<&str>,
    person_answer: Option<&str>,
    conversation_key: &str,
) -> Result<String, ModelTurnError> {
    write_body_with_system(
        candidate,
        transcript,
        tools,
        page,
        person_answer,
        None,
        SYSTEM_INSTRUCTION,
        conversation_key,
    )
}

#[allow(clippy::too_many_arguments)]
pub(super) fn write_body_with_system(
    candidate: &RouteCandidate,
    transcript: &TaskTranscript,
    tools: &task_engine::EffectiveToolSet,
    page: Option<&str>,
    person_answer: Option<&str>,
    media: Option<&MediaAttachment>,
    system_instruction: &str,
    conversation_key: &str,
) -> Result<String, ModelTurnError> {
    // Keep the non-tool path allocation-free with respect to the registry.
    let declarations = if candidate.tool_calling {
        schema::declarations(tools).ok_or(ModelTurnError::ToolSchemaUnavailable)?
    } else {
        Vec::new()
    };
    let views = transcript.views(page, person_answer);
    let turns = views.turns();
    let request = WireRequest {
        model_id: candidate.model.model_id.as_str(),
        tool_calling: candidate.tool_calling,
        system: Some(system_instruction),
        system_preamble: None,
        credential_method: None,
        compat: None,
        turns: &turns,
        tools: declarations.as_slice(),
        // These declarations come only from the compiled registry. Marking
        // their final entry retains that prefix, before every system, page,
        // person answer, or transcript block in the provider's prompt order.
        tool_cache_retention: tool_cache_retention(candidate),
        // Offered on every family; written only by the one that names a place
        // for it. Deciding here which families want it would put the same fact
        // in two tables.
        conversation_key: Some(conversation_key),
        thinking: &candidate.thinking,
        answer_tokens: ANSWER_TOKENS,
        stream: true,
    };
    let mut body = String::new();
    match media.map(wire_media) {
        Some(media) => {
            model_router::write_media_request(candidate.wire_api, &request, media, &mut body)
        }
        None => model_router::write_request(candidate.wire_api, &request, &mut body),
    }
    .map_err(ModelTurnError::Body)?;
    Ok(body)
}

fn tool_cache_retention(candidate: &RouteCandidate) -> CacheRetention {
    // Speaking a compatible wire format does not establish support for its
    // optional cache fields. Restrict the default to the documented vendor
    // service; custom endpoints and other compatible vendors remain opt-out.
    if candidate.wire_api == WireApi::AnthropicMessages
        && candidate.model.provider_id.as_str() == "anthropic"
        && candidate.endpoint_layer != CatalogLayer::UserOverride
        && super::shape::origin_of(candidate.endpoint.as_str()).as_deref()
            == Some("https://api.anthropic.com")
    {
        CacheRetention::Short
    } else {
        CacheRetention::None
    }
}

fn wire_media(media: &MediaAttachment) -> WireMediaAttachment<'_> {
    WireMediaAttachment {
        handle: media.handle.as_str(),
        mime_type: media.mime_type.as_str(),
    }
}
