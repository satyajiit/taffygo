// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Writing one request body for whichever family the plan selected.
//!
//! # Borrowed, on purpose, everywhere
//!
//! Every type here carries a lifetime and every content-bearing field is a
//! borrow. There is no owned `String` in a single one of them, and the writer
//! appends into a buffer the caller supplies rather than returning one. That
//! is not a performance choice: this crate must remain structurally unable to
//! hold page-derived or locally retrieved material across a call, so that
//! "does the router retain content?" is answered by the type signatures
//! instead of by an audit of the code inside them. A `String` field would make
//! the answer "no, currently"; a reference makes it "no, and it cannot".
//!
//! # What a turn can be
//!
//! Three things, and only the first of them is somebody speaking: what was
//! said, the tools the model asked for, and the results it was given back.
//! Between them they are the whole of replaying an earlier exchange into a
//! later request, and the shapes the four families want are read from
//! [`super::dialect`] in the sibling `turns` module rather than decided per
//! family.
//!
//! It is deliberately not a normalization layer. Nothing reorders a
//! transcript, invents a result for a call that has none, drops a call whose
//! result went missing, or renames an identity from one family's spelling into
//! another's. A transcript that has gone wrong is refused, because every one
//! of those repairs is what turns a builder's defect into a request that looks
//! reasonable and is not — and the model reads the result of a repaired
//! request as a fact about the world.
//!
//! # What it does not build
//!
//! - **No transport.** No URL, no header, no method, no socket. Decision 0052
//!   puts the request itself in the browser process, where the network stack
//!   and the credential store are; this crate hands over a body.
//! - **No content-cache resources.** Explicit retention is limited to the
//!   tool-definition prefix on families whose table declares support. No
//!   system instruction, conversation, or page attachment receives a marker.

pub use super::reasoning::OpaqueReasoning;
pub use super::refusal::WireRefusal;

use super::compat::ServerCompat;
use super::emit::Json;
use crate::catalog::WireApi;
use crate::credential::AuthType;
use crate::json::JsonValue;
use crate::request::CacheRetention;
use crate::thinking::ThinkingPlan;

mod scalars;
mod tools;

/// Largest body this crate will write, in bytes.
///
/// A body is assembled from material whose size the router does not control.
/// The JSON sink stops retaining bytes as soon as this limit is crossed, but
/// keeps counting so a refusal still reports the complete encoded size.
pub const MAX_BODY_BYTES: usize = 8 * 1024 * 1024;

/// The only media type the browser's page attachment store can mint.
///
/// The value is carried beside an opaque one-use handle. The core writes that
/// handle into the provider-shaped data field; the browser claims the bytes
/// and replaces the handle immediately before dispatch.
pub const MEDIA_ATTACHMENT_MIME_TYPE: &str = "image/png";

/// Borrowed view of one browser-resident visual attachment.
///
/// No bytes can enter this type. `handle` is both the capability the browser
/// spends and the placeholder the body carries exactly once.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WireMediaAttachment<'a> {
    /// Browser-minted one-use handle.
    pub handle: &'a str,
    /// Exact declared media type; currently only PNG is accepted.
    pub mime_type: &'a str,
}

/// Who said one turn.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Speaker {
    /// The person, and anything assembled on their behalf.
    User,
    /// The model.
    Assistant,
}

/// One turn of the conversation.
///
/// Three shapes rather than one, and only the first is somebody speaking. The
/// other two are what keeps [`Speaker`] at two members: a tool result is not a
/// third speaker on any of the four families. Two of them wrap it in a turn
/// whose role is the *user's*, one gives it a role no speaker has, and one
/// gives it no role at all — so a third speaker would have been a false answer
/// in whichever row it was read in, and having the member without using it is
/// worse than not having it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Turn<'a> {
    /// Somebody said something.
    Said {
        /// Who spoke.
        speaker: Speaker,
        /// What was said, in order.
        ///
        /// A slice of borrowed pieces rather than one string, so a turn
        /// assembled from several sources is written without ever being joined
        /// into a value this crate owns.
        text: &'a [&'a str],
    },
    /// The model asked for tools, having possibly said something first.
    Called {
        /// What it said alongside, in order. Empty when it only called.
        text: &'a [&'a str],
        /// The calls, in the order the reply declared them.
        calls: &'a [ToolCallReplay<'a>],
        /// The sealed reasoning that turn produced, in the order the reply
        /// produced it.
        ///
        /// Empty on every family but the one that seals its reasoning, and
        /// empty on that one whenever the caller no longer holds the reply it
        /// came out of. Empty is the honest answer in both cases rather than a
        /// blob assembled from something else: a family that asked for its
        /// reasoning back reads a missing one as a turn that did not think,
        /// which is survivable, where it reads a wrong one as its own.
        reasoning: &'a [OpaqueReasoning<'a>],
    },
    /// The results of those calls came back.
    Returned {
        /// The results, in the order the calls were made.
        results: &'a [ToolResultView<'a>],
    },
}

