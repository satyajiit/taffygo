// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Reading one reply into the taxonomy every decision above is made from.
//!
//! # Borrowed, on purpose, everywhere
//!
//! The reply is a value the *caller* owns: [`read_reply`] takes `&'a
//! JsonValue` and every content-bearing field of [`ReplyReading`] is a `&'a
//! str` into it. The reading therefore cannot outlive the buffer the reply was
//! parsed from, and this crate holds no answer text and no tool argument once
//! the call returns. The one owned string in the module is
//! [`BoundedText`][crate::request::BoundedText] around a provider's own stop
//! or error token — a short word from a published vocabulary, kept because a
//! diagnosis that cannot quote the provider's word is a diagnosis nobody can
//! act on.
//!
//! # The three failures that look like successes
//!
//! This is where [`detect_overflow`] and [`recoverable_signal`] earn their
//! place, and the order they run in is the contract:
//!
//! 1. **Detect.** A reply that parsed cleanly can still be an overflow: input
//!    larger than the window, or a length stop with no output at a full one.
//!    Both otherwise read as a model that had nothing to say.
//! 2. **Recover.** The unpromoted result is asked what the caller may do about
//!    it — one bounded compact-and-retry, or a notice that the answer was cut
//!    short below the allowance that was asked for. Asked *before* the
//!    promotion, so the signal names how the overflow was actually noticed
//!    rather than the class the promotion just wrote.
//! 3. **Promote.** Only then does a detected overflow become a terminal
//!    [`ErrorClass::Overflow`], because a success that is not one must not
//!    reach the ledger, the audit record or the surface as a success.
//!
//! # What it does not do
//!
//! It reads a whole reply. Streaming is deliberately absent: decision 0052
//! leaves it out of the first version rather than answering by implementation
//! what a partial reply means to a record that says what happened.

use super::dialect::{
    ArgumentsShape, ErrorFields, ReasoningCarriage, Step, StopFields, TextLocation,
    ToolCallLocation, UsageFields,
};
use super::request::OpaqueReasoning;
use crate::catalog::WireApi;
use crate::cost::TokenUsage;
use crate::ids::RequestId;
use crate::json::JsonValue;
use crate::request::{
    detect_overflow, recoverable_signal, BoundedText, ErrorClass, OverflowKind, ProviderError,
    RecoverableSignal, StopReason, TerminalResult,
};

/// Largest number of tool calls one reply may carry.
///
/// One at a time is dispatched (decision 0052), so a reply naming more than a
/// handful is not a plan — it is a provider or a model behaving in a way
/// nobody designed for, and reading all of them would size a decision on
/// whatever the reply happened to contain.
pub const MAX_TOOL_CALLS: usize = 16;

/// What the caller knows about the request this reply answers.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReplyContext {
    /// Which request ended.
    pub request_id: RequestId,
    /// The model's context window, from the catalog entry.
    pub context_window: u64,
    /// The answer allowance the request asked for.
    pub requested_answer_tokens: u64,
    /// Whether the server at the other end says why an answer stopped.
    ///
    /// `true` for every published family. A server a person runs themselves
    /// may not, and on such a server the ordinary refusal for a missing stop
    /// word is *every* reply — so the fact has to arrive with the request that
    /// produced this one rather than being learned from it. It comes from
    /// [`ServerCompat`][crate::wire::ServerCompat], which is a detected kind
    /// and the catalog over it, never a guess made from this reply.
    pub reports_finish_reason: bool,
}

/// How a tool call's arguments arrived.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Arguments<'a> {
    /// Decoded in place.
    Value(&'a JsonValue),
    /// A JSON document inside a string. It is handed on unparsed: the bound a
    /// second parse runs under belongs to whoever owns the arena, not here.
    Text(&'a str),
}

/// One tool call the model asked for.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToolCallView<'a> {
    /// The identity the provider minted, when its family mints one.
    ///
    /// `None` is a real answer, not a missing one: one of the four families
    /// pairs a result to a call by the tool's name and mints nothing.
    pub call_id: Option<&'a str>,
    /// The tool named. Free-form here and closed one layer up — the invocation
    /// type it is decoded into is compiled in, so nothing free-form survives
    /// that boundary.
    pub tool: &'a str,
    /// The arguments.
    pub arguments: Arguments<'a>,
}

