// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The decision table: what the reducer does next, for every shape a turn can
//! be in.
//!
//! # The table
//!
//! Read top to bottom; the first row that matches is the answer. Every row is
//! a pure function of the task's durable state and the turn's residency, so
//! the same pair always produces the same command.
//!
//! | # | The task | The turn | The reply | Answer |
//! |---|---|---|---|---|
//! | 1 | terminal | any | any | nothing |
//! | 2 | `QUEUED` | any | any | `ExecutorStarted` |
//! | 3a | `PAUSING` or `CANCELLING` | in flight, residency matches | any | `RecordModelTurn`, and nothing after it: the settlement waits for the turn to close |
//! | 3 | not `RUNNING` | any | any | nothing |
//! | 4 | `RUNNING` | none yet | — | `RequestModelTurn`, or `FailTask` when no turn is affordable |
//! | 5 | `RUNNING` | in flight, no residency | — | nothing: the browser is holding a paid call |
//! | 6 | `RUNNING` | in flight, residency for another call | — | `ResidencyMismatch` |
//! | 7 | `RUNNING` | in flight, residency matches | any | `RecordModelTurn`, carrying the shape and no content |
//! | 8 | `RUNNING` | gap `Cancelled` | — | nothing: settling owns the task from here |
//! | 9 | `RUNNING` | gap `Refused` or `Unavailable` | — | `FailTask(ProviderUnavailable)` |
//! | 10 | `RUNNING` | gap `OutcomeUnknown` | — | `PauseTask(NoAnswer)`; never mint another paid identity, and never throw away the work either — a resume mints the next one, and only a person can ask for it |
//! | 10a | `RUNNING` | gap `Unreadable` | — | a fresh turn, at most [`MAX_CONSECUTIVE_REASKS`](crate::MAX_CONSECUTIVE_REASKS) in a row, then `FailTask(ProviderUnavailable)` |
//! | 11 | `RUNNING` | recorded, an action of this turn still in flight | any | nothing: one call at a time |
//! | 12 | `RUNNING` | recorded, this turn already reached the person | any | a fresh turn — unless [`MAX_UNANSWERED_VALUE_ASKS`] asks for values in a row have come back with nothing, or row 21k's bound is reached, and then the ending its own facts name |
//! | 12a | `RUNNING` | recorded, this turn already reached the person, and the person's held values name their fields | any | before row 12's fresh turn: the next held value's fill, one at a time and each position once; nothing while one is in flight; the fresh turn once every fill has verified or the first one did not (decision 0238) |
//! | 13 | `RUNNING` | recorded | `Complete`, no calls | `ResultCandidateReady`; **an errand with no verified outcome yet** gets a fresh turn instead, at most [`MAX_UNPRODUCTIVE_ERRAND_REPLIES`] times and under row 21k's bound, then the ending its own facts name: `PartialResultValidated` if it read anything, else `FailTask(PolicyRefused)` if its moves were refused, else `FailTask(SourcesUnavailable)` |
//! | 14 | `RUNNING` | recorded | `Complete` **with** calls | `ContradictoryReply` |
//! | 15 | `RUNNING` | recorded | `Error` | `FailTask(ProviderUnavailable)` |
//! | 16 | `RUNNING` | recorded | `ProviderStop` | `RequestUserInput` |
//! | 17 | `RUNNING` | recorded | `Length`, **with** calls | a fresh turn under row 10a's bound; **every call is `NotAttempted(TruncatedArguments)`** |
//! | 18 | `RUNNING` | recorded | `Length`, no calls | a fresh turn under row 10a's bound |
//! | 19 | `RUNNING` | recorded | `ToolCall`, no calls | `ContradictoryReply` |
//! | 20 | `RUNNING` | recorded, residency gone | `ToolCall` | a fresh turn: the arena died with the process and the reply cannot be reconstructed |
//! | 21 | `RUNNING` | recorded, residency present | `ToolCall` | walk the calls — see below |
//! | 22 | `COMPLETING` | any | any | `CompleteResultValidated` from durable source counts |
//!
//! The walk of row 21, in the order the provider declared the calls:
//!
//! | # | The call | Answer |
//! |---|---|---|
//! | 21a | not attemptable | skip it; **every call after it is `NotAttempted(PriorCallRefused)`** |
//! | 21b | attemptable, never proposed | propose it, and stop |
//! | 21c | attemptable, proposed, still in flight | nothing — but row 11 has already caught this |
//! | 21d | attemptable, proposed, verified | move to the next call |
//! | 21e | attemptable, proposed, ended any other way | the turn is over; a fresh turn, under row 21k's bound |
//! | 21f | no call left | a fresh turn (the default): the model is told what happened and re-plans, under row 21k's bound |
//! | 21g | attemptable `Loop`, no residency outcome | nothing: the runtime settles it, then asks again |
//! | 21h | attemptable `Loop`, settled | treat as verified; move to the next call |
//! | 21i | walk finished; every call attemptable and `terminal_safe`; `answer_segments > 0` | `ResultCandidateReady` |
//! | 21j | walk finished; [`MAX_TURNS_ATTEMPTING_NOTHING`] turns in a row had every call refused on sight | the ending its own facts name |
//! | 21k | an errand about to be asked again (rows 12, 13, 21e, 21f); [`MAX_TURNS_WITHOUT_PROGRESS`](crate::MAX_TURNS_WITHOUT_PROGRESS) turns in a row, this one included, changed nothing | the ending its own facts name |
//!
//! # The two rows that keep the model honest
//!
//! Row 17 and row 21a are the reason this table is written down rather than
//! inferred.
//!
//! **Row 17.** A `Length` ending means the reply stopped at the token
//! allowance. Tool calls in such a reply are a *prefix of an intention*: some
//! parsed, some did not, and there is no way to tell which of the ones that
//! parsed were the whole of what the model meant to say. Running the ones that
//! happen to be well formed executes part of a sentence, and the part that ran
//! is the part that reached the page.
//!
//! **Row 21a.** The model ordered its calls, and a later call may depend on an
//! earlier one having happened — read this field, then click that button.
//! Refusing the third and running the fourth is running a plan whose
//! precondition was refused.
//!
//! # Completing is a durable claim, not a persistence of prose
//!
//! Row 13 records that the model finished without tool calls
//! (`ResultCandidateReady`). Row 21i is the other way a result is offered:
//! the walk already ran every call, every call is `terminal_safe`, and the
//! reply already carried a visible answer, so asking the model to restate it
//! is a follow-up that does not exist. Taffy never journals the prose;
//! `answer_segments` is the signal that it was there. Row 22 then validates
//! that candidate as a complete result. The counts come from the sources the
//! task already consented to; the model's last sentence is not a field of
//! [`crate::task::TaskResult`] and is not invented here. That is the same
//! move the reviewed workflow already makes from `COMPLETING`: a result with
//! no unmet requirement, built from facts the journal already holds.

