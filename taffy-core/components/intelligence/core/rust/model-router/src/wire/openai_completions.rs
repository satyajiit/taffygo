// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The `OpenAI` completions compatibility dialect, as one table row.
//!
//! This is the generic dialect most aggregators and locally run servers speak,
//! and it is refined per model by
//! [`Model::compat`][crate::catalog::Model::compat] rather than by another row
//! here. A server that renames one field is catalog data; a server that
//! restructures the body is a new family.
//!
//! What separates this row from the other three:
//!
//! - The standing instruction is a turn like any other, distinguished only by
//!   its role. It is the only family where the instruction and the page-derived
//!   material travel in the same list, which is why
//!   [`SystemPlacement::LeadingTurn`] exists.
//! - A turn's content is the string itself, with no blocks, so several pieces
//!   of text have to be written as one.
//! - The whole answer is the first element of `choices`, which is the reason
//!   [`Step::FirstItem`] is part of a path rather than a special case in the
//!   reader.
//! - A tool declaration nests under `function` inside a typed envelope, and a
//!   tool call's arguments arrive as a JSON document inside a string.
//! - Having no blocks, it is the only family whose replayed tool calls cannot
//!   live in the turn's content: they are a `tool_calls` list beside it, which
//!   is what [`CallPosition::BesideContent`] exists for.

use super::dialect::{
    ArgumentsShape, CallPlacement, CallPosition, CompatPlacement, ContentShape, Dialect,
    ErrorFields, FailureSignal, FieldPath, Flag, IdentityPosition, MediaPlacement, MediaShape,
    PartWrapping, ResultGrouping, ResultPayload, ResultPlacement, Step, StopFields,
    SystemPlacement, TextLocation, TextPart, ThinkingControl, ToolCallLocation, ToolPlacement,
    ToolWrapping, UsageFields,
};
use crate::catalog::WireApi;
use crate::request::{ErrorClass, StopReason};

