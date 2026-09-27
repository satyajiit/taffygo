// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What an errand has to show before a prose reply may finish it.
//!
//! A research task ends on an answer. An errand ends on an outcome the
//! browser verified — a download started, a form submitted, or a hand-back
//! the person completed — and a model that narrates instead of acting has not
//! finished anything. The reducer
//! counts prose-only replies that arrive before any such witness, so the
//! agent table can nudge the model a bounded number of times and then fail rather than
//! calling a description of work "done" (decision 0136).
//!
//! The reply counter is derived state: it is advanced by commands the journal
//! holds and rebuilt by replay, so a restart reaches the same answer without
//! anything being persisted for it.

use super::Reducer;
use crate::action::{ActionIntent, ActionState, BrowserIntent};
use crate::agent::{ModelStopReason, TurnDigest};
use crate::field_values::SuppliedValueCount;
use crate::ids::IdSource;
use crate::task::TaskTemplateId;
use crate::time::Clock;

/// How many prose-only replies an errand is nudged past before it fails.
///
/// Two, not one: a model that has just been told the page is blank may fairly
/// ask a question in prose once. A third such reply is a model that is not
/// going to act, and the person is better served by a named failure than by a
/// fourth turn charged to their budget.
pub const MAX_UNPRODUCTIVE_ERRAND_REPLIES: u8 = 2;

/// How many times an errand may arrive where it already was before it stops.
///
/// Two is the nudge: the opening turn names what the task has not tried and
/// what a repeat costs. Four is the end, under the same three endings a prose
/// reply reaches, because a model going round the same page four times is not
/// going to act on the fifth and every one of them is charged to a person's
/// budget. Nothing else bounds this — a `browser.search` that succeeds and
/// lands on the same results page moves no refusal counter, and a search
/// fingerprints as its tab, so three different queries look like one repeat.
pub const MAX_FRUITLESS_ERRAND_ARRIVALS: u8 = 4;

/// Where the opening turn starts saying the task has been here before.
pub const FRUITLESS_ERRAND_ARRIVAL_NUDGE: u8 = 2;

/// How many turns in a row may name calls and have none of them attempted.
///
/// A call refused on sight never reaches policy, the browser or a page. The
/// model is told which clause refused it and gets a fresh turn, and that turn
/// is paid for — so a model that keeps naming a number from a reading two
/// pages ago is charged for every attempt and nothing anywhere counts them.
/// [`TurnDigest::every_call_refused_on_sight`] has said so since it was
/// written and nothing read it.
///
/// Three, because an on-sight refusal is a correction the model can act on:
/// the first says which clause, the second is the model trying the correction,
/// and a fourth turn that still attempts nothing is not going to be the one
/// that works. A phone measured it on 2026-09-19: an errand that had reached
/// the page it wanted spent turn after turn this way, every reply refused
/// before it left the process (decision 0198).
pub const MAX_TURNS_ATTEMPTING_NOTHING: u8 = 3;

/// How many asks for values in a row may come back with nothing.
///
/// The gap the other three counters leave. A `user.request_values` call that
/// reaches the browser is attempted, so
/// [`MAX_TURNS_ATTEMPTING_NOTHING`] resets on it; it carries a tool call, so
/// [`MAX_UNPRODUCTIVE_ERRAND_REPLIES`] never sees it; and it navigates
/// nowhere, so [`MAX_FRUITLESS_ERRAND_ARRIVALS`] never sees it either. The
/// turn that asked a person is then sent straight back for a fresh one with
/// nothing counting the round trip, which is a loop with no bound at all: the
/// myAadhaar CAPTCHA was asked for eleven times on a phone on 2026-09-19, and
/// what ended the run was the person watching it (decision 0215).
///
/// Three, and the middle one is the point. Decision 0215 gives the model a
/// move for each way an ask can come back empty — scroll it into view, name
/// the form instead, take a fresh snapshot — so the second ask is the model
/// acting on advice and deserves to happen. A third that still comes back
/// with nothing means the advice did not work here, and a fourth is charged
/// to a person's budget to learn the same thing again.
///
/// Counted on the count and not on the outcome, which is what lets a replayed
/// journal rebuild it: [`crate::field_values::SuppliedFieldValues::outcome`]
/// is `None` after a restart by design, and "no value came back" is legible
/// from the number alone.
pub const MAX_UNANSWERED_VALUE_ASKS: u8 = 3;

