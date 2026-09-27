// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The durable projection of one model turn.
//!
//! Every field here is a count, a closed enumeration, or a thirty-two byte
//! digest, in both directions. The pair is total and lossless: a turn recorded
//! and restored is the same turn, which is what lets a replay reach the state
//! that decides whether a paid call still has to be made.
//!
//! The render digest is the one field worth a sentence. It names the exact
//! projection the model was shown, so a replay can compare what a turn was
//! built from without the journal holding a line of the page — the whole
//! reason the durable layer is shapes rather than prose.

use core_service_types as wire;
use task_engine::{
    Command, ModelAttemptKind, ModelCallId, ModelStopReason, PageReadability, RenderShape,
};
use task_engine::{TurnDigest, TurnGap, TurnOverflow, TurnUsage};

use super::ConversionError;

pub(super) const fn stop(value: ModelStopReason) -> wire::PersistedModelStopReason {
    match value {
        ModelStopReason::Complete => wire::PersistedModelStopReason::Complete,
        ModelStopReason::ToolCall => wire::PersistedModelStopReason::ToolCall,
        ModelStopReason::Length => wire::PersistedModelStopReason::Length,
        ModelStopReason::ProviderStop => wire::PersistedModelStopReason::ProviderStop,
        ModelStopReason::Error => wire::PersistedModelStopReason::Error,
    }
}

pub(super) const fn unstop(value: wire::PersistedModelStopReason) -> ModelStopReason {
    match value {
        wire::PersistedModelStopReason::Complete => ModelStopReason::Complete,
        wire::PersistedModelStopReason::ToolCall => ModelStopReason::ToolCall,
        wire::PersistedModelStopReason::Length => ModelStopReason::Length,
        wire::PersistedModelStopReason::ProviderStop => ModelStopReason::ProviderStop,
        wire::PersistedModelStopReason::Error => ModelStopReason::Error,
    }
}

pub(super) const fn overflow(value: TurnOverflow) -> wire::PersistedTurnOverflow {
    match value {
        TurnOverflow::ProviderReported => wire::PersistedTurnOverflow::ProviderReported,
        TurnOverflow::Silent => wire::PersistedTurnOverflow::Silent,
        TurnOverflow::Truncation => wire::PersistedTurnOverflow::Truncation,
    }
}

pub(super) const fn unoverflow(value: wire::PersistedTurnOverflow) -> TurnOverflow {
    match value {
        wire::PersistedTurnOverflow::ProviderReported => TurnOverflow::ProviderReported,
        wire::PersistedTurnOverflow::Silent => TurnOverflow::Silent,
        wire::PersistedTurnOverflow::Truncation => TurnOverflow::Truncation,
    }
}

pub(super) const fn gap(value: TurnGap) -> wire::PersistedTurnGap {
    match value {
        TurnGap::Refused => wire::PersistedTurnGap::Refused,
        TurnGap::Unavailable => wire::PersistedTurnGap::Unavailable,
        TurnGap::Cancelled => wire::PersistedTurnGap::Cancelled,
        TurnGap::OutcomeUnknown => wire::PersistedTurnGap::OutcomeUnknown,
        TurnGap::Unreadable => wire::PersistedTurnGap::Unreadable,
    }
}

pub(super) const fn ungap(value: wire::PersistedTurnGap) -> TurnGap {
    match value {
        wire::PersistedTurnGap::Refused => TurnGap::Refused,
        wire::PersistedTurnGap::Unavailable => TurnGap::Unavailable,
        wire::PersistedTurnGap::Cancelled => TurnGap::Cancelled,
        wire::PersistedTurnGap::OutcomeUnknown => TurnGap::OutcomeUnknown,
        wire::PersistedTurnGap::Unreadable => TurnGap::Unreadable,
    }
}

pub(super) const fn attempt_kind(value: ModelAttemptKind) -> wire::PersistedModelAttemptKind {
    match value {
        ModelAttemptKind::Retry => wire::PersistedModelAttemptKind::Retry,
        ModelAttemptKind::Failover => wire::PersistedModelAttemptKind::Failover,
    }
}

pub(super) const fn unattempt_kind(value: wire::PersistedModelAttemptKind) -> ModelAttemptKind {
    match value {
        wire::PersistedModelAttemptKind::Retry => ModelAttemptKind::Retry,
        wire::PersistedModelAttemptKind::Failover => ModelAttemptKind::Failover,
    }
}

pub(super) const fn readability(value: PageReadability) -> wire::PersistedPageReadability {
    match value {
        PageReadability::Readable => wire::PersistedPageReadability::Readable,
        PageReadability::Empty => wire::PersistedPageReadability::Empty,
        PageReadability::Unreadable => wire::PersistedPageReadability::Unreadable,
    }
}

