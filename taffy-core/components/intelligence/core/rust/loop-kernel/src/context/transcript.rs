// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The bounded conversation one task carries into its next model turn.
//!
//! # Why a task needs one at all
//!
//! Without it turn *n + 1* is byte-identical to turn *n*, so the model is
//! asked the same question forever and the loop cannot progress. What makes
//! the next turn a different question is the record of what the last one did.
//!
//! # A call and its result are one value
//!
//! [`RecordedCall`] carries both, and there is no constructor that separates
//! them. That is decision 0069 section 8's pairing rule made unrepresentable
//! rather than tested: `write_request` refuses a `Called` whose call no later
//! `Returned` answers (`WireRefusal::CallWithoutResult`) and a `Returned`
//! answering no earlier `Called` (`ResultWithoutCall`), and the only way to be
//! sure an eviction ladder cannot produce either is for the ladder to have no
//! unit smaller than the pair. [`TurnExchange`] is that unit, and it is what
//! the ladder drops.
//!
//! # What a replayed turn does and does not carry
//!
//! It carries the tool the registry named, the identity decision 0069 section
//! 3 mints, and one closed-vocabulary sentence saying what became of the call.
//! It carries **no page content**. The current page is injected at compose
//! time from the live arena (decision 0070) and is not a field of this type,
//! because a durable conversation that remembered a page would be the journal
//! holding prose. `views` borrows the projection for one body write.
//!
//! A transcript rebuilt from durable state alone names what was called and
//! not what it was given: [`TurnDigest`](task_engine::TurnDigest) declares no
//! string, and the node a proposal retains is a `SemanticNodeId` the model
//! may never read (decision 0053 section 3). The turn whose residency is
//! still resident can do better. [`TaskTranscript::overlay_resident_calls`]
//! fills that turn's arguments from the reply the process is still holding,
//! and a Length-stop reply that named calls — refused on sight, so they never
//! became actions — is appended as a paired exchange whose result tells the
//! model to re-issue. A restart that lost the residency writes `{}` again
//! rather than inventing values the journal never kept, and it has no such
//! exchange.
//!
//! # Nothing here reads a clock, a socket or an identifier mint
//!
//! Every value below is a pure function of the reducer's durable records and
//! the turn's transient residency, which is what keeps the composed request
//! replay-stable (decision 0052 section 1).

mod exchange;
mod overlay;
mod recent;

pub use self::exchange::{RecordedCall, TurnExchange};
pub use self::recent::{RecentTurns, MAX_RECENT_SAID_BYTES, MAX_RECENT_TURNS};

use model_router::wire::{Speaker, ToolCallReplay, ToolResultView, Turn};

use self::exchange::RETIRED_HANDLE_SENTENCE;
use crate::context::vocabulary::{elision_words, Elision, ElisionWords};

/// Bytes of transcript material one task may carry into a request.
///
/// **This number is estimated, not measured, and OD-110 owns the real one.**
/// Saying what it was estimated from is the whole of its defence:
///
/// - the core-service contract already lets a person write a goal of
///   `MAX_GOAL_BYTES`, and that goal is the one thing a transcript may never
///   drop, so any bound at or below it could be spent entirely on the opening
///   turn and leave no room for a conversation at all;
/// - the same contract puts `MAX_TASK_OBSERVATION_TEXT_BYTES` — the identical
///   figure — on one page's text, which is where [`MAX_ARENA_TEXT_BYTES`] gets
///   its value and why that constant is derived rather than chosen;
/// - so a conversation that may carry the goal in full and about as much again
///   in turns is twice the contract's own per-item figure.
///
/// At the four-bytes-to-a-token estimate the composer already spends, that is
/// roughly thirty-two thousand estimated input tokens. It is an estimate about
/// an estimate: no tokenizer ships on the device, so "estimated tokens" is a
/// division rather than a count, and OD-110 records that the corpus which
/// would settle either number does not exist yet. Nothing here should be read
/// as a measurement, and a benchmark that produces one replaces this constant
/// rather than confirming it.
///
/// [`MAX_ARENA_TEXT_BYTES`]: crate::context::MAX_ARENA_TEXT_BYTES
pub const MAX_TRANSCRIPT_BYTES: usize = 2 * core_service_types::MAX_GOAL_BYTES;

// The first bullet of the derivation above, checked rather than described. A
// bound at or below the contract's goal ceiling could be spent entirely on the
// opening turn, and the ladder would then hold a budget it can never satisfy
// while dropping every turn it is allowed to drop. Whoever lowers the constant
// — including to a number a benchmark measured — fails here rather than in a
// task that quietly stopped carrying a conversation.
const _: () = assert!(MAX_TRANSCRIPT_BYTES > core_service_types::MAX_GOAL_BYTES);

