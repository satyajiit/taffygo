// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The three model-turn commands: asking, recording a reply, recording a gap.
//!
//! # Asking is where the money is spent, so it is where the budget is charged
//!
//! [`Reducer::on_request_model_turn`] charges `MaxModelRequests` before the
//! effect leaves, not when the answer comes back. That is what makes an
//! unknown outcome survivable: the call was paid for whether or not anything
//! was received, and a loop that asks again after an unanswered turn is
//! bounded by the same counter as every other loop. Charging on the reply
//! would give a task an unlimited number of calls it never heard back from.
//!
//! # The identity is derived and never supplied
//!
//! `Guard::ModelCallIsNext` refuses any call identity that is not the one
//! [`Reducer::next_model_call_id`] would derive. A replay therefore reaches
//! the same identity for the same turn, which is precisely what the browser's
//! effect-identity journal needs in order to refuse a second delivery of a
//! call it has already made — the guarantee is worthless if the thing
//! proposing the identity could have chosen a different one.
//!
//! # Recording holds shape and never content
//!
//! [`crate::agent::TurnDigest`] declares no string and no byte string, so
//! there is no field in the journal for page text, node names, tool arguments
//! or model prose to travel in. The reply itself lives in the arena for as
//! long as the turn needs it and is gone afterwards (decision 0052 section 5).

use super::outcome::Outcome;
use super::Reducer;
use crate::agent::{
    call_id_for_turn, ModelAttemptKind, ModelStopReason, ModelTurn, TurnDigest, TurnGap, TurnPhase,
};
use crate::budget::BudgetKind;
use crate::command::{Command, CommandKind};
use crate::effect::Effect;
use crate::event::{EventKind, TaskEvent};
use crate::ids::{IdSource, ModelCallId};
use crate::time::Clock;
use crate::transition::RefusalReason;

