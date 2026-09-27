// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Reading a provider stream into the same taxonomy a complete reply uses.
//!
//! Decision 0071: frames are live; the first terminal event wins; the journal
//! still records only the ending. This module never holds answer text. Tool
//! names and argument JSON are model-authored operands, the same material
//! [`super::reply`] already borrows from a complete document.
//!
//! Three families put `stream: true` in the body. The fourth streams by URL
//! path, which the browser owns. Frames arrive here as SSE `data:` payloads
//! or as NDJSON objects. A body that is one JSON object is not a stream —
//! [`super::read_reply`] reads those.

use crate::catalog::WireApi;
use crate::cost::TokenUsage;
use crate::json::{self, JsonValue};
use crate::request::{
    detect_overflow, recoverable_signal, AlreadyTerminated, BoundedText, ErrorClass, OverflowKind,
    ProviderError, RecoverableSignal, StopReason, TerminalResult, TerminalSlot,
};

use super::reply::ReplyContext;
use super::ReplyDefect;

pub(super) mod incremental;
mod providers;

pub use self::incremental::{
    ModelStreamDecoder, ModelStreamDefect, ModelStreamReading, ModelStreamToolCall,
    MAX_DIRECT_STREAM_BYTES,
};

/// Largest number of tool calls one stream may name, matching the complete
/// reader.
const MAX_TOOL_CALLS: usize = super::reply::MAX_TOOL_CALLS;

/// One event borrowed from a single frame the caller still owns.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StreamEvent<'a> {
    /// The family announced an answer is starting.
    Start,
    /// More of the answer.
    TextDelta {
        /// The fragment.
        text: &'a str,
    },
    /// A tool call began. Arguments may still be arriving.
    ToolCallStart {
        /// Provider stream slot, when the family carries one.
        index: Option<u32>,
        /// The provider's identity, when the family mints one.
        call_id: Option<&'a str>,
        /// The tool named.
        name: &'a str,
    },
    /// More of the in-flight tool call's arguments, as a JSON fragment.
    ToolCallDelta {
        /// Slot of the call these arguments extend. `None` means the latest.
        index: Option<u32>,
        /// The fragment. Concatenated, then parsed, never spliced raw.
        arguments: &'a str,
    },
    /// A family delivered one complete decoded argument object.
    ToolCallValue {
        /// Slot of the call these arguments belong to. `None` means the latest.
        index: Option<u32>,
        /// Decoded arguments, cloned only after the frame passed the JSON bound.
        arguments: &'a JsonValue,
    },
    /// Usage so far. Provider counters are merged component by component.
    Usage(TokenUsage),
    /// The stream ended successfully. First terminal wins.
    Done {
        /// Why it ended.
        stop: StopReason,
        /// The provider's own stop word.
        provider_stop: Option<&'a str>,
    },
    /// The stream failed. First terminal wins.
    Failed {
        /// The class every decision is made from.
        class: ErrorClass,
        /// The provider's own words, bounded later.
        detail: &'a str,
    },
}

/// Why a stream could not be read.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum StreamDefect {
    /// A frame was not JSON.
    MalformedFrame,
    /// A tool call announced itself without a name.
    MalformedToolCall,
    /// More tool calls than the complete reader would accept.
    TooManyToolCalls,
    /// Reassembled tool arguments exceeded one bounded JSON document.
    ToolArgumentsTooLarge,
    /// The stream ended twice.
    AlreadyTerminated,
    /// The stream ended without a stop reason and without tool calls.
    MissingStopReason,
    /// The stream ended without usage.
    MissingUsage,
}

impl StreamDefect {
    /// A short, compiled-in name for a diagnostic line. It names the defect
    /// and never carries a byte of the stream.
    pub const fn label(&self) -> &'static str {
        match self {
            Self::MalformedFrame => "malformed_frame",
            Self::MalformedToolCall => "malformed_tool_call",
            Self::TooManyToolCalls => "too_many_tool_calls",
            Self::ToolArgumentsTooLarge => "tool_arguments_too_large",
            Self::AlreadyTerminated => "already_terminated",
            Self::MissingStopReason => "missing_stop_reason",
            Self::MissingUsage => "missing_usage",
        }
    }
}

