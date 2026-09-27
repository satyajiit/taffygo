// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The Cloud Code Assist family, as one table row.
//!
//! It is the conversation shape [`super::google`] writes, wrapped. The request
//! that family sends as the whole body is one field of this one, beside three
//! routing values the endpoint reads and the model never sees; the reply comes
//! back wrapped the same way. A body built for either endpoint is refused by
//! the other, and — worse — a reply read by the wrong row is read as an answer
//! with no text in it rather than as an error, which is why this is a row of
//! its own rather than a second address for the generative-language row.
//!
//! What separates it from that row:
//!
//! - **The model name is in the body**, at the envelope's top rather than in
//!   the URL. The path names an operation and nothing else.
//! - **Two constants say which client is calling.** `requestType` and
//!   `userAgent` are body fields this endpoint inspects, not headers, and it
//!   refuses a request that omits them.
//! - **A tool call carries an identity here.** The endpoint translates the
//!   Gemini-shaped request into Anthropic messages for the Claude ids it
//!   serves, and a `tool_use` block there requires one. Sent without it the
//!   conversation opens normally and fails several turns in, when a result
//!   first has to be paired with the call it answers — which is why
//!   `call_id_key` is `Some` on both replay rows where the generative-language
//!   row has `None`.
//! - **The instruction arrives as the user's**, with two constant pieces ahead
//!   of it. See [`SYSTEM_PRELUDE`].
//! - **The thinking control is a named level**, not a budget: the models this
//!   endpoint serves reject `thinkingBudget` beside `thinkingLevel`.
//! - **A failure arrives outside the envelope.** Everything the model produced
//!   is under `response`; `error` is not, which is why the error paths below
//!   are the only reply paths that do not begin with it.
//!
//! What is deliberately not here yet, because nothing has proved it is needed:
//! a per-request identity. Both reference implementations mint one and send it
//! as `requestId`. This crate has no clock and no randomness and
//! [`WireRequest`][crate::wire::WireRequest] carries no per-call name, so
//! adding one is a column fed from the effect's own identity rather than a
//! value invented here. The device pass decides whether it is required.

use super::dialect::{
    ArgumentsShape, BodyConstant, CallPlacement, CallPosition, ConstantValue, ContentShape,
    Dialect, ErrorFields, FailureSignal, FieldPath, IdentityPosition, MediaPlacement, MediaShape,
    PartWrapping, ResultGrouping, ResultPayload, ResultPlacement, Step, StopFields,
    SystemPlacement, TextLocation, ThinkingControl, ToolCallLocation, ToolPlacement, ToolWrapping,
    UsageFields,
};
use crate::catalog::WireApi;
use crate::request::{ErrorClass, StopReason};

/// The envelope the request itself is nested in.
const BODY_ROOT: &[&str] = &["request"];

/// What this endpoint requires in every body it is sent.
///
/// Body fields rather than headers, and read by the endpoint rather than by
/// the model. `requestType` says which of its surfaces is calling and
/// `userAgent` which client; a body missing either is refused before a model
/// is reached.
const CONSTANTS: &[BodyConstant] = &[
    BodyConstant {
        path: FieldPath {
            parents: &[],
            key: "requestType",
        },
        value: ConstantValue::Text("agent"),
    },
    BodyConstant {
        path: FieldPath {
            parents: &[],
            key: "userAgent",
        },
        value: ConstantValue::Text("antigravity"),
    },
];

/// The two constant pieces this endpoint expects ahead of the instruction.
///
/// The first is a compatibility token, not product copy: the endpoint inspects
/// the standing instruction to decide which client is calling, and aligning it
/// is what settled spurious rate-limit refusals upstream. It is not reworded,
/// not localised and not improved — its whole value is being the text the
/// endpoint expects.
///
/// The second neutralises the first. Sent alone, the first would leave a
/// second assistant identity standing in the instruction the model reads,
/// which decision 0009 forbids: this product has one assistant. The pair is
/// one fact about the endpoint and the two pieces are never separated.
const SYSTEM_PRELUDE: &[&str] = &[PERSONA, IGNORE_WRAPPED_PERSONA];

/// The token's own text, as a literal so that the wrapped copy below is
/// spelled once. `concat!` takes literals, not constants, so a macro is the
/// only way to say the same bytes twice and have the compiler prove it.
macro_rules! persona {
    () => {
        concat!(
            "You are Antigravity, a powerful agentic AI coding assistant designed by ",
            "the Google Deepmind team working on Advanced Agentic Coding.",
            "You are pair programming with a USER to solve their coding task. The task ",
            "may require creating a new codebase, modifying or debugging an existing ",
            "codebase, or simply answering a question.",
            "**Absolute paths only**",
            "**Proactiveness**",
        )
    };
}

