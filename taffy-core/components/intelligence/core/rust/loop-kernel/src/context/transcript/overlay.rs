// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fill the still-resident turn from the reply the process holds.

use std::collections::BTreeMap;

use model_router::json::JsonValue;
use task_engine::{
    turn_call_of, ArgumentValue, IdempotencyKey, ModelStopReason, NotAttempted, SuppliedArgument,
    TurnResidency,
};

use super::recent::RecentTurns;
use super::{RecordedCall, TaskTranscript, TurnExchange};

impl TaskTranscript {
    /// Fills the newest exchange from `residency`.
    ///
    /// Durable reconstruction writes `{}` because the journal keeps no
    /// argument text. The reply that is still in this process can do better,
    /// and only for the turn it answers: overlaying any earlier exchange would
    /// put one turn's operands on another turn's calls. A restart that lost
    /// the residency is a no-op, which is the honest reconstruction.
    ///
    /// A Length stop that named tool calls never became actions, so durable
    /// reconstruction has no exchange for them. While this process still holds
    /// the reply, those calls are appended as a paired exchange whose result
    /// is the compiled-in re-issue sentence — not a journal field, and not
    /// the wire-layer synthesis of a result for a call nobody made.
    pub fn overlay_resident_calls(&mut self, residency: &TurnResidency) {
        self.overlay_length_stop(residency);
        self.overlay_resident_turn(residency);
    }

    /// Fills each earlier exchange this generation still remembers with its
    /// own arguments and what the model said beside them.
    ///
    /// Matched by ordinal, and call by call by sequence, so one turn's
    /// operands can only ever land on that turn's calls — the reason
    /// [`Self::overlay_resident_calls`] refuses to reach further back does not
    /// apply to a memory that is keyed the same way. Nothing is appended: an
    /// exchange the journal did not produce is not invented here, and a call
    /// already filled keeps what it has. Runs after the ladder, as the
    /// resident overlay does, so what it adds is counted by
    /// [`Self::text_bytes`] for the composer's estimate but never changes
    /// which turns were kept.
    pub fn overlay_recent_turns(&mut self, recent: &RecentTurns) {
        if recent.is_empty() {
            return;
        }
        self.overlay_recent_refusals(recent);
        for exchange in &mut self.exchanges {
            let ordinal = exchange.ordinal;
            if let Some(said) = recent.said(ordinal) {
                exchange.said = vec![said.to_owned()];
            }
            for call in &mut exchange.calls {
                if !is_empty_object(&call.arguments) {
                    continue;
                }
                let key = IdempotencyKey::new(call.call_id.clone());
                let Some((_, sequence)) = turn_call_of(&key) else {
                    continue;
                };
                if let Some(arguments) = recent.arguments(ordinal, sequence) {
                    call.arguments = arguments.clone();
                }
            }
        }
    }

    fn overlay_length_stop(&mut self, residency: &TurnResidency) {
        if residency.reply().stop != ModelStopReason::Length || residency.call_count() == 0 {
            return;
        }
        let Some(ordinal) = turn_ordinal_of(residency.call_id().as_str()) else {
            return;
        };
        let first_id = format!("turn-{ordinal}-call-0");
        if self
            .exchanges
            .last()
            .is_some_and(|exchange| exchange.calls.iter().any(|call| call.call_id == first_id))
        {
            return;
        }
        let mut calls = Vec::new();
        for (index, call) in residency.reply().tool_calls.iter().enumerate() {
            let Ok(sequence) = u32::try_from(index) else {
                return;
            };
            calls.push(RecordedCall::not_attempted(
                format!("turn-{ordinal}-call-{sequence}"),
                call.tool_name.clone(),
                arguments_json(&call.arguments),
                NotAttempted::TruncatedArguments,
            ));
        }
        let Some(exchange) = TurnExchange::new(ordinal, calls) else {
            return;
        };
        self.exchanges.push(exchange);
    }

