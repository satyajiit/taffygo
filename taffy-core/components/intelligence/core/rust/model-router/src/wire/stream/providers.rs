// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Provider-vocabulary adapters from decoded frames to route-blind events.

use crate::catalog::WireApi;
use crate::cost::TokenUsage;
use crate::json::JsonValue;
use crate::request::{ErrorClass, StopReason};
use crate::wire::dialect::Dialect;
use crate::wire::dialect_for;

use super::{StreamDefect, StreamEvent, StreamFold};

#[path = "providers/anthropic.rs"]
mod anthropic;

pub(super) fn apply_anthropic(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    anthropic::apply_anthropic(document, fold, emit_text)
}

pub(super) fn apply_openai_completions(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    if let Some(error) = document.field("error") {
        let detail = error
            .field("message")
            .and_then(JsonValue::as_str)
            .unwrap_or("error");
        let token = error
            .field("code")
            .and_then(JsonValue::as_str)
            .unwrap_or("");
        return apply_event(
            fold,
            emit_text,
            StreamEvent::Failed {
                class: map_error(dialect_for(WireApi::OpenAiCompletions), token),
                detail,
            },
        );
    }
    if let Some(usage) = document.field("usage") {
        let prompt = required_int(usage, "prompt_tokens")?;
        let cached = match usage.field("prompt_tokens_details") {
            Some(details) => optional_int(details, "cached_tokens")?,
            None => 0,
        };
        let input = prompt
            .checked_sub(cached)
            .ok_or(StreamDefect::MalformedFrame)?;
        apply_event(
            fold,
            emit_text,
            StreamEvent::Usage(TokenUsage {
                input,
                output: required_int(usage, "completion_tokens")?,
                cache_read: cached,
                cache_write: 0,
            }),
        )?;
    }
    let choice = document
        .field("choices")
        .and_then(JsonValue::as_array)
        .and_then(|choices| choices.first());
    if let Some(choice) = choice {
        if let Some(delta) = choice.field("delta") {
            if let Some(text) = delta.field("content").and_then(JsonValue::as_str) {
                apply_event(fold, emit_text, StreamEvent::TextDelta { text })?;
            }
            if let Some(calls) = delta.field("tool_calls").and_then(JsonValue::as_array) {
                for call in calls {
                    let index = optional_index(call, "index")?;
                    let function = call.field("function").unwrap_or(call);
                    if let Some(name) = function.field("name").and_then(JsonValue::as_str) {
                        let call_id = call.field("id").and_then(JsonValue::as_str);
                        apply_event(
                            fold,
                            emit_text,
                            StreamEvent::ToolCallStart {
                                index,
                                call_id,
                                name,
                            },
                        )?;
                    }
                    if let Some(args) = function.field("arguments").and_then(JsonValue::as_str) {
                        apply_event(
                            fold,
                            emit_text,
                            StreamEvent::ToolCallDelta {
                                index,
                                arguments: args,
                            },
                        )?;
                    }
                }
            }
        }
        if let Some(stop) = choice.field("finish_reason").and_then(JsonValue::as_str) {
            let mapped = map_stop(dialect_for(WireApi::OpenAiCompletions), stop);
            apply_event(
                fold,
                emit_text,
                StreamEvent::Done {
                    stop: mapped,
                    provider_stop: Some(stop),
                },
            )?;
        }
    }
    Ok(())
}

