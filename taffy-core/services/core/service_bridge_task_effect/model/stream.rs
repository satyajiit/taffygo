// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Backpressured raw model-stream delivery.
//!
//! This hot path borrows the pending correlation record and allocates only
//! sanitized text that crosses to the visible answer stream.

use core_runtime::{wire, TaskId};

use crate::ffi;
use crate::service_bridge_runtime::ServiceBridge;

/// Applies one exact next raw response chunk and returns only sanitized text.
///
/// The acknowledgement is the backpressure boundary: the browser may not
/// resume network delivery until this function has consumed the bytes. No
/// provider framing, reasoning, identity, or raw stop word crosses back.
#[allow(non_snake_case)]
pub(crate) fn DeliverModelStreamChunk(
    bridge: &mut ServiceBridge,
    chunk: ffi::BridgeModelStreamChunk,
) -> ffi::BridgeModelStreamDelivery {
    let stale = || ffi::BridgeModelStreamDelivery {
        status: wire::ModelStreamChunkStatus::Stale as u8,
        answer_events: Vec::new(),
    };
    let invalid = || ffi::BridgeModelStreamDelivery {
        status: wire::ModelStreamChunkStatus::Invalid as u8,
        answer_events: Vec::new(),
    };
    let unavailable = || ffi::BridgeModelStreamDelivery {
        status: wire::ModelStreamChunkStatus::Unavailable as u8,
        answer_events: Vec::new(),
    };
    let Some(pending) = bridge.pending_task_effects.get(&chunk.effect_id) else {
        return stale();
    };
    if pending.kind != wire::TaskReducerEffectKind::CallModel
        || pending.call_id.is_empty()
        || chunk.data.is_empty()
        || chunk.data.len() > wire::MAX_MODEL_STREAM_CHUNK_BYTES
        || chunk.sequence != pending.stream_sequence
        || chunk.operation.operation_id != pending.operation.operation_id
        || chunk.operation.service_generation != bridge.generation.value()
        || chunk.operation.task_revision != pending.operation.task_revision
        || chunk.operation.deadline_monotonic_ms != pending.operation.deadline_monotonic_ms
        || chunk.operation.idempotency_key != pending.operation.idempotency_key
    {
        return invalid();
    }
    let Some(runtime) = bridge.runtime.as_mut() else {
        return unavailable();
    };
    let mut visible = String::new();
    let accepted = runtime.push_model_stream_chunk(
        &TaskId::new(pending.task_id.clone()),
        &pending.call_id,
        &chunk.data,
        &mut |text| visible.push_str(text),
    );
    let Some(visible) = accepted
        .then(|| bounded_visible_deltas(&visible, wire::MAX_TASK_ANSWER_EVENTS_PER_BATCH))
        .flatten()
    else {
        return invalid();
    };
    let Some(next_stream_sequence) = pending.stream_sequence.checked_add(1) else {
        return invalid();
    };
    let Ok(visible_count) = u32::try_from(visible.len()) else {
        return invalid();
    };
    let Some(next_answer_sequence) = pending.answer_sequence.checked_add(visible_count) else {
        return invalid();
    };
    let answer_events = (pending.answer_sequence..next_answer_sequence)
        .zip(visible)
        .map(|(sequence, text)| ffi::BridgeModelStreamAnswerEvent {
            task_id: pending.task_id.clone(),
            call_id: pending.call_id.clone(),
            sequence,
            has_text: true,
            text,
            terminal: false,
            complete: false,
        })
        .collect();
    let Some(stored) = bridge.pending_task_effects.get_mut(&chunk.effect_id) else {
        return stale();
    };
    stored.stream_sequence = next_stream_sequence;
    stored.answer_sequence = next_answer_sequence;
    ffi::BridgeModelStreamDelivery {
        status: wire::ModelStreamChunkStatus::Accepted as u8,
        answer_events,
    }
}

pub(super) fn bounded_visible_deltas(text: &str, max_events: usize) -> Option<Vec<String>> {
    if text.is_empty() {
        return Some(Vec::new());
    }
    let mut out = Vec::new();
    let mut start = 0;
    while start < text.len() {
        if out.len() == max_events {
            return None;
        }
        let mut end = start
            .checked_add(wire::MAX_TASK_ANSWER_DELTA_BYTES)?
            .min(text.len());
        while end > start && !text.is_char_boundary(end) {
            end -= 1;
        }
        if end == start {
            return None;
        }
        out.push(text.get(start..end)?.to_owned());
        start = end;
    }
    Some(out)
}
