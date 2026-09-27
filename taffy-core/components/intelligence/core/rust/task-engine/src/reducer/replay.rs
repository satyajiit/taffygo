// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Rebuilding a task from its journal (domain model section 18.3).
//!
//! The rebuild re-applies the journalled commands through the same fold, which
//! is what makes "the same state" a consequence of determinism rather than a
//! second implementation that has to be kept in step. Its own module because
//! it is the only caller of `replaying`, and because the three divergence
//! checks it makes — state, revision, and in-flight attempts — are a
//! recovery policy rather than part of applying a command.
//!
//! No effect is returned while replaying, so nothing a journalled command
//! already did can happen twice, and an attempt that was in flight when the
//! process died ends as `OUTCOME_UNKNOWN` rather than as a success or a retry.
//!
//! # A journal is history, and a bound on conduct is not re-judged
//!
//! A journal written by one build is replayed by every later one, and a later
//! build may count the task's conduct more strictly. Decision 0233 taught the
//! repetition register to count policy denials; two finished errands on a
//! phone had proposed a call policy had already denied three times, which the
//! build that ran them admitted, and the build that restored them refused the
//! fourth proposal `repeated_refusals_abandoned`. Every browser start replays
//! every task, so one finished errand refused the whole core. A bound that
//! counts what the task did is therefore held as history on replay rather
//! than refused — [`bounds_only_conduct`] is the closed list — and the
//! recovery names every command it was held over. The counters it reads are
//! still rebuilt from every command, so the bound applies to the first command
//! the restored task is asked to admit live (decision 0235).

use super::seed::TaskSeed;
use super::verdict::{HistoryAdmission, Recovery, ReplayError};
use super::Reducer;
use crate::agent::TurnGap;
use crate::budget::BudgetDefaults;
use crate::command::{Command, CommandEnvelope, CommandKind};
use crate::ids::IdSource;
use crate::journal::{CommandRecord, JournalEntry, TaskJournal};
use crate::task::TaskState;
use crate::time::Clock;
use crate::transition::{Guard, RefusalReason};
use crate::ModelCallId;
use crate::Refusal;

/// Whether a replay holds this guard as history when a journalled command
/// crosses it (decision 0235).
///
/// Two guards, each for the commands where it reads nothing but a count of
/// what this task has already done: the repetition register a proposal is
/// checked against, and the budget ledger a proposal, a model turn or a model
/// attempt draws on. Neither grants authority — policy does, through its own
/// journalled decision, whose guards stay strict — and neither decides what a
/// task may touch. `WithinBudget` on `StartTask` is left out on purpose,
/// because that check also validates the shape of the scope the person
/// consented to, and a scope is authority.
///
/// Everything else refuses a replay as it refuses a live command, because it
/// is what the fold stands on: identity, revision, the state table, authority,
/// tool availability, and the registers' own ceilings. A journal that crosses
/// one of those is one this build cannot rebuild, not one it judges strictly.
pub(super) const fn bounds_only_conduct(guard: Guard, command: &Command) -> bool {
    match guard {
        Guard::NotLoopingOnRefusals => matches!(command, Command::ProposeAction(_)),
        Guard::WithinBudget => matches!(
            command,
            Command::ProposeAction(_)
                | Command::RequestModelTurn { .. }
                | Command::RequestModelAttempt { .. }
        ),
        _ => false,
    }
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Rebuilds a task from its journal (domain model section 18.3).
    ///
    /// The rebuild re-applies the journalled commands through the same reducer,
    /// which is what makes "the same state" a consequence of determinism rather
    /// than a second implementation that has to be kept in step. No effect is
    /// returned while replaying, so nothing a journalled command already did
    /// can happen twice, and an attempt that was in flight when the process
    /// died ends as `OUTCOME_UNKNOWN` rather than as a success or a retry.
    pub fn replay(
        seed: TaskSeed,
        defaults: BudgetDefaults,
        clock: C,
        ids: I,
        journal: &TaskJournal,
    ) -> Result<(Self, Recovery), ReplayError> {
        let entries = journal.entries();
        let Some(JournalEntry::Event(creation)) = entries.first() else {
            return Err(ReplayError::Journal(crate::journal::JournalError::Empty));
        };
        let mut reducer = Self::create(
            seed,
            defaults,
            clock,
            ids,
            creation.causation_key.clone(),
            creation.trace_id.clone(),
        );
        reducer.replaying = true;

        let mut commands_replayed = 0_u64;
        let mut admitted_as_history = Vec::new();
        let mut preceding = (None, None);
        for record in journal.commands() {
            // Recovery marks interrupted attempts unknown in memory, then
            // journals an ambiguous reconciliation as RequestUserInput.
            // A following committed settlement proves its live in-flight guard
            // passed, but carries no correlated action outcome to replay. Restore
            // only that exact boundary before applying the settlement normally.
            // This grants no authority or success, and never applies to result
            // completion or to a stop whose settlement was not committed. No
            // action-result command is fabricated: no correlated result exists.
            if matches!(
                (preceding, record.envelope.kind(), reducer.task.state),
                (
                    (
                        Some(CommandKind::RequestUserInput),
                        Some(CommandKind::CancelTask)
                    ),
                    CommandKind::CancelSettled,
                    TaskState::Cancelling,
                ) | (
                    (
                        Some(CommandKind::RequestUserInput),
                        Some(CommandKind::PauseTask)
                    ),
                    CommandKind::PauseSettled,
                    TaskState::Pausing,
                )
            ) {
                for action in reducer.actions.values_mut() {
                    action.mark_outcome_unknown();
                }
            }
            if let Some(guard) = apply_recorded(&mut reducer, record)? {
                admitted_as_history.push(HistoryAdmission {
                    sequence: record.sequence,
                    command: record.envelope.kind(),
                    bound: guard.refusal(),
                });
            }
            preceding = (preceding.1, Some(record.envelope.kind()));
            commands_replayed = commands_replayed.saturating_add(1);
        }
        reducer.replaying = false;

        if let Some(recorded) = journal.last_recorded_state() {
            if recorded != reducer.task.state {
                return Err(ReplayError::StateDiverged {
                    recorded,
                    recomputed: reducer.task.state,
                });
            }
        }
        if journal.revision() != reducer.task.revision {
            return Err(ReplayError::RevisionDiverged {
                recorded: journal.revision(),
                recomputed: reducer.task.revision,
            });
        }

        let mut unknown_outcome_actions = Vec::new();
        for action in reducer.actions.values_mut() {
            if action.state().may_have_reached_the_page() {
                action.mark_outcome_unknown();
                unknown_outcome_actions.push(action.action_id().clone());
            }
        }
        // The same rule as the actions above, for the one call the browser was
        // holding. A model turn in flight is a request handed to a generation
        // that no longer exists: its reply cannot arrive, because there is
        // nothing left to deliver it to. Waiting for it is not caution, it is a
        // wedge — a task paused mid-turn came back Pausing, and `PauseSettled`
        // was refused `ModelTurnInFlight` on every start for the life of the
        // profile, which is a state with no controls at all and no way out of
        // it (decision 0150).
        //
        // In memory and not as a command, exactly as the actions are: replay
        // reproduces the journal it was given and fabricates nothing. The
        // record is `OutcomeUnknown`, which is what it is — the call may well
        // have been paid for, and nobody will ever read the answer.
        let interrupted_model_call = give_up_on_any_call_in_flight(&mut reducer);
        let recovery = Recovery {
            commands_replayed,
            state: reducer.task.state,
            revision: reducer.task.revision,
            unknown_outcome_actions,
            interrupted_model_call,
            leases_restored: 0,
            requires_revalidation: !reducer.task.state.is_terminal(),
            admitted_as_history,
        };
        Ok((reducer, recovery))
    }
}