pub(super) const fn unreadability(value: wire::PersistedPageReadability) -> PageReadability {
    match value {
        wire::PersistedPageReadability::Readable => PageReadability::Readable,
        wire::PersistedPageReadability::Empty => PageReadability::Empty,
        wire::PersistedPageReadability::Unreadable => PageReadability::Unreadable,
    }
}

pub(super) fn digest(value: &TurnDigest) -> wire::PersistedTurnDigest {
    wire::PersistedTurnDigest {
        stop: stop(value.stop),
        overflow: value.overflow.map(overflow),
        input_units: value.usage.input_units,
        output_units: value.usage.output_units,
        cache_read_units: value.usage.cache_read_units,
        cache_write_units: value.usage.cache_write_units,
        answer_segment_count: value.answer_segments,
        tool_call_count: value.tool_calls,
        refused_tool_call_count: value.refused_tool_calls,
        readability: readability(value.render.readability),
        offered_node_count: value.render.offered_nodes,
        omitted_node_count: value.render.omitted_nodes,
        unreadable_node_count: value.render.unreadable_nodes,
        unreadable_text_bytes: value.render.unreadable_text_bytes,
        render_digest: value.render.digest,
    }
}

pub(super) fn undigest(value: &wire::PersistedTurnDigest) -> TurnDigest {
    TurnDigest {
        stop: unstop(value.stop),
        overflow: value.overflow.map(unoverflow),
        usage: TurnUsage {
            input_units: value.input_units,
            output_units: value.output_units,
            cache_read_units: value.cache_read_units,
            cache_write_units: value.cache_write_units,
        },
        answer_segments: value.answer_segment_count,
        tool_calls: value.tool_call_count,
        refused_tool_calls: value.refused_tool_call_count,
        render: RenderShape {
            readability: unreadability(value.readability),
            offered_nodes: value.offered_node_count,
            omitted_nodes: value.omitted_node_count,
            unreadable_nodes: value.unreadable_node_count,
            unreadable_text_bytes: value.unreadable_text_bytes,
            digest: value.render_digest,
        },
    }
}

/// A model call identity, refused when it is empty.
///
/// An empty identity would name a paid call that nothing can be correlated
/// with, which is the one thing the effect-identity journal cannot work
/// without.
fn model_call(value: String) -> Result<ModelCallId, ConversionError> {
    if value.is_empty() {
        return Err(ConversionError::InvalidIdentifier);
    }
    Ok(ModelCallId::new(value))
}

pub(super) fn persisted_model_request(call_id: &ModelCallId) -> wire::PersistedCommand {
    wire::PersistedCommand::RequestModelTurn {
        call_id: call_id.as_str().to_owned(),
    }
}

pub(super) fn persisted_model_attempt(
    call_id: &ModelCallId,
    attempt_ordinal: u32,
    candidate_ordinal: u32,
    kind: ModelAttemptKind,
) -> wire::PersistedCommand {
    wire::PersistedCommand::RequestModelAttempt {
        call_id: call_id.as_str().to_owned(),
        attempt_ordinal,
        candidate_ordinal,
        kind: attempt_kind(kind),
    }
}

pub(super) fn persisted_model_reply(
    call_id: &ModelCallId,
    digest: &TurnDigest,
) -> wire::PersistedCommand {
    wire::PersistedCommand::RecordModelTurn {
        call_id: call_id.as_str().to_owned(),
        digest: self::digest(digest),
    }
}

pub(super) fn persisted_model_gap(call_id: &ModelCallId, gap: TurnGap) -> wire::PersistedCommand {
    wire::PersistedCommand::RecordModelTurnGap {
        call_id: call_id.as_str().to_owned(),
        gap: self::gap(gap),
    }
}

pub(super) fn restored_model_request(call_id: String) -> Result<Command, ConversionError> {
    Ok(Command::RequestModelTurn {
        call_id: model_call(call_id)?,
    })
}

pub(super) fn restored_model_attempt(
    call_id: String,
    attempt_ordinal: u32,
    candidate_ordinal: u32,
    kind: wire::PersistedModelAttemptKind,
) -> Result<Command, ConversionError> {
    Ok(Command::RequestModelAttempt {
        call_id: model_call(call_id)?,
        attempt_ordinal,
        candidate_ordinal,
        kind: unattempt_kind(kind),
    })
}

pub(super) fn restored_model_reply(
    call_id: String,
    digest: &wire::PersistedTurnDigest,
) -> Result<Command, ConversionError> {
    Ok(Command::RecordModelTurn {
        call_id: model_call(call_id)?,
        digest: Box::new(self::undigest(digest)),
    })
}

pub(super) fn restored_model_gap(
    call_id: String,
    gap: wire::PersistedTurnGap,
) -> Result<Command, ConversionError> {
    Ok(Command::RecordModelTurnGap {
        call_id: model_call(call_id)?,
        gap: self::ungap(gap),
    })
}