/// What the reply said, borrowed from it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReplyReading<'a> {
    /// The answer text, in the order it was produced.
    pub text: Vec<&'a str>,
    /// The tool calls, in the order the provider declared them.
    pub tool_calls: Vec<ToolCallView<'a>>,
    /// The provider's own stop word.
    pub provider_stop_token: Option<&'a str>,
    /// The sealed reasoning this turn produced, in order.
    ///
    /// Empty on every family that does not seal any. The values carry no
    /// accessor outside `wire`, so a caller can move them into the next
    /// request's turn and can do nothing else with them at all — which is the
    /// whole of what the family that produces them permits.
    pub reasoning: Vec<OpaqueReasoning<'a>>,
}

/// One reply, read.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReplyOutcome<'a> {
    /// What the reply said.
    pub reading: ReplyReading<'a>,
    /// The single ending, ready to journal.
    pub result: TerminalResult,
    /// How an overflow was noticed, when one was.
    pub overflow: Option<OverflowKind>,
    /// What the caller may do about it, inside its own budget.
    pub recovery: Option<RecoverableSignal>,
}

/// Why a reply could not be read.
///
/// Every member is a refusal. Nothing here is repaired to a plausible value:
/// a reply that cannot be read is a request whose outcome is unknown, and an
/// unknown outcome recorded as a known one is worse than no record.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ReplyDefect {
    /// No stop token at any path the family declares.
    MissingStopReason,
    /// A stop token the family's vocabulary does not list. Mapping it onto the
    /// nearest word would report a filtered or truncated answer as a finished
    /// one.
    UnknownStopToken(BoundedText),
    /// No usage object. Reading an absent one as zero would tell the ledger
    /// the call was free, and a budget that is told a call was free stops
    /// being a budget.
    MissingUsage,
    /// A tool call that announced itself and then did not carry a name, an
    /// identity its family mints, or its arguments.
    MalformedToolCall,
    /// More tool calls than [`MAX_TOOL_CALLS`].
    TooManyToolCalls,
}

impl core::fmt::Display for ReplyDefect {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::MissingStopReason => f.write_str("the reply carries no stop reason"),
            Self::UnknownStopToken(token) => {
                write!(f, "unknown stop reason {:?}", token.as_str())
            }
            Self::MissingUsage => f.write_str("the reply carries no usage"),
            Self::MalformedToolCall => f.write_str("a tool call is incomplete"),
            Self::TooManyToolCalls => f.write_str("the reply carries too many tool calls"),
        }
    }
}

/// Reads one reply.
pub fn read_reply<'a>(
    api: WireApi,
    reply: &'a JsonValue,
    context: &ReplyContext,
) -> Result<ReplyOutcome<'a>, ReplyDefect> {
    let dialect = super::dialect_for(api);
    if let Some(error) = read_error(reply, &dialect.error) {
        return Ok(failed(context, error));
    }
    let tool_calls = read_tool_calls(reply, &dialect.tool_calls)?;
    let (declared_stop, provider_stop_token) =
        read_stop(reply, &dialect.stop, context.reports_finish_reason)?;
    let usage = read_usage(reply, &dialect.usage).ok_or(ReplyDefect::MissingUsage)?;
    // Structure decides, not the word. Every family announces a tool call by
    // what it produced, and two of them report an ordinary finish word beside
    // one — a reply read by its word alone would hand back "the model is
    // finished" for a turn that is waiting on a tool. The one word that
    // outranks the structure is the provider's own "stopped at the
    // allowance": a call beside it is a call the model did not finish, and
    // row 17 of the agent table can refuse it as truncated only if the stop
    // it reads is `Length`.
    let stop = if tool_calls.is_empty() || declared_stop == StopReason::Length {
        declared_stop
    } else {
        StopReason::ToolCall
    };
    let reading = ReplyReading {
        text: read_text(reply, &dialect.text),
        tool_calls,
        provider_stop_token,
        reasoning: read_reasoning(reply, dialect.reasoning),
    };
    let mut result = TerminalResult {
        request_id: context.request_id,
        stop,
        usage,
        provider_stop_reason: provider_stop_token.map(BoundedText::new),
        error: None,
    };
    let reported_input = usage.total_input();
    let overflow = detect_overflow(&result, context.context_window, reported_input);
    let recovery = recoverable_signal(
        &result,
        context.context_window,
        reported_input,
        context.requested_answer_tokens,
    );
    if let Some(kind) = overflow {
        result.stop = StopReason::Error;
        result.error = Some(ProviderError {
            class: ErrorClass::Overflow,
            retry_after_millis: None,
            detail: BoundedText::new(overflow_detail(kind)),
        });
    }
    Ok(ReplyOutcome {
        reading,
        result,
        overflow,
        recovery,
    })
}

