// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The cells for the commands a running task issues.
//!
//! Plans, action proposals, model turns, results and artifacts. Split from
//! [`super::control`] along the seam section 9.2's diagram already draws:
//! these are the commands a task issues while it executes, and those are the
//! commands that decide whether it executes.

use super::super::disposition::{
    moves, moves_if, records, records_if, records_or_moves_if, refuse,
};
use super::super::guard::Guard;
use super::super::refusal::RefusalReason as R;
use super::super::Disposition;
use crate::task::TaskState;
use crate::task::TaskState as S;

/// A plan is explanatory, so setting one moves nothing.
pub(super) const fn set_plan(state: TaskState) -> Disposition {
    match state {
        S::Draft | S::AwaitingConsent | S::Queued | S::Running | S::Paused => {
            records_if(&[Guard::PlanIsWellFormed])
        }
        S::WaitingUser | S::Completing => refuse(R::NotRunning),
        S::Pausing | S::Cancelling => refuse(R::AlreadySettling),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn advance_step(state: TaskState) -> Disposition {
    match state {
        S::Running => records_if(&[Guard::StepMayMove]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotRunning),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// Proposing grants nothing, so it moves nothing. A terminal task
/// proposes nothing at all.
pub(super) const fn propose_action(state: TaskState) -> Disposition {
    match state {
        S::Running => records_if(&[
            Guard::ToolAvailable,
            Guard::NotLoopingOnRefusals,
            Guard::ProposalNotAlreadyDispatched,
            Guard::WithinBudget,
        ]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotRunning),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// A decision for a running task records in place unless it needs approval,
/// which atomically enters the consent state. A reply that arrives after the
/// task stopped running is recorded and closes the action.
pub(super) const fn record_policy_decision(state: TaskState) -> Disposition {
    match state {
        S::Running => records_or_moves_if(&[S::AwaitingConsent], &[Guard::ActionKnown]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing
        | S::Cancelled
        | S::Completed
        | S::Partial
        | S::Failed => records_if(&[Guard::ActionKnown]),
    }
}

pub(super) const fn dispatch_action(state: TaskState) -> Disposition {
    match state {
        S::Running => records_if(&[Guard::ActionKnown, Guard::ActionAuthorized]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotRunning),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// An outcome belongs to an action that was already dispatched.
/// Refusing it would leave the journal claiming an outcome that never
/// arrived, which is exactly what recovery must not have to guess about.
pub(super) const fn record_action_outcome(_state: TaskState) -> Disposition {
    records_if(&[Guard::ActionKnown, Guard::ActionOutcomeMatches])
}

/// Same disposition as [`record_action_outcome`], for the same reason: a
/// terminal or settling task may still need to record the one exact result of
/// a job that was already dispatched.
pub(super) const fn record_tool_job_outcome(_state: TaskState) -> Disposition {
    records_if(&[Guard::ActionKnown, Guard::ActionOutcomeMatches])
}

/// Asking the model is the one command here that spends money, so it
/// is `RUNNING` and nothing else: a queued task holds no lease, a
/// settling task is giving its authority back, and a completing task
/// has already produced the answer a turn would be for.
pub(super) const fn request_model_turn(state: TaskState) -> Disposition {
    match state {
        S::Running => records_if(&[
            Guard::NoModelTurnInFlight,
            Guard::ModelCallIsNext,
            Guard::WithinBudget,
        ]),
        S::Draft => refuse(R::NotStarted),
        S::AwaitingConsent | S::Queued | S::WaitingUser | S::Paused => refuse(R::NotRunning),
        S::Pausing | S::Cancelling => refuse(R::AlreadySettling),
        S::Completing => refuse(R::ResultIsBeingValidated),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// A sub-attempt belongs to the one call already in flight. It spends the same
/// model-request budget as the initial attempt, plus the per-step retry bound.
pub(super) const fn request_model_attempt(state: TaskState) -> Disposition {
    match state {
        S::Running => records_if(&[Guard::ModelTurnMatches, Guard::WithinBudget]),
        S::Draft => refuse(R::NotStarted),
        S::AwaitingConsent | S::Queued | S::WaitingUser | S::Paused => refuse(R::NotRunning),
        S::Pausing | S::Cancelling => refuse(R::AlreadySettling),
        S::Completing => refuse(R::ResultIsBeingValidated),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// A reply, or the fact that there will not be one, belongs to a call
/// that was already made. It is accepted wherever that call could still
/// be outstanding — including while the task is settling, because a
/// cancellation reaching the reducer as a positive fact is the whole
/// point of the gap. A terminal task refuses: by then the settlement
/// guards have already required that no turn is in flight, so a
/// completion arriving here answers a call nobody is holding.
pub(super) const fn record_model_turn(state: TaskState) -> Disposition {
    match state {
        S::Running | S::WaitingUser | S::Pausing | S::Paused | S::Cancelling | S::Completing => {
            records_if(&[Guard::ModelTurnMatches])
        }
        S::Draft | S::AwaitingConsent | S::Queued => refuse(R::NotRunning),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// The boundary moves while no call is out, because the composed request the
/// browser is holding must stay the request that was reviewed. The range and
/// monotonicity of the boundary itself are [`Guard::EvictionAdvances`]'s.
pub(super) const fn record_context_eviction(state: TaskState) -> Disposition {
    match state {
        S::Running => records_if(&[Guard::NoModelTurnInFlight, Guard::EvictionAdvances]),
        S::Draft => refuse(R::NotStarted),
        S::AwaitingConsent | S::Queued | S::WaitingUser | S::Paused => refuse(R::NotRunning),
        S::Pausing | S::Cancelling => refuse(R::AlreadySettling),
        S::Completing => refuse(R::ResultIsBeingValidated),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn result_candidate_ready(state: TaskState) -> Disposition {
    match state {
        S::Running => moves(&[S::Completing]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotRunning),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn complete_result_validated(state: TaskState) -> Disposition {
    match state {
        S::Completing => moves_if(
            &[S::Completed],
            &[
                Guard::ResultIsComplete,
                Guard::NoActionWorkInFlight,
                Guard::NoUnresolvedActionOutcomes,
            ],
        ),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling => refuse(R::NotCompleting),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn partial_result_validated(state: TaskState) -> Disposition {
    match state {
        S::Completing => moves_if(
            &[S::Partial],
            &[
                Guard::NoActionWorkInFlight,
                Guard::NoUnresolvedActionOutcomes,
            ],
        ),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling => refuse(R::NotCompleting),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn resume_for_correction(state: TaskState) -> Disposition {
    match state {
        S::Completing => moves(&[S::Running]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling => refuse(R::NotCompleting),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn fail_task(state: TaskState) -> Disposition {
    match state {
        S::Running => moves(&[S::Failed]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing => refuse(R::NotRunning),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// A correction is not an action, so a finished task still takes one.
pub(super) const fn correct_fact(state: TaskState) -> Disposition {
    match state {
        S::Running | S::WaitingUser | S::Paused | S::Completed | S::Partial => records(),
        S::Draft | S::AwaitingConsent | S::Queued => refuse(R::NoFactsYet),
        S::Pausing | S::Cancelling => refuse(R::AlreadySettling),
        S::Completing => refuse(R::ResultIsBeingValidated),
        S::Cancelled | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn exclude_source(state: TaskState) -> Disposition {
    match state {
        S::Draft | S::AwaitingConsent | S::Queued | S::Running | S::WaitingUser | S::Paused => {
            records()
        }
        S::Pausing | S::Cancelling => refuse(R::AlreadySettling),
        S::Completing => refuse(R::ResultIsBeingValidated),
        // The result already cites its sources; correcting it is the way
        // to change what it rests on.
        S::Completed | S::Partial => refuse(R::ScopeIsFrozen),
        S::Cancelled | S::Failed => refuse(R::TaskIsTerminal),
    }
}

/// Artifact readiness is committed only after the workspace renderer has
/// accepted one exact cited revision.
pub(super) const fn request_artifact(state: TaskState) -> Disposition {
    match state {
        S::Running => records(),
        S::Draft | S::AwaitingConsent | S::Queued | S::WaitingUser | S::Paused => {
            refuse(R::NotRunning)
        }
        S::Pausing | S::Cancelling => refuse(R::AlreadySettling),
        S::Completing => refuse(R::ResultIsBeingValidated),
        S::Cancelled | S::Completed | S::Partial | S::Failed => refuse(R::TaskIsTerminal),
    }
}

pub(super) const fn accept_artifact(state: TaskState) -> Disposition {
    match state {
        S::Completed | S::Partial => records_if(&[Guard::ArtifactReady]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing
        | S::Cancelled
        | S::Failed => refuse(R::NoResultToAccept),
    }
}

pub(super) const fn export_artifact(state: TaskState) -> Disposition {
    match state {
        S::Completed | S::Partial => records_if(&[Guard::ArtifactAccepted]),
        S::Draft
        | S::AwaitingConsent
        | S::Queued
        | S::Running
        | S::WaitingUser
        | S::Pausing
        | S::Paused
        | S::Cancelling
        | S::Completing
        | S::Cancelled
        | S::Failed => refuse(R::NoResultToAccept),
    }
}