impl From<AlreadyTerminated> for StreamDefect {
    fn from(_: AlreadyTerminated) -> Self {
        Self::AlreadyTerminated
    }
}

impl From<ReplyDefect> for StreamDefect {
    fn from(defect: ReplyDefect) -> Self {
        match defect {
            ReplyDefect::MissingUsage => Self::MissingUsage,
            ReplyDefect::MalformedToolCall => Self::MalformedToolCall,
            ReplyDefect::TooManyToolCalls => Self::TooManyToolCalls,
            ReplyDefect::MissingStopReason | ReplyDefect::UnknownStopToken(_) => {
                Self::MissingStopReason
            }
        }
    }
}

/// Whether `body` is framed as a stream rather than one JSON document.
pub fn looks_like_stream(body: &str) -> bool {
    let trimmed = body.trim_start();
    if trimmed.starts_with("data:") || trimmed.starts_with("event:") || trimmed.starts_with("id:") {
        return true;
    }
    // Google may return newline-delimited JSON rather than SSE. One JSON
    // object remains a complete response; two framed objects are a stream.
    let mut json_lines = trimmed.lines().filter(|line| !line.trim().is_empty());
    matches!(
        (json_lines.next(), json_lines.next()),
        (Some(first), Some(second)) if first.trim_start().starts_with('{')
            && second.trim_start().starts_with('{')
    )
}

/// One completed tool call reconstructed from the stream.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FoldedToolCall {
    /// The tool named.
    pub name: String,
    /// The provider's identity, when one was given.
    pub call_id: Option<String>,
    /// The arguments, parsed. Empty object when none arrived.
    pub arguments: JsonValue,
}

/// The stream after its first terminal event.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FoldedReply {
    /// How many text fragments arrived. The words stay with the caller.
    pub text_segments: u32,
    /// The tool calls, in the order they started.
    pub tool_calls: Vec<FoldedToolCall>,
    /// The single ending.
    pub result: TerminalResult,
    /// How an overflow was noticed, when one was.
    pub overflow: Option<OverflowKind>,
    /// What the caller may do about it.
    pub recovery: Option<RecoverableSignal>,
}

/// Accumulates frames until the first terminal event.
#[derive(Debug, Default)]
pub struct StreamFold {
    text_segments: u32,
    tools: Vec<OpenTool>,
    tool_argument_bytes: usize,
    usage: Option<TokenUsage>,
    slot: TerminalSlot,
    failure_terminal: bool,
}

#[derive(Debug)]
struct OpenTool {
    index: Option<u32>,
    name: String,
    call_id: Option<String>,
    arguments: OpenArguments,
}

#[derive(Debug)]
enum OpenArguments {
    Fragments(String),
    Value(JsonValue),
}

impl StreamFold {
    /// An empty fold.
    pub fn new() -> Self {
        Self::default()
    }

    /// Whether the first terminal event has already arrived.
    pub fn is_terminated(&self) -> bool {
        self.slot.is_terminated()
    }

    fn can_finish(&self) -> bool {
        self.is_terminated() && (self.usage.is_some() || self.failure_terminal)
    }

    fn text_segments(&self) -> u32 {
        self.text_segments
    }