/// A reply that carried a failure instead of an answer.
///
/// The usage is zero because a failing reply reports none in any of the four
/// families. That is a claim about the reply, not about the money: whether the
/// attempt was billed is the provider's ledger to answer, and the task ledger
/// already spends the estimate at admission rather than the report.
fn failed<'a>(context: &ReplyContext, error: ProviderError) -> ReplyOutcome<'a> {
    ReplyOutcome {
        reading: ReplyReading {
            text: Vec::new(),
            tool_calls: Vec::new(),
            provider_stop_token: None,
            reasoning: Vec::new(),
        },
        result: TerminalResult {
            request_id: context.request_id,
            stop: StopReason::Error,
            usage: TokenUsage::default(),
            provider_stop_reason: None,
            error: Some(error),
        },
        overflow: None,
        recovery: None,
    }
}

fn overflow_detail(kind: OverflowKind) -> &'static str {
    match kind {
        OverflowKind::ProviderReported => "the provider reported a context overflow",
        OverflowKind::Silent => "the reply reported more input than the window holds",
        OverflowKind::Truncation => "the reply stopped on length with no output at a full window",
    }
}

/// Follows a path from a value, entering the first element of any array the
/// path steps into.
fn walk<'a>(root: &'a JsonValue, steps: &[Step]) -> Option<&'a JsonValue> {
    let mut current = root;
    for step in steps {
        current = match step {
            Step::Key(name) => current.field(name)?,
            Step::FirstItem => current.as_array()?.first()?,
        };
    }
    Some(current)
}

/// Whether one element of a produced array is one of the declared kinds.
///
/// An empty tag list means the family does not tag its elements at all, and
/// every element is a candidate — the caller then tells them apart by which
/// field they carry. It is not "match anything" as a fallback; it is a fact
/// two of the four tables state.
fn matches_tag(item: &JsonValue, type_key: Option<&str>, tags: &[&str]) -> bool {
    if tags.is_empty() {
        return true;
    }
    let Some(tag) = type_key
        .and_then(|key| item.field(key))
        .and_then(JsonValue::as_str)
    else {
        return false;
    };
    tags.contains(&tag)
}

fn read_text<'a>(reply: &'a JsonValue, location: &TextLocation) -> Vec<&'a str> {
    let mut out = Vec::new();
    let Some(value) = walk(reply, location.path) else {
        return out;
    };
    if let Some(text) = value.as_str() {
        out.push(text);
        return out;
    }
    let Some(items) = value.as_array() else {
        return out;
    };
    for item in items {
        collect_text(item, location, &mut out, true);
    }
    out
}

