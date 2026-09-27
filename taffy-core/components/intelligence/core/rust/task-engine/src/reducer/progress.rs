// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Whether an errand is getting anywhere, and the bound on a run of turns
//! that did not (decision 0233).
//!
//! # Why the other counters cannot see this
//!
//! Every earlier bound on an unproductive errand counts one *shape* of turn:
//! a prose reply, an arrival where the task already was, a turn whose every
//! call was refused on sight, an ask that came back empty. A loop made of two
//! shapes clears each of them on every second turn. On a phone, errand
//! `7b367bd8` alternated a reading of one unchanged page with a click refused
//! `handle_unknown`, and [`super::MAX_TURNS_ATTEMPTING_NOTHING`] reset on
//! every reading because a reading is attempted. The bound here counts the
//! one thing both strokes of that loop share: nothing changed.
//!
//! # What counts as something changing
//!
//! A fact the task did not hold before the turn, from a command the journal
//! already carries:
//!
//! * a verified action that is not a repeat — not a reading, not an arrival
//!   where the task already was, and not the same call on the same thing the
//!   task already verified since its tab last moved;
//! * a site admitted as a new source;
//! * a value, an answer or a completed hand-back from the person;
//! * an artifact.
//!
//! **A reading is never one of them**, and that is the rule this module
//! exists to hold. Re-reading an unchanged page is the loop. A reading of a
//! page the task has not read is not counted either, because every move
//! mints a new document — a reload of the same address, a search landing on
//! the results it was already on — so a first reading of a fresh document is
//! exactly what a repeated move produces, and counting it would let every
//! repeated move launder itself through the page it lands on. A new page is
//! credited through the move that reached it, which is the one fact that can
//! tell a new place from the same place again.
//!
//! # Replay
//!
//! The run is derived: advanced by the commands named above and closed by
//! `RequestModelTurn`, all of which the journal holds, and persisted nowhere.
//! A rebuilt task re-applies the same commands in the same order and reaches
//! the same count. Nothing it reads is transient: not the reply, not the page
//! projection, and not the outcome of an ask, which a rebuilt journal does not
//! carry (decision 0216).

use bip_types::identity::{ActionId, PageEpoch};

use super::Reducer;
use crate::action::{ActionIntent, ActionState, BrowserIntent};
use crate::agent::TurnPhase;
use crate::authority::ActionClass;
use crate::ids::IdSource;
use crate::time::Clock;

/// How many turns in a row an errand may take without changing anything.
///
/// Six, because the looking an errand legitimately does between two moves is
/// short: a reading, a query for the control it wants, an inspection of the
/// form it found — three turns is a thorough look, and six is twice that. A
/// walk across several pages of one site reads each page once and moves, so
/// its run never passes two. The loop this ends is the one where six turns
/// have gone by and the page, the person and the task's results are exactly
/// what they were when the first began; a seventh is charged to the person's
/// budget to learn the same thing again.
pub const MAX_TURNS_WITHOUT_PROGRESS: u8 = 6;

/// Where the opening turn starts saying the turns are changing nothing.
///
/// Two turns before the bound, so a model told it is stalled gets two turns to
/// act on being told.
pub const TURNS_WITHOUT_PROGRESS_NUDGE: u8 = 4;

const _: () = assert!(TURNS_WITHOUT_PROGRESS_NUDGE < MAX_TURNS_WITHOUT_PROGRESS);

/// The run of recorded turns that changed nothing, and whether the turn now
/// open has changed anything yet.
///
/// Derived reducer state, and one value rather than two fields so the close
/// that consumes the flag and the count it advances cannot drift apart.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(super) struct ProgressRun {
    /// Recorded turns in a row, already closed, that changed nothing.
    closed: u8,
    /// Something changed since the last close.
    progressed: bool,
}

impl ProgressRun {
    pub(super) const fn new() -> Self {
        Self {
            closed: 0,
            progressed: false,
        }
    }

    /// The run with the turn now open counted in: nothing if it changed
    /// something, one more than the closed run if it has not.
    const fn including_the_open_turn(self) -> u8 {
        if self.progressed {
            0
        } else {
            self.closed.saturating_add(1)
        }
    }

    fn close(&mut self) {
        self.closed = self.including_the_open_turn();
        self.progressed = false;
    }
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Recorded turns in a row, already closed, that changed nothing.
    ///
    /// The turn now being walked is not in it until the next turn opens;
    /// [`Self::errand_stalled`] adds it.
    pub const fn turns_without_progress(&self) -> u8 {
        self.progress.closed
    }

    /// Whether anything has changed since the last recorded turn closed.
    pub const fn progressed_this_turn(&self) -> bool {
        self.progress.progressed
    }