/// What one transcript may spend.
///
/// A value rather than a constant read at the point of use, for the reason
/// [`RenderBudget`](crate::context::RenderBudget) is one: the ladder is
/// testable at a size a test can construct, and the compiled-in default is one
/// caller's choice rather than the ladder's own.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TranscriptBudget {
    bytes: usize,
}

impl TranscriptBudget {
    /// A budget of `bytes`, past which the oldest exchanges are dropped.
    pub const fn new(bytes: usize) -> Self {
        Self { bytes }
    }

    /// What it may spend.
    pub const fn bytes(self) -> usize {
        self.bytes
    }
}

impl Default for TranscriptBudget {
    fn default() -> Self {
        Self::new(MAX_TRANSCRIPT_BYTES)
    }
}

/// The most an elision line can cost, held out of the budget before anything
/// is kept.
///
/// Reserved unconditionally rather than added afterwards. A ladder that
/// decided what to keep and *then* wrote the line explaining what it dropped
/// would be a transcript that passed its own bound and then exceeded it — by a
/// line whose length depends on a count the decision produced. Twenty digits
/// covers `u64::MAX`, and two spaces cover the joins.
const ELISION_RESERVE: usize = {
    let words: ElisionWords = elision_words(Elision::OldestTurns);
    words.before.len() + words.after.len() + 22
};

/// The conversation of one task, cut to fit.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskTranscript {
    goal: String,
    preface: Vec<String>,
    exchanges: Vec<TurnExchange>,
    elided: usize,
    notice: Option<String>,
    ladder_dropped_through: Option<u64>,
    over_budget: bool,
}

impl TaskTranscript {
    /// The conversation `goal` opened and `exchanges` continued, with the
    /// oldest exchanges dropped until it fits `budget`.
    ///
    /// `exchanges` is oldest first. The ladder is four rules and three of them
    /// are structural rather than checked:
    ///
    /// - **The goal is never dropped**, because it is not in the list the
    ///   ladder walks. A conversation without the thing that was asked for is
    ///   not this task's conversation, and a model handed one re-plans against
    ///   whatever the surviving turns imply.
    /// - **A call and its result go together**, because [`TurnExchange`] is
    ///   the unit and it holds both.
    /// - **The most recent exchange is never dropped**, whatever it costs. It
    ///   is the whole of what makes the next turn a different question from
    ///   the last one.
    /// - **Oldest first**, which is the only ordering that leaves a
    ///   contiguous, still-coherent tail: the newest turns are the ones a
    ///   later turn's handles and reasoning refer back to.
    ///
    /// A goal and a most-recent exchange that together exceed the budget are
    /// kept anyway. That is the honest end of the ladder — there is nothing
    /// left it is allowed to drop — and the alternative is a request with no
    /// history, sent at the same price.
    pub fn new(goal: String, exchanges: Vec<TurnExchange>, budget: TranscriptBudget) -> Self {
        Self::with_floor(goal, exchanges, budget, None)
    }

    /// [`Self::new`], with the durable eviction boundary applied first.
    ///
    /// Every exchange at or below `evicted_through` is dropped before the
    /// byte ladder runs, and is counted into the elision the model reads
    /// about. The boundary is the journaled [`RecordContextEviction`] fact,
    /// which is what makes the shrunken conversation replay-stable: two
    /// composes at the same durable revision drop exactly the same turns,
    /// whatever the ladder would have decided on its own.
    ///
    /// What the ladder drops *beyond* the boundary is reported by
    /// [`Self::pending_eviction_through`], and the walk turns it into the
    /// next journaled boundary before the next model turn is requested — so
    /// at request time the ladder has nothing left to decide and the
    /// composed prefix is byte-stable.
    ///
    /// [`RecordContextEviction`]: task_engine::Command::RecordContextEviction
    pub fn with_floor(
        goal: String,
        exchanges: Vec<TurnExchange>,
        budget: TranscriptBudget,
        evicted_through: Option<u64>,
    ) -> Self {
        let mut exchanges = exchanges;
        let durably_elided = match evicted_through {
            Some(boundary) => {
                let kept_from = exchanges
                    .iter()
                    .position(|exchange| exchange.ordinal() > boundary)
                    .unwrap_or(exchanges.len());
                exchanges.drain(..kept_from);
                kept_from
            }
            None => 0,
        };
        let mut spent = goal.len().saturating_add(ELISION_RESERVE);
        let mut kept = 0usize;
        for exchange in exchanges.iter().rev() {
            let cost = exchange.text_bytes();
            if kept > 0 && spent.saturating_add(cost) > budget.bytes() {
                break;
            }
            spent = spent.saturating_add(cost);
            kept = kept.saturating_add(1);
        }
        let ladder_elided = exchanges.len().saturating_sub(kept);
        let ladder_dropped_through = ladder_elided
            .checked_sub(1)
            .and_then(|last| exchanges.get(last))
            .map(TurnExchange::ordinal);
        exchanges.drain(..ladder_elided);
        let elided = durably_elided.saturating_add(ladder_elided);
        let notice = (elided > 0).then(|| elision_line(elided));
        // The honest end of the ladder: the goal and the newest exchange are
        // kept whatever they cost, so a transcript can finish over budget.
        let over_budget = spent > budget.bytes();
        Self {
            goal,
            preface: Vec::new(),
            exchanges,
            elided,
            notice,
            ladder_dropped_through,
            over_budget,
        }
    }

