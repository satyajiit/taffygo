// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The named seam a model-written summary would plug into. Nothing implements
//! it, and that is the point of writing it down (decision 0074): the seam
//! records *where* summarization goes and *what it is allowed to be*, so the
//! first implementation cannot quietly become something else.

use crate::context::TurnExchange;

/// Turns evicted exchanges into one line the surviving transcript may carry.
///
/// Three constraints are the contract, and each is structural in the types:
///
/// - **A summary is produced by an ordinary model turn.** An implementation
///   composes it through `Effect::CallModel` like every other turn — journaled
///   request, journaled reply shape, budget charged — never as an inline
///   awaited call inside the walk. This trait is consulted only *between*
///   turns, with material that is already durably evicted.
/// - **A summary can only add, never veto.** The verdict about what is
///   evicted was [`crate::window::plan_context_window`]'s and is already in
///   the journal by the time this runs; returning `None` changes nothing but
///   the words the model reads about the gap.
/// - **A summary is derived from evicted exchanges only** — the durable call
///   shapes, not page content, which never survives into a
///   [`TurnExchange`] in the first place.
///
/// No production adapter exists. The elision line
/// (`crate::context::TaskTranscript::elision_notice`) is what ships instead:
/// a count, not a summary, and honest about being one.
pub trait TurnSummarizer {
    /// One line describing `evicted`, or `None` to keep the plain count.
    fn summarize(&self, evicted: &[TurnExchange]) -> Option<String>;
}