/// Reads one produced item, descending at most one level.
///
/// One level and no more: a reply that nests without bound is a reply that
/// decides how much work reading it costs, and the one family that nests at
/// all nests exactly once.
fn collect_text<'a>(
    item: &'a JsonValue,
    location: &TextLocation,
    out: &mut Vec<&'a str>,
    may_descend: bool,
) {
    if may_descend {
        if let Some(key) = location.nested_key {
            if matches_tag(item, location.type_key, location.nested_tags) {
                if let Some(blocks) = item.field(key).and_then(JsonValue::as_array) {
                    for block in blocks {
                        collect_text(block, location, out, false);
                    }
                }
                return;
            }
        }
    }
    if !matches_tag(item, location.type_key, location.tags) {
        return;
    }
    if let Some(text) = item.field(location.text_key).and_then(JsonValue::as_str) {
        out.push(text);
    }
}

fn read_tool_calls<'a>(
    reply: &'a JsonValue,
    location: &ToolCallLocation,
) -> Result<Vec<ToolCallView<'a>>, ReplyDefect> {
    let mut out = Vec::new();
    let Some(items) = walk(reply, location.path).and_then(JsonValue::as_array) else {
        return Ok(out);
    };
    for item in items {
        if !matches_tag(item, location.type_key, location.tags) {
            continue;
        }
        // An element that does not announce a tool call at all is another kind
        // of block, not a broken one: two families put text and calls in the
        // same array.
        let body = match location.inner_key {
            Some(key) => match item.field(key) {
                Some(inner) => inner,
                None => continue,
            },
            None => item,
        };
        if out.len() >= MAX_TOOL_CALLS {
            return Err(ReplyDefect::TooManyToolCalls);
        }
        out.push(read_tool_call(item, body, location)?);
    }
    Ok(out)
}

fn read_tool_call<'a>(
    item: &'a JsonValue,
    body: &'a JsonValue,
    location: &ToolCallLocation,
) -> Result<ToolCallView<'a>, ReplyDefect> {
    let tool = body
        .field(location.name_key)
        .and_then(JsonValue::as_str)
        .ok_or(ReplyDefect::MalformedToolCall)?;
    // The identity sits on the outer element in every family that mints one,
    // even where the name and arguments have moved inside.
    let call_id = match location.id_key {
        Some(key) => Some(
            item.field(key)
                .and_then(JsonValue::as_str)
                .ok_or(ReplyDefect::MalformedToolCall)?,
        ),
        None => None,
    };
    let raw = body
        .field(location.arguments_key)
        .ok_or(ReplyDefect::MalformedToolCall)?;
    let arguments = match location.arguments {
        ArgumentsShape::Value => Arguments::Value(raw),
        ArgumentsShape::EmbeddedText => {
            Arguments::Text(raw.as_str().ok_or(ReplyDefect::MalformedToolCall)?)
        }
    };
    Ok(ToolCallView {
        call_id,
        tool,
        arguments,
    })
}

/// The sealed reasoning items, in the order the reply produced them.
///
/// An item that announces itself and carries no payload is skipped rather than
/// carried: there is nothing in it the next turn could hand back, and sending
/// a reasoning item the family cannot decrypt is worse than sending none —
/// none reads as a turn that did not think, and one it cannot open reads as a
/// conversation that has been tampered with.
fn read_reasoning(
    reply: &JsonValue,
    carriage: Option<ReasoningCarriage>,
) -> Vec<OpaqueReasoning<'_>> {
    let Some(carriage) = carriage else {
        return Vec::new();
    };
    let Some(items) = walk(reply, carriage.path).and_then(JsonValue::as_array) else {
        return Vec::new();
    };
    items
        .iter()
        .filter(|item| {
            item.field(carriage.type_key).and_then(JsonValue::as_str) == Some(carriage.tag)
                && item.field(carriage.payload_key).is_some()
        })
        .map(OpaqueReasoning::new)
        .collect()
}

