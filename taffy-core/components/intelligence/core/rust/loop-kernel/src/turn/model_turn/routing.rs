// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The content-free context one composed turn gives route selection.

use model_router::ids::SourceId as RouterSourceId;
use model_router::request::{ContextManifest, DataSensitivity, RequestPurpose};
use model_router::route::{RoutePreference, RouteRequest};

use crate::context::LivePage;
use crate::ports::ModelTurnFacts;

use super::{ModelTurnError, ANSWER_TOKENS, BYTES_PER_TOKEN, MEDIA_INPUT_TOKEN_ESTIMATE};

/// Page-derived routing facts kept together so content and media carriage
/// cannot be varied independently at a call site.
pub(super) struct PageRouteContext<'a> {
    pub(super) page: &'a LivePage,
    pub(super) text_bytes: usize,
    pub(super) carries_page_content: bool,
    pub(super) carries_media_attachment: bool,
}

/// Describes the conversation and exact observed sources without carrying any
/// of their content into the router.
pub(super) fn request(
    facts: &ModelTurnFacts,
    task_id: model_router::TaskId,
    preference: RoutePreference,
    tools: &task_engine::EffectiveToolSet,
    page_context: &PageRouteContext<'_>,
) -> Result<RouteRequest, ModelTurnError> {
    let source_ids = page_context
        .page
        .source_ids()
        .map(|source_id| {
            RouterSourceId::parse(&source_id.to_text()).map_err(|_| ModelTurnError::Overflow)
        })
        .collect::<Result<Vec<_>, _>>()?;
    let classes = if page_context.carries_page_content {
        vec![DataSensitivity::Personal, DataSensitivity::Sensitive]
    } else {
        vec![DataSensitivity::Personal]
    };
    Ok(RouteRequest {
        task_id,
        role: if page_context.carries_media_attachment {
            model_router::ModelRole::Vision
        } else {
            model_router::ModelRole::PrimaryReasoning
        },
        preference,
        purpose: RequestPurpose::Planning,
        context: ContextManifest {
            source_ids,
            classes,
            item_count: 1u32.saturating_add(u32::from(page_context.text_bytes > 0)),
            // The whole conversation and all current pages are what the body
            // carries. Looking only at the opening turn would under-report by
            // everything the task has done since, which is the half that grows.
            estimated_input_tokens: (facts.transcript.text_bytes() as u64)
                .saturating_add(page_context.text_bytes as u64)
                .div_ceil(BYTES_PER_TOKEN)
                .saturating_add(if page_context.carries_media_attachment {
                    MEDIA_INPUT_TOKEN_ESTIMATE
                } else {
                    0
                }),
        },
        required_modalities: if page_context.carries_media_attachment {
            vec![
                model_router::catalog::InputModality::Text,
                model_router::catalog::InputModality::Image,
            ]
        } else {
            vec![model_router::catalog::InputModality::Text]
        },
        requires_tool_calling: !tools.is_empty(),
        // The person's rung when they chose one, and Taffy's otherwise. The
        // router clamps this against the selected model's own ladder.
        thinking: facts.thinking.unwrap_or(model_router::ThinkingLevel::Low),
        answer_tokens: ANSWER_TOKENS,
        estimated_output_tokens: ANSWER_TOKENS,
        // A pinned model leads its role through installed policy. A hard pin
        // here would defeat catalog failover when that model is refused.
        pinned_model: None,
    })
}