/// Applies one journalled command, and says which bound on conduct, if any, it
/// was applied over.
fn apply_recorded<C: Clock, I: IdSource>(
    reducer: &mut Reducer<C, I>,
    record: &CommandRecord,
) -> Result<Option<Guard>, ReplayError> {
    let refusal = match reducer.apply_journalled(record.envelope.clone()) {
        Ok((_, held)) => return Ok(held),
        Err(refusal) => refusal,
    };
    // A settlement that is in the journal was admitted by a live reducer whose
    // guards all passed, so a replay that refuses it is the replay being wrong
    // rather than the journal. There is exactly one way that can happen and it
    // is the boundary this record is about: the generation that dispatched the
    // model call died, the next one gave up on it (below), and the settlement
    // the browser then sent was committed. The journal holds no command for
    // the giving-up, because giving up is not something anybody did — so the
    // replay has to reach the same conclusion at the same point, which is
    // here.
    //
    // Bounded to that exact boundary: this settlement, in its own settling
    // state, refused for this one reason, with a call actually in flight. It
    // grants no success, invents no reply, and never applies to a result
    // completion.
    if !gave_up_on_the_interrupted_call(reducer, &record.envelope, refusal) {
        return Err(ReplayError::CommandRefused {
            sequence: record.sequence,
            refusal,
        });
    }
    reducer
        .apply_journalled(record.envelope.clone())
        .map(|(_, held)| held)
        .map_err(|refusal| ReplayError::CommandRefused {
            sequence: record.sequence,
            refusal,
        })
}

/// Gives up on a model call the previous generation was holding, and says which.
///
/// The reply had nowhere to be delivered, so it is not late — it is absent, and
/// `OUTCOME_UNKNOWN` is the honest record of a call that may well have been
/// paid for and will never be read (decision 0150).
fn give_up_on_any_call_in_flight<C: Clock, I: IdSource>(
    reducer: &mut Reducer<C, I>,
) -> Option<ModelCallId> {
    let call_id = reducer
        .turn
        .as_ref()
        .filter(|turn| turn.phase().is_in_flight())
        .map(|turn| turn.call_id().clone())?;
    if let Some(turn) = reducer.turn.as_mut() {
        turn.record_gap(TurnGap::OutcomeUnknown);
    }
    reducer.reply_being_reasked = false;
    Some(call_id)
}

/// Whether this refusal is the one the boundary explains, and if so, crosses it.
///
/// Every clause is load-bearing. The command must be a settlement, because a
/// settlement is the only thing a browser sends about a task whose generation
/// has gone. The state must be the settling one it belongs to. The refusal must
/// be exactly the in-flight call, because any other refusal is a real
/// disagreement between this journal and this reducer. And a call must actually
/// be in flight, or there is nothing to give up on and the refusal came from
/// somewhere else.
fn gave_up_on_the_interrupted_call<C: Clock, I: IdSource>(
    reducer: &mut Reducer<C, I>,
    envelope: &CommandEnvelope,
    refusal: Refusal,
) -> bool {
    let settling = match envelope.kind() {
        CommandKind::PauseSettled => TaskState::Pausing,
        CommandKind::CancelSettled => TaskState::Cancelling,
        _ => return false,
    };
    if refusal.reason != RefusalReason::ModelTurnInFlight || reducer.task.state != settling {
        return false;
    }
    give_up_on_any_call_in_flight(reducer).is_some()
}
