// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Replaying a journal reconstructs the task and repeats nothing.
//!
//! Process death is the interesting case, so these properties are stated the
//! way recovery meets them: take a run, cut it anywhere, rebuild from what was
//! written, and check the rebuild is the run.
//!
//! Three things must hold over every generated script:
//!
//! 1. **Reconstruction.** The rebuilt task has the same state, the same
//!    revision, and the same journal, byte for byte in structure.
//! 2. **No duplicated action.** Every dispatch that happened once in the run
//!    happened once in the journal, the rebuild performs no effect at all, and
//!    a proposal repeating a dispatched key is refused afterwards.
//! 3. **No invented completion.** A rebuild reaches `COMPLETED` only where the
//!    run did, and an attempt that was in flight ends `OUTCOME_UNKNOWN` rather
//!    than verified.
//! 4. **The loop is pure.** The rebuilt task holds the same model turn, has
//!    started the same number of them, and answers the assistant loop's next
//!    question with the same command the live run would have answered. That is
//!    the property every guarantee in decision 0052 rests on: a loop whose
//!    answer changed after a restart would propose a different effect identity,
//!    and the browser's ledger would have nothing to refuse.
#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

mod common;
#[path = "replay_properties/property_cases.rs"]
mod property_cases;

use bip_types::identity::DispatchId;
use common::agent::Digest;
use task_engine::action::ActionState;
use task_engine::command::{Command, CommandKind, PauseCause};
use task_engine::ids::{IdempotencyKey, SequentialIds};
use task_engine::journal::TaskJournal;
use task_engine::plan::StepState;
use task_engine::reducer::Reducer;
use task_engine::task::TaskState;
use task_engine::{ManualClock, ModelTurn, TraceId, TurnGap, TurnPhase};

/// The commands a generated script draws from.
const ALPHABET: &[CommandKind] = &[
    CommandKind::StartTask,
    CommandKind::AcceptInitialConsent,
    CommandKind::ExecutorStarted,
    CommandKind::SetPlan,
    CommandKind::AdvanceStep,
    CommandKind::ProposeAction,
    CommandKind::RecordPolicyDecision,
    CommandKind::DispatchAction,
    CommandKind::RecordActionOutcome,
    CommandKind::RequestApproval,
    CommandKind::ApproveAction,
    CommandKind::RequestUserInput,
    CommandKind::SupplyUserInput,
    CommandKind::PauseTask,
    CommandKind::PauseSettled,
    CommandKind::ResumeTask,
    CommandKind::TakeOver,
    CommandKind::CancelTask,
    CommandKind::CancelSettled,
    CommandKind::RequestModelTurn,
    CommandKind::RequestModelAttempt,
    CommandKind::RecordModelTurn,
    CommandKind::RecordModelTurnGap,
    CommandKind::ResultCandidateReady,
    CommandKind::CompleteResultValidated,
    CommandKind::PartialResultValidated,
    CommandKind::ExcludeSource,
    CommandKind::CorrectFact,
    CommandKind::FailTask,
];

/// A checkpoint the rebuild has to be able to land on.
#[derive(Clone, Debug, PartialEq, Eq)]
struct Checkpoint {
    entries: usize,
    state: TaskState,
    revision: u64,
}

/// What one live run produced.
struct Run {
    journal: TaskJournal,
    checkpoints: Vec<Checkpoint>,
    final_state: TaskState,
    final_revision: u64,
    dispatched: Vec<IdempotencyKey>,
    turn: Option<ModelTurn>,
    turns_started: u64,
    next_agent_command: Result<Option<Command>, task_engine::AgentError>,
    in_flight_actions: usize,
}

fn request_model_attempt(fixture: &common::Fixture) -> Command {
    let (call_id, attempt_ordinal, candidate_ordinal) = fixture.reducer.model_turn().map_or_else(
        || (task_engine::ModelCallId::new("model-absent"), 1, 0),
        |turn| {
            (
                turn.call_id().clone(),
                turn.attempts_started(),
                turn.candidate_ordinal(),
            )
        },
    );
    Command::RequestModelAttempt {
        call_id,
        attempt_ordinal,
        candidate_ordinal,
        kind: task_engine::ModelAttemptKind::Retry,
    }
}

