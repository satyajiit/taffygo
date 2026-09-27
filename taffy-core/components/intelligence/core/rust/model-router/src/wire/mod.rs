// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The four protocol families, as one table and two readers of it.
//!
//! A provider is catalog data and a model is catalog data; the family a model
//! speaks is the only per-provider *code* in this crate, which is why adding a
//! provider or a model is a catalog change and adding a family is a release.
//! This module is that code, and it is written so that the four families are
//! four rows rather than four code paths.
//!
//! # Why a table
//!
//! The families differ almost entirely in field names and nesting. The
//! conversation is `messages`, `input` or `contents`; the assistant is
//! `assistant` or `model`; the answer allowance is `max_tokens`,
//! `max_output_tokens`, `max_completion_tokens` or two objects deep under
//! `generationConfig`. Written as four encoders, each of those is an
//! independent chance to omit a branch — and the omission produces a body a
//! provider accepts and answers wrongly, which is not what a test looking for
//! an error will find. Written as four rows against closed enumerations that
//! every reader matches exhaustively, a case nobody handled does not compile.
//!
//! It also puts the differences somewhere they can be read. Four facts in
//! these tables are load-bearing and none of them is obvious:
//!
//! - one family calls the assistant `model`, and the others' spelling is
//!   rejected for a reason that reads as nothing to do with roles;
//! - one family mints no identity for a tool call at all, so a caller that
//!   needs one mints its own;
//! - three of the four fold cached tokens into the prompt total and one
//!   reports them apart, so a reader that ignored the difference would price
//!   three providers out of four with the cached tokens counted twice;
//! - a tool result is not a speaker anywhere. Two families wrap it in a turn
//!   whose role is the user's, one gives it a role no speaker has, and one
//!   gives it no role at all — which is why [`Speaker`] has two members and
//!   [`Turn`] has three.
//!
//! # What is here and what is not
//!
//! [`write_request`] appends a body; [`read_reply`] reads one. Neither touches
//! the network: decision 0052 puts the request in the browser process, where
//! the network stack and the credential store are, and this crate shapes what
//! goes in it. There is no URL, no header, no method, no client, no executor
//! and no clock anywhere below this module.
//!
//! Every function that touches page-derived material takes a borrowed view —
//! `&str`, `&[..]`, `&JsonValue` — and writes into a buffer the caller owns.
//! No type in this module has an owned field carrying content, which is what
//! makes "the router holds no page content" a property of the signatures
//! rather than a claim about the code inside them.
//!
//! | Module | Owns |
//! |---|---|
//! | [`dialect`] | The vocabulary a family is described in |
//! | [`request`] | The borrowed conversation views and the body writer |
//! | `transcript` | What a replayed conversation has to satisfy to be written at all |
//! | `turns` | Writing the conversation, replayed tool calls and results included |
//! | [`reply`] | Reading a reply into the taxonomy, and the overflow rules |
//! | [`managed`] | The managed route's own canonical schema — one writer and one reader, deliberately outside the dialect table |

mod anthropic;
pub mod compat;
pub mod dialect;
mod emit;
mod exchange;
mod google;
mod google_cloud_code_assist;
pub mod managed;
mod managed_stream;
mod openai_codex_responses;
mod openai_completions;
mod openai_responses;
mod overflow_text;
pub mod reasoning;
pub mod refusal;
pub mod reply;
pub mod request;
pub mod stream;
mod transcript;
mod turns;

pub use self::compat::{detect, ServerCompat, ServerKind};
pub use self::dialect::Dialect;
pub use self::managed::{
    tools_travel_at, write_managed_media_request, write_managed_request, ManagedRequest,
    ManagedWireRefusal, MANAGED_SCHEMA_VERSION, MANAGED_TOOL_SCHEMA_VERSION,
};
pub use self::managed_stream::{
    fold_managed_stream, ManagedReading, ManagedReplyDefect, ManagedToolCall,
    MAX_MANAGED_STREAM_BYTES,
};
pub use self::reply::{
    read_reply, Arguments, ReplyContext, ReplyDefect, ReplyOutcome, ReplyReading, ToolCallView,
};
pub use self::request::{
    write_media_request, write_request, OpaqueReasoning, Speaker, ToolCallReplay, ToolDeclaration,
    ToolResultView, Turn, WireMediaAttachment, WireRefusal, WireRequest, MAX_BODY_BYTES,
    MEDIA_ATTACHMENT_MIME_TYPE,
};
pub use self::stream::{
    apply_frame, fold_stream, looks_like_stream, FoldedReply, FoldedToolCall, ModelStreamDecoder,
    ModelStreamDefect, ModelStreamReading, ModelStreamToolCall, StreamDefect, StreamEvent,
    StreamFold, MAX_DIRECT_STREAM_BYTES,
};

use crate::catalog::WireApi;
use crate::wire::dialect::CallPosition;

/// The table row for one family.
///
/// Exhaustive with no catch-all arm, which is the whole mechanism: a family
/// added to [`WireApi`] without a row here fails to compile, rather than
/// falling through to whichever row was written first.
pub fn dialect_for(api: WireApi) -> &'static Dialect {
    match api {
        WireApi::AnthropicMessages => &anthropic::DIALECT,
        WireApi::OpenAiResponses => &openai_responses::DIALECT,
        WireApi::OpenAiCompletions => &openai_completions::DIALECT,
        WireApi::GoogleGenerativeLanguage => &google::DIALECT,
        WireApi::OpenAiCodexResponses => &openai_codex_responses::DIALECT,
        WireApi::GoogleCloudCodeAssist => &google_cloud_code_assist::DIALECT,
    }
}

/// Whether this family seals the reasoning a turn produced and reads it back.
///
/// The row's own answer, not a list of family names: a family declares a
/// [`dialect::ReasoningCarriage`] exactly when it wants the sealed item
/// returned beside the tool call it preceded, so the row that would break on a
/// missing item is the row that declares one. A fifth family that seals its
/// reasoning is therefore covered the day its row is written.
///
/// The router asks this before it agrees to write a tool-carrying body; see
/// [`crate::RouteRefusal::ToolLoopCarriageMissing`].
pub fn seals_reasoning(api: WireApi) -> bool {
    dialect_for(api).reasoning.is_some()
}

/// Whether the row's own two answers about its content agree.
///
/// [`CallPosition::InContent`] puts a replayed tool call in the turn's content
/// array, which exists only on a family whose [`dialect::ContentShape`] is an
/// array. They are separate fields and nothing in the type system pairs them,
/// so the writer asks [`dialect::ContentShape::parts`] rather than assuming —
/// and this is what makes the answer it gets exhaustive rather than hopeful.
const fn content_holds_calls(dialect: &Dialect) -> bool {
    !matches!(dialect.calls.position, CallPosition::InContent) || dialect.content.parts().is_some()
}

// Checked at compile time, once per row. A row that paired the two wrongly
// would otherwise write a turn whose calls are silently missing from the body
// — a request the provider accepts and answers as though the model had never
// asked for anything.
const _: () = assert!(content_holds_calls(&anthropic::DIALECT));
const _: () = assert!(content_holds_calls(&openai_responses::DIALECT));
const _: () = assert!(content_holds_calls(&openai_completions::DIALECT));
const _: () = assert!(content_holds_calls(&google::DIALECT));
const _: () = assert!(content_holds_calls(&openai_codex_responses::DIALECT));
const _: () = assert!(content_holds_calls(&google_cloud_code_assist::DIALECT));
