// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Sanitizing provider folds into the one public terminal reading.

use crate::cost::TokenUsage;
use crate::request::{BoundedText, ErrorClass, ProviderError, StopReason, TerminalResult};
use crate::wire::reply::ReplyContext;

use super::{ModelStreamReading, ModelStreamToolCall, RawReading};

pub(super) fn sanitize(reading: RawReading) -> ModelStreamReading {
    match reading {
        RawReading::Direct(reading) => {
            let mut result = reading.result;
            result.provider_stop_reason = None;
            let tool_calls = if result.stop == StopReason::Error {
                Vec::new()
            } else {
                reading
                    .tool_calls
                    .into_iter()
                    .map(|call| ModelStreamToolCall {
                        name: call.name,
                        arguments: call.arguments,
                    })
                    .collect()
            };
            ModelStreamReading {
                text_segments: reading.text_segments,
                tool_calls,
                result,
                overflow: reading.overflow,
                recovery: reading.recovery,
            }
        }
        RawReading::Managed(reading) => ModelStreamReading {
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
    }
}

pub(super) fn canceled(context: ReplyContext, text_segments: u32) -> ModelStreamReading {
    ModelStreamReading {
        text_segments,
        tool_calls: Vec::new(),
        result: TerminalResult {
            request_id: context.request_id,
            stop: StopReason::Error,
            usage: TokenUsage::default(),
            provider_stop_reason: None,
            error: Some(ProviderError {
                class: ErrorClass::Canceled,
                retry_after_millis: None,
                detail: BoundedText::new("model response canceled"),
            }),
        },
        overflow: None,
        recovery: None,
    }
}