impl Turn<'_> {
    /// Whether writing this turn needs a model that can call a tool at all.
    const fn replays_a_tool(&self) -> bool {
        !matches!(self, Self::Said { .. })
    }
}

/// One tool call the model already made, on its way back into a request.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToolCallReplay<'a> {
    /// The identity this call is known by.
    ///
    /// The core's own, on every family — including the three that mint one
    /// themselves, each of which matches a result against whatever identity
    /// the document it was sent carried. A replay-derived identity survives a
    /// restart where a provider's does not, and using one identity everywhere
    /// is why nothing in this crate renames an identity from one family's
    /// spelling into another's.
    pub call_id: &'a str,
    /// The tool named, in the registry's own spelling and never a model's.
    pub tool: &'a str,
    /// The arguments, decoded.
    ///
    /// Decoded even for the two families that carry them as a JSON document
    /// inside a string: the writer escapes the value back into a string for
    /// those two rather than quoting text it was handed, because quoting bytes
    /// from anywhere is how structure gets spliced into a request.
    pub arguments: &'a JsonValue,
}

/// What one tool call answered with.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToolResultView<'a> {
    /// The identity of the call this answers.
    pub call_id: &'a str,
    /// The tool that ran.
    ///
    /// Carried beside the identity rather than looked up from it, because one
    /// of the four families has no place for an identity at all and pairs a
    /// result to a call by this name.
    pub tool: &'a str,
    /// Whether the tool failed.
    ///
    /// One family carries a boolean for it, one changes the field the text
    /// arrives under, and two have nowhere at all — on those two it is folded
    /// into the text, because a failure delivered in the shape of a success is
    /// a result the model builds its next step on.
    pub is_error: bool,
    /// What it answered, in order.
    ///
    /// Strings are owned by the transient transcript, not this view. The
    /// outer reference keeps the router borrow-only while permitting a local
    /// retrieval result whose bytes are not compile-time vocabulary.
    pub text: &'a [String],
}

/// One tool offered to the model.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToolDeclaration<'a> {
    /// The name the model answers with.
    pub name: &'a str,
    /// The sentence the model reads.
    pub description: &'a str,
    /// The argument schema, already decoded.
    ///
    /// Decoded rather than pre-rendered text because a rendered schema would
    /// have to be spliced into the body verbatim, and a verbatim splice of
    /// bytes from anywhere is how structure gets injected into a request.
    pub parameters: &'a JsonValue,
}

/// Everything one request body is written from.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WireRequest<'a> {
    /// The provider's own name for the model.
    pub model_id: &'a str,
    /// Whether the catalog entry says this model may be handed tools.
    ///
    /// Carried on the request rather than looked up, so the encoder enforces
    /// the same fact the router selected on without resolving a snapshot that
    /// may since have been replaced.
    pub tool_calling: bool,
    /// The standing instruction.
    pub system: Option<&'a str>,
    /// The block that says who is calling, ahead of the instruction.
    ///
    /// One vendor's subscription endpoint requires it and refuses a request
    /// without it; its key endpoint requires nothing of the kind. It is a
    /// *system block on the request*, not a header the transport adds, because
    /// the model reads it — a fact hidden in a header is a fact nobody
    /// reviewing what the model was told can see.
    pub system_preamble: Option<&'a str>,
    /// How the credential this call goes out on authenticates.
    ///
    /// The registry's answer and nothing else. It is what decides whether the
    /// preamble is written, and it is carried rather than derived because the
    /// alternative — reading the shape of a stored token — is a guess about a
    /// convention the vendor may change, made at the one moment where being
    /// wrong sends a subscription request the vendor refuses or a key request
    /// carrying an identity it did not ask for.
    pub credential_method: Option<AuthType>,
    /// The overrides the server at the other end needs, when it is one a
    /// person runs themselves.
    pub compat: Option<ServerCompat<'a>>,
    /// The conversation, oldest first.
    pub turns: &'a [Turn<'a>],
    /// The tools this call offers.
    pub tools: &'a [ToolDeclaration<'a>],
    /// Requested retention of the tool-definition prefix only.
    ///
    /// Unsupported families omit the marker. Callers must keep person- and
    /// page-derived material out of declarations before opting in; later
    /// system and conversation blocks are never marked by this writer.
    pub tool_cache_retention: CacheRetention,
    /// The caller's own name for this conversation, stable across its turns.
    ///
    /// Written only where the family names a path for it, and only when the
    /// caller supplies one: it is a hint about which cached prefix a turn
    /// belongs to, so a family that has no such field and a request that has
    /// no such name are both answered by writing nothing.
    ///
    /// Not an identity anything is keyed on here. The value that crosses to a
    /// vendor is derived under its own domain by the caller, never a task or
    /// call identity reused — see decision 0111 section 3.
    pub conversation_key: Option<&'a str>,
    /// The thinking control, already clamped to this model's ladder.
    pub thinking: &'a ThinkingPlan,
    /// Tokens the answer may use.
    pub answer_tokens: u64,
    /// Whether the body asks the family to stream.
    ///
    /// The browser still performs the request. This flag is how three of the
    /// four families are told to emit frames instead of one object. The fourth
    /// streams by URL path, which this crate does not name.
    pub stream: bool,
}

