// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What this generation remembers of a task's recent turns.
//!
//! A transcript rebuilt from the journal names each earlier call and nothing
//! it was given, and says nothing the model said beside it, because the
//! journal keeps no prose (decision 0069). Only the turn still resident was
//! filled in. On a phone on 2026-09-18 that was the whole of the model's
//! memory: every earlier step reached Grok as a tool name with `{}` and the
//! sentence "done, and checked", so a model that had written "we're on the
//! official eAadhaar page" one turn earlier began the next with "I need to
//! find the official site" and searched again, twenty-nine times.
//!
//! [`RecentTurns`] is the same residency, kept a little longer: the last
//! [`MAX_RECENT_TURNS`] readings' arguments and at most
//! [`MAX_RECENT_SAID_BYTES`] of what the model said beside its calls. It
//! lives on the loop state, so it dies with the generation exactly as the
//! residency does (decision 0072); a restore constructs it empty and the
//! transcript reverts to the journal's `{}`, which is the honest
//! reconstruction. Nothing here is page content: the arguments and the
//! words are the model's own, sent back to the provider that wrote them.

use std::collections::VecDeque;

use model_router::json::JsonValue;
use task_engine::{NotAttempted, TurnResidency};

use super::overlay::{arguments_json, turn_ordinal_of};

/// Readings a task keeps. An errand's working set is a handful of turns —
/// search, read, open, read, fill — and sixteen keeps the whole of one such
/// sequence with room for a retry or two inside it. Twelve was measured short
/// on a phone: a stretch of refused moves outran it, and the turn that had
/// found the right page fell out of memory while the model was still on it.
pub const MAX_RECENT_TURNS: usize = 16;

/// Bytes of the model's own words kept for one turn. A step's narration is a
/// sentence or two; a longer answer is cut at a character boundary rather
/// than kept whole, because this is memory of intent and not the answer.
pub const MAX_RECENT_SAID_BYTES: usize = 1024;

/// One reading, as the next compose needs it.
#[derive(Clone, PartialEq, Eq)]
struct RecentTurn {
    ordinal: u64,
    said: String,
    /// Each call's name as the model spelled it, for a refused call that has
    /// no action record to take a registry name from.
    tools: Vec<String>,
    arguments: Vec<JsonValue>,
    /// The calls the reducer refused on sight, and why.
    refused: Vec<(u32, NotAttempted)>,
}

/// The last few readings of one task in this generation, oldest first.
#[derive(Clone, Default, PartialEq, Eq)]
pub struct RecentTurns {
    turns: VecDeque<RecentTurn>,
}

impl core::fmt::Debug for RecentTurns {
    // Counts only: the words are the model's and have no place in a log.
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("RecentTurns")
            .field("turns", &self.turns.len())
            .finish()
    }
}

impl RecentTurns {
    /// Remembers one reading, what the model said beside it, and which of its
    /// calls the reducer refused on sight.
    ///
    /// A second reading of the same turn replaces the first, so a re-read
    /// reply cannot leave two memories of one turn.
    pub fn remember(
        &mut self,
        residency: &TurnResidency,
        said: &str,
        refused: Vec<(u32, NotAttempted)>,
    ) {
        let Some(ordinal) = turn_ordinal_of(residency.call_id().as_str()) else {
            return;
        };
        self.turns.retain(|turn| turn.ordinal != ordinal);
        self.turns.push_back(RecentTurn {
            ordinal,
            said: bounded(said.trim()).to_owned(),
            tools: residency
                .reply()
                .tool_calls
                .iter()
                .map(|call| call.tool_name.clone())
                .collect(),
            refused,
            arguments: residency
                .reply()
                .tool_calls
                .iter()
                .map(|call| arguments_json(&call.arguments))
                .collect(),
        });
        while self.turns.len() > MAX_RECENT_TURNS {
            self.turns.pop_front();
        }
    }

    /// How many readings are held.
    pub fn len(&self) -> usize {
        self.turns.len()
    }

    /// Whether no reading is held.
    pub fn is_empty(&self) -> bool {
        self.turns.is_empty()
    }

    /// What the model said on turn `ordinal`, when it said anything and this
    /// generation still remembers it.
    pub(super) fn said(&self, ordinal: u64) -> Option<&str> {
        self.find(ordinal)
            .map(|turn| turn.said.as_str())
            .filter(|said| !said.is_empty())
    }

    /// The arguments call `sequence` of turn `ordinal` carried.
    pub(super) fn arguments(&self, ordinal: u64, sequence: u32) -> Option<&JsonValue> {
        let index = usize::try_from(sequence).ok()?;
        self.find(ordinal)?.arguments.get(index)
    }

    /// Each refused call of each remembered turn, oldest turn first: its
    /// turn, sequence, name, arguments and reason.
    pub(super) fn refusals(
        &self,
    ) -> impl Iterator<Item = (u64, u32, &str, Option<&JsonValue>, NotAttempted)> + '_ {
        self.turns.iter().flat_map(|turn| {
            turn.refused.iter().filter_map(move |(sequence, reason)| {
                let index = usize::try_from(*sequence).ok()?;
                let tool = turn.tools.get(index)?;
                Some((
                    turn.ordinal,
                    *sequence,
                    tool.as_str(),
                    turn.arguments.get(index),
                    *reason,
                ))
            })
        })
    }

    fn find(&self, ordinal: u64) -> Option<&RecentTurn> {
        self.turns.iter().find(|turn| turn.ordinal == ordinal)
    }
}

/// `said` cut to [`MAX_RECENT_SAID_BYTES`] at a character boundary.
fn bounded(said: &str) -> &str {
    if said.len() <= MAX_RECENT_SAID_BYTES {
        return said;
    }
    let mut end = MAX_RECENT_SAID_BYTES;
    while end > 0 && !said.is_char_boundary(end) {
        end -= 1;
    }
    said.get(..end).unwrap_or_default()
}
