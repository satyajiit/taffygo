// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The Google generative-language family, as one table row.
//!
//! What separates this row from the other three:
//!
//! - **The model name is not in the body.** It is part of the URL the browser
//!   builds, so `model` is `None` here. A table that carried a key anyway
//!   would have this crate writing a field the provider ignores.
//! - **The assistant is called `model`.** Writing `assistant` produces a body
//!   the provider rejects for a reason that reads as nothing to do with roles.
//! - **A tool call has no identity.** Every other family mints one and expects
//!   it back on the result; this one pairs a result to a call by the tool's
//!   name. `id_key: None` is what makes a caller that needs an identity mint
//!   its own instead of reading an absent field as an empty string — and
//!   `call_id_key: None` on both replay rows is the same fact on the way back,
//!   which is why a replayed result carries the tool's name and not only an
//!   identity this family would discard.
//! - **A result is an object, not a string.** `functionResponse.response` is
//!   a JSON object on this family alone, so the result text sits in a field
//!   inside it and a failure changes that field's name rather than raising a
//!   flag beside it.
//! - The answer allowance and the thinking budget are two objects deep and
//!   share only their first parent, which is the case the request writer's
//!   grouping exists for.

use super::dialect::{
    ArgumentsShape, CallPlacement, CallPosition, ContentShape, Dialect, ErrorFields, FailureSignal,
    FieldPath, IdentityPosition, MediaPlacement, MediaShape, PartWrapping, ResultGrouping,
    ResultPayload, ResultPlacement, Step, StopFields, SystemPlacement, TextLocation,
    ThinkingControl, ToolCallLocation, ToolPlacement, ToolWrapping, UsageFields,
};
use crate::catalog::WireApi;
use crate::request::{ErrorClass, StopReason};

pub(super) const DIALECT: Dialect = Dialect {
    family: WireApi::GoogleGenerativeLanguage,
    body_root: &[],
    model: None,
    turns_key: "contents",
    role_key: "role",
    user_role: "user",
    assistant_role: "model",
    content_key: "parts",
    content: ContentShape::UntaggedParts { text_key: "text" },
    media: MediaPlacement {
        text_when_mixed: super::dialect::TextPart {
            type_key: None,
            user_tag: None,
            assistant_tag: None,
            text_key: "text",
        },
        shape: MediaShape::InlineBase64 {
            nested_key: "inlineData",
            mime_key: "mimeType",
            data_key: "data",
        },
    },
    system: SystemPlacement::TopLevelParts {
        key: "systemInstruction",
        role: None,
    },
    system_prelude: &[],
    system_blocks: None,
    assistant_envelope: None,
    constants: &[],
    conversation_key: None,
    compat: None,
    reasoning: None,
    answer_tokens: FieldPath {
        parents: &["generationConfig"],
        key: "maxOutputTokens",
    },
    thinking: ThinkingControl::Budget {
        path: FieldPath {
            parents: &["generationConfig", "thinkingConfig"],
            key: "thinkingBudget",
        },
        enable: None,
    },
    tools: ToolPlacement {
        list_key: "tools",
        wrapping: ToolWrapping::Grouped {
            group_key: "functionDeclarations",
        },
        name_key: "name",
        description_key: "description",
        schema_key: "parameters",
        strict_key: None,
        prefix_cache: None,
    },
    calls: CallPlacement {
        position: CallPosition::InContent,
        wrapping: PartWrapping::Nested {
            key: "functionCall",
        },
        // The identity the core minted has nowhere to go on this family, and
        // writing it under a name the family does not read would be inventing
        // a field. It is dropped here and the pairing is carried by the name
        // below, which is why `ToolResultView` carries the tool's name at all.
        call_id_key: None,
        call_id_position: IdentityPosition::OnElement,
        name_key: "name",
        arguments_key: "args",
        arguments: ArgumentsShape::Value,
    },
    results: ResultPlacement {
        grouping: ResultGrouping::SharedTurn { role: "user" },
        wrapping: PartWrapping::Nested {
            key: "functionResponse",
        },
        call_id_key: None,
        call_id_position: IdentityPosition::OnElement,
        name_key: Some("name"),
        // The family requires an object here and rejects a bare string, so the
        // result text is given a field inside it.
        payload: ResultPayload::Object {
            key: "response",
            text_key: "output",
        },
        // Not a flag beside the payload but the payload's own field name: a
        // failure arrives as `response.error` rather than `response.output`,
        // which is the shape this family's own function-calling guidance uses.
        failure: FailureSignal::PayloadKey { key: "error" },
    },
    text: TextLocation {
        path: &[
            Step::Key("candidates"),
            Step::FirstItem,
            Step::Key("content"),
            Step::Key("parts"),
        ],
        type_key: None,
        tags: &[],
        text_key: "text",
        nested_key: None,
        nested_tags: &[],
    },
    tool_calls: ToolCallLocation {
        path: &[
            Step::Key("candidates"),
            Step::FirstItem,
            Step::Key("content"),
            Step::Key("parts"),
        ],
        type_key: None,
        tags: &[],
        inner_key: Some("functionCall"),
        id_key: None,
        name_key: "name",
        arguments_key: "args",
        arguments: ArgumentsShape::Value,
    },
    usage: UsageFields {
        object: &[Step::Key("usageMetadata")],
        input: &[Step::Key("promptTokenCount")],
        output: &[Step::Key("candidatesTokenCount")],
        cache_read: Some(&[Step::Key("cachedContentTokenCount")]),
        cache_write: None,
        input_includes_cache: true,
    },
    stop: StopFields {
        paths: &[&[
            Step::Key("candidates"),
            Step::FirstItem,
            Step::Key("finishReason"),
        ]],
        vocabulary: &[
            ("STOP", StopReason::Complete),
            ("MAX_TOKENS", StopReason::Length),
            ("SAFETY", StopReason::ProviderStop),
            ("RECITATION", StopReason::ProviderStop),
            ("PROHIBITED_CONTENT", StopReason::ProviderStop),
            ("SPII", StopReason::ProviderStop),
            ("BLOCKLIST", StopReason::ProviderStop),
            ("MALFORMED_FUNCTION_CALL", StopReason::Error),
            ("OTHER", StopReason::ProviderStop),
        ],
    },
    error: ErrorFields {
        object: &[Step::Key("error")],
        token: &[Step::Key("status")],
        message: &[Step::Key("message")],
        vocabulary: &[
            ("UNAUTHENTICATED", ErrorClass::Auth),
            ("PERMISSION_DENIED", ErrorClass::Auth),
            ("RESOURCE_EXHAUSTED", ErrorClass::Quota),
            ("UNAVAILABLE", ErrorClass::Overloaded),
            // The call was given up on before it finished, not answered
            // wrongly. Read as `Unknown` it was terminal, which ends a task on
            // the one class of failure a second attempt usually clears.
            ("ABORTED", ErrorClass::Overloaded),
            ("INVALID_ARGUMENT", ErrorClass::InvalidRequest),
            ("FAILED_PRECONDITION", ErrorClass::InvalidRequest),
            ("OUT_OF_RANGE", ErrorClass::InvalidRequest),
            ("NOT_FOUND", ErrorClass::InvalidRequest),
            ("UNIMPLEMENTED", ErrorClass::InvalidRequest),
            ("DEADLINE_EXCEEDED", ErrorClass::Network),
            ("INTERNAL", ErrorClass::Unknown),
            ("UNKNOWN", ErrorClass::Unknown),
        ],
    },
    stream: None,
    stream_usage: None,
};
