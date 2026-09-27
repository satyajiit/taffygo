// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Folding the managed route's canonical NDJSON response.
//!
//! This is deliberately one deep module. Callers hand it the complete bounded
//! body and the identity/context of the request; it hides frame ordering,
//! schema validation, correlation, interleaved tool reconstruction, terminal
//! promotion, and every allocation bound behind that one interface.
//!
//! Answer text is counted and immediately dropped. Tool names and decoded
//! argument objects are the only model-authored operands retained in the
//! returned reading, for exactly as long as the caller retains that reading.

use std::collections::BTreeMap;

use crate::cost::TokenUsage;
use crate::json::{self, JsonValue};
use crate::request::{
    detect_overflow, BoundedText, ErrorClass, OverflowKind, ProviderError, StopReason,
    TerminalResult,
};

use super::reply::{ReplyContext, MAX_TOOL_CALLS};

#[path = "managed_stream/schema.rs"]
mod schema;

use self::schema::{
    exact_fields, nonempty_bounded, read_cost, read_error_class, read_header, read_route,
    read_stop, read_usage, unsigned,
};

/// Largest canonical managed response accepted by the sandbox reader.
///
/// This matches the Worker's response ceiling and the browser effect's
/// completion ceiling. The reader owns the same bound independently: an
/// adapter forgetting its limit must not turn the parser into an unbounded
/// allocation site.
pub const MAX_MANAGED_STREAM_BYTES: usize = 1024 * 1024;

/// Longest tool name the canonical Worker emits.
const MAX_MANAGED_TOOL_NAME_BYTES: usize = 64;

/// One completed canonical tool call.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ManagedToolCall {
    /// The compiled-in tool name the model selected.
    pub tool: String,
    /// Its reassembled and decoded argument object.
    pub arguments: JsonValue,
}

/// One canonical managed response, folded into the route-blind taxonomy.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ManagedReading {
    /// How many visible text fragments arrived. Their bytes are not retained.
    pub text_segments: u32,
    /// Tool calls ordered by their canonical index.
    pub tool_calls: Vec<ManagedToolCall>,
    /// The single terminal result, including usage and promoted overflow.
    pub result: TerminalResult,
    /// How an overflow was noticed, when one was.
    pub overflow: Option<OverflowKind>,
}

/// Why a canonical managed stream was not read.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ManagedReplyDefect {
    /// The complete body exceeded [`MAX_MANAGED_STREAM_BYTES`].
    StreamTooLarge,
    /// A line was absent, was not one complete JSON object, or did not have
    /// exactly the fields and field types its event promises.
    MalformedFrame,
    /// A line named a schema version this build does not read.
    UnsupportedSchemaVersion,
    /// A line names a different request from the one the device sent.
    RequestMismatch,
    /// A delta or terminal event arrived before the one required start event.
    MissingStart,
    /// More than one start event arrived.
    DuplicateStart,
    /// Any event followed the first terminal event.
    EventAfterTerminal,
    /// The body ended without `done` or `error`.
    MissingTerminal,
    /// More than [`MAX_TOOL_CALLS`] distinct call indices were announced.
    TooManyToolCalls,
    /// A call had an empty/oversized name, repeated its index, received a
    /// delta before its start, or did not finish as an argument object.
    MalformedToolCall,
    /// Reassembled argument fragments passed the stream's allocation bound.
    ToolArgumentsTooLarge,
}

/// Folds one complete schema-3 canonical NDJSON response.
///
/// Every non-empty line is one event and every event repeats
/// `schema_version` and the device-minted `request_id`. The start event must
/// be first, one terminal must be last, and tool deltas are paired by their
/// explicit index so interleaved calls cannot write into one another.
pub fn fold_managed_stream(
    body: &str,
    expected_request_id: &str,
    context: &ReplyContext,
) -> Result<ManagedReading, ManagedReplyDefect> {
    super::stream::incremental::fold_managed(body, expected_request_id, context)
}

#[derive(Debug, Default)]
pub(super) struct ManagedFold {
    started: bool,
    terminated: bool,
    text_segments: u32,
    tool_argument_bytes: usize,
    tools: BTreeMap<u64, OpenTool>,
    terminal: Option<ManagedTerminal>,
}

#[derive(Debug)]
struct OpenTool {
    tool: String,
    arguments: String,
}

#[derive(Debug)]
enum ManagedTerminal {
    Done { stop: StopReason, usage: TokenUsage },
    Error { class: ErrorClass, message: String },
}

