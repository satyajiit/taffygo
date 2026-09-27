// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Reading one provider's bytes into the reducer's taxonomy.
//!
//! This runs in the sandboxed utility process and not in the browser, for two
//! independent reasons. Parsing a provider's JSON is parsing untrusted
//! structure, and the browser process holds every capability there is (Rule of
//! Two). And the usage counts and the stop token sit in a different place in
//! each of the four protocol families, so `model-router`'s reader is the only
//! party that can find them at all.
//!
//! Nothing read here becomes durable prose. The answer text stays in this
//! function's borrow of the reply and is counted, never carried: what reaches
//! the reducer is a [`task_engine::ModelReply`], whose only text field is a
//! tool name that is about to be resolved against the compiled-in registry.

use model_router::catalog::WireApi;
use model_router::ids::RequestId;
use model_router::json::{self, JsonValue};
use model_router::request::{OverflowKind, StopReason};
use model_router::wire::reply::{Arguments, ReplyContext, ToolCallView};
use model_router::wire::{
    fold_managed_stream, fold_stream, looks_like_stream, read_reply, ModelStreamDecoder,
    ModelStreamDefect, ModelStreamReading, ModelStreamToolCall,
};

use super::{ComposedModelTurn, ReplyWire};
use task_engine::{
    ArgumentValue, Milestone, ModelReply, ModelStopReason, ModelToolCall, ParameterType,
    SuppliedArgument, ToolEntry, TurnGap, TurnOverflow, TurnUsage, MAX_SUPPLIED_ARGUMENTS,
    MAX_TURN_TOOL_CALLS,
};

/// What one completion turned out to be.
///
/// Two members and not one: a reply that was read is a fact about what the
/// model said, and a reply that could not be read is a fact about a call that
/// was paid for and produced nothing usable. Collapsing them would record an
/// unknown outcome as a known one.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ModelReplyReading {
    /// The reply was read into the taxonomy.
    Read(Box<ModelReply>),
    /// There is no readable reply, for this reason.
    Gap(TurnGap),
}

/// One held model turn while raw response chunks arrive in the sandbox.
///
/// This adapter owns the route/context pairing from composition through the
/// single settlement. It does not hold emitted answer text: `push` lends each
/// sanitized delta to the caller's callback, and `finish` promotes the typed
/// terminal through the same milestone-aware path as [`read_model_reply`].
#[derive(Debug)]
pub struct ModelReplyStream {
    decoder: ModelStreamDecoder,
    milestone: Milestone,
    /// The first defect a pushed chunk met, by its compiled-in name. One
    /// defect closes the decoder, so the first is the one that decided.
    defect: Option<&'static str>,
}

impl ModelReplyStream {
    /// Starts incremental reading from the exact turn retained for settlement.
    pub fn for_turn(turn: &ComposedModelTurn, request_id: RequestId, milestone: Milestone) -> Self {
        Self::for_wire(
            &turn.wire,
            request_id,
            turn.context_window,
            turn.answer_tokens,
            milestone,
        )
    }

    /// Starts incremental reading from an explicitly retained wire/context.
    pub fn for_wire(
        wire: &ReplyWire,
        request_id: RequestId,
        context_window: u64,
        requested_answer_tokens: u64,
        milestone: Milestone,
    ) -> Self {
        let context = ReplyContext {
            request_id,
            context_window,
            requested_answer_tokens,
            reports_finish_reason: true,
        };
        let decoder = match wire {
            ReplyWire::Family(api) => ModelStreamDecoder::direct(*api, context),
            ReplyWire::Managed {
                request_id: canonical_id,
            } => ModelStreamDecoder::managed(canonical_id, context),
        };
        Self {
            decoder,
            milestone,
            defect: None,
        }
    }

    /// Applies arbitrary raw browser bytes and emits only visible text.
    pub fn push(
        &mut self,
        chunk: &[u8],
        emit_text: &mut dyn FnMut(&str),
    ) -> Result<(), ModelStreamDefect> {
        let result = self.decoder.push(chunk, emit_text);
        if let Err(defect) = &result {
            self.defect.get_or_insert(defect.label());
        }
        result
    }

    /// Whether the provider terminal and mandatory usage are present.
    pub fn terminal_ready(&self) -> bool {
        self.defect.is_none() && self.decoder.terminal_ready()
    }