    /// The compiled-in pieces the model reads after the goal.
    ///
    /// An errand's opening — what the task is, where it stands, and the nudge
    /// after a reply that did nothing — belongs to the opening turn the same
    /// way the elision line does: assembled on the person's behalf, never a
    /// turn of its own. The pieces are not budgeted by the ladder, because
    /// every one of them is compiled in and bounded by [`crate::context::opening`];
    /// they are counted by [`Self::text_bytes`] so the composer's estimate
    /// still sees them.
    #[must_use]
    pub fn with_preface(mut self, pieces: Vec<String>) -> Self {
        self.preface = pieces;
        self
    }

    /// The pieces [`Self::with_preface`] set, in order.
    pub fn preface(&self) -> &[String] {
        &self.preface
    }

    /// Whether the kept conversation still exceeds the budget it was cut to.
    ///
    /// True only at the ladder's honest end — the goal and the most recent
    /// exchange together exceed the budget, and there is nothing left the
    /// ladder is allowed to drop. The request is sent anyway, at the same
    /// price; this is the fact [`crate::window::plan_context_window`] names.
    pub const fn over_budget(&self) -> bool {
        self.over_budget
    }

    /// The boundary the byte ladder moved past the durable one, if it did.
    ///
    /// `Some(turn)` means this transcript dropped turns nothing has journaled
    /// yet. The walk records that boundary as the one durable eviction fact
    /// before it requests the next model turn; a transcript built after the
    /// commit reports `None` here and drops the same turns under the floor.
    pub const fn pending_eviction_through(&self) -> Option<u64> {
        self.ladder_dropped_through
    }

    /// What the person asked for.
    pub fn goal(&self) -> &str {
        &self.goal
    }

    /// The turns this transcript carries, oldest first.
    pub fn exchanges(&self) -> &[TurnExchange] {
        &self.exchanges
    }

    /// How many turns the ladder dropped.
    pub const fn elided(&self) -> usize {
        self.elided
    }

    /// The line the model reads about what was dropped, when anything was.
    ///
    /// `None` and not an empty string: a transcript that dropped nothing says
    /// nothing, rather than saying that nothing was dropped on every turn of
    /// every task for the life of the product.
    pub fn elision_notice(&self) -> Option<&str> {
        self.notice.as_deref()
    }

    /// The material this transcript would put into a request body.
    ///
    /// Not the body: the body is four different documents and this figure is
    /// the same on all of them, because what it counts is the transcript's own
    /// material rather than a family's framing of it. It is what the ladder
    /// spends and what the composer estimates input tokens from.
    /// Takes every number the live pages will not honour out of the replayed
    /// calls, and tells the model on the call it was taken from.
    ///
    /// The transcript replays the model's own arguments for the last
    /// `MAX_RECENT_TURNS` turns, which is there for a measured reason: without
    /// it a model that had written "we're on the official eAadhaar page" began
    /// the next turn by searching for it again. The cost was never counted.
    /// Every one of those turns carries its handle numbers, in the model's own
    /// voice, so a task that has moved to another page drags a pool of dead
    /// numbers through sixteen turns of prompt — and on 2026-09-20 errand
    /// `f8356d76` named one on 14 of its 43 turns, 8 of them numbers no table
    /// still held at all.
    ///
    /// A projection footer cannot win that argument: one sentence about
    /// numbers in general loses to a dozen concrete numbers the model can see
    /// itself having used. So the numbers go instead. What is left is the
    /// call, its other arguments and a sentence saying the number is gone —
    /// which is what a transcript rebuilt from the journal alone would have
    /// shown anyway, because the journal keeps no argument text (decision
    /// 0222).
    pub fn retire_unusable_handles(&mut self, honours: &dyn Fn(u32) -> bool) {
        for exchange in &mut self.exchanges {
            for call in &mut exchange.calls {
                if call.drop_unusable_handles(honours) {
                    call.result.push(RETIRED_HANDLE_SENTENCE.to_owned());
                }
            }
        }
    }

