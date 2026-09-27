// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What a replayed transcript has to satisfy before a body is written from it.
//!
//! Every rule here is an assertion about what the caller built, and not one of
//! them repairs anything (decision 0069 section 8). That is the whole design:
//! a transcript can only be wrong in ways that mean the loop above went wrong,
//! and each of the obvious repairs turns a builder's defect into a request that
//! reads as reasonable.
//!
//! The one worth naming is synthesizing a result for a call nobody answered.
//! It is the repair a normalization layer reaches for first, and what it puts
//! in the body is a sentence saying a tool failed when nothing ran — a fact
//! about the world, to a model that has no way to know otherwise, invented to
//! keep a request that should have stopped.
//!
//! Borrowed like the rest of `wire`: the checks walk `&str` slices the caller
//! owns and hold nothing.

use super::reply::MAX_TOOL_CALLS;
use super::request::{within_depth, Turn, WireRefusal, MAX_TOOL_SCHEMA_DEPTH};

/// Refuses a conversation a truthful body cannot be written from.
pub(super) fn check(turns: &[Turn<'_>]) -> Result<(), WireRefusal> {
    for (turn_index, turn) in turns.iter().enumerate() {
        match turn {
            Turn::Said { .. } => {}
            Turn::Called { calls, .. } => {
                if calls.is_empty() {
                    return Err(WireRefusal::EmptyToolTurn { turn_index });
                }
                // The same ceiling the reply reader applies, for the reason the
                // schema depth gives: a writer stricter than the reader would
                // refuse a transcript the process has already accepted, and a
                // writer looser than it would write one the process refused.
                if calls.len() > MAX_TOOL_CALLS {
                    return Err(WireRefusal::TooManyToolCalls { turn_index });
                }
                for (call_index, call) in calls.iter().enumerate() {
                    if !within_depth(call.arguments, MAX_TOOL_SCHEMA_DEPTH) {
                        return Err(WireRefusal::ToolArgumentsTooDeep {
                            turn_index,
                            call_index,
                        });
                    }
                    if !answered_after(turns, turn_index, call.call_id) {
                        return Err(WireRefusal::CallWithoutResult {
                            turn_index,
                            call_index,
                        });
                    }
                }
            }
            Turn::Returned { results } => {
                if results.is_empty() {
                    return Err(WireRefusal::EmptyToolTurn { turn_index });
                }
                for (result_index, result) in results.iter().enumerate() {
                    if !called_before(turns, turn_index, result.call_id) {
                        return Err(WireRefusal::ResultWithoutCall {
                            turn_index,
                            result_index,
                        });
                    }
                }
            }
        }
    }
    Ok(())
}

/// Whether some turn after `turn_index` answers `call_id`.
fn answered_after(turns: &[Turn<'_>], turn_index: usize, call_id: &str) -> bool {
    turns
        .iter()
        .skip(turn_index.saturating_add(1))
        .filter_map(|turn| match turn {
            Turn::Returned { results } => Some(*results),
            Turn::Said { .. } | Turn::Called { .. } => None,
        })
        .any(|results| results.iter().any(|result| result.call_id == call_id))
}

/// Whether some turn before `turn_index` made the call `call_id` names.
fn called_before(turns: &[Turn<'_>], turn_index: usize, call_id: &str) -> bool {
    turns
        .iter()
        .take(turn_index)
        .filter_map(|turn| match turn {
            Turn::Called { calls, .. } => Some(*calls),
            Turn::Said { .. } | Turn::Returned { .. } => None,
        })
        .any(|calls| calls.iter().any(|call| call.call_id == call_id))
}
