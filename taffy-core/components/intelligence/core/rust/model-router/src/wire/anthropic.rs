// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The Anthropic messages family, as one table row.
//!
//! What separates this row from the other three:
//!
//! - The standing instruction is a top-level string rather than a turn, so it
//!   cannot be confused with something the user said.
//! - Thinking is a token budget that has to be switched on explicitly, which
//!   is why [`ThinkingControl::Budget`] carries an enabling flag at all.
//! - It is the only family that reports uncached input separately from cache
//!   reads and cache writes, which is what `input_includes_cache: false`
//!   records. Every other family folds them together.
//! - It is also the only one of the four with a field for saying a tool result
//!   failed, which is why [`FailureSignal`] has three members and only this
//!   row uses the first.

use super::dialect::{
    ArgumentsShape, CallPlacement, CallPosition, ContentShape, Dialect, ErrorFields, FailureSignal,
    FieldPath, Flag, IdentityPosition, MediaPlacement, MediaShape, PartWrapping, ResultGrouping,
    ResultPayload, ResultPlacement, Step, StopFields, SystemBlocks, SystemPlacement, TextLocation,
    ThinkingControl, ToolCallLocation, ToolPlacement, ToolPrefixCache, ToolWrapping, UsageFields,
};
use crate::catalog::WireApi;
use crate::request::{ErrorClass, StopReason};

pub(super) const DIALECT: Dialect = Dialect {
    family: WireApi::AnthropicMessages,
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
    content: ContentShape::TaggedBlocks {
        type_key: "type",
        user_tag: "text",
        assistant_tag: "text",
        text_key: "text",
    },
    media: MediaPlacement {
        text_when_mixed: super::dialect::TextPart {
            type_key: Some("type"),
            user_tag: Some("text"),
            assistant_tag: Some("text"),
            text_key: "text",
        },
        shape: MediaShape::Base64Source {
            type_key: "type",
            type_value: "image",
            source_key: "source",
            source_type_key: "type",
            source_type_value: "base64",
            mime_key: "media_type",
            data_key: "data",
        },
    },
    system: SystemPlacement::TopLevelText { key: "system" },
    // The one family whose standing instruction can be a list of blocks, and
    // the one whose subscription credential requires a block ahead of it.
    system_prelude: &[],
    system_blocks: Some(SystemBlocks {
        type_key: "type",
        tag: "text",
        text_key: "text",
    }),
    assistant_envelope: None,
    constants: &[],
    conversation_key: None,
    compat: None,
    reasoning: None,
    answer_tokens: FieldPath {
        parents: &[],
        key: "max_tokens",
    },
    thinking: ThinkingControl::Budget {
        path: FieldPath {
            parents: &["thinking"],
            key: "budget_tokens",
        },
        // Without this the object is a budget with nothing turned on, which
        // the family reads as thinking left off.
        enable: Some(Flag {
            key: "type",
            value: "enabled",
        }),
    },
    tools: ToolPlacement {
        list_key: "tools",
        wrapping: ToolWrapping::Direct,
        name_key: "name",
        description_key: "description",
        schema_key: "input_schema",
        strict_key: None,
        prefix_cache: Some(ToolPrefixCache {
            key: "cache_control",
            kind: Flag {
                key: "type",
                value: "ephemeral",
            },
            extended_retention: Flag {
                key: "ttl",
                value: "1h",
            },
        }),
    },
    calls: CallPlacement {
        position: CallPosition::InContent,
        wrapping: PartWrapping::Tagged {
            tag: Flag {
                key: "type",
                value: "tool_use",
            },
        },
        call_id_key: Some("id"),
        call_id_position: IdentityPosition::OnElement,
        name_key: "name",
        arguments_key: "input",
        arguments: ArgumentsShape::Value,
    },
    results: ResultPlacement {
        // The results of a turn's calls come back in a turn whose role is the
        // user's. Nobody said them, which is the whole reason a tool result is
        // not a third speaker.
        grouping: ResultGrouping::SharedTurn { role: "user" },
        wrapping: PartWrapping::Tagged {
            tag: Flag {
                key: "type",
                value: "tool_result",
            },
        },
        call_id_key: Some("tool_use_id"),
        call_id_position: IdentityPosition::OnElement,
        name_key: None,
        payload: ResultPayload::Text { key: "content" },
        // The only first-class one of the four.
        failure: FailureSignal::Flag { key: "is_error" },
    },
    text: TextLocation {
        path: &[Step::Key("content")],
        type_key: Some("type"),
        tags: &["text"],
        text_key: "text",
        nested_key: None,
        nested_tags: &[],
    },
    tool_calls: ToolCallLocation {
        path: &[Step::Key("content")],
        type_key: Some("type"),
        tags: &["tool_use"],
        inner_key: None,
        id_key: Some("id"),
        name_key: "name",
        arguments_key: "input",
        arguments: ArgumentsShape::Value,
    },
    usage: UsageFields {
        object: &[Step::Key("usage")],
        input: &[Step::Key("input_tokens")],
        output: &[Step::Key("output_tokens")],
        cache_read: Some(&[Step::Key("cache_read_input_tokens")]),
        cache_write: Some(&[Step::Key("cache_creation_input_tokens")]),
        input_includes_cache: false,
    },
    stop: StopFields {
        paths: &[&[Step::Key("stop_reason")]],
        vocabulary: &[
            ("end_turn", StopReason::Complete),
            ("stop_sequence", StopReason::Complete),
            ("tool_use", StopReason::ToolCall),
            ("max_tokens", StopReason::Length),
            ("pause_turn", StopReason::ProviderStop),
            ("refusal", StopReason::ProviderStop),
        ],
    },
    error: ErrorFields {
        object: &[Step::Key("error")],
        token: &[Step::Key("type")],
        message: &[Step::Key("message")],
        vocabulary: &[
            ("authentication_error", ErrorClass::Auth),
            ("permission_error", ErrorClass::Auth),
            ("rate_limit_error", ErrorClass::Quota),
            ("billing_error", ErrorClass::Quota),
            // What a subscription credential is refused with when the plan's
            // window has run out, which is a wait rather than a request that
            // was wrong.
            ("usage_limit_error", ErrorClass::Quota),
            ("overloaded_error", ErrorClass::Overloaded),
            ("service_unavailable_error", ErrorClass::Overloaded),
            ("invalid_request_error", ErrorClass::InvalidRequest),
            ("not_found_error", ErrorClass::InvalidRequest),
            ("request_too_large", ErrorClass::Overflow),
            ("timeout_error", ErrorClass::Network),
            ("api_error", ErrorClass::Unknown),
        ],
    },
    stream: Some("stream"),
    stream_usage: None,
};