    pub fn text_bytes(&self) -> usize {
        self.exchanges
            .iter()
            .map(TurnExchange::text_bytes)
            .fold(self.goal.len(), usize::saturating_add)
            .saturating_add(self.notice.as_ref().map_or(0, String::len))
            .saturating_add(
                self.preface
                    .iter()
                    .map(String::len)
                    .fold(0, usize::saturating_add),
            )
    }

    /// The borrowed views one request body is written from.
    ///
    /// `page` is the current projection, when there is one, and is a piece of
    /// the opening turn rather than a turn of its own — the same reason the
    /// elision line sits there. `person_answer` is the classified line from
    /// the last `user.ask`, when one was supplied. The transcript owns neither.
    pub fn views<'a>(
        &'a self,
        page: Option<&'a str>,
        person_answer: Option<&'a str>,
    ) -> TranscriptViews<'a> {
        let mut opening = Vec::with_capacity(4_usize.saturating_add(self.preface.len()));
        opening.push(self.goal.as_str());
        opening.extend(self.preface.iter().map(String::as_str));
        if let Some(notice) = self.notice.as_deref() {
            opening.push(notice);
        }
        if let Some(answer) = person_answer.filter(|text| !text.is_empty()) {
            opening.push(answer);
        }
        if let Some(page) = page.filter(|text| !text.is_empty()) {
            opening.push(page);
        }
        let exchanges = self
            .exchanges
            .iter()
            .map(|exchange| ExchangeViews {
                said: exchange.said.iter().map(String::as_str).collect(),
                calls: exchange
                    .calls
                    .iter()
                    .map(|call| ToolCallReplay {
                        call_id: &call.call_id,
                        tool: &call.tool,
                        arguments: &call.arguments,
                    })
                    .collect(),
                results: exchange
                    .calls
                    .iter()
                    .map(|call| ToolResultView {
                        call_id: &call.call_id,
                        tool: &call.tool,
                        is_error: call.is_error(),
                        text: &call.result,
                    })
                    .collect(),
            })
            .collect();
        TranscriptViews { opening, exchanges }
    }
}

/// The elision line, with the count the ladder produced between two compiled-in
/// halves.
fn elision_line(elided: usize) -> String {
    let words = elision_words(Elision::OldestTurns);
    format!("{} {elided} {}", words.before, words.after)
}

/// One transcript, as the borrowed views `model-router` takes.
///
/// It exists because every content-bearing field of a wire turn is a borrow
/// and a `ToolCallReplay` is a value that has to live somewhere. Two stages
/// rather than one: this holds the views, and [`Self::turns`] holds the
/// conversation that borrows slices of them.
#[derive(Debug)]
pub struct TranscriptViews<'a> {
    opening: Vec<&'a str>,
    exchanges: Vec<ExchangeViews<'a>>,
}

#[derive(Debug)]
struct ExchangeViews<'a> {
    said: Vec<&'a str>,
    calls: Vec<ToolCallReplay<'a>>,
    results: Vec<ToolResultView<'a>>,
}

impl<'a> TranscriptViews<'a> {
    /// The conversation, oldest first.
    ///
    /// The goal opens it as one `Said` turn from the person's side, carrying
    /// the elision line as a second piece when there is one. The line is a
    /// piece of that turn rather than a turn of its own for two reasons: it is
    /// assembled on the person's behalf, which is what `Speaker::User` already
    /// means, and a second consecutive turn in the same role is a shape two of
    /// the four families argue with.
    ///
    /// Each exchange is then a `Called` and the `Returned` that answers it, in
    /// that order, which is the pairing `write_request` checks.
    pub fn turns(&'a self) -> Vec<Turn<'a>> {
        let mut turns = Vec::with_capacity(1 + self.exchanges.len().saturating_mul(2));
        turns.push(Turn::Said {
            speaker: Speaker::User,
            text: &self.opening,
        });
        for exchange in &self.exchanges {
            // The model's own words for this turn, while this generation
            // remembers them, and nothing otherwise. They were never durable,
            // and putting anything else here would be this layer writing
            // prose into the model's mouth.
            turns.push(Turn::Called {
                text: &exchange.said,
                calls: &exchange.calls,
                reasoning: &[],
            });
            turns.push(Turn::Returned {
                results: &exchange.results,
            });
        }
        turns
    }
}

#[cfg(test)]
mod tests;