    fn overlay_resident_turn(&mut self, residency: &TurnResidency) {
        let Some(ordinal) = turn_ordinal_of(residency.call_id().as_str()) else {
            return;
        };

        let existing = self
            .exchanges
            .iter()
            .position(|exchange| exchange.ordinal == ordinal);
        let mut resident_loop_calls = Vec::new();
        for (index, supplied) in residency.reply().tool_calls.iter().enumerate() {
            let Ok(sequence) = u32::try_from(index) else {
                return;
            };
            let Some(outcome) = residency.loop_outcome(sequence).copied() else {
                continue;
            };
            resident_loop_calls.push((sequence, supplied, outcome));
        }

        if existing.is_none() && !resident_loop_calls.is_empty() {
            let calls = resident_loop_calls
                .iter()
                .map(|(sequence, supplied, outcome)| {
                    RecordedCall::loop_outcome(
                        format!("turn-{ordinal}-call-{sequence}"),
                        supplied.tool_name.clone(),
                        arguments_json(&supplied.arguments),
                        *outcome,
                    )
                })
                .collect();
            let Some(exchange) = TurnExchange::new(ordinal, calls) else {
                return;
            };
            self.exchanges.push(exchange);
        }

        let Some(exchange) = self
            .exchanges
            .iter_mut()
            .find(|exchange| exchange.ordinal == ordinal)
        else {
            return;
        };
        for (sequence, supplied, outcome) in resident_loop_calls {
            let call_id = format!("turn-{ordinal}-call-{sequence}");
            if exchange.calls.iter().all(|call| call.call_id != call_id) {
                exchange.calls.push(RecordedCall::loop_outcome(
                    call_id,
                    supplied.tool_name.clone(),
                    arguments_json(&supplied.arguments),
                    outcome,
                ));
            }
        }
        exchange.calls.sort_by_key(|call| {
            turn_call_of(&IdempotencyKey::new(call.call_id.clone()))
                .map_or(u32::MAX, |(_, sequence)| sequence)
        });

        for call in &mut exchange.calls {
            let key = IdempotencyKey::new(call.call_id.clone());
            let Some((_, sequence)) = turn_call_of(&key) else {
                continue;
            };
            let Some(supplied) = residency.call(sequence) else {
                continue;
            };
            call.arguments = arguments_json(&supplied.arguments);
            if let Some(result) = residency.loop_result(sequence) {
                call.result = result.to_vec();
            } else if let Some(outcome) = residency.memory_search_outcome(sequence) {
                call.result = outcome.result_pieces().to_vec();
            } else if let Some(outcome) = residency.library_search_outcome(sequence) {
                call.result = outcome.result_pieces().to_vec();
            } else if let Some(outcome) = residency.store_outcome(sequence) {
                call.result = outcome.result_pieces().to_vec();
            } else if let Some(outcome) = residency.task_download_outcome(sequence) {
                call.result = outcome
                    .result_pieces()
                    .into_iter()
                    .map(str::to_owned)
                    .collect();
            } else if let Some(outcome) = residency.task_tab_outcome(sequence) {
                call.result = outcome
                    .result_pieces()
                    .into_iter()
                    .map(str::to_owned)
                    .collect();
            } else if let Some(outcome) = residency.media_probe_outcome(sequence) {
                call.result = outcome.result_pieces().to_vec();
            }
        }
    }
}

impl TaskTranscript {
    /// Writes each call the reducer refused on sight back as a paired call and
    /// result carrying the compiled-in reason.
    ///
    /// A refused call never became an action, so the journal-built transcript
    /// has nothing for it, and the model was never told: on a phone it named
    /// the same absent number ten turns running. This is the one place an
    /// exchange the journal did not produce is added, and only for a turn this
    /// generation read. An older turn the ladder already dropped is not
    /// brought back: a remembered turn is inserted only after the oldest kept
    /// exchange, or anywhere when nothing was dropped.
    fn overlay_recent_refusals(&mut self, recent: &RecentTurns) {
        for (ordinal, sequence, tool, arguments, reason) in recent.refusals() {
            let call_id = format!("turn-{ordinal}-call-{sequence}");
            let call = RecordedCall::not_attempted(
                call_id.clone(),
                tool.to_owned(),
                arguments
                    .cloned()
                    .unwrap_or_else(|| JsonValue::Object(BTreeMap::new())),
                reason,
            );
            if let Some(exchange) = self
                .exchanges
                .iter_mut()
                .find(|exchange| exchange.ordinal == ordinal)
            {
                if exchange.calls.iter().all(|call| call.call_id != call_id) {
                    exchange.calls.push(call);
                    exchange.calls.sort_by_key(|call| {
                        turn_call_of(&IdempotencyKey::new(call.call_id.clone()))
                            .map_or(u32::MAX, |(_, sequence)| sequence)
                    });
                }
                continue;
            }
            let dropped_before = self.elided > 0
                && self
                    .exchanges
                    .first()
                    .is_none_or(|first| ordinal < first.ordinal);
            if dropped_before {
                continue;
            }
            let Some(exchange) = TurnExchange::new(ordinal, vec![call]) else {
                continue;
            };
            let at = self
                .exchanges
                .iter()
                .position(|existing| existing.ordinal > ordinal)
                .unwrap_or(self.exchanges.len());
            self.exchanges.insert(at, exchange);
        }
    }
}

/// The turn ordinal encoded as the last segment of `model-{task}-{ordinal}`.
pub(super) fn turn_ordinal_of(call_id: &str) -> Option<u64> {
    call_id.rsplit_once('-')?.1.parse().ok()
}

fn is_empty_object(value: &JsonValue) -> bool {
    matches!(value, JsonValue::Object(map) if map.is_empty())
}

pub(super) fn arguments_json(arguments: &[SuppliedArgument]) -> JsonValue {
    let mut map = BTreeMap::new();
    for argument in arguments {
        let Some(value) = json_from_value(&argument.value) else {
            continue;
        };
        map.insert(argument.name.clone(), value);
    }
    JsonValue::Object(map)
}

/// Values the wire can write. A count that does not fit `i64` is dropped
/// rather than wrapped: wrapping would be a different call from the one that
/// was made.
fn json_from_value(value: &ArgumentValue) -> Option<JsonValue> {
    match value {
        ArgumentValue::Handle(handle) => Some(JsonValue::Integer(i64::from(*handle))),
        // The index is what the model sent and what it is replayed with. The
        // person's value is not here and never was.
        ArgumentValue::SuppliedValue(index) => Some(JsonValue::Integer(i64::from(*index))),
        ArgumentValue::Count(count) => i64::try_from(*count).ok().map(JsonValue::Integer),
        ArgumentValue::Flag(flag) => Some(JsonValue::Bool(*flag)),
        ArgumentValue::Text(text) | ArgumentValue::Address(text) | ArgumentValue::Choice(text) => {
            Some(JsonValue::Text(text.clone()))
        }
    }
}
