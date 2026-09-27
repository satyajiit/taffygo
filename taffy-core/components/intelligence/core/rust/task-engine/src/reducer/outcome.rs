// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The reducer's internal answer for one command, and the three total
//! functions that turn a state into the facts the journal records.
//!
//! [`Outcome`] never leaves the crate: it is what a command handler hands back
//! to the fold, before the fold decides what to journal. The three `const fn`s
//! beside it exist so that the mapping from a state to its entry event, its
//! execution phase, and a settling command's revocation reason is written down
//! once rather than at each call site; [`phase_after`] is the fourth reading,
//! from the loop's own commands rather than from a state.

use bip_types::identity::ActionId;

use crate::authority::{ActionClass, RevocationReason};
use crate::command::{Command, PauseCause};
use crate::effect::Effect;
use crate::event::{EventKind, TaskEvent};
use crate::task::{ExecutionPhase, StateReason, TaskState};

pub(super) struct Outcome {
    pub(super) target: Option<TaskState>,
    pub(super) reason: Option<StateReason>,
    pub(super) events: Vec<TaskEvent>,
    pub(super) effects: Vec<Effect>,
}

impl Outcome {
    pub(super) const fn nothing() -> Self {
        Self {
            target: None,
            reason: None,
            events: Vec::new(),
            effects: Vec::new(),
        }
    }

    pub(super) fn recorded(events: Vec<TaskEvent>, effects: Vec<Effect>) -> Self {
        Self {
            target: None,
            reason: None,
            events,
            effects,
        }
    }

    pub(super) fn moves(target: TaskState, reason: StateReason) -> Self {
        Self {
            target: Some(target),
            reason: Some(reason),
            events: Vec::new(),
            effects: Vec::new(),
        }
    }

    pub(super) fn with_events(mut self, events: Vec<TaskEvent>) -> Self {
        self.events = events;
        self
    }

    pub(super) fn with_effects(mut self, effects: Vec<Effect>) -> Self {
        self.effects = effects;
        self
    }
}

/// Which revocation reason a pause carries.
///
/// The reason reaches `policy-engine` unchanged and is written into an audit
/// record, so it is chosen once, here, rather than at each call site. The stop
/// and take-over paths pass their reason as a literal, which is where a reader
/// looking at those transitions expects to see it.
///
/// Exhaustive over every cause, and deliberately not parameterised by the
/// command. An earlier shape took a `CommandKind` and fell through to
/// "the user took over" for every command it did not name — which would have
/// put a claim about a person into an audit record for a command no person
/// issued. Narrowing the input removed the arm that could lie.
pub(super) const fn revocation_for_pause(cause: PauseCause) -> RevocationReason {
    match cause {
        PauseCause::User => RevocationReason::UserTookOver,
        // Nobody took the page: the platform, the provider or the network
        // stopped the assistant, and the closest compiled-in reason for an
        // authority withdrawn by something other than the person is this one.
        PauseCause::BackgroundRestricted
        | PauseCause::ProviderLimit
        | PauseCause::ProviderBusy
        | PauseCause::Offline
        | PauseCause::NoAnswer => RevocationReason::PolicyRevoked,
    }
}

/// The event that records entering `state`.
pub(super) const fn entry_event(state: TaskState) -> EventKind {
    match state {
        TaskState::Draft => EventKind::TaskCreated,
        TaskState::AwaitingConsent => EventKind::ConsentRequested,
        TaskState::Queued => EventKind::TaskQueued,
        TaskState::Running => EventKind::TaskStarted,
        TaskState::WaitingUser => EventKind::TaskWaitingUser,
        TaskState::Pausing => EventKind::TaskPausing,
        TaskState::Paused => EventKind::TaskPaused,
        TaskState::Cancelling => EventKind::TaskCancelling,
        TaskState::Cancelled => EventKind::TaskCancelled,
        TaskState::Completing => EventKind::TaskCompleting,
        TaskState::Completed => EventKind::TaskCompleted,
        TaskState::Partial => EventKind::TaskPartial,
        TaskState::Failed => EventKind::TaskFailed,
    }
}

/// The phase a task is in on entering `state`.
pub(super) const fn phase_for(state: TaskState) -> Option<ExecutionPhase> {
    match state {
        TaskState::Running => Some(ExecutionPhase::Planning),
        TaskState::Completing => Some(ExecutionPhase::Verifying),
        _ => None,
    }
}

/// The phase a loop-side command leaves a running task in, or `None` when the
/// command says nothing about it.
///
/// The state table knows only that a task is `RUNNING`; what the runtime is
/// doing inside that state — asking a model, reading a page, acting on one —
/// is said by the commands the loop journals, so the phase is read from them.
/// A dispatch takes its action's class: an observation is reading the page,
/// every other class is acting on it. A settled turn, or an outcome that came
/// back, leaves the loop deciding its next move, which is planning. Nothing
/// here is journaled; a replay re-applies the same commands and reaches the
/// same phase.
pub(super) fn phase_after(
    command: &Command,
    class_of: impl Fn(&ActionId) -> Option<ActionClass>,
) -> Option<ExecutionPhase> {
    match command {
        Command::RequestModelTurn { .. } | Command::RequestModelAttempt { .. } => {
            Some(ExecutionPhase::Inferencing)
        }
        Command::DispatchAction { action_id, .. }
        | Command::RecordPolicyDecision {
            action_id,
            dispatch_id: Some(_),
            ..
        } => Some(match class_of(action_id) {
            Some(ActionClass::ObservePage) => ExecutionPhase::Observing,
            _ => ExecutionPhase::Acting,
        }),
        Command::RecordModelTurn { .. }
        | Command::RecordModelTurnGap { .. }
        | Command::RecordActionOutcome { .. }
        | Command::RecordToolJobOutcome { .. } => Some(ExecutionPhase::Planning),
        _ => None,
    }
}