    /// Ends the transport and settles this turn exactly once.
    pub fn finish(self, emit_text: &mut dyn FnMut(&str)) -> ModelReplyReading {
        self.finish_noted(emit_text).0
    }

    /// Ends the transport like [`Self::finish`], and names why a gap is one.
    ///
    /// The note is the compiled-in name of the defect that decided —
    /// `missing_usage`, `malformed_frame` — or `tool_call_ceiling` for a reply
    /// naming more calls than a turn may carry. It is `None` beside a reading.
    /// Every such reason reaches the reducer as the same
    /// [`TurnGap::Unreadable`], so this is the only place they differ; the
    /// phone re-asked Grok seventy-four times on 2026-09-18 with no line
    /// saying why each reply was refused.
    pub fn finish_noted(
        mut self,
        emit_text: &mut dyn FnMut(&str),
    ) -> (ModelReplyReading, Option<&'static str>) {
        if let Some(defect) = self.defect {
            return (ModelReplyReading::Gap(TurnGap::Unreadable), Some(defect));
        }
        match self.decoder.finish(emit_text) {
            Ok(reading) => {
                let read = read_model_stream_terminal(&reading, self.milestone);
                let note = matches!(read, ModelReplyReading::Gap(_)).then_some("tool_call_ceiling");
                (read, note)
            }
            Err(defect) => (
                ModelReplyReading::Gap(TurnGap::Unreadable),
                Some(defect.label()),
            ),
        }
    }

    /// Settles an interrupted transport as a typed canceled model reply.
    pub fn cancel(mut self) -> ModelReplyReading {
        if self.defect.is_some() {
            return ModelReplyReading::Gap(TurnGap::Unreadable);
        }
        if let Ok(reading) = self.decoder.cancel() {
            read_model_stream_terminal(&reading, self.milestone)
        } else {
            ModelReplyReading::Gap(TurnGap::Unreadable)
        }
    }
}

/// Promotes one sanitized incremental terminal into the reducer's reply.
///
/// This is the sandbox seam for a transport that feeds
/// [`model_router::wire::ModelStreamDecoder`]. Provider identities, reasoning,
/// and raw frames cannot reach this function because its input type has no
/// fields for them. Tool arguments are still typed against the milestone's
/// compiled-in registry before they reach the reducer.
pub fn read_model_stream_terminal(
    reading: &ModelStreamReading,
    milestone: Milestone,
) -> ModelReplyReading {
    if reading.tool_calls.len() > MAX_TURN_TOOL_CALLS {
        return ModelReplyReading::Gap(TurnGap::Unreadable);
    }
    let mut tool_calls = Vec::with_capacity(reading.tool_calls.len());
    for call in &reading.tool_calls {
        let view = ToolCallView {
            call_id: None,
            tool: call.name.as_str(),
            arguments: Arguments::Value(&call.arguments),
        };
        tool_calls.push(tool_call(&view, milestone));
    }
    ModelReplyReading::Read(Box::new(ModelReply {
        stop: stop_reason(reading.result.stop),
        overflow: reading.overflow.map(overflow),
        usage: TurnUsage {
            input_units: reading.result.usage.input,
            output_units: reading.result.usage.output,
            cache_read_units: reading.result.usage.cache_read,
            cache_write_units: reading.result.usage.cache_write,
        },
        answer_segments: reading.text_segments,
        tool_calls,
    }))
}