/// How many times in a row a turn may be asked again because the reply before
/// it could not be read or was cut off.
///
/// Two, so a call is made at most three times for one step. A reply that
/// cannot be read once is a provider's bad moment; one that cannot be read
/// three times running is a provider or a request that will not change on the
/// fourth, and every re-ask is a paid call. On 2026-09-18 a stream this build
/// could not read was asked for seventy-four times in three and a half
/// minutes, bounded by nothing but the errand's whole budget.
pub const MAX_CONSECUTIVE_REASKS: u8 = 2;

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Routes only the durable model-turn command family.
    pub(super) fn execute_model_command(
        &mut self,
        command: &Command,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        match command {
            Command::RequestModelTurn { call_id } => self.on_request_model_turn(call_id, kind),
            Command::RequestModelAttempt {
                call_id,
                attempt_ordinal,
                candidate_ordinal,
                kind: attempt_kind,
            } => self.on_request_model_attempt(
                call_id,
                *attempt_ordinal,
                *candidate_ordinal,
                *attempt_kind,
                kind,
            ),
            Command::RecordModelTurn { call_id, digest } => {
                self.on_record_model_turn(call_id, **digest, kind)
            }
            Command::RecordModelTurnGap { call_id, gap } => {
                self.on_record_model_turn_gap(call_id, *gap, kind)
            }
            Command::RecordContextEviction { through_turn } => {
                Ok(self.on_record_context_eviction(*through_turn, kind))
            }
            _ => Err(RefusalReason::JournalRefused),
        }
    }

    /// The turn this task is on, when it has started one.
    pub const fn model_turn(&self) -> Option<&ModelTurn> {
        self.turn.as_ref()
    }

    /// How many model turns this task has started, ever.
    ///
    /// It only ever increases, which is what makes a turn ordinal an identity
    /// rather than a position: the *n*-th turn of a task happens exactly once.
    pub const fn turns_started(&self) -> u64 {
        self.turns_started
    }

    /// The identity the next turn of this task would carry.
    ///
    /// Pure and derived from state the journal already reproduces, so a replay
    /// and the run it replays agree on it.
    pub fn next_model_call_id(&self) -> ModelCallId {
        call_id_for_turn(self.task.task_id(), self.turns_started)
    }

    /// Whether a call is outstanding with the browser.
    pub fn model_turn_in_flight(&self) -> bool {
        self.turn
            .as_ref()
            .is_some_and(|turn| turn.phase().is_in_flight())
    }

    /// The paid attempt in flight beyond the first call, if the call the
    /// browser is holding is a retry or a failover.
    pub fn model_attempt_in_flight(&self) -> Option<ModelAttemptKind> {
        self.turn
            .as_ref()
            .filter(|turn| turn.phase().is_in_flight())
            .and_then(ModelTurn::last_attempt)
    }

    /// Whether the turn in flight re-asks a question whose last answer could
    /// not be read or was cut off.
    pub const fn reply_being_reasked(&self) -> bool {
        self.reply_being_reasked
    }

    /// Whether the re-asks in a row have reached [`MAX_CONSECUTIVE_REASKS`],
    /// so the next unreadable or cut-off reply ends the task instead.
    pub const fn reasks_exhausted(&self) -> bool {
        self.consecutive_reasks >= MAX_CONSECUTIVE_REASKS
    }

    /// Whether the task's budgets admit one more model request.
    pub fn can_afford_a_model_turn(&self) -> bool {
        self.task
            .ledger
            .admit(
                BudgetKind::MaxModelRequests,
                1,
                &self.task.budgets,
                &self.defaults,
            )
            .is_ok()
    }

    /// Whether the in-flight logical turn may start one more paid attempt.
    pub fn can_afford_model_attempt(&self) -> bool {
        let Some(turn) = self
            .turn
            .as_ref()
            .filter(|turn| turn.phase().is_in_flight())
        else {
            return false;
        };
        let retries_started = u64::from(turn.attempts_started().saturating_sub(1));
        retries_started
            < self
                .task
                .budgets
                .effective_limit(BudgetKind::MaxRetriesPerStep, &self.defaults)
            && self.can_afford_a_model_turn()
    }

    pub(super) fn on_request_model_turn(
        &mut self,
        call_id: &ModelCallId,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        // The guard already refused an identity that is not the next one; this
        // re-derives rather than trusting the argument, so the turn the
        // reducer records and the call the browser is asked for cannot be two
        // different things.
        if *call_id != self.next_model_call_id() {
            return Err(RefusalReason::ModelCallNotNext);
        }
        let ordinal = self.turns_started;
        self.turns_started = self.turns_started.saturating_add(1);
        self.task.ledger.charge(BudgetKind::MaxModelRequests, 1);
        // Read before the turn is replaced: whether this request exists
        // because the last answer could not be used as one.
        self.reply_being_reasked = self.turn.as_ref().is_some_and(|turn| match turn.phase() {
            TurnPhase::Gap(gap) => matches!(gap, TurnGap::Unreadable),
            TurnPhase::Recorded(digest) => matches!(digest.stop, ModelStopReason::Length),
            TurnPhase::InFlight => false,
        });
        self.consecutive_reasks = if self.reply_being_reasked {
            self.consecutive_reasks.saturating_add(1)
        } else {
            0
        };
        // Also before the turn is replaced: the recorded turn this one follows
        // is closed here, with everything its calls did already in, so the run
        // of turns that changed nothing is advanced by the journal's own
        // boundary between two turns (decision 0233).
        self.close_turn_for_progress();
        self.turn = Some(ModelTurn::in_flight(call_id.clone(), ordinal));
        self.note_readings_used();
        Ok(Outcome::recorded(
            vec![TaskEvent::record(EventKind::BudgetCharged, kind)],
            vec![Effect::CallModel {
                call_id: call_id.clone(),
            }],
        ))
    }

    pub(super) fn on_request_model_attempt(
        &mut self,
        call_id: &ModelCallId,
        attempt_ordinal: u32,
        candidate_ordinal: u32,
        attempt_kind: ModelAttemptKind,
        command_kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        let Some(turn) = self.turn.as_mut().filter(|turn| turn.call_id() == call_id) else {
            return Err(RefusalReason::ModelTurnMismatch);
        };
        if !turn.start_attempt(attempt_ordinal, candidate_ordinal, attempt_kind) {
            return Err(RefusalReason::ModelTurnMismatch);
        }
        self.task.ledger.charge(BudgetKind::MaxModelRequests, 1);
        Ok(Outcome::recorded(
            vec![TaskEvent::record(EventKind::BudgetCharged, command_kind)],
            vec![Effect::CallModel {
                call_id: call_id.clone(),
            }],
        ))
    }

    /// Records what the model answered. The reply moves the turn's durable
    /// state, so it is journalled as an event: a commit is a revision step,
    /// and a command that moved the task while leaving the revision where it
    /// was is a commit storage refuses.
    pub(super) fn on_record_model_turn(
        &mut self,
        call_id: &ModelCallId,
        digest: TurnDigest,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        let Some(turn) = self.turn.as_mut().filter(|turn| turn.call_id() == call_id) else {
            return Err(RefusalReason::ModelTurnMismatch);
        };
        // Counted before the digest moves into the turn, and only after the
        // turn has been matched: a reply to some other call is refused above
        // and counts for nothing.
        let recorded = digest;
        turn.record(digest);
        self.reply_being_reasked = false;
        self.note_recorded_reply(&recorded);
        Ok(Outcome::recorded(
            vec![TaskEvent::record(EventKind::ModelTurnRecorded, kind)],
            Vec::new(),
        ))
    }

    /// Marks every current whole-document reading as one that did its job.
    ///
    /// A turn only opens once the pre-model prerequisite is satisfied, so every
    /// bootstrap read sitting in `Verified` at this moment is a reading the
    /// walk was able to use. `MAX_SOURCE_BOOTSTRAP_READS` is about a source
    /// that cannot be read, and a reading that opened a paid turn is not
    /// evidence of that — it is the opposite.
    ///
    /// The bound counted every read of a source for the life of the task,
    /// including the ones that worked, so an errand that did four things on one
    /// page ran out of them. A phone ran into it on 2026-09-19: six readings of
    /// the myAadhaar download page, nine model turns served from them, three
    /// clicks and a query in between — and the errand then reported that it
    /// could not read enough to answer, about the page it had just spent a
    /// minute working on (decision 0197).
    fn note_readings_used(&mut self) {
        for action in self.actions.values_mut() {
            if action.state == crate::action::ActionState::Verified
                && !action.reading_was_used
                && crate::agent::is_agent_observation(&action.proposal)
            {
                action.reading_was_used = true;
            }
        }
    }

    /// Records that the model did not answer. An event for the same reason as
    /// [`Self::on_record_model_turn`]: the gap is what the next agent-table
    /// row reads, so it has to be a durable step of its own.
    pub(super) fn on_record_model_turn_gap(
        &mut self,
        call_id: &ModelCallId,
        gap: TurnGap,
        kind: CommandKind,
    ) -> Result<Outcome, RefusalReason> {
        let Some(turn) = self.turn.as_mut().filter(|turn| turn.call_id() == call_id) else {
            return Err(RefusalReason::ModelTurnMismatch);
        };
        turn.record_gap(gap);
        self.reply_being_reasked = false;
        Ok(Outcome::recorded(
            vec![TaskEvent::record(EventKind::ModelTurnGapRecorded, kind)],
            Vec::new(),
        ))
    }

    /// The highest turn ordinal durably evicted from the context, if any.
    ///
    /// The transcript builder drops every exchange at or below this boundary
    /// before its byte ladder runs, which is what makes the shrunken
    /// conversation a durable fact a replay reproduces rather than a
    /// projection that ran once.
    pub const fn evicted_through(&self) -> Option<u64> {
        self.evicted_through
    }

    pub(super) fn on_record_context_eviction(
        &mut self,
        through_turn: u64,
        kind: CommandKind,
    ) -> Outcome {
        // Range and monotonicity are the guard's; by the time this applies,
        // the boundary is a later one over turns that exist. The boundary is
        // durable state the transcript builder reads, so it is an event.
        self.evicted_through = Some(through_turn);
        Outcome::recorded(
            vec![TaskEvent::record(EventKind::ContextEvicted, kind)],
            Vec::new(),
        )
    }

    /// Notes that this turn has reached for the person.
    ///
    /// Called from the user-input path rather than from the agent table,
    /// because the fact worth remembering is that the command was *applied*:
    /// the flag is then re-derived by a replay from the journalled command
    /// rather than restored from a snapshot of a decision.
    pub(super) fn note_turn_asked_person(&mut self) {
        if let Some(turn) = self.turn.as_mut() {
            turn.note_person_asked();
        }
    }
}