/// Where the opening turn starts saying the asks are coming back empty.
pub const UNANSWERED_VALUE_ASK_NUDGE: u8 = 2;

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Whether this task is walked as a web errand (decision 0087).
    pub fn is_web_errand(&self) -> bool {
        self.task.snapshot().template_id == TaskTemplateId::WebErrand
    }

    /// Whether the browser has verified an outcome this errand exists for.
    ///
    /// A verified download start or form submission, or a completed handover.
    /// Filling a field and navigating are preparation, and cannot witness that
    /// the requested work happened. A handover is different in kind and is
    /// decision 0136 section 5's third arm: the person was handed the page,
    /// did the thing only they could do, and handed it back, so something was
    /// accomplished even though no browser action of Taffy's carries it. Match
    /// exact intents for the rest: download cancel shares the start action
    /// class but cannot prove a download was started.
    pub fn errand_outcome_witnessed(&self) -> bool {
        if self.handovers_completed > 0 {
            return true;
        }
        self.actions.values().any(|action| {
            action.state() == ActionState::Verified
                && matches!(
                    action.proposal().intent(),
                    ActionIntent::Browser(
                        BrowserIntent::DownloadStart { .. }
                            | BrowserIntent::DownloadFromLink { .. }
                            | BrowserIntent::FormSubmit { .. }
                    )
                )
        })
    }

    /// A started download is still work in progress. Completion requires the
    /// typed browser result installed after its action outcome committed;
    /// model prose, another download and a lost residency are not evidence.
    pub(crate) fn errand_downloads_complete(
        &self,
        residency: Option<&crate::TurnResidency>,
    ) -> bool {
        let started = self
            .actions
            .values()
            .filter(|action| {
                action.state() == ActionState::Verified
                    && matches!(
                        action.proposal().intent(),
                        ActionIntent::Browser(
                            BrowserIntent::DownloadStart { .. }
                                | BrowserIntent::DownloadFromLink { .. }
                        )
                    )
            })
            .count();
        started == 0
            || residency.is_some_and(|residency| {
                let downloads = residency.task_downloads();
                self.model_turn()
                    .is_some_and(|turn| turn.call_id() == residency.call_id())
                    && downloads.browser_session_id()
                        == Some(&self.task.snapshot().browser_session_id)
                    && downloads.completed_task_downloads() == started
            })
    }

    /// Prose-only replies recorded before any outcome was witnessed.
    pub const fn unproductive_replies(&self) -> u8 {
        self.unproductive_replies
    }

    /// Verified navigating moves that arrived where the task already was.
    pub const fn fruitless_arrivals(&self) -> u8 {
        self.fruitless_arrivals
    }

    /// Whether any move of this task was refused before it reached the browser.
    ///
    /// [`ActionState::Rejected`] is the one terminal state that means "schema,
    /// scope, or policy refused it", so this reads the actions the journal
    /// already holds rather than a second ledger. It is what lets a task whose
    /// every move was denied say so, instead of reporting that it could not
    /// read enough — the sentence for which is compiled in and, until this,
    /// unreachable from anywhere in the engine.
    pub fn moves_were_refused(&self) -> bool {
        self.actions
            .values()
            .any(|action| action.state() == ActionState::Rejected)
    }

    /// Whether this task read anything at all.
    ///
    /// The difference between "could not read enough to answer" and "read
    /// several pages and could not act on them", which are two different
    /// sentences and were one.
    pub fn sources_were_read(&self) -> bool {
        !self.task.scope.included().is_empty()
    }

    /// Counts one verified navigating move by where it landed.
    ///
    /// An arrival at a source the task already holds is a move that spent a
    /// turn and changed nothing, which is the loop the phone's journal shows:
    /// search, read, try to leave, refused, search again. An admission — a
    /// site the task did not have — is progress and resets the count, as does
    /// any outcome the errand exists for.
    pub(super) fn note_arrival(&mut self, already_bound: bool) {
        if !self.is_web_errand() {
            return;
        }
        if already_bound && !self.errand_outcome_witnessed() {
            self.fruitless_arrivals = self.fruitless_arrivals.saturating_add(1);
        } else {
            self.fruitless_arrivals = 0;
        }
    }

    /// Counts a recorded reply that finished without a call while nothing had
    /// been done yet. Only an errand counts them; a research task's prose
    /// reply is its answer.
    pub(super) fn note_recorded_reply(&mut self, digest: &TurnDigest) {
        let is_prose_only = digest.stop == ModelStopReason::Complete && digest.tool_calls == 0;
        if is_prose_only && self.is_web_errand() && !self.errand_outcome_witnessed() {
            self.unproductive_replies = self.unproductive_replies.saturating_add(1);
        }
        // A run, not a total: a turn that got a call as far as policy is
        // progress, whatever policy then said about it, and it starts the
        // count again.
        if digest.tool_calls > 0 {
            self.turns_attempting_nothing = if digest.every_call_refused_on_sight() {
                self.turns_attempting_nothing.saturating_add(1)
            } else {
                0
            };
        }
    }

    /// Turns in a row that named calls and had none of them attempted.
    pub const fn turns_attempting_nothing(&self) -> u8 {
        self.turns_attempting_nothing
    }

    /// Counts one answered ask by whether anything came back.
    ///
    /// A run, not a total, and the reset is the interesting half: one value
    /// from the person is the ask working, whatever came back empty before it,
    /// and a person filling a form in over several steps must never walk into
    /// a cap.
    ///
    /// Gated on [`Self::is_web_errand`] like the other three, and the reason
    /// is not that only an errand can loop. Every template admits
    /// `user.request_values`; what does not reach the other three is the
    /// *advice* — `values_answer` and its nudge are composed inside the
    /// errand arm of the opening turn, so a comparison task asking for values
    /// is told nothing about how its ask came back. Bounding a model that was
    /// never given the move is the defect decision 0215 was written about,
    /// one level up, so the bound reaches exactly as far as the advice does
    /// and decision 0216 records the gap rather than closing it blind.
    pub(super) fn note_value_ask_answer(&mut self, supplied: SuppliedValueCount) {
        if !self.is_web_errand() {
            return;
        }
        self.unanswered_value_asks = if supplied.is_empty() {
            self.unanswered_value_asks.saturating_add(1)
        } else {
            // And a value from the person is a thing the task did not hold,
            // which clears the run of turns that changed nothing as well
            // (decision 0233) — from the same count, for the same reason.
            self.note_progress();
            0
        };
    }

    /// Asks for values in a row that came back with no value at all.
    pub const fn unanswered_value_asks(&self) -> u8 {
        self.unanswered_value_asks
    }
}