/// Builds the payload for `kind` against the reducer as it stands.
fn payload(kind: CommandKind, fixture: &common::Fixture, step: usize) -> Command {
    match kind {
        CommandKind::ProposeAction => {
            Command::ProposeAction(Box::new(common::proposal(&format!("action_key_{step}"))))
        }
        CommandKind::DispatchAction => {
            let action_id = fixture
                .reducer
                .actions()
                .find(|action| action.state() == ActionState::Authorized)
                .map_or_else(
                    || bip_types::identity::ActionId::new("act_absent"),
                    |action| action.action_id().clone(),
                );
            Command::DispatchAction {
                action_id,
                dispatch_id: DispatchId::new(format!("dispatch_{step}")),
            }
        }
        CommandKind::RecordPolicyDecision => {
            let action_id = fixture
                .reducer
                .actions()
                .find(|action| action.state() == ActionState::Proposed)
                .map_or_else(
                    || bip_types::identity::ActionId::new("act_absent"),
                    |action| action.action_id().clone(),
                );
            Command::RecordPolicyDecision {
                action_id,
                decision: Box::new(common::authorize()),
                dispatch_id: None,
            }
        }
        CommandKind::RecordActionOutcome => {
            let action_id = fixture
                .reducer
                .actions()
                .find(|action| action.state() == ActionState::Dispatching)
                .map_or_else(
                    || bip_types::identity::ActionId::new("act_absent"),
                    |action| action.action_id().clone(),
                );
            Command::RecordActionOutcome {
                action_id,
                outcome: Box::new(common::verified_outcome()),
            }
        }
        CommandKind::RequestApproval => {
            let action_id = fixture.reducer.actions().next().map_or_else(
                || bip_types::identity::ActionId::new("act_absent"),
                |action| action.action_id().clone(),
            );
            Command::RequestApproval { action_id }
        }
        CommandKind::AdvanceStep => {
            let plan_step_id = fixture
                .reducer
                .plan()
                .and_then(|plan| {
                    plan.steps()
                        .iter()
                        .find(|step| !step.state().is_final())
                        .map(|step| step.plan_step_id().clone())
                })
                .unwrap_or_else(|| task_engine::ids::PlanStepId::new("step_absent"));
            Command::AdvanceStep {
                plan_step_id,
                to: StepState::Running,
            }
        }
        // The identity is derived rather than drawn from the script, because
        // that is the contract: `Guard::ModelCallIsNext` refuses any other,
        // and a replay derives the same one from the same state.
        CommandKind::RequestModelTurn => common::agent::request_turn(fixture),
        CommandKind::RequestModelAttempt => request_model_attempt(fixture),
        CommandKind::RecordModelTurn => common::agent::record_turn(fixture),
        CommandKind::RecordModelTurnGap => common::agent::record_gap(fixture, step),
        CommandKind::PauseTask => Command::PauseTask {
            cause: if step.is_multiple_of(2) {
                PauseCause::User
            } else {
                PauseCause::BackgroundRestricted
            },
        },
        CommandKind::ExcludeSource => Command::ExcludeSource {
            source_id: common::source_id(u8::try_from(step % 8).unwrap_or(0)),
        },
        CommandKind::CorrectFact => Command::CorrectFact {
            fact_id: common::fact_id(u8::try_from(step % 8).unwrap_or(0)),
        },
        other => common::command::command_for(other, fixture),
    }
}

