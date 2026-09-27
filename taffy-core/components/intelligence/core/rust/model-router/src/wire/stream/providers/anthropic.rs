// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Anthropic Messages stream vocabulary.

use crate::catalog::WireApi;
use crate::cost::TokenUsage;
use crate::json::JsonValue;
use crate::request::StopReason;
use crate::wire::dialect_for;

use super::{apply_event, map_error, map_stop, optional_index, optional_int, required_int};
use crate::wire::stream::{StreamDefect, StreamEvent, StreamFold};

pub(super) fn apply_anthropic(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    let kind = document
        .field("type")
        .and_then(JsonValue::as_str)
        .unwrap_or("");
    match kind {
        "message_start" => message_start(document, fold, emit_text),
        "content_block_start" => content_block_start(document, fold, emit_text),
        "content_block_delta" => content_block_delta(document, fold, emit_text),
        "message_delta" => message_delta(document, fold, emit_text),
        "message_stop" => {
            if fold.is_terminated() {
                Ok(())
            } else {
                apply_event(
                    fold,
                    emit_text,
                    StreamEvent::Done {
                        stop: StopReason::Complete,
                        provider_stop: None,
                    },
                )
            }
        }
        "error" => error(document, fold, emit_text),
        _ => Ok(()),
    }
}

fn message_start(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    apply_event(fold, emit_text, StreamEvent::Start)?;
    let Some(usage) = document
        .field("message")
        .and_then(|message| message.field("usage"))
    else {
        return Ok(());
    };
    apply_event(
        fold,
        emit_text,
        StreamEvent::Usage(TokenUsage {
            input: required_int(usage, "input_tokens")?,
            output: optional_int(usage, "output_tokens")?,
            cache_read: optional_int(usage, "cache_read_input_tokens")?,
            cache_write: optional_int(usage, "cache_creation_input_tokens")?,
        }),
    )
}

fn content_block_start(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    let block = document.field("content_block").unwrap_or(document);
    if block.field("type").and_then(JsonValue::as_str) != Some("tool_use") {
        return Ok(());
    }
    let name = block
        .field("name")
        .and_then(JsonValue::as_str)
        .unwrap_or("");
    apply_event(
        fold,
        emit_text,
        StreamEvent::ToolCallStart {
            index: optional_index(document, "index")?,
            call_id: block.field("id").and_then(JsonValue::as_str),
            name,
        },
    )
}

fn content_block_delta(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    let delta = document.field("delta").unwrap_or(document);
    match delta.field("type").and_then(JsonValue::as_str) {
        Some("text_delta") => {
            if let Some(text) = delta.field("text").and_then(JsonValue::as_str) {
                return apply_event(fold, emit_text, StreamEvent::TextDelta { text });
            }
        }
        Some("input_json_delta") => {
            if let Some(arguments) = delta.field("partial_json").and_then(JsonValue::as_str) {
                return apply_event(
                    fold,
                    emit_text,
                    StreamEvent::ToolCallDelta {
                        index: optional_index(document, "index")?,
                        arguments,
                    },
                );
            }
        }
        None | Some(_) => {}
    }
    Ok(())
}

fn message_delta(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    if let Some(usage) = document.field("usage") {
        apply_event(
            fold,
            emit_text,
            StreamEvent::Usage(TokenUsage {
                input: optional_int(usage, "input_tokens")?,
                output: required_int(usage, "output_tokens")?,
                cache_read: optional_int(usage, "cache_read_input_tokens")?,
                cache_write: optional_int(usage, "cache_creation_input_tokens")?,
            }),
        )?;
    }
    let Some(stop) = document
        .field("delta")
        .and_then(|delta| delta.field("stop_reason"))
        .and_then(JsonValue::as_str)
    else {
        return Ok(());
    };
    apply_event(
        fold,
        emit_text,
        StreamEvent::Done {
            stop: map_stop(dialect_for(WireApi::AnthropicMessages), stop),
            provider_stop: Some(stop),
        },
    )
}

fn error(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    let error = document.field("error").unwrap_or(document);
    let detail = error
        .field("message")
        .and_then(JsonValue::as_str)
        .unwrap_or("error");
    let token = error
        .field("type")
        .and_then(JsonValue::as_str)
        .unwrap_or("");
    apply_event(
        fold,
        emit_text,
        StreamEvent::Failed {
            class: map_error(dialect_for(WireApi::AnthropicMessages), token),
            detail,
        },
    )
}