pub(super) const DIALECT: Dialect = Dialect {
    family: WireApi::OpenAiCompletions,
    body_root: &[],
    model: Some(FieldPath {
        parents: &[],
        key: "model",
    }),
    turns_key: "messages",
    role_key: "role",
    user_role: "user",
    assistant_role: "assistant",
    content_key: "content",
    content: ContentShape::PlainText,
    media: MediaPlacement {
        text_when_mixed: TextPart {
            type_key: Some("type"),
            user_tag: Some("text"),
            assistant_tag: Some("text"),
            text_key: "text",
        },
        shape: MediaShape::DataUrl {
            type_key: "type",
            type_value: "image_url",
            url_key: "url",
            nested_key: Some("image_url"),
        },
    },
    system: SystemPlacement::LeadingTurn { role: "system" },
    system_prelude: &[],
    system_blocks: None,
    assistant_envelope: None,
    constants: &[],
    conversation_key: None,
    // The dialect a person's own server speaks, so it is the one row with
    // somewhere to put what such a server needs.
    compat: Some(CompatPlacement {
        template_kwargs_key: "chat_template_kwargs",
    }),
    reasoning: None,
    answer_tokens: FieldPath {
        parents: &[],
        key: "max_completion_tokens",
    },
    thinking: ThinkingControl::Effort {
        path: FieldPath {
            parents: &[],
            key: "reasoning_effort",
        },
    },
    tools: ToolPlacement {
        list_key: "tools",
        wrapping: ToolWrapping::Envelope {
            tag: Flag {
                key: "type",
                value: "function",
            },
            inner_key: Some("function"),
        },
        name_key: "name",
        description_key: "description",
        schema_key: "parameters",
        strict_key: None,
        prefix_cache: None,
    },
    calls: CallPlacement {
        // The one family whose calls are not in the turn's content at all: the
        // assistant message keeps its string content and carries a `tool_calls`
        // list beside it. A family with no blocks has nowhere else to put them,
        // which is why `CallPosition` has this member and why asking
        // `ContentShape::parts` is what keeps the two facts paired.
        position: CallPosition::BesideContent { key: "tool_calls" },
        wrapping: PartWrapping::TaggedAndNested {
            tag: Flag {
                key: "type",
                value: "function",
            },
            key: "function",
        },
        call_id_key: Some("id"),
        call_id_position: IdentityPosition::OnElement,
        name_key: "name",
        arguments_key: "arguments",
        arguments: ArgumentsShape::EmbeddedText,
    },
    results: ResultPlacement {
        // A message of its own in a role no other family has, carrying the
        // identity and nothing else that says what it is.
        grouping: ResultGrouping::OwnElement { role: Some("tool") },
        wrapping: PartWrapping::Direct,
        call_id_key: Some("tool_call_id"),
        call_id_position: IdentityPosition::OnElement,
        name_key: None,
        payload: ResultPayload::Text { key: "content" },
        failure: FailureSignal::FoldedIntoText,
    },
    text: TextLocation {
        path: &[
            Step::Key("choices"),
            Step::FirstItem,
            Step::Key("message"),
            Step::Key("content"),
        ],
        type_key: None,
        tags: &[],
        text_key: "text",
        nested_key: None,
        nested_tags: &[],
    },
    tool_calls: ToolCallLocation {
        path: &[
            Step::Key("choices"),
            Step::FirstItem,
            Step::Key("message"),
            Step::Key("tool_calls"),
        ],
        type_key: None,
        tags: &[],
        inner_key: Some("function"),
        id_key: Some("id"),
        name_key: "name",
        arguments_key: "arguments",
        arguments: ArgumentsShape::EmbeddedText,
    },
    usage: UsageFields {
        object: &[Step::Key("usage")],
        input: &[Step::Key("prompt_tokens")],
        output: &[Step::Key("completion_tokens")],
        cache_read: Some(&[
            Step::Key("prompt_tokens_details"),
            Step::Key("cached_tokens"),
        ]),
        cache_write: None,
        input_includes_cache: true,
    },
    stop: StopFields {
        paths: &[&[
            Step::Key("choices"),
            Step::FirstItem,
            Step::Key("finish_reason"),
        ]],
        vocabulary: &[
            ("stop", StopReason::Complete),
            ("tool_calls", StopReason::ToolCall),
            ("function_call", StopReason::ToolCall),
            ("length", StopReason::Length),
            ("content_filter", StopReason::ProviderStop),
        ],
    },
    error: ErrorFields {
        object: &[Step::Key("error")],
        token: &[Step::Key("code")],
        message: &[Step::Key("message")],
        vocabulary: &[
            ("invalid_api_key", ErrorClass::Auth),
            ("account_deactivated", ErrorClass::Auth),
            ("insufficient_quota", ErrorClass::Quota),
            ("rate_limit_exceeded", ErrorClass::Quota),
            // The aggregators and self-hosted servers that speak this dialect
            // spell a spent budget several ways, and every spelling nobody
            // listed was read as the taxonomy's terminal `Unknown` — which
            // ends a task on a failure that waiting clears.
            ("quota_exceeded", ErrorClass::Quota),
            ("billing_hard_limit_reached", ErrorClass::Quota),
            ("insufficient_credits", ErrorClass::Quota),
            ("server_error", ErrorClass::Overloaded),
            ("slow_down", ErrorClass::Overloaded),
            ("engine_overloaded", ErrorClass::Overloaded),
            ("service_unavailable", ErrorClass::Overloaded),
            ("model_overloaded", ErrorClass::Overloaded),
            ("context_length_exceeded", ErrorClass::Overflow),
            ("string_above_max_length", ErrorClass::Overflow),
            ("invalid_request_error", ErrorClass::InvalidRequest),
            ("model_not_found", ErrorClass::InvalidRequest),
            ("unsupported_parameter", ErrorClass::InvalidRequest),
        ],
    },
    stream: Some("stream"),
    // The platform sends a stream's usage only when asked, in one last frame
    // whose `choices` is empty. Some servers used to send it unasked; SpaceXAI
    // stopped doing so, and on 2026-09-18 every streamed Grok turn ended at
    // `[DONE]` with no counts and was re-asked until the budget ran out. The
    // managed route's writer for this family has always asked.
    stream_usage: Some(FieldPath {
        parents: &["stream_options"],
        key: "include_usage",
    }),
};