    /// Applies one event. A second terminal is refused.
    pub fn apply(&mut self, event: StreamEvent<'_>) -> Result<(), StreamDefect> {
        if self.is_terminated() && !matches!(event, StreamEvent::Usage(_)) {
            return Err(StreamDefect::AlreadyTerminated);
        }
        match event {
            StreamEvent::Start => Ok(()),
            StreamEvent::TextDelta { .. } => {
                self.text_segments = self.text_segments.saturating_add(1);
                Ok(())
            }
            StreamEvent::ToolCallStart {
                index,
                call_id,
                name,
            } => {
                if name.is_empty() {
                    return Err(StreamDefect::MalformedToolCall);
                }
                if self.tools.len() >= MAX_TOOL_CALLS {
                    return Err(StreamDefect::TooManyToolCalls);
                }
                if index.is_some() && self.tools.iter().any(|tool| tool.index == index) {
                    return Err(StreamDefect::MalformedToolCall);
                }
                self.tools.push(OpenTool {
                    index,
                    name: name.to_owned(),
                    call_id: call_id.map(str::to_owned),
                    arguments: OpenArguments::Fragments(String::new()),
                });
                Ok(())
            }
            StreamEvent::ToolCallDelta { index, arguments } => {
                let next_bytes = self
                    .tool_argument_bytes
                    .checked_add(arguments.len())
                    .ok_or(StreamDefect::ToolArgumentsTooLarge)?;
                if next_bytes > json::MAX_INPUT_BYTES {
                    return Err(StreamDefect::ToolArgumentsTooLarge);
                }
                let Some(tool) = self.open_tool_mut(index) else {
                    return Err(StreamDefect::MalformedToolCall);
                };
                let OpenArguments::Fragments(open) = &mut tool.arguments else {
                    return Err(StreamDefect::MalformedToolCall);
                };
                open.push_str(arguments);
                self.tool_argument_bytes = next_bytes;
                Ok(())
            }
            StreamEvent::ToolCallValue { index, arguments } => {
                let bytes = value_weight(arguments).ok_or(StreamDefect::ToolArgumentsTooLarge)?;
                let next_bytes = self
                    .tool_argument_bytes
                    .checked_add(bytes)
                    .ok_or(StreamDefect::ToolArgumentsTooLarge)?;
                if next_bytes > json::MAX_INPUT_BYTES {
                    return Err(StreamDefect::ToolArgumentsTooLarge);
                }
                let Some(tool) = self.open_tool_mut(index) else {
                    return Err(StreamDefect::MalformedToolCall);
                };
                match &tool.arguments {
                    OpenArguments::Fragments(open) if open.is_empty() => {}
                    OpenArguments::Fragments(_) | OpenArguments::Value(_) => {
                        return Err(StreamDefect::MalformedToolCall)
                    }
                }
                tool.arguments = OpenArguments::Value(arguments.clone());
                self.tool_argument_bytes = next_bytes;
                Ok(())
            }
            StreamEvent::Usage(usage) => {
                let previous = self.usage.unwrap_or_default();
                self.usage = Some(TokenUsage {
                    input: previous.input.max(usage.input),
                    output: previous.output.max(usage.output),
                    cache_read: previous.cache_read.max(usage.cache_read),
                    cache_write: previous.cache_write.max(usage.cache_write),
                });
                Ok(())
            }
            StreamEvent::Done {
                stop,
                provider_stop,
            } => {
                let stop = if self.tools.is_empty() {
                    stop
                } else {
                    StopReason::ToolCall
                };
                self.finish_slot(stop, provider_stop, None)
            }
            StreamEvent::Failed { class, detail } => self.fail(class, detail),
        }
    }

    fn fail(&mut self, class: ErrorClass, detail: &str) -> Result<(), StreamDefect> {
        self.finish_slot(
            StopReason::Error,
            None,
            Some(ProviderError {
                class,
                retry_after_millis: None,
                detail: BoundedText::new(detail),
            }),
        )?;
        self.failure_terminal = true;
        Ok(())
    }

    fn open_tool_mut(&mut self, index: Option<u32>) -> Option<&mut OpenTool> {
        match index {
            Some(index) => self.tools.iter_mut().find(|tool| tool.index == Some(index)),
            None => self.tools.last_mut(),
        }
    }

    fn finish_slot(
        &mut self,
        stop: StopReason,
        provider_stop: Option<&str>,
        error: Option<ProviderError>,
    ) -> Result<(), StreamDefect> {
        let usage = self.usage.unwrap_or_default();
        self.slot
            .complete(TerminalResult {
                request_id: crate::ids::RequestId::from_bytes([0; 16]),
                stop,
                usage,
                provider_stop_reason: provider_stop.map(BoundedText::new),
                error,
            })
            .map_err(StreamDefect::from)
    }