impl ManagedFold {
    pub(super) fn apply_emitting(
        &mut self,
        document: &JsonValue,
        expected_request_id: &str,
        emit_text: &mut dyn FnMut(&str),
    ) -> Result<(), ManagedReplyDefect> {
        let (object, event_type) = read_header(document, expected_request_id)?;

        if self.terminated {
            return Err(ManagedReplyDefect::EventAfterTerminal);
        }
        match event_type {
            "start" => self.start(object),
            "text_delta" => {
                let text = self.text_delta(object)?;
                emit_text(text);
                Ok(())
            }
            "tool_call_start" => self.tool_start(object),
            "tool_call_delta" => self.tool_delta(object),
            "done" => self.done(object),
            "error" => self.error(object),
            _ => Err(ManagedReplyDefect::MalformedFrame),
        }
    }

    fn start(&mut self, object: &BTreeMap<String, JsonValue>) -> Result<(), ManagedReplyDefect> {
        if self.started {
            return Err(ManagedReplyDefect::DuplicateStart);
        }
        if !exact_fields(object, &["schema_version", "type", "request_id", "route"]) {
            return Err(ManagedReplyDefect::MalformedFrame);
        }
        read_route(object.get("route"))?;
        self.started = true;
        Ok(())
    }

    fn text_delta<'a>(
        &mut self,
        object: &'a BTreeMap<String, JsonValue>,
    ) -> Result<&'a str, ManagedReplyDefect> {
        self.require_start()?;
        if !exact_fields(object, &["schema_version", "type", "request_id", "text"]) {
            return Err(ManagedReplyDefect::MalformedFrame);
        }
        let text = object
            .get("text")
            .and_then(JsonValue::as_str)
            .ok_or(ManagedReplyDefect::MalformedFrame)?;
        self.text_segments = self.text_segments.saturating_add(1);
        Ok(text)
    }

    fn tool_start(
        &mut self,
        object: &BTreeMap<String, JsonValue>,
    ) -> Result<(), ManagedReplyDefect> {
        self.require_start()?;
        if !exact_fields(
            object,
            &["schema_version", "type", "request_id", "index", "tool"],
        ) {
            return Err(ManagedReplyDefect::MalformedFrame);
        }
        let index = unsigned(object.get("index"))?;
        let tool = object
            .get("tool")
            .and_then(JsonValue::as_str)
            .ok_or(ManagedReplyDefect::MalformedFrame)?;
        if tool.is_empty() || tool.len() > MAX_MANAGED_TOOL_NAME_BYTES {
            return Err(ManagedReplyDefect::MalformedToolCall);
        }
        if self.tools.len() >= MAX_TOOL_CALLS {
            return Err(ManagedReplyDefect::TooManyToolCalls);
        }
        if self
            .tools
            .insert(
                index,
                OpenTool {
                    tool: tool.to_owned(),
                    arguments: String::new(),
                },
            )
            .is_some()
        {
            return Err(ManagedReplyDefect::MalformedToolCall);
        }
        Ok(())
    }

    fn tool_delta(
        &mut self,
        object: &BTreeMap<String, JsonValue>,
    ) -> Result<(), ManagedReplyDefect> {
        self.require_start()?;
        if !exact_fields(
            object,
            &["schema_version", "type", "request_id", "index", "arguments"],
        ) {
            return Err(ManagedReplyDefect::MalformedFrame);
        }
        let index = unsigned(object.get("index"))?;
        let arguments = object
            .get("arguments")
            .and_then(JsonValue::as_str)
            .ok_or(ManagedReplyDefect::MalformedFrame)?;
        let next = self
            .tool_argument_bytes
            .checked_add(arguments.len())
            .ok_or(ManagedReplyDefect::ToolArgumentsTooLarge)?;
        if next > MAX_MANAGED_STREAM_BYTES {
            return Err(ManagedReplyDefect::ToolArgumentsTooLarge);
        }
        let tool = self
            .tools
            .get_mut(&index)
            .ok_or(ManagedReplyDefect::MalformedToolCall)?;
        tool.arguments.push_str(arguments);
        self.tool_argument_bytes = next;
        Ok(())
    }

    fn done(&mut self, object: &BTreeMap<String, JsonValue>) -> Result<(), ManagedReplyDefect> {
        self.require_start()?;
        if !exact_fields(
            object,
            &[
                "schema_version",
                "type",
                "request_id",
                "stop_reason",
                "usage",
                "cost",
            ],
        ) {
            return Err(ManagedReplyDefect::MalformedFrame);
        }
        let stop = read_stop(object.get("stop_reason"))?;
        let usage = read_usage(object.get("usage"))?;
        read_cost(object.get("cost"))?;
        self.terminal = Some(ManagedTerminal::Done { stop, usage });
        self.terminated = true;
        Ok(())
    }

    fn error(&mut self, object: &BTreeMap<String, JsonValue>) -> Result<(), ManagedReplyDefect> {
        self.require_start()?;
        if !exact_fields(object, &["schema_version", "type", "request_id", "error"]) {
            return Err(ManagedReplyDefect::MalformedFrame);
        }
        let error = object
            .get("error")
            .and_then(JsonValue::as_object)
            .ok_or(ManagedReplyDefect::MalformedFrame)?;
        if !exact_fields(error, &["class", "code", "message", "retryable"]) {
            return Err(ManagedReplyDefect::MalformedFrame);
        }
        let class = read_error_class(error.get("class"))?;
        let code = nonempty_bounded(error.get("code"))?;
        let message = nonempty_bounded(error.get("message"))?;
        if error
            .get("retryable")
            .and_then(JsonValue::as_bool)
            .is_none()
            || code.len() > 128
        {
            return Err(ManagedReplyDefect::MalformedFrame);
        }
        self.terminal = Some(ManagedTerminal::Error {
            class,
            message: message.to_owned(),
        });
        self.terminated = true;
        Ok(())
    }

    fn require_start(&self) -> Result<(), ManagedReplyDefect> {
        if self.started {
            Ok(())
        } else {
            Err(ManagedReplyDefect::MissingStart)
        }
    }

    pub(super) fn is_terminated(&self) -> bool {
        self.terminated
    }

    pub(super) fn text_segments(&self) -> u32 {
        self.text_segments
    }

    pub(super) fn finish(
        self,
        context: &ReplyContext,
    ) -> Result<ManagedReading, ManagedReplyDefect> {
        if !self.started {
            return Err(ManagedReplyDefect::MissingStart);
        }
        let terminal = self.terminal.ok_or(ManagedReplyDefect::MissingTerminal)?;

        let (mut result, tool_calls) = match terminal {
            ManagedTerminal::Done { stop, usage } => {
                let tool_calls = finish_tools(self.tools)?;
                let stop = if tool_calls.is_empty() {
                    stop
                } else {
                    StopReason::ToolCall
                };
                (
                    TerminalResult {
                        request_id: context.request_id,
                        stop,
                        usage,
                        provider_stop_reason: None,
                        error: None,
                    },
                    tool_calls,
                )
            }
            ManagedTerminal::Error { class, message } => (
                TerminalResult {
                    request_id: context.request_id,
                    stop: StopReason::Error,
                    usage: TokenUsage::default(),
                    provider_stop_reason: None,
                    error: Some(ProviderError {
                        class,
                        retry_after_millis: None,
                        detail: BoundedText::new(&message),
                    }),
                },
                // A canonical error ends an attempt, not a partial tool plan.
                // Fragments already received are residency and are discarded.
                Vec::new(),
            ),
        };

        let reported_input = result.usage.total_input();
        let overflow = detect_overflow(&result, context.context_window, reported_input);
        if let Some(kind) = overflow {
            result.stop = StopReason::Error;
            if result.error.is_none() {
                result.error = Some(ProviderError {
                    class: ErrorClass::Overflow,
                    retry_after_millis: None,
                    detail: BoundedText::new(overflow_detail(kind)),
                });
            }
        }
        Ok(ManagedReading {
            text_segments: self.text_segments,
            tool_calls,
            result,
            overflow,
        })
    }
}

fn finish_tools(
    tools: BTreeMap<u64, OpenTool>,
) -> Result<Vec<ManagedToolCall>, ManagedReplyDefect> {
    let mut completed = Vec::with_capacity(tools.len());
    for (_, tool) in tools {
        let arguments = if tool.arguments.trim().is_empty() {
            JsonValue::Object(BTreeMap::new())
        } else {
            let value = json::parse_provider(&tool.arguments)
                .map_err(|_| ManagedReplyDefect::MalformedToolCall)?;
            if value.as_object().is_none() {
                return Err(ManagedReplyDefect::MalformedToolCall);
            }
            value
        };
        completed.push(ManagedToolCall {
            tool: tool.tool,
            arguments,
        });
    }
    Ok(completed)
}

const fn overflow_detail(kind: OverflowKind) -> &'static str {
    match kind {
        OverflowKind::ProviderReported => "the provider reported an overflow",
        OverflowKind::Silent => "reported input exceeded the context window",
        OverflowKind::Truncation => "a length stop with no output at a full window",
    }
}