pub(super) fn apply_openai_responses(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    let kind = document
        .field("type")
        .and_then(JsonValue::as_str)
        .unwrap_or("");
    match kind {
        "response.output_text.delta" | "response.text.delta" => {
            if let Some(text) = document.field("delta").and_then(JsonValue::as_str) {
                apply_event(fold, emit_text, StreamEvent::TextDelta { text })?;
            }
            Ok(())
        }
        "response.output_item.added" => {
            let item = document.field("item").unwrap_or(document);
            if item.field("type").and_then(JsonValue::as_str) == Some("function_call") {
                let name = item.field("name").and_then(JsonValue::as_str).unwrap_or("");
                let call_id = item.field("call_id").and_then(JsonValue::as_str);
                apply_event(
                    fold,
                    emit_text,
                    StreamEvent::ToolCallStart {
                        index: optional_index(document, "output_index")?,
                        call_id,
                        name,
                    },
                )?;
            }
            Ok(())
        }
        "response.function_call_arguments.delta" => {
            if let Some(arguments) = document.field("delta").and_then(JsonValue::as_str) {
                apply_event(
                    fold,
                    emit_text,
                    StreamEvent::ToolCallDelta {
                        index: optional_index(document, "output_index")?,
                        arguments,
                    },
                )?;
            }
            Ok(())
        }
        "response.completed" | "response.incomplete" => {
            apply_openai_response_completed(document, fold, emit_text)
        }
        "error" | "response.failed" => {
            let detail = document
                .field("error")
                .and_then(|error| error.field("message"))
                .and_then(JsonValue::as_str)
                .unwrap_or("error");
            apply_event(
                fold,
                emit_text,
                StreamEvent::Failed {
                    class: ErrorClass::Unknown,
                    detail,
                },
            )
        }
        _ => apply_openai_completions(document, fold, emit_text),
    }
}

fn apply_openai_response_completed(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    if let Some(usage) = document
        .field("response")
        .and_then(|response| response.field("usage"))
    {
        let input_total = required_int(usage, "input_tokens")?;
        let cached = match usage.field("input_tokens_details") {
            Some(details) => optional_int(details, "cached_tokens")?,
            None => 0,
        };
        apply_event(
            fold,
            emit_text,
            StreamEvent::Usage(TokenUsage {
                input: input_total
                    .checked_sub(cached)
                    .ok_or(StreamDefect::MalformedFrame)?,
                output: required_int(usage, "output_tokens")?,
                cache_read: cached,
                cache_write: 0,
            }),
        )?;
    }
    let response = document.field("response").unwrap_or(document);
    let stop = if response
        .field("incomplete_details")
        .and_then(|details| details.field("reason"))
        .and_then(JsonValue::as_str)
        == Some("max_output_tokens")
    {
        StopReason::Length
    } else {
        StopReason::Complete
    };
    apply_event(
        fold,
        emit_text,
        StreamEvent::Done {
            stop,
            provider_stop: Some("completed"),
        },
    )
}

pub(super) fn apply_google(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    apply_google_shaped(WireApi::GoogleGenerativeLanguage, document, fold, emit_text)
}

/// The Cloud Code Assist family: a failure at the top, the frame underneath.
///
/// The envelope is required rather than unwrapped when present. A frame
/// without it is not this family's frame, and reading one anyway would find no
/// candidates and no usage — which folds into a reply that finished with
/// nothing to say, the one failure a caller cannot tell from a quiet model.
pub(super) fn apply_cloud_code_assist(
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    let dialect = dialect_for(WireApi::GoogleCloudCodeAssist);
    if let Some(error) = document.field("error") {
        let detail = error
            .field("message")
            .and_then(JsonValue::as_str)
            .unwrap_or("error");
        let token = error
            .field("status")
            .and_then(JsonValue::as_str)
            .unwrap_or("");
        return apply_event(
            fold,
            emit_text,
            StreamEvent::Failed {
                class: map_error(dialect, token),
                detail,
            },
        );
    }
    let Some(inner) = document.field("response") else {
        return Err(StreamDefect::MalformedFrame);
    };
    apply_google_shaped(WireApi::GoogleCloudCodeAssist, inner, fold, emit_text)
}