/// Reads `completion` as a reply in `wire`'s vocabulary.
///
/// Never `Err`: every way of failing to read a provider's answer is already a
/// [`TurnGap`], and a caller with two error channels for one question would
/// have to decide which of them means "the money was spent".
pub fn read_model_reply(
    wire: &ReplyWire,
    completion: &[u8],
    request_id: RequestId,
    context_window: u64,
    requested_answer_tokens: u64,
    milestone: Milestone,
) -> ModelReplyReading {
    let Ok(text) = core::str::from_utf8(completion) else {
        return ModelReplyReading::Gap(TurnGap::Unreadable);
    };
    let context = ReplyContext {
        request_id,
        context_window,
        requested_answer_tokens,
        // The kernel reads replies from published families, which all report
        // why an answer stopped. A server a person runs themselves that does
        // not is reached through the compat table the request was written
        // with, and threading that here is the caller's to do when it does.
        reports_finish_reason: true,
    };
    let wire_api = match wire {
        ReplyWire::Family(wire_api) => *wire_api,
        ReplyWire::Managed {
            request_id: canonical_id,
        } => return read_managed(text, canonical_id, &context, milestone),
    };
    if looks_like_stream(text) {
        return read_stream(wire_api, text, &context, milestone);
    }
    let Ok(document) = json::parse_provider(text) else {
        return ModelReplyReading::Gap(TurnGap::Unreadable);
    };
    let Ok(outcome) = read_reply(wire_api, &document, &context) else {
        return ModelReplyReading::Gap(TurnGap::Unreadable);
    };
    if outcome.reading.tool_calls.len() > MAX_TURN_TOOL_CALLS {
        return ModelReplyReading::Gap(TurnGap::Unreadable);
    }
    let mut tool_calls = Vec::with_capacity(outcome.reading.tool_calls.len());
    for call in &outcome.reading.tool_calls {
        tool_calls.push(tool_call(call, milestone));
    }
    ModelReplyReading::Read(Box::new(ModelReply {
        stop: stop_reason(outcome.result.stop),
        overflow: outcome.overflow.map(overflow),
        usage: TurnUsage {
            input_units: outcome.result.usage.input,
            output_units: outcome.result.usage.output,
            cache_read_units: outcome.result.usage.cache_read,
            cache_write_units: outcome.result.usage.cache_write,
        },
        answer_segments: u32::try_from(outcome.reading.text.len()).unwrap_or(u32::MAX),
        tool_calls,
    }))
}

/// Reads one canonical response from the managed route.
///
/// The worker answers in bounded canonical NDJSON. A response that fails the
/// canonical shape — a version this build does not read, a mismatched request
/// identity on any event, malformed ordering, or incomplete tool arguments —
/// is a gap: the money question it leaves open is the same one an unreadable
/// provider reply leaves.
///
/// Tool calls are folded by their canonical index and read here rather than
/// dropped. The identity is minted in this process by
/// `turn_call_key`, never taken from the response, so a managed call and a
/// direct call arrive as the same shape with the same identity and the kernel's
/// walk cannot learn which upstream answered — which is what keeps a recorded
/// procedure replayable after a person's key expires and the managed route
/// serves the next turn.
fn read_managed(
    text: &str,
    canonical_id: &str,
    context: &ReplyContext,
    milestone: Milestone,
) -> ModelReplyReading {
    let Ok(reading) = fold_managed_stream(text, canonical_id, context) else {
        return ModelReplyReading::Gap(TurnGap::Unreadable);
    };
    read_model_stream_terminal(
        &ModelStreamReading {
            text_segments: reading.text_segments,
            tool_calls: reading
                .tool_calls
                .into_iter()
                .map(|call| ModelStreamToolCall {
                    name: call.tool,
                    arguments: call.arguments,
                })
                .collect(),
            result: reading.result,
            overflow: reading.overflow,
            recovery: None,
        },
        milestone,
    )
}

fn read_stream(
    wire_api: WireApi,
    text: &str,
    context: &ReplyContext,
    milestone: Milestone,
) -> ModelReplyReading {
    let Ok(folded) = fold_stream(wire_api, text, context) else {
        return ModelReplyReading::Gap(TurnGap::Unreadable);
    };
    read_model_stream_terminal(
        &ModelStreamReading {
            text_segments: folded.text_segments,
            tool_calls: folded
                .tool_calls
                .into_iter()
                .map(|call| ModelStreamToolCall {
                    name: call.name,
                    arguments: call.arguments,
                })
                .collect(),
            result: folded.result,
            overflow: folded.overflow,
            recovery: folded.recovery,
        },
        milestone,
    )
}

const fn stop_reason(stop: StopReason) -> ModelStopReason {
    match stop {
        StopReason::Complete => ModelStopReason::Complete,
        StopReason::ToolCall => ModelStopReason::ToolCall,
        StopReason::Length => ModelStopReason::Length,
        StopReason::ProviderStop => ModelStopReason::ProviderStop,
        StopReason::Error => ModelStopReason::Error,
    }
}

