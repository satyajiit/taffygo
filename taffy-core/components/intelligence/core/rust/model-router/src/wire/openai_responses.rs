// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The `OpenAI` responses family, as one table row.
//!
//! What separates this row from the other three:
//!
//! - The conversation is `input` rather than `messages`, and a text block's
//!   tag depends on who said it: `input_text` from the user, `output_text`
//!   from the model. It is the reason [`ContentShape::TaggedBlocks`] carries
//!   two tags instead of one.
//! - A reply nests its text one level deeper than the others: the produced
//!   items are messages, and the text blocks are inside them. That is the only
//!   use of `nested_key` in the four tables, and it is why the reader descends
//!   exactly one level rather than recursing.
//! - The stop token is split in two. `status` says whether the answer
//!   finished, and `incomplete_details.reason` says why it did not, so the
//!   specific path is tried first.
//! - A tool call's arguments arrive as a JSON document inside a string.
//! - A tool call and its result are items of the conversation list in their
//!   own right, and neither carries a role. That is the reason a tool result
//!   could not have been a third [`Speaker`][crate::wire::Speaker]: there is
//!   no role here to give it.

use super::dialect::{
    ArgumentsShape, CallPlacement, CallPosition, ContentShape, Dialect, ErrorFields, FailureSignal,
    FieldPath, Flag, IdentityPosition, MediaPlacement, MediaShape, PartWrapping, ResultGrouping,
    ResultPayload, ResultPlacement, Step, StopFields, SystemPlacement, TextLocation,
    ThinkingControl, ToolCallLocation, ToolPlacement, ToolWrapping, UsageFields,
};
use crate::catalog::WireApi;
use crate::request::{ErrorClass, StopReason};

pub(super) const DIALECT: Dialect = Dialect {
    family: WireApi::OpenAiResponses,
    body_root: &[],
    model: Some(FieldPath {
        parents: &[],
        key: "model",
    }),
    turns_key: "input",
    role_key: "role",
    user_role: "user",
    assistant_role: "assistant",
    content_key: "content",
    content: ContentShape::TaggedBlocks {
        type_key: "type",
        user_tag: "input_text",
        assistant_tag: "output_text",
        text_key: "text",
    },
    media: MediaPlacement {
        text_when_mixed: super::dialect::TextPart {
            type_key: Some("type"),
            user_tag: Some("input_text"),
            assistant_tag: Some("output_text"),
            text_key: "text",
        },
        shape: MediaShape::DataUrl {
            type_key: "type",
            type_value: "input_image",
            url_key: "image_url",
            nested_key: None,
        },
    },
    system: SystemPlacement::TopLevelText {
        key: "instructions",
    },
    system_prelude: &[],
    system_blocks: None,
    assistant_envelope: None,
    constants: &[],
    conversation_key: None,
    compat: None,
    reasoning: None,
    answer_tokens: FieldPath {
        parents: &[],
        key: "max_output_tokens",
    },
    thinking: ThinkingControl::Effort {
        path: FieldPath {
            parents: &["reasoning"],
            key: "effort",
        },
    },
    tools: ToolPlacement {
        list_key: "tools",
        // The envelope tag is present but the declaration is not nested under
        // it: name, description and schema sit beside `type` in the same
        // object. The completions family spells the same idea with a nested
        // object, which is exactly the sort of difference a table keeps
        // visible.
        wrapping: ToolWrapping::Envelope {
            tag: Flag {
                key: "type",
                value: "function",
            },
            inner_key: None,
        },
        name_key: "name",
        description_key: "description",
        schema_key: "parameters",
        strict_key: None,
        prefix_cache: None,
    },
    calls: CallPlacement {
        // A call is not part of the message that preceded it: the produced
        // items are a flat list, and a `function_call` is an item beside the
        // message rather than a block inside it.
        position: CallPosition::OwnElement,
        wrapping: PartWrapping::Tagged {
            tag: Flag {
                key: "type",
                value: "function_call",
            },
        },
        call_id_key: Some("call_id"),
        call_id_position: IdentityPosition::OnElement,
        name_key: "name",
        arguments_key: "arguments",
        arguments: ArgumentsShape::EmbeddedText,
    },
    results: ResultPlacement {
        // The one family whose tool result carries no role at all. It is an
        // item of the input list identified by its `type`, which is why
        // `ResultGrouping::OwnElement` has an optional role rather than a
        // required one.
        grouping: ResultGrouping::OwnElement { role: None },
        wrapping: PartWrapping::Tagged {
            tag: Flag {
                key: "type",
                value: "function_call_output",
            },
        },
        call_id_key: Some("call_id"),
        call_id_position: IdentityPosition::OnElement,
        name_key: None,
        payload: ResultPayload::Text { key: "output" },
        failure: FailureSignal::FoldedIntoText,
    },
    text: TextLocation {
        path: &[Step::Key("output")],
        type_key: Some("type"),
        tags: &["output_text"],
        text_key: "text",
        nested_key: Some("content"),
        nested_tags: &["message"],
    },
    tool_calls: ToolCallLocation {
        path: &[Step::Key("output")],
        type_key: Some("type"),
        tags: &["function_call"],
        inner_key: None,
        id_key: Some("call_id"),
        name_key: "name",
        arguments_key: "arguments",
        arguments: ArgumentsShape::EmbeddedText,
    },
    usage: UsageFields {
        object: &[Step::Key("usage")],
        input: &[Step::Key("input_tokens")],
        output: &[Step::Key("output_tokens")],
        cache_read: Some(&[
            Step::Key("input_tokens_details"),
            Step::Key("cached_tokens"),
        ]),
        cache_write: None,
        input_includes_cache: true,
    },
    stop: StopFields {
        paths: &[
            &[Step::Key("incomplete_details"), Step::Key("reason")],
            &[Step::Key("status")],
        ],
        vocabulary: &[
            ("completed", StopReason::Complete),
            ("max_output_tokens", StopReason::Length),
            ("content_filter", StopReason::ProviderStop),
            ("incomplete", StopReason::ProviderStop),
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
            ("billing_hard_limit_reached", ErrorClass::Quota),
            ("quota_exceeded", ErrorClass::Quota),
            ("server_error", ErrorClass::Overloaded),
            ("slow_down", ErrorClass::Overloaded),
            ("engine_overloaded", ErrorClass::Overloaded),
            ("service_unavailable", ErrorClass::Overloaded),
            ("context_length_exceeded", ErrorClass::Overflow),
            ("string_above_max_length", ErrorClass::Overflow),
            ("invalid_request_error", ErrorClass::InvalidRequest),
            ("model_not_found", ErrorClass::InvalidRequest),
            ("unsupported_parameter", ErrorClass::InvalidRequest),
        ],
    },
    stream: Some("stream"),
    stream_usage: None,
};