impl<'a> WireRequest<'a> {
    /// The preamble this call actually carries.
    ///
    /// Present only when both halves are: the caller supplied one *and* the
    /// registry says the credential is a subscription. Written on a key
    /// credential it would be an identity the vendor never asked for, in a
    /// place the model reads.
    pub(super) fn preamble(&self) -> Option<&'a str> {
        match self.credential_method {
            Some(AuthType::Oauth) => self.system_preamble,
            Some(AuthType::ApiKey) | None => None,
        }
    }
}

/// Deepest tool argument schema this crate will write.
///
/// The same bound the catalog reader applies, so a schema that could be
/// decoded can be written back out again — a writer stricter than the reader
/// would refuse documents the process had already accepted.
pub const MAX_TOOL_SCHEMA_DEPTH: usize = crate::json::MAX_DEPTH;

/// Whether `value` nests no deeper than `remaining` further levels.
///
/// Bounded by construction: it descends at most `remaining` times, so checking
/// the bound cannot itself be the thing that overruns the stack.
pub(super) fn within_depth(value: &JsonValue, remaining: usize) -> bool {
    match value {
        JsonValue::Null
        | JsonValue::Bool(_)
        | JsonValue::Integer(_)
        | JsonValue::Decimal(_)
        | JsonValue::Text(_) => true,
        JsonValue::Array(items) => {
            remaining > 0 && items.iter().all(|item| within_depth(item, remaining - 1))
        }
        JsonValue::Object(map) => {
            remaining > 0 && map.values().all(|item| within_depth(item, remaining - 1))
        }
    }
}