const fn overflow(kind: OverflowKind) -> TurnOverflow {
    match kind {
        OverflowKind::ProviderReported => TurnOverflow::ProviderReported,
        OverflowKind::Silent => TurnOverflow::Silent,
        OverflowKind::Truncation => TurnOverflow::Truncation,
    }
}

/// One call, with its arguments typed against the row it names.
///
/// The type of each value comes from the compiled-in parameter table, never
/// from the JSON shape alone: `Handle` and `Count` are both integers on the
/// wire, and `Address`, `Choice` and `Text` are all strings, so a reader that
/// guessed from the shape would hand `tool::validate` a `Count` where the row
/// declares a handle and the call would be refused as a type mismatch it never
/// made. Guessing is what this avoids; repairing is still refused — a value
/// whose JSON shape cannot satisfy the declared type yields no argument at
/// all, and the row's own validator then says which parameter is missing.
///
/// A name that resolves to no row is typed by shape, because there is no row
/// to read. Such a call is `NotAttempted::ToolNotAvailable` one step later
/// whatever its arguments look like.
fn tool_call(call: &ToolCallView<'_>, milestone: Milestone) -> ModelToolCall {
    let parsed;
    let arguments = match call.arguments {
        Arguments::Value(value) => Some(value),
        Arguments::Text(raw) => {
            parsed = json::parse_provider(raw).ok();
            parsed.as_ref()
        }
    };
    let definition = task_engine::resolve(call.tool, milestone)
        .entry()
        .map(ToolEntry::definition);
    let supplied = arguments
        .and_then(JsonValue::as_object)
        .map(|object| {
            object
                .iter()
                // One past the ceiling, so a call carrying too many arguments
                // still reaches `validate` as `TooManyArguments` rather than
                // being trimmed here into one that passes.
                .take(MAX_SUPPLIED_ARGUMENTS.saturating_add(1))
                .filter_map(|(name, value)| {
                    let declared = definition
                        .and_then(|definition| definition.parameter(name))
                        .map(|parameter| parameter.value_type);
                    argument_value(value, declared)
                        .map(|typed| SuppliedArgument::new(name.clone(), typed))
                })
                .collect::<Vec<_>>()
        })
        .unwrap_or_default();
    ModelToolCall::new(call.tool.to_owned(), supplied)
}

fn argument_value(value: &JsonValue, declared: Option<ParameterType>) -> Option<ArgumentValue> {
    match value {
        JsonValue::Bool(flag) => matches!(declared, None | Some(ParameterType::Flag))
            .then_some(ArgumentValue::Flag(*flag)),
        JsonValue::Integer(number) => integer_value(*number, declared),
        JsonValue::Text(text) => Some(text_value(text, declared)),
        JsonValue::Null | JsonValue::Array(_) | JsonValue::Object(_) | JsonValue::Decimal(_) => {
            None
        }
    }
}

fn integer_value(number: i64, declared: Option<ParameterType>) -> Option<ArgumentValue> {
    let count = u64::try_from(number).ok()?;
    match declared {
        Some(ParameterType::Handle) => u32::try_from(count).ok().map(ArgumentValue::Handle),
        // A position in what the person supplied. It parses only where the
        // schema declares one, so an integer sent for any other parameter can
        // never become a claim on a person's value.
        Some(ParameterType::SuppliedValue) => {
            u32::try_from(count).ok().map(ArgumentValue::SuppliedValue)
        }
        None | Some(ParameterType::Count) => Some(ArgumentValue::Count(count)),
        Some(
            ParameterType::Text
            | ParameterType::Address
            | ParameterType::Flag
            | ParameterType::Choice(_),
        ) => None,
    }
}

fn text_value(text: &str, declared: Option<ParameterType>) -> ArgumentValue {
    match declared {
        Some(ParameterType::Address) => ArgumentValue::Address(text.to_owned()),
        Some(ParameterType::Choice(_)) => ArgumentValue::Choice(text.to_owned()),
        // Text supplied where a supplied value was declared stays text, and is
        // refused a moment later by the type check in `tool::arguments`. It
        // must not be read as an index: a model that could turn its own string
        // into a reference to a person's value would be composing one.
        None
        | Some(
            ParameterType::Text
            | ParameterType::Handle
            | ParameterType::Count
            | ParameterType::Flag
            | ParameterType::SuppliedValue,
        ) => ArgumentValue::Text(text.to_owned()),
    }
}