    /// Whether this errand has gone [`MAX_TURNS_WITHOUT_PROGRESS`] turns
    /// without changing anything, counting the turn now being walked.
    ///
    /// An errand that has already achieved what it exists for is not stalled,
    /// exactly as every other errand bound has it: a download that started is
    /// the task working, whatever its turns do after.
    pub fn errand_stalled(&self) -> bool {
        self.is_web_errand()
            && self.progress.including_the_open_turn() >= MAX_TURNS_WITHOUT_PROGRESS
            && !self.errand_outcome_witnessed()
    }

    /// Something changed during the turn now open.
    pub(super) fn note_progress(&mut self) {
        if self.is_web_errand() {
            self.progress.progressed = true;
        }
    }

    /// Closes the recorded turn a new turn is about to replace.
    ///
    /// Called by `RequestModelTurn` before the turn is replaced, so the turn
    /// being closed is the one whose walk just finished and everything its
    /// calls did is already in. A turn with no readable reply is not closed:
    /// it had no calls to make anything change, and whatever happens before
    /// the next recorded turn belongs with that one. A task with no turn yet
    /// has nothing to close.
    pub(super) fn close_turn_for_progress(&mut self) {
        let closes_a_recorded_turn = self
            .turn
            .as_ref()
            .is_some_and(|turn| matches!(turn.phase(), TurnPhase::Recorded(_)));
        if self.is_web_errand() && closes_a_recorded_turn {
            self.progress.close();
        }
    }

    /// Counts one settled action by whether it changed anything.
    ///
    /// Only a verified action can have: a refusal, a failure and an unknown
    /// outcome left the page as it was, or cannot say it did not. `arrival` is
    /// the verdict [`Self::note_arrival`] already reads: `Some` for a move that
    /// landed on a site, `true` when the task already held it. An admission is
    /// a new site and is progress. An arrival where the task already was is
    /// not, whatever else is true of it — the browser verified the move and the
    /// task went nowhere, which is decision 0167's loop.
    pub(super) fn note_action_settled(&mut self, action_id: &ActionId, arrival: Option<bool>) {
        let verified = self
            .actions
            .get(action_id.as_str())
            .is_some_and(|action| action.state == ActionState::Verified);
        let changed = verified
            && match arrival {
                Some(already_there) => !already_there,
                None => self.verified_something_new(action_id),
            };
        if changed {
            self.note_progress();
        }
    }

    /// Whether the verified action `action_id` did something this task had
    /// not already done to the same thing.
    ///
    /// A reading never has: it changes nothing, and the module header says why
    /// a first reading does not count either. Anything else has, unless another
    /// verified record makes the same call on the same thing and its tab has
    /// not moved since. "The same call" is the repetition register's own
    /// fingerprint — tool, tab, and node or address — so the two counters
    /// agree about what a repeat is. "The same thing" adds the document a
    /// pressed node was read from where the intent names it, because node
    /// identities restart with every document and two pages' first link are
    /// otherwise one call.
    ///
    /// "Since its tab last moved" is `superseded_by_a_move`, which a verified
    /// move sets on the tab's settled records. A move does not mark itself, so
    /// the same reload, or the same navigation, made twice running is a
    /// repeat; made again after the task went somewhere else, it is not.
    fn verified_something_new(&self, action_id: &ActionId) -> bool {
        let Some(action) = self.actions.get(action_id.as_str()) else {
            return false;
        };
        let proposal = action.proposal();
        if proposal.action_class() == ActionClass::ObservePage {
            return false;
        }
        let call = Self::call_of(proposal);
        let document = observed_document(proposal.intent());
        !self.actions.values().any(|other| {
            other.action_id != *action_id
                && other.state == ActionState::Verified
                && !other.superseded_by_a_move
                && Self::call_of(&other.proposal) == call
                && observed_document(other.proposal.intent()) == document
        })
    }
}

/// The document an intent's target was read from, where the intent says.
///
/// Only the intents that carry the browser's frozen observation of their
/// target do; a form field names its node alone, and for those the tab having
/// moved is the only boundary there is.
fn observed_document(intent: &ActionIntent) -> Option<&PageEpoch> {
    let ActionIntent::Browser(intent) = intent else {
        return None;
    };
    match intent {
        BrowserIntent::DomClick { target, .. }
        | BrowserIntent::DomFocus { target }
        | BrowserIntent::LinkOpen { target }
        | BrowserIntent::DownloadFromLink { target, .. } => Some(target.page_epoch()),
        BrowserIntent::TabsActivate { target, .. } | BrowserIntent::TabsClose { target, .. } => {
            Some(target.page_epoch())
        }
        _ => None,
    }
}