/// Refuses a replayed identity the core did not mint.
///
/// Pairing has already been checked. This is the next assertion: the string
/// that paired is a `turn-{ordinal}-call-{sequence}` identity, not a
/// provider-minted one, and not a sanitizer's rewrite of one. An unpaired
/// call is already [`WireRefusal::CallWithoutResult`]; this does not invent a
/// result for it.
///
/// Visible to the module so [`super::managed`] asserts the same thing over the
/// same turns: the managed schema carries the core's identities unaltered too,
/// and a second copy of this rule is a copy that can come to differ.
pub(super) fn assert_replayed_call_ids(turns: &[Turn<'_>]) -> Result<(), WireRefusal> {
    for (turn_index, turn) in turns.iter().enumerate() {
        match turn {
            Turn::Said { .. } => {}
            Turn::Called { calls, .. } => {
                for (index, call) in calls.iter().enumerate() {
                    crate::normalize::assert_call_id(call.call_id)
                        .map_err(|_| WireRefusal::InvalidCallId { turn_index, index })?;
                }
            }
            Turn::Returned { results } => {
                for (index, result) in results.iter().enumerate() {
                    crate::normalize::assert_call_id(result.call_id)
                        .map_err(|_| WireRefusal::InvalidCallId { turn_index, index })?;
                }
            }
        }
    }
    Ok(())
}

/// Appends one request body to `out`.
///
/// On a refusal `out` is left exactly as it was found, so a caller that writes
/// several bodies into one buffer does not have to unwind a partial one.
pub fn write_request(
    api: WireApi,
    request: &WireRequest<'_>,
    out: &mut String,
) -> Result<(), WireRefusal> {
    write_request_body(api, request, None, out)
}

/// Appends one request body carrying a browser-resident visual attachment.
///
/// The handle is written exactly once. If any text, model id, or replayed
/// value already contains it, the whole write is rolled back: the browser
/// must never have to guess which occurrence is the capability placeholder.
pub fn write_media_request(
    api: WireApi,
    request: &WireRequest<'_>,
    media: WireMediaAttachment<'_>,
    out: &mut String,
) -> Result<(), WireRefusal> {
    write_request_body(api, request, Some(media), out)
}

fn write_request_body(
    api: WireApi,
    request: &WireRequest<'_>,
    media: Option<WireMediaAttachment<'_>>,
    out: &mut String,
) -> Result<(), WireRefusal> {
    // A replayed call or result needs the same capability a tool vocabulary
    // does: a model that cannot answer with a tool call cannot be shown a
    // transcript in which it did.
    let replays = request.turns.iter().any(Turn::replays_a_tool);
    if (!request.tools.is_empty() || replays) && !request.tool_calling {
        return Err(WireRefusal::ToolsUnsupportedByModel);
    }
    if request.turns.is_empty() {
        return Err(WireRefusal::EmptyConversation);
    }
    if let Some(media) = media {
        validate_media(media)?;
        if !request.turns.iter().any(|turn| {
            matches!(
                turn,
                Turn::Said {
                    speaker: Speaker::User,
                    ..
                }
            )
        }) {
            return Err(WireRefusal::MediaWithoutUserTurn);
        }
    }
    for (tool_index, tool) in request.tools.iter().enumerate() {
        if !within_depth(tool.parameters, MAX_TOOL_SCHEMA_DEPTH) {
            return Err(WireRefusal::ToolSchemaTooDeep { tool_index });
        }
    }
    super::transcript::check(request.turns)?;
    assert_replayed_call_ids(request.turns)?;
    let dialect = super::dialect_for(api);
    if request.preamble().is_some() && dialect.preamble_shape().is_none() {
        return Err(WireRefusal::PreambleUnsupportedByFamily);
    }
    let start = out.len();
    let mut json = Json::with_limit(out, MAX_BODY_BYTES);
    json.open_object();
    // The scalars are collected before anything is written, because a family
    // with an envelope wants some of them outside it and the rest within, and
    // that is one partition rather than two passes over the table. On the four
    // rows that declare no envelope every scalar is inside a zero-deep one —
    // an empty chain is a prefix of every path — so `outside` is empty, no
    // object is opened, and the body is byte for byte what it was.
    let (inside, outside): (Vec<_>, Vec<_>) = scalars::collect(dialect, request)
        .into_iter()
        .partition(|entry| entry.parents.starts_with(dialect.body_root));
    scalars::write_group(&mut json, &[], &outside);
    for key in dialect.body_root {
        json.key(key);
        json.open_object();
    }
    scalars::write_group(&mut json, dialect.body_root, &inside);
    if let (Some(placement), Some(compat)) = (dialect.compat, request.compat) {
        scalars::write_template_kwargs(&mut json, &placement, &compat);
    }
    if request.stream {
        if let Some(key) = dialect.stream {
            json.key(key);
            json.boolean(true);
            if let Some(usage) = dialect.stream_usage {
                for parent in usage.parents {
                    json.key(parent);
                    json.open_object();
                }
                json.key(usage.key);
                json.boolean(true);
                for _ in usage.parents {
                    json.close_object();
                }
            }
        }
    }
    super::turns::write_system(&mut json, dialect, request);
    super::turns::write_turns(&mut json, dialect, request, media);
    if !request.tools.is_empty() {
        tools::write(
            &mut json,
            &dialect.tools,
            request.tools,
            request.tool_cache_retention,
        );
    }
    for _ in dialect.body_root {
        json.close_object();
    }
    json.close_object();
    let bytes = json.attempted_bytes();
    drop(json);
    if bytes > MAX_BODY_BYTES {
        return Err(WireRefusal::BodyTooLarge { bytes });
    }
    if let Some(media) = media {
        let occurrences = out[start..].matches(media.handle).count();
        if occurrences != 1 {
            out.truncate(start);
            return Err(WireRefusal::AmbiguousMediaHandle { occurrences });
        }
    }
    Ok(())
}

/// Asserts the common shape both direct and managed media writers accept.
pub(super) fn validate_media(media: WireMediaAttachment<'_>) -> Result<(), WireRefusal> {
    if media.mime_type != MEDIA_ATTACHMENT_MIME_TYPE
        || media.handle.is_empty()
        || !media
            .handle
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_'))
    {
        return Err(WireRefusal::InvalidMediaAttachment);
    }
    Ok(())
}