/// Runs a script, keeping only what a journal would keep.
fn run(script: &[usize]) -> Run {
    let mut fixture = common::draft();
    let mut checkpoints = Vec::new();
    for (step, choice) in script.iter().enumerate() {
        let kind = ALPHABET
            .get(choice % ALPHABET.len())
            .copied()
            .unwrap_or(CommandKind::ExecutorStarted);
        let command = payload(kind, &fixture, step);
        let envelope = fixture.envelope(command);
        if fixture.reducer.apply(envelope).is_ok() {
            checkpoints.push(Checkpoint {
                entries: fixture.reducer.journal().len(),
                state: fixture.reducer.task().state(),
                revision: fixture.reducer.task().revision(),
            });
        }
    }
    let in_flight_actions = fixture
        .reducer
        .actions()
        .filter(|action| action.state().may_have_reached_the_page())
        .count();
    Run {
        journal: fixture.reducer.journal().clone(),
        checkpoints,
        final_state: fixture.reducer.task().state(),
        final_revision: fixture.reducer.task().revision(),
        dispatched: fixture.reducer.dispatched_keys().iter().cloned().collect(),
        in_flight_actions,
        turn: fixture.reducer.model_turn().cloned(),
        turns_started: fixture.reducer.turns_started(),
        next_agent_command: fixture.reducer.next_agent_command(None, &Digest),
    }
}

/// Rebuilds from a journal, exactly as recovery would.
fn rebuild(
    journal: &TaskJournal,
) -> (
    Reducer<ManualClock, SequentialIds>,
    task_engine::reducer::Recovery,
) {
    match Reducer::replay(
        common::seed(),
        common::defaults(),
        ManualClock::at(1_000),
        SequentialIds::new(),
        journal,
    ) {
        Ok(rebuilt) => rebuilt,
        Err(error) => unreachable!("a journal this reducer wrote must replay: {error:?}"),
    }
}

#[test]
fn a_proposal_repeating_a_dispatched_key_is_refused_after_a_rebuild() {
    let mut fixture = common::running();
    let action_id = fixture
        .action_id
        .clone()
        .unwrap_or_else(|| unreachable!("the running fixture has an action"));
    fixture.must_apply(Command::DispatchAction {
        action_id,
        dispatch_id: DispatchId::new("dispatch_0"),
    });

    let journal = fixture.reducer.journal().clone();
    let (mut rebuilt, recovery) = rebuild(&journal);
    assert_eq!(recovery.unknown_outcome_actions.len(), 1);

    let repeat = task_engine::CommandEnvelope::new(
        IdempotencyKey::new("repeat"),
        rebuilt.task().revision(),
        TraceId::new("trace_test"),
        Command::ProposeAction(Box::new(common::proposal("action_key_0"))),
    );
    let refusal = rebuilt
        .apply(repeat)
        .err()
        .unwrap_or_else(|| unreachable!("a dispatched key cannot be proposed again"));
    assert_eq!(
        refusal.reason,
        task_engine::RefusalReason::ProposalAlreadyDispatched
    );
}

#[test]
fn a_rebuild_returns_no_effects_at_all() {
    let mut fixture = common::running();
    let action_id = fixture
        .action_id
        .clone()
        .unwrap_or_else(|| unreachable!("the running fixture has an action"));
    fixture.must_apply(Command::DispatchAction {
        action_id,
        dispatch_id: DispatchId::new("dispatch_0"),
    });
    fixture.must_apply(Command::PauseTask {
        cause: PauseCause::User,
    });

    let journal = fixture.reducer.journal().clone();
    let (rebuilt, recovery) = rebuild(&journal);
    assert_eq!(rebuilt.task().state(), TaskState::Pausing);
    assert_eq!(
        recovery.commands_replayed,
        journal.commands().count() as u64
    );
    assert!(recovery.requires_revalidation);
}

#[test]
fn a_terminal_failure_keeps_its_exact_reason_across_replay() {
    let mut fixture = common::running();
    fixture.must_apply(Command::FailTask {
        reason: task_engine::FailureReason::UnverifiableAction,
    });
    assert_eq!(
        fixture.reducer.task().terminal_failure(),
        Some(task_engine::FailureReason::UnverifiableAction)
    );

    let journal = fixture.reducer.journal().clone();
    let (rebuilt, _) = rebuild(&journal);
    assert_eq!(rebuilt.task().state(), TaskState::Failed);
    assert_eq!(
        rebuilt.task().terminal_failure(),
        Some(task_engine::FailureReason::UnverifiableAction)
    );
}