use super::ask_view::ViewStep;
use super::dispatch::{artifact_id_for_call, turn_call_key};
use super::held_values::PlacementStep;
use super::r#loop::loop_entry_for;
use super::reply::{CallVerdict, TurnResidency};
use super::turn::{ModelStopReason, ModelTurn, TurnGap, TurnPhase};
use super::AgentError;
use crate::action::{ActionRecord, ActionState};
use crate::command::{Command, PauseCause};
use crate::ids::IdSource;
use crate::reducer::{
    Reducer, MAX_FRUITLESS_ERRAND_ARRIVALS, MAX_TURNS_ATTEMPTING_NOTHING,
    MAX_UNANSWERED_VALUE_ASKS, MAX_UNPRODUCTIVE_ERRAND_REPLIES,
};
use crate::task::{FailureReason, TaskKind, TaskResult, TaskState};
use crate::time::Clock;
use crate::workflow::WorkflowDigest;

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// The next replayable command for the assistant loop, or `None` when the
    /// loop is waiting on something it does not own.
    ///
    /// Pure. It takes `&self`, mints nothing, applies nothing, and reads no
    /// clock. `residency` is the turn's transient half — the reply and the
    /// projection it answered — and it is an argument rather than reducer
    /// state precisely because it is not durable: the journal holds the shape
    /// of a turn and never its content, so a replay reconstructs the state
    /// this function reads without reconstructing any prose.
    pub fn next_agent_command(
        &self,
        residency: Option<&TurnResidency>,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, AgentError> {
        if self.task().state().is_terminal() {
            return Ok(None);
        }
        match self.task().state() {
            TaskState::Queued => Ok(Some(Command::ExecutorStarted)),
            TaskState::Running => self.agent_running(residency, digest),
            TaskState::Completing => Ok(Some(self.agent_complete_result())),
            TaskState::Pausing | TaskState::Cancelling => self.agent_settling(residency),
            TaskState::Draft
            | TaskState::AwaitingConsent
            | TaskState::WaitingUser
            | TaskState::Paused
            | TaskState::Cancelled
            | TaskState::Completed
            | TaskState::Partial
            | TaskState::Failed => Ok(None),
        }
    }

    fn agent_running(
        &self,
        residency: Option<&TurnResidency>,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, AgentError> {
        let Some(turn) = self.model_turn() else {
            return Ok(Some(self.agent_next_turn()));
        };
        match turn.phase() {
            TurnPhase::InFlight => self.agent_awaiting_reply(turn, residency),
            TurnPhase::Gap(gap) => Ok(self.agent_after_gap(gap)),
            TurnPhase::Recorded(recorded) => {
                self.agent_after_reply(turn, recorded.stop, recorded.tool_calls, residency, digest)
            }
        }
    }

    /// A pause or a stop under way, with a reply to the turn it interrupted
    /// already read: record it, and nothing else.
    ///
    /// Both settlements wait for the turn to close (`Guard::NoModelTurnInFlight`),
    /// and a reply that finished streaming before the browser cancelled the
    /// call is never gapped. Answering nothing here left the turn open for
    /// good: on a phone on 2026-09-18 **Take over** was accepted, the reply
    /// arrived and was read, `PauseSettled` was refused as `invalid_command`,
    /// and the task showed "Taffy is thinking…" with nothing left to happen
    /// (verification report, section 2.48). Recording proposes nothing — the
    /// calls it carries are the settled task's to make after a resume, not
    /// this row's.
    fn agent_settling(
        &self,
        residency: Option<&TurnResidency>,
    ) -> Result<Option<Command>, AgentError> {
        match self.model_turn() {
            Some(turn) if turn.phase().is_in_flight() && residency.is_some() => {
                self.agent_awaiting_reply(turn, residency)
            }
            _ => Ok(None),
        }
    }

    /// Row 5 to row 7: a paid call is outstanding.
    fn agent_awaiting_reply(
        &self,
        turn: &ModelTurn,
        residency: Option<&TurnResidency>,
    ) -> Result<Option<Command>, AgentError> {
        let Some(residency) = residency else {
            return Ok(None);
        };
        if residency.call_id() != turn.call_id() {
            return Err(AgentError::ResidencyMismatch);
        }
        Ok(Some(Command::RecordModelTurn {
            call_id: turn.call_id().clone(),
            digest: Box::new(residency.digest(&self.turn_dispositions(residency))),
        }))
    }

    /// Rows 8 to 10.
    ///
    /// An unreadable known reply may earn another logical turn. An unknown
    /// outcome may not: the provider could have completed and billed it, and
    /// minting a fresh identity would bypass the durable claim that prevents
    /// the original paid work from running twice after a disconnect.
    fn agent_after_gap(&self, gap: TurnGap) -> Option<Command> {
        match gap {
            // The pause or the stop that cancelled the turn owns the task from
            // here. Proposing anything would be this function competing with a
            // settlement that is already under way.
            TurnGap::Cancelled => None,
            TurnGap::Refused | TurnGap::Unavailable => Some(Command::FailTask {
                reason: FailureReason::ProviderUnavailable,
            }),
            // The request left and nothing came back. The task is held rather
            // than failed, and the two are not the same trade (decision 0217).
            //
            // Failing was read as the cautious answer because of the rule
            // below it — never mint another paid identity for a call that may
            // already have been billed. A pause keeps that rule exactly: it
            // mints nothing, dispatches nothing and leaves the gap in the
            // ledger saying it does not know. What it does not do is throw
            // away every verified action the task already has, which failing
            // did — and a person whose errand died at move sixty starts it
            // again from nothing, which costs *more* paid calls than the one
            // resume this offers. The identity is minted only if they ask.
            TurnGap::OutcomeUnknown => Some(Command::PauseTask {
                cause: PauseCause::NoAnswer,
            }),
            TurnGap::Unreadable => Some(self.agent_reask()),
        }
    }

    /// Rows 10a, 17 and 18: the last reply could not be used, so the model is
    /// asked again — at most [`MAX_CONSECUTIVE_REASKS`](crate::MAX_CONSECUTIVE_REASKS) times in a row, and
    /// then the task ends. A reply the product cannot read, or one cut off at
    /// the same allowance, comes back the same way on the next paid call.
    fn agent_reask(&self) -> Command {
        if self.reasks_exhausted() {
            return Command::FailTask {
                reason: FailureReason::ProviderUnavailable,
            };
        }
        self.agent_next_turn()
    }

    /// Rows 11 to 21.
    fn agent_after_reply(
        &self,
        turn: &ModelTurn,
        stop: ModelStopReason,
        tool_calls: u32,
        residency: Option<&TurnResidency>,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, AgentError> {
        if self.turn_has_work_in_flight(turn) {
            return Ok(None);
        }
        if turn.asked_person() {
            // Row 12, and the one bound on it. Reaching for a person is not a
            // paid call's worth of progress by itself: the sheet may have been
            // closed, drawn over a line that takes no value, or never drawn at
            // all, and each of those comes back as a turn that did nothing and
            // earns another turn that can do the same. Three in a row with no
            // value from the person is the model out of moves here, and a
            // fourth is charged to the person's budget to learn it again
            // (decision 0216). An outcome that was witnessed keeps the task
            // going, exactly as the other three counters have it: an errand
            // that has already downloaded what it came for is not stalled.
            if self.unanswered_value_asks() >= MAX_UNANSWERED_VALUE_ASKS
                && !self.errand_outcome_witnessed()
            {
                return Ok(Some(self.agent_errand_gave_up()));
            }
            // Row 12a. The person's values go into the fields they were
            // minted for, one at a time, before the model is asked again:
            // everything a fill needs is already known, and a turn spent
            // reproducing it from a snapshot is a turn the model spent on
            // everything else on the page instead (decision 0238).
            match self.next_held_value_fill(digest)? {
                PlacementStep::Propose(command) => return Ok(Some(command)),
                PlacementStep::Waiting => return Ok(None),
                PlacementStep::Done => {}
            }
            // A value from the person is progress and clears row 21k's run; an
            // ask that came back empty is one more turn of it.
            return Ok(Some(self.agent_turn_again()));
        }
        match stop {
            ModelStopReason::Complete => {
                if tool_calls != 0 {
                    return Err(AgentError::ContradictoryReply);
                }
                Ok(Some(self.agent_after_prose_reply(residency)))
            }
            ModelStopReason::Error => Ok(Some(Command::FailTask {
                reason: FailureReason::ProviderUnavailable,
            })),
            // The provider stopped the model for a reason of its own, and the
            // product has no vocabulary for it. Asking again would be asking
            // the same provider the same thing; the honest move is to stop and
            // let the person decide.
            ModelStopReason::ProviderStop => Ok(Some(Command::RequestUserInput)),
            // Row 17. Nothing in a truncated reply is attempted, whether or
            // not some of it parsed.
            ModelStopReason::Length => Ok(Some(self.agent_reask())),
            ModelStopReason::ToolCall => {
                if tool_calls == 0 {
                    return Err(AgentError::ContradictoryReply);
                }
                self.agent_walk_calls(turn, residency, digest)
            }
        }
    }

    /// Row 21: one call at a time, in the order the provider declared them.
    ///
    /// When the walk finishes, row 21i offers a result if every call was
    /// attemptable and `terminal_safe` and the reply already carried a visible
    /// answer; otherwise row 21f asks the model again.
    fn agent_walk_calls(
        &self,
        turn: &ModelTurn,
        residency: Option<&TurnResidency>,
        digest: &dyn WorkflowDigest,
    ) -> Result<Option<Command>, AgentError> {
        // Row 20. The reply lived in the arena and the arena died with the
        // process. The turn's digest says it was paid for and what shape it
        // was, which is what stops the call being made a second time; the
        // calls themselves are gone, and inventing them is not on the list of
        // things this function may do.
        let Some(residency) = residency else {
            return Ok(Some(self.agent_next_turn()));
        };
        if residency.call_id() != turn.call_id() {
            return Err(AgentError::ResidencyMismatch);
        }
        for disposition in self.turn_dispositions(residency) {
            match disposition.verdict {
                // Row 21a. It was refused on sight, and everything after it is
                // already `PriorCallRefused` in the list being walked.
                CallVerdict::NotAttempted(_) => continue,
                CallVerdict::Attemptable => {}
            }
            let milestone = self.task().snapshot().milestone;
            if loop_entry_for(milestone, residency, disposition.sequence).is_some() {
                // Rows 21g and 21h. Loop calls never become an action record:
                // a missing outcome means the runtime still has to settle, and
                // a recorded one is verified for the rest of this walk.
                if residency.loop_outcome(disposition.sequence).is_none() {
                    return Ok(None);
                }
                continue;
            }
            let artifact_id = artifact_id_for_call(turn.ordinal(), disposition.sequence);
            let is_artifact = residency
                .call(disposition.sequence)
                .and_then(|call| crate::tool::resolve(&call.tool_name, milestone).entry())
                .is_some_and(|entry| {
                    matches!(entry.dispatch, crate::tool::ToolDispatch::Artifact(_))
                });
            if is_artifact {
                if self.artifact(&artifact_id).is_some() {
                    continue;
                }
                return self
                    .attempt_call(turn, residency, disposition.sequence, digest)
                    .map(Some);
            }
            match self.turn_action(turn.ordinal(), disposition.sequence) {
                // Row 21b. An ask for values whose sheet would show a
                // challenge is preceded by the one scroll that brings it into
                // view, because the sheet copies its picture from the part of
                // the page in view and leaves it off otherwise (decision
                // 0240).
                None => {
                    match self.next_ask_view(
                        turn.ordinal(),
                        residency,
                        disposition.sequence,
                        digest,
                    )? {
                        ViewStep::Propose(command) => return Ok(Some(command)),
                        ViewStep::Waiting => return Ok(None),
                        ViewStep::Ask => {}
                    }
                    return self
                        .attempt_call(turn, residency, disposition.sequence, digest)
                        .map(Some);
                }
                // Row 21d. It happened, so the next call in the order the
                // model gave is the one to look at.
                Some(action) if action.state() == ActionState::Verified => {}
                // Row 21c: row 11 caught it, and this is the honest answer if
                // it ever does not.
                Some(action) if action.state().may_have_reached_the_page() => return Ok(None),
                // Row 21e. Refused, failed, rejected, cancelled, or of unknown
                // outcome — the calls the model ordered after this one are
                // `PriorCallRefused`, so the turn is over and the model is
                // told — unless row 21k says this errand has been told enough.
                Some(_) => return Ok(Some(self.agent_turn_again())),
            }
        }
        if self.terminal_safe_turn(residency) {
            // Row 21i.
            Ok(Some(Command::ResultCandidateReady))
        } else if self.is_web_errand()
            && self.turns_attempting_nothing() >= MAX_TURNS_ATTEMPTING_NOTHING
            && !self.errand_outcome_witnessed()
        {
            // Row 21j. Every call of this turn was refused before it left the
            // process, and so was every call of the turns before it. The model
            // has been told which clause each time and is still not reaching
            // the page, so the person gets the ending its own facts name
            // rather than another turn charged to their budget (decision 0198).
            Ok(Some(self.agent_errand_gave_up()))
        } else {
            // Row 21f, under row 21k's bound.
            Ok(Some(self.agent_turn_again()))
        }
    }

    /// Whether the walk that just finished may offer a result instead of
    /// asking the model again.
    ///
    /// Every call must have been attemptable, every registry row must be
    /// `terminal_safe`, and the reply must already have carried a visible
    /// answer. Settlement is guaranteed by reaching the end of
    /// [`Self::agent_walk_calls`]. The prose is never read: `answer_segments`
    /// is the signal. Unknown names are not `terminal_safe`.
    pub(super) fn terminal_safe_turn(&self, residency: &TurnResidency) -> bool {
        let dispositions = self.turn_dispositions(residency);
        if !dispositions
            .iter()
            .all(|disposition| disposition.verdict.is_attemptable())
        {
            return false;
        }
        let milestone = self.task().snapshot().milestone;
        names_are_safe(residency, |name| {
            crate::tool::resolve(name, milestone)
                .entry()
                .is_some_and(|entry| entry.terminal_safe)
        })
    }

    /// Row 13, and the errand exception to it.
    ///
    /// A research task's prose reply is its answer, so the result is offered.
    /// An errand's prose reply is its answer only once the browser has verified
    /// an outcome the errand exists for; before that it is narration, and the
    /// model is asked again with the nudge in its opening turn — at most
    /// [`MAX_UNPRODUCTIVE_ERRAND_REPLIES`] times. The reply's words are never
    /// read here; the reducer counted the shape when it recorded the turn.
    ///
    /// What happens at the bound is [`Self::agent_errand_gave_up`], and it used
    /// to be one sentence for every reason.
    fn agent_after_prose_reply(&self, residency: Option<&TurnResidency>) -> Command {
        if !self.is_web_errand()
            || (self.errand_outcome_witnessed() && self.errand_downloads_complete(residency))
        {
            return Command::ResultCandidateReady;
        }
        if self.unproductive_replies() <= MAX_UNPRODUCTIVE_ERRAND_REPLIES
            && self.fruitless_arrivals() < MAX_FRUITLESS_ERRAND_ARRIVALS
        {
            self.agent_turn_again()
        } else {
            self.agent_errand_gave_up()
        }
    }

    /// How an errand ends when it will not act, in the words that are true.
    ///
    /// Three endings, because there are three different things that happen and
    /// the product reported all of them as "Taffy could not read enough to
    /// answer". A phone's journal had seven tasks end that way; in every one
    /// the pages had been read perfectly well, which is the opposite of what
    /// the sentence said.
    ///
    /// * It read pages and could not act on them: that is a partial result,
    ///   not a failure, and [`TaskState::Partial`] with one labelled gap is
    ///   what the product already shows as "Partly done".
    /// * It read nothing and its moves were refused: the reason is the
    ///   refusals. [`FailureReason::PolicyRefused`] is declared, persisted,
    ///   projected and carries shipped copy — "Couldn't finish — Taffy's moves
    ///   were refused" — and nothing anywhere constructed it until here.
    /// * It read nothing and nothing was refused: sources really were
    ///   unavailable, which is what that reason has always meant.
    fn agent_errand_gave_up(&self) -> Command {
        if self.sources_were_read() {
            // Through the ordinary door: `ResultCandidateReady` moves the task
            // to `COMPLETING`, and row 22 builds the result there — with the
            // gap, because [`Self::errand_outcome_witnessed`] is still false.
            // A result command from `RUNNING` is refused `NotCompleting`, and
            // rightly: validation is a step of its own.
            return Command::ResultCandidateReady;
        }
        Command::FailTask {
            reason: if self.moves_were_refused() {
                FailureReason::PolicyRefused
            } else {
                FailureReason::SourcesUnavailable
            },
        }
    }

    /// Row 22: the model already finished, so the result is the sources this
    /// task already consented to and no unmet requirement.
    ///
    /// Built from durable counts, never from the last sentence. The journal
    /// already recorded `stop = Complete` and zero tool calls; this command
    /// is the validation of that shape, the same way the reviewed workflow
    /// validates a complete observation.
    fn agent_complete_result(&self) -> Command {
        let source_count = u64::try_from(self.task().scope().included().len()).unwrap_or(u64::MAX);
        let mut result = TaskResult {
            artifact_ids: self
                .artifacts()
                .map(|artifact| artifact.artifact_id().clone())
                .collect(),
            unmet: Vec::new(),
            fact_count: 0,
            source_count,
        };
        // An errand that gave up reaches here too: row 13 sends one that read
        // pages and would not act through this door rather than failing it,
        // and the thing the errand exists for still did not happen. That is
        // one labelled gap on a partial result — "Partly done" — rather than a
        // completion, and rather than the claim that it could not read.
        //
        // Keyed on the template, like every other errand rule in this table,
        // because the walk is what an errand is; `TaskKind` says what the
        // result may be missing, which is the question below.
        if self.is_web_errand() && !self.errand_outcome_witnessed() {
            result.unmet.push(crate::template::errand_outcome_gap());
            return Command::PartialResultValidated(result);
        }
        match self.task().kind() {
            TaskKind::Research if result.fact_count == 0 && result.artifact_ids.is_empty() => {
                result
                    .unmet
                    .push(crate::template::empty_research_result_gap());
                Command::PartialResultValidated(result)
            }
            // An errand reached here after a verified outcome, and an outcome
            // is what it was for: complete, not partial.
            TaskKind::Research | TaskKind::Errand => Command::CompleteResultValidated(result),
        }
    }

    /// Row 21k: ask the model again, unless this errand has gone
    /// [`MAX_TURNS_WITHOUT_PROGRESS`](crate::MAX_TURNS_WITHOUT_PROGRESS)
    /// turns, this one included, without changing anything.
    ///
    /// Every bound above it counts one shape of turn, and a loop made of two
    /// shapes clears each of them on every second turn: a reading of an
    /// unchanged page, then a click refused on sight, then the same reading
    /// again. Row 21j resets on the reading, because a reading is attempted.
    /// This counts what both strokes share — nothing changed — and ends the
    /// errand with the same three endings every other bound reaches rather
    /// than a new one, because what the person needs to know is the same:
    /// what was read, and that the errand did not get done (decision 0233).
    ///
    /// Rows 12, 13, 21e and 21f come through here. Rows 10a, 17 and 18 do not:
    /// a reply that could not be read is the provider's failure, and it has its
    /// own bound and its own ending. A research task is never stalled.
    fn agent_turn_again(&self) -> Command {
        if self.errand_stalled() {
            return self.agent_errand_gave_up();
        }
        self.agent_next_turn()
    }

    /// Rows 4, 10, 17, 18 and 20, and rows 12, 13, 21e and 21f when row 21k
    /// lets them through: ask the model again, if the task can still afford
    /// to.
    ///
    /// The budget is what bounds every loop in this table. A task that cannot
    /// afford another turn is not stuck waiting for one — it fails, and says
    /// which limit it reached.
    fn agent_next_turn(&self) -> Command {
        if self.can_afford_a_model_turn() {
            Command::RequestModelTurn {
                call_id: self.next_model_call_id(),
            }
        } else {
            Command::FailTask {
                reason: FailureReason::BudgetExhausted,
            }
        }
    }

    /// The action that attempted the `sequence`-th call of turn `ordinal`.
    pub(super) fn turn_action(&self, ordinal: u64, sequence: u32) -> Option<&ActionRecord> {
        let key = turn_call_key(ordinal, sequence);
        self.actions()
            .find(|action| action.proposal().idempotency_key == key)
    }

    /// Whether any call of `turn` is still somewhere between proposal and a
    /// terminal outcome.
    ///
    /// Matched by the key prefix rather than by walking the residency, so the
    /// answer holds when the residency is gone. The `-call-` delimiter is what
    /// keeps turn 1 from claiming turn 10's actions.
    pub(super) fn turn_has_work_in_flight(&self, turn: &ModelTurn) -> bool {
        let prefix = format!("turn-{}-call-", turn.ordinal());
        self.actions().any(|action| {
            action
                .proposal()
                .idempotency_key
                .as_str()
                .starts_with(&prefix)
                && !action.state().is_terminal()
        })
    }
}

/// The name-and-visible-answer half of row 21i.
///
/// [`Reducer::terminal_safe_turn`] also requires every call to have been
/// attemptable; that half is a function of the task, so it stays on the
/// reducer. Production always passes a registry lookup; tests pass a
/// predicate so the true path can be named without a `terminal_safe` row.
fn names_are_safe(residency: &TurnResidency, is_safe: impl Fn(&str) -> bool) -> bool {
    if residency.reply().answer_segments == 0 {
        return false;
    }
    let calls = &residency.reply().tool_calls;
    !calls.is_empty() && calls.iter().all(|call| is_safe(&call.tool_name))
}

#[cfg(test)]
mod tests {
    use super::super::reply::{ModelReply, ModelToolCall, TurnPage, TurnResidency};
    use super::super::turn::{ModelStopReason, RenderShape, TurnUsage};
    use super::names_are_safe;
    use crate::authority::{ControlMode, PolicyVersion};
    use crate::budget::{BudgetDefaults, TaskBudgets};
    use crate::handle::HandleTable;
    use crate::ids::{IdempotencyKey, ModelCallId, SequentialIds};
    use crate::reducer::{Reducer, TaskSeed};
    use crate::task::{BrowserSessionId, TaskKind, TaskSnapshot, TaskTemplateId};
    use crate::time::{ManualClock, TraceId};
    use crate::tool::{self, ArgumentValue, Milestone, SuppliedArgument};
    use bip_types::identity::{ProfileId, TabId, TaskId};

    fn reducer() -> Reducer<ManualClock, SequentialIds> {
        Reducer::create(
            TaskSeed {
                task_id: TaskId::new("task_1"),
                workspace_id: None,
                browser_profile_id: ProfileId::new("profile_1"),
                kind: TaskKind::Research,
                user_goal: "goal".to_owned(),
                control_mode: ControlMode::Assistant,
                snapshot: TaskSnapshot {
                    template_id: TaskTemplateId::CompareProducts,
                    assistant_config_version: 1,
                    skill_version_id: None,
                    builtin_skill: None,
                    tool_allowlist: Vec::new(),
                    capability_policy_version: PolicyVersion(1),
                    provider_route: None,
                    consented_sources: Vec::new(),
                    source_discovery_enabled: false,
                    remaining_new_source_cap: 0,
                    discovery_tab_id: None,
                    library_refresh: None,
                    browser_session_id: BrowserSessionId::new("browser_session_1")
                        .expect("a session id"),
                    milestone: Milestone::M3,
                },
                budgets: TaskBudgets::none(),
                deadline: None,
                deadline_utc: None,
                predecessor_task_id: None,
            },
            BudgetDefaults::uniform(64),
            ManualClock::at(1_000),
            SequentialIds::new(),
            IdempotencyKey::new("key_create"),
            TraceId::new("trace_test"),
        )
    }

    fn residency(answer_segments: u32, calls: Vec<ModelToolCall>) -> TurnResidency {
        let page = TurnPage::new(
            TabId::new("tab_1"),
            HandleTable::new(),
            RenderShape::empty([0; 32]),
        );
        TurnResidency::read(
            ModelCallId::new("model-task-1"),
            page,
            ModelReply {
                stop: ModelStopReason::ToolCall,
                overflow: None,
                usage: TurnUsage::default(),
                answer_segments,
                tool_calls: calls,
            },
        )
        .expect("a readable reply")
    }

    fn search_call() -> ModelToolCall {
        ModelToolCall::new(
            tool::SEARCH_TOOLS,
            vec![SuppliedArgument::new(
                "query",
                ArgumentValue::Text("pdf".to_owned()),
            )],
        )
    }

    fn registry_safe(name: &str) -> bool {
        tool::resolve(name, Milestone::M3)
            .entry()
            .is_some_and(|entry| entry.terminal_safe)
    }

    #[test]
    fn a_registered_ordinary_name_is_not_terminal_safe() {
        let residency = residency(1, vec![search_call()]);
        assert!(!names_are_safe(&residency, registry_safe));
        assert!(!reducer().terminal_safe_turn(&residency));
    }

    #[test]
    fn names_are_safe_when_every_name_matches_and_the_answer_is_visible() {
        let residency = residency(1, vec![ModelToolCall::new("display.note", Vec::new())]);
        assert!(names_are_safe(&residency, |name| name == "display.note"));
        assert!(!names_are_safe(&residency, |_| false));
    }

    #[test]
    fn names_are_safe_is_false_without_a_visible_answer() {
        let silent_note = residency(0, vec![ModelToolCall::new("display.note", Vec::new())]);
        assert!(!names_are_safe(&silent_note, |_| true));
        // An ordinary loop tool is attemptable; the missing answer is what
        // keeps row 21i from firing.
        let silent_search = residency(0, vec![search_call()]);
        assert!(!reducer().terminal_safe_turn(&silent_search));
    }

    #[test]
    fn names_are_safe_is_false_when_any_name_is_ordinary() {
        let residency = residency(
            1,
            vec![
                ModelToolCall::new("display.note", Vec::new()),
                search_call(),
            ],
        );
        assert!(!names_are_safe(&residency, |name| name == "display.note"));
    }

    #[test]
    fn names_are_safe_is_false_for_an_unknown_name() {
        let residency = residency(1, vec![ModelToolCall::new("not.a.tool", Vec::new())]);
        assert!(!names_are_safe(&residency, registry_safe));
    }

    #[test]
    fn names_are_safe_is_false_with_no_calls() {
        let residency = residency(1, Vec::new());
        assert!(!names_are_safe(&residency, |_| true));
        assert!(!reducer().terminal_safe_turn(&residency));
    }
}