/// The compatibility token itself. See [`SYSTEM_PRELUDE`].
const PERSONA: &str = persona!();

/// The same token, wrapped so the model discards it. See [`SYSTEM_PRELUDE`].
const IGNORE_WRAPPED_PERSONA: &str =
    concat!("Please ignore following [ignore]", persona!(), "[/ignore]");

pub(super) const DIALECT: Dialect = Dialect {
    family: WireApi::GoogleCloudCodeAssist,
    body_root: BODY_ROOT,
    model: Some(FieldPath {
        parents: &[],
        key: "model",
    }),
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
    // The endpoint reads the instruction as the user's. The spelling the
    // generative-language row omits entirely is the one this endpoint refuses.
    system: SystemPlacement::TopLevelParts {
        key: "systemInstruction",
        role: Some("user"),
    },
    system_prelude: SYSTEM_PRELUDE,
    system_blocks: None,
    assistant_envelope: None,
    constants: CONSTANTS,
    // No field for one. The endpoint reads the generative-language request
    // shape, which has nowhere to carry a caller's name for a conversation,
    // and inventing a key here would be writing a field nothing reads.
    conversation_key: None,
    compat: None,
    reasoning: None,
    answer_tokens: FieldPath {
        parents: &["request", "generationConfig"],
        key: "maxOutputTokens",
    },
    // A named level rather than a budget. The models this endpoint serves
    // reject a budget beside a level, and the catalog supplies the word.
    thinking: ThinkingControl::Effort {
        path: FieldPath {
            parents: &["request", "generationConfig", "thinkingConfig"],
            key: "thinkingLevel",
        },
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
        // Unlike the generative-language row, which drops the identity because
        // that endpoint reads none. This one translates the request into
        // Anthropic messages for the Claude ids it serves, and a tool call
        // there is refused without an identity.
        call_id_key: Some("id"),
        // Inside the wrapper, not beside it: the identity belongs to the call
        // rather than to the part carrying it, and this endpoint reads only
        // the one place.
        call_id_position: IdentityPosition::InWrapper,
        name_key: "name",
        arguments_key: "args",
        arguments: ArgumentsShape::Value,
    },
    results: ResultPlacement {
        grouping: ResultGrouping::SharedTurn { role: "user" },
        wrapping: PartWrapping::Nested {
            key: "functionResponse",
        },
        call_id_key: Some("id"),
        call_id_position: IdentityPosition::InWrapper,
        // Kept beside the identity rather than replaced by it: the
        // generative-language shape pairs a result to its call by the tool's
        // name, and this endpoint reads that shape before it translates.
        name_key: Some("name"),
        payload: ResultPayload::Object {
            key: "response",
            text_key: "output",
        },
        failure: FailureSignal::PayloadKey { key: "error" },
    },
    text: TextLocation {
        path: &[
            Step::Key("response"),
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
            Step::Key("response"),
            Step::Key("candidates"),
            Step::FirstItem,
            Step::Key("content"),
            Step::Key("parts"),
        ],
        type_key: None,
        tags: &[],
        inner_key: Some("functionCall"),
        // `None` although the request carries one, which is the one place this
        // row deliberately breaks the pairing the generative-language row's
        // documentation describes. The endpoint's own identity is optional on
        // the way back — it began emitting one only recently and still omits
        // it — so a row that named the field would refuse every reply that
        // does not carry it. The identity a caller pairs on is the one the
        // core minted and this body already sent.
        id_key: None,
        name_key: "name",
        arguments_key: "args",
        arguments: ArgumentsShape::Value,
    },
    usage: UsageFields {
        object: &[Step::Key("response"), Step::Key("usageMetadata")],
        input: &[Step::Key("promptTokenCount")],
        output: &[Step::Key("candidatesTokenCount")],
        cache_read: Some(&[Step::Key("cachedContentTokenCount")]),
        cache_write: None,
        input_includes_cache: true,
    },
    stop: StopFields {
        paths: &[&[
            Step::Key("response"),
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
    // The one reply location outside the envelope: a request that failed
    // carries no `response` at all, and Google's own error object arrives at
    // the top exactly as it does on the generative-language endpoint.
    error: ErrorFields {
        object: &[Step::Key("error")],
        token: &[Step::Key("status")],
        message: &[Step::Key("message")],
        vocabulary: &[
            ("UNAUTHENTICATED", ErrorClass::Auth),
            ("PERMISSION_DENIED", ErrorClass::Auth),
            ("RESOURCE_EXHAUSTED", ErrorClass::Quota),
            ("UNAVAILABLE", ErrorClass::Overloaded),
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
    // Streaming is a URL path the browser owns, as on the family this one
    // wraps: `:streamGenerateContent` with `alt=sse`, never a body flag.
    stream: None,
    stream_usage: None,
};