/// The generative-language frame shape, for whichever row is reading it.
///
/// The family is a parameter rather than a constant because the stop word is
/// mapped through the reading row's own vocabulary, and a row that later
/// diverges on one word would otherwise be read through the other's.
fn apply_google_shaped(
    api: WireApi,
    document: &JsonValue,
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
) -> Result<(), StreamDefect> {
    if let Some(usage) = document.field("usageMetadata") {
        let prompt = required_int(usage, "promptTokenCount")?;
        let cached = optional_int(usage, "cachedContentTokenCount")?;
        let output = required_int(usage, "candidatesTokenCount")?
            .checked_add(optional_int(usage, "thoughtsTokenCount")?)
            .ok_or(StreamDefect::MalformedFrame)?;
        apply_event(
            fold,
            emit_text,
            StreamEvent::Usage(TokenUsage {
                input: prompt
                    .checked_sub(cached)
                    .ok_or(StreamDefect::MalformedFrame)?,
                output,
                cache_read: cached,
                cache_write: 0,
            }),
        )?;
    }
    let candidate = document
        .field("candidates")
        .and_then(JsonValue::as_array)
        .and_then(|candidates| candidates.first());
    if let Some(candidate) = candidate {
        if let Some(parts) = candidate
            .field("content")
            .and_then(|content| content.field("parts"))
            .and_then(JsonValue::as_array)
        {
            for part in parts {
                if part.field("thought").and_then(JsonValue::as_bool) == Some(true) {
                    continue;
                }
                if let Some(text) = part.field("text").and_then(JsonValue::as_str) {
                    apply_event(fold, emit_text, StreamEvent::TextDelta { text })?;
                }
                if let Some(call) = part.field("functionCall") {
                    let name = call.field("name").and_then(JsonValue::as_str).unwrap_or("");
                    apply_event(
                        fold,
                        emit_text,
                        StreamEvent::ToolCallStart {
                            index: None,
                            call_id: None,
                            name,
                        },
                    )?;
                    if let Some(arguments) = call.field("args") {
                        apply_event(
                            fold,
                            emit_text,
                            StreamEvent::ToolCallValue {
                                index: None,
                                arguments,
                            },
                        )?;
                    }
                }
            }
        }
        if let Some(stop) = candidate.field("finishReason").and_then(JsonValue::as_str) {
            let mapped = map_stop(dialect_for(api), stop);
            apply_event(
                fold,
                emit_text,
                StreamEvent::Done {
                    stop: mapped,
                    provider_stop: Some(stop),
                },
            )?;
        }
    }
    Ok(())
}

fn apply_event<'a>(
    fold: &mut StreamFold,
    emit_text: &mut dyn FnMut(&str),
    event: StreamEvent<'a>,
) -> Result<(), StreamDefect> {
    fold.apply(event)?;
    if let StreamEvent::TextDelta { text } = event {
        emit_text(text);
    }
    Ok(())
}

fn optional_index(value: &JsonValue, name: &str) -> Result<Option<u32>, StreamDefect> {
    let Some(index) = value.field(name) else {
        return Ok(None);
    };
    let number = index.as_i64().ok_or(StreamDefect::MalformedFrame)?;
    Ok(Some(
        u32::try_from(number).map_err(|_| StreamDefect::MalformedFrame)?,
    ))
}

fn required_int(value: &JsonValue, name: &str) -> Result<u64, StreamDefect> {
    let number = value
        .field(name)
        .and_then(JsonValue::as_i64)
        .ok_or(StreamDefect::MalformedFrame)?;
    u64::try_from(number).map_err(|_| StreamDefect::MalformedFrame)
}

fn optional_int(value: &JsonValue, name: &str) -> Result<u64, StreamDefect> {
    match value.field(name) {
        Some(_) => required_int(value, name),
        None => Ok(0),
    }
}

fn map_stop(dialect: &Dialect, token: &str) -> StopReason {
    dialect
        .stop
        .vocabulary
        .iter()
        .find(|(word, _)| *word == token)
        .map_or(StopReason::ProviderStop, |(_, stop)| *stop)
}

fn map_error(dialect: &Dialect, token: &str) -> ErrorClass {
    dialect
        .error
        .vocabulary
        .iter()
        .find(|(word, _)| *word == token)
        .map_or(ErrorClass::Unknown, |(_, class)| *class)
}
