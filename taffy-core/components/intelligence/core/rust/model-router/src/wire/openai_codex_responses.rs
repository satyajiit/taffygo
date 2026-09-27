// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The responses family as the subscription endpoint speaks it, as one row.
//!
//! It is the same conversation shape as [`super::openai_responses`] reached at
//! a different address on a different kind of credential, and it is a row of
//! its own because three of its fields are ones the platform endpoint refuses
//! and the platform endpoint's absence of them is one this endpoint refuses:
//!
//! - `store` is `false`. The subscription endpoint will not keep the answer,
//!   and a body that asks it to is rejected outright.
//! - `include` asks for `reasoning.encrypted_content`. Without it the endpoint
//!   returns reasoning the next turn cannot replay, and a conversation that
//!   cannot replay its own reasoning is one the model answers as though it had
//!   never thought.
//! - What comes back under that key is sealed. It is carried to the next turn
//!   of the same conversation exactly as it arrived and is never read, joined,
//!   rendered or recorded — [`OpaqueReasoning`][crate::wire::OpaqueReasoning]
//!   is the type that makes "never" a property of the signature rather than a
//!   promise about the code.
//!
//! The standing instruction is `instructions` rather than a turn, which the
//! responses row already says; the difference is that here it is the only
//! place an instruction may go, because this endpoint reads a `system` turn as
//! something the user said.

use super::dialect::{
    ArgumentsShape, AssistantEnvelope, BodyConstant, CallPlacement, CallPosition, ConstantValue,
    ContentShape, Dialect, ErrorFields, FailureSignal, FieldPath, Flag, IdentityPosition,
    MediaPlacement, MediaShape, PartWrapping, ReasoningCarriage, ResultGrouping, ResultPayload,
    ResultPlacement, Step, StopFields, SystemPlacement, TextLocation, ThinkingControl,
    ToolCallLocation, ToolPlacement, ToolWrapping, UsageFields,
};
use crate::catalog::WireApi;
use crate::request::{ErrorClass, StopReason};

/// What this endpoint requires in every body it is sent.
const CONSTANTS: &[BodyConstant] = &[
    BodyConstant {
        path: FieldPath {
            parents: &[],
            key: "store",
        },
        value: ConstantValue::Boolean(false),
    },
    BodyConstant {
        path: FieldPath {
            parents: &[],
            key: "include",
        },
        value: ConstantValue::TextList(&["reasoning.encrypted_content"]),
    },
];

pub(super) const DIALECT: Dialect = Dialect {
    family: WireApi::OpenAiCodexResponses,
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
    // This endpoint reads the conversation back as its own output items, so a
    // replayed assistant turn has to look like the item it emitted rather
    // than like an input message.
    assistant_envelope: Some(AssistantEnvelope {
        type_key: "type",
        type_value: "message",
        status_key: "status",
        status_value: "completed",
        id_key: "id",
        id_prefix: "msg_taffy_",
        annotations_key: "annotations",
    }),
    constants: CONSTANTS,
    // The endpoint's own name for a conversation, written into the body so
    // successive turns of one task reach the same cached prefix. It is the
    // same value the transport sends in the session header, and they are one
    // value on purpose: two names for one conversation would let the body and
    // the header disagree about which conversation this is.
    conversation_key: Some(FieldPath {
        parents: &[],
        key: "prompt_cache_key",
    }),
    compat: None,
    reasoning: Some(ReasoningCarriage {
        path: &[Step::Key("output")],
        type_key: "type",
        tag: "reasoning",
        payload_key: "encrypted_content",
    }),
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
        // Written as JSON `null`, which is not `false` and is not omission.
        // `null` leaves the family's own default in place; `false` asks it
        // not to constrain sampling; omitting the key is a third answer, and
        // it is the one this row was giving by accident.
        strict_key: Some("strict"),
        prefix_cache: None,
    },
    calls: CallPlacement {
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
        // The two words at the top are this endpoint's own, and they are the
        // reason it needs a vocabulary rather than borrowing the platform
        // row's. A plan that has run out for the month and a plan that never
        // covered this model both arrive as codes no metered endpoint sends,
        // and both are a quota rather than a broken request — read as
        // `Unknown` they would end the task terminally with nothing that
        // explains why.
        vocabulary: &[
            ("usage_limit_reached", ErrorClass::Quota),
            ("usage_not_included", ErrorClass::Quota),
            ("invalid_api_key", ErrorClass::Auth),
            ("account_deactivated", ErrorClass::Auth),
            ("insufficient_quota", ErrorClass::Quota),
            ("rate_limit_exceeded", ErrorClass::Quota),
            ("billing_hard_limit_reached", ErrorClass::Quota),
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