    /// The ending, with overflow rules applied the same way a complete reply
    /// is promoted.
    pub fn finish(self, context: &ReplyContext) -> Result<FoldedReply, StreamDefect> {
        let Some(mut result) = self.slot.take() else {
            return Err(StreamDefect::MissingStopReason);
        };
        if self.usage.is_none() && !self.failure_terminal {
            return Err(StreamDefect::MissingUsage);
        }
        result.usage = self.usage.unwrap_or_default();
        result.request_id = context.request_id;
        let reported_input = result.usage.total_input();
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
        let mut tool_calls = Vec::with_capacity(self.tools.len());
        for tool in self.tools {
            let arguments = match tool.arguments {
                OpenArguments::Fragments(arguments) if arguments.is_empty() => {
                    JsonValue::Object(std::collections::BTreeMap::new())
                }
                OpenArguments::Fragments(arguments) => {
                    let value = json::parse_provider(&arguments)
                        .map_err(|_| StreamDefect::MalformedToolCall)?;
                    if !matches!(value, JsonValue::Object(_)) {
                        return Err(StreamDefect::MalformedToolCall);
                    }
                    value
                }
                OpenArguments::Value(value) => {
                    if !matches!(value, JsonValue::Object(_)) {
                        return Err(StreamDefect::MalformedToolCall);
                    }
                    value
                }
            };
            tool_calls.push(FoldedToolCall {
                name: tool.name,
                call_id: tool.call_id,
                arguments,
            });
        }
        Ok(FoldedReply {
            text_segments: self.text_segments,
            tool_calls,
            result,
            overflow,
            recovery,
        })
    }
}

fn value_weight(value: &JsonValue) -> Option<usize> {
    match value {
        JsonValue::Null => Some(4),
        JsonValue::Bool(flag) => Some(if *flag { 4 } else { 5 }),
        JsonValue::Integer(number) => Some(number.to_string().len()),
        JsonValue::Decimal(spelling) | JsonValue::Text(spelling) => spelling.len().checked_add(2),
        JsonValue::Array(items) => items.iter().try_fold(2usize, |total, item| {
            total
                .checked_add(value_weight(item)?)
                .and_then(|value| value.checked_add(1))
        }),
        JsonValue::Object(fields) => fields.iter().try_fold(2usize, |total, (key, item)| {
            total
                .checked_add(key.len())
                .and_then(|value| value.checked_add(3))
                .and_then(|value| value.checked_add(value_weight(item)?))
                .and_then(|value| value.checked_add(1))
        }),
    }
}

fn overflow_detail(kind: OverflowKind) -> &'static str {
    match kind {
        OverflowKind::ProviderReported => "the provider reported an overflow",
        OverflowKind::Silent => "reported input exceeded the context window",
        OverflowKind::Truncation => "a length stop with no output at a full window",
    }
}

/// Folds every frame in `body` for `api`.
pub fn fold_stream(
    api: WireApi,
    body: &str,
    context: &ReplyContext,
) -> Result<FoldedReply, StreamDefect> {
    incremental::fold_direct(api, body, context)
}

/// Parses `frame` and applies it to `fold`.
pub fn apply_frame(api: WireApi, frame: &str, fold: &mut StreamFold) -> Result<(), StreamDefect> {
    apply_frame_emitting(api, frame, fold, &mut |_| {})
}

pub(super) fn apply_frame_emitting(
    api: WireApi,
    frame: &str,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    let document = json::parse_provider(frame).map_err(|_| StreamDefect::MalformedFrame)?;
    match api {
        WireApi::AnthropicMessages => providers::apply_anthropic(&document, fold, emit_text),
        WireApi::OpenAiCompletions => {
            providers::apply_openai_completions(&document, fold, emit_text)
        }
        // One decoder for both responses rows: the subscription endpoint
        // differs in what a *request* may say, never in the frames a reply
        // arrives as.
        WireApi::OpenAiResponses | WireApi::OpenAiCodexResponses => {
            providers::apply_openai_responses(&document, fold, emit_text)
        }
        WireApi::GoogleGenerativeLanguage => providers::apply_google(&document, fold, emit_text),
        // Not one decoder for both Google rows: this family's frames arrive
        // inside an envelope, and read without it a frame carries no
        // candidates, which folds as an answer with nothing in it rather than
        // as a defect.
        WireApi::GoogleCloudCodeAssist => {
            providers::apply_cloud_code_assist(&document, fold, emit_text)
        }
    }
}