fn read_stop<'a>(
    reply: &'a JsonValue,
    fields: &StopFields,
    reports_finish_reason: bool,
) -> Result<(StopReason, Option<&'a str>), ReplyDefect> {
    for path in fields.paths {
        let Some(token) = walk(reply, path).and_then(JsonValue::as_str) else {
            continue;
        };
        let mapped = fields
            .vocabulary
            .iter()
            .find(|(word, _)| *word == token)
            .map(|(_, stop)| *stop);
        // The first path that is *present* decides, even when its word is
        // unknown. Falling through to a more general path would answer a
        // question the specific one already answered, with a word that means
        // less.
        return match mapped {
            Some(stop) => Ok((stop, Some(token))),
            None => Err(ReplyDefect::UnknownStopToken(BoundedText::new(token))),
        };
    }
    // A server that never says why it stopped is not a reply that went wrong,
    // and refusing every one of its replies would leave a person's own server
    // unusable rather than imprecise. The absence is read as an ordinary
    // finish and no provider word is claimed, so nothing downstream can quote
    // a stop reason that was never sent.
    if !reports_finish_reason {
        return Ok((StopReason::Complete, None));
    }
    Err(ReplyDefect::MissingStopReason)
}

fn read_usage(reply: &JsonValue, fields: &UsageFields) -> Option<TokenUsage> {
    // Same rule as [`read_error`], for the opposite reason: a null where the
    // counts belong has to read as *absent* so it refuses with
    // [`ReplyDefect::MissingUsage`]. Accepting the null would walk it for
    // every count, find nothing, and hand back four zeros — which is exactly
    // the "this call was free" the refusal exists to prevent, and worse than
    // the absent object because the reply looked like it answered.
    let object = walk(reply, fields.object).filter(|value| value.as_object().is_some())?;
    let count = |steps: &[Step]| -> u64 {
        walk(object, steps)
            .and_then(JsonValue::as_i64)
            .and_then(|value| u64::try_from(value).ok())
            .unwrap_or(0)
    };
    let cache_read = fields.cache_read.map_or(0, count);
    let cache_write = fields.cache_write.map_or(0, count);
    let reported = count(fields.input);
    // Subtracting is what keeps `TokenUsage::total_input` equal to the prompt
    // total the provider reported, on every family, which is what both the
    // price and the overflow check are read against.
    let input = if fields.input_includes_cache {
        reported
            .saturating_sub(cache_read)
            .saturating_sub(cache_write)
    } else {
        reported
    };
    Some(TokenUsage {
        input,
        output: count(fields.output),
        cache_read,
        cache_write,
    })
}

fn read_error(reply: &JsonValue, fields: &ErrorFields) -> Option<ProviderError> {
    // A failure is an *object* at that path, not a key at it. One family
    // carries `"error": null` in every successful body it sends, so reading
    // presence as failure turns each of that family's successes into a
    // terminal [`ErrorClass::Unknown`]: the answer discarded, the token counts
    // reported to the ledger as zero, and no retry, because Unknown is
    // terminal by design. The reply that fails this way is the ordinary one,
    // which is why it has to be checked rather than assumed.
    let object = walk(reply, fields.object).filter(|value| value.as_object().is_some())?;
    let token = walk(object, fields.token).and_then(JsonValue::as_str);
    let class = token
        .and_then(|word| {
            fields
                .vocabulary
                .iter()
                .find(|(candidate, _)| *candidate == word)
                .map(|(_, class)| *class)
        })
        // The taxonomy's own "nothing above matched", whose disposition is
        // terminal — so an unrecognized failure stops instead of being retried
        // into a bill.
        .unwrap_or(ErrorClass::Unknown);
    let detail = walk(object, fields.message)
        .and_then(JsonValue::as_str)
        .or(token)
        .unwrap_or_default();
    // A provider that named its failure is believed. This only fills in for the
    // ones that did not: the OpenAI-compatible shape is spoken by aggregators
    // and by servers a person runs themselves, and several report a context
    // overflow as a plain invalid request, or with no token at all, leaving the
    // reason in the sentence. Unknown is terminal, so reading that sentence is
    // the difference between compacting a transcript and ending the task.
    let class = if matches!(class, ErrorClass::Unknown) {
        super::overflow_text::classify(detail).unwrap_or(class)
    } else {
        class
    };
    Some(ProviderError {
        class,
        retry_after_millis: super::overflow_text::retry_after_millis(detail),
        detail: BoundedText::new(detail),
    })
}
