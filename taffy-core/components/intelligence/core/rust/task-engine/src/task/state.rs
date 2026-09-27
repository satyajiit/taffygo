// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The durable state taxonomy: what state a task is in, and why.
//!
//! Domain model section 9.2 fixes the thirteen states; this module holds them
//! and the three closed enumerations that qualify a state — the execution
//! phase inside `RUNNING`, the reason the task entered the state, and which
//! consent it is waiting on. Every one of them is closed and carries a
//! compiled-in `label`, so no page text and no model text can ever become a
//! state explanation.
//!
//! What a *person* sees is deliberately not here: that projection is
//! [`super::display::DisplayState`], reached only through
//! [`TaskState::display`].

use core::fmt;

use super::display::DisplayState;

/// The durable task state (domain model section 9.2).
///
/// The list is closed and the order is the order the specification's diagram
/// introduces them.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum TaskState {
    /// Being composed. Nothing has been consented to.
    Draft,
    /// Waiting on the user: the initial consent, or an in-task approval.
    AwaitingConsent,
    /// Consented and waiting for an executor.
    Queued,
    /// Executing.
    Running,
    /// Stopped on a missing source or a missing input.
    WaitingUser,
    /// Revoking authority and settling in-flight work, on the way to paused.
    Pausing,
    /// Held. No authority stands.
    Paused,
    /// Revoking authority and settling the journal, on the way to stopped.
    Cancelling,
    /// A result candidate is being validated and persisted.
    Completing,
    /// Terminal: stopped by the user.
    Cancelled,
    /// Terminal: a validated complete result.
    Completed,
    /// Terminal: a useful result with labelled gaps.
    Partial,
    /// Terminal: it could not be finished.
    Failed,
}

impl TaskState {
    /// Every state, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Draft,
        Self::AwaitingConsent,
        Self::Queued,
        Self::Running,
        Self::WaitingUser,
        Self::Pausing,
        Self::Paused,
        Self::Cancelling,
        Self::Completing,
        Self::Cancelled,
        Self::Completed,
        Self::Partial,
        Self::Failed,
    ];

    /// The four terminal states.
    pub const TERMINAL: &'static [Self] = &[
        Self::Cancelled,
        Self::Completed,
        Self::Partial,
        Self::Failed,
    ];

    /// A short, compiled-in name, safe to record in an audit event. It is never
    /// shown to a person.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Draft => "DRAFT",
            Self::AwaitingConsent => "AWAITING_CONSENT",
            Self::Queued => "QUEUED",
            Self::Running => "RUNNING",
            Self::WaitingUser => "WAITING_USER",
            Self::Pausing => "PAUSING",
            Self::Paused => "PAUSED",
            Self::Cancelling => "CANCELLING",
            Self::Completing => "COMPLETING",
            Self::Cancelled => "CANCELLED",
            Self::Completed => "COMPLETED",
            Self::Partial => "PARTIAL",
            Self::Failed => "FAILED",
        }
    }

    /// Whether the task has ended. A terminal task issues no new actions; a
    /// rerun is a new task linked by its predecessor.
    pub const fn is_terminal(self) -> bool {
        matches!(
            self,
            Self::Cancelled | Self::Completed | Self::Partial | Self::Failed
        )
    }

    /// Whether the task is revoking authority and settling in-flight work.
    ///
    /// A settling task takes no new user instruction except the one it is
    /// already settling towards.
    pub const fn is_settling(self) -> bool {
        matches!(self, Self::Pausing | Self::Cancelling)
    }

    /// What a person sees, or `None` when the task is still in setup and
    /// consent, which owns its own surfaces and shows no task state.
    ///
    /// This is the whole of the section 9.2 display table, as one pure
    /// function. Nothing else in the crate renders a state.
    pub const fn display(self) -> Option<DisplayState> {
        match self {
            Self::Draft | Self::AwaitingConsent => None,
            Self::Queued | Self::Running | Self::Completing => Some(DisplayState::Running),
            Self::WaitingUser => Some(DisplayState::WaitingForYou),
            Self::Pausing | Self::Paused => Some(DisplayState::Paused),
            Self::Completed => Some(DisplayState::Done),
            Self::Partial => Some(DisplayState::PartlyDone),
            Self::Cancelling | Self::Cancelled => Some(DisplayState::Stopped),
            Self::Failed => Some(DisplayState::Failed),
        }
    }
}

impl fmt::Display for TaskState {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.label())
    }
}
/// What the runtime is doing inside `RUNNING` (domain model section 9.1).
///
/// A phase is implementation detail. It never becomes a user-visible task
/// state and never competes with [`DisplayState`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ExecutionPhase {
    /// Building or revising a plan.
    Planning,
    /// Taking observations.
    Observing,
    /// Asking a model.
    Inferencing,
    /// Dispatching an action.
    Acting,
    /// Checking a postcondition.
    Verifying,
}

impl ExecutionPhase {
    /// Every phase, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::Planning,
        Self::Observing,
        Self::Inferencing,
        Self::Acting,
        Self::Verifying,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Planning => "planning",
            Self::Observing => "observing",
            Self::Inferencing => "inferencing",
            Self::Acting => "acting",
            Self::Verifying => "verifying",
        }
    }
}
/// Why the task is in the state it is in.
///
/// A closed enumeration of compiled-in reasons. Never a free string, so no page
/// text and no model text can reach a state explanation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum StateReason {
    /// The scope and provider preview is ready for the user to consent to.
    PreviewReady,
    /// The user edited the initial scope.
    ScopeEdited,
    /// The user accepted the initial consent.
    InitialConsentAccepted,
    /// An executor picked the task up.
    ExecutorStarted,
    /// An exact action, data, or scope expansion needs the user's consent.
    ScopeExpansionNeedsConsent,
    /// The user accepted an in-task approval that was still current.
    ApprovalAccepted,
    /// The user denied or dismissed an approval.
    ApprovalDenied,
    /// A source or an input is missing.
    UserInputNeeded,
    /// The user supplied the input.
    UserInputSupplied,
    /// A native permission surface is waiting on one exact platform result.
    PermissionRequested,
    /// The exact pending native permission received a terminal result.
    PermissionDecided,
    /// The assistant gave the page to the person and is waiting for them.
    HandoverRequested,
    /// The person came back, so the assistant may work again.
    HandoverCompleted,
    /// The handover window closed with nobody coming back.
    HandoverExpired,
    /// The user paused the task.
    UserPaused,
    /// The user took over the tab.
    UserTookOver,
    /// The platform restricted background work.
    BackgroundRestricted,
    /// Authority is revoked and in-flight work has settled.
    SettlingComplete,
    /// The user asked to resume, and revalidation is required.
    ResumeRequested,
    /// The user asked to stop.
    StopRequested,
    /// The user discarded the draft.
    Discarded,
    /// A result candidate is ready for validation.
    ResultCandidateReady,
    /// Validation and persistence succeeded.
    ResultValidated,
    /// A useful result has labelled gaps.
    ResultHasGaps,
    /// A correction or retry sent the task back to work.
    CorrectionRequested,
    /// The task hit a terminal failure.
    TerminalFailure,
    /// The provider's limit was reached or the device went offline, and the
    /// task is held until it can resume.
    ProviderPaused,
    /// The person asked a follow-up of a finished task, which went back to
    /// work (decision 0137).
    FollowUpAsked,
}

impl StateReason {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::PreviewReady => "preview_ready",
            Self::ScopeEdited => "scope_edited",
            Self::InitialConsentAccepted => "initial_consent_accepted",
            Self::ExecutorStarted => "executor_started",
            Self::ScopeExpansionNeedsConsent => "scope_expansion_needs_consent",
            Self::ApprovalAccepted => "approval_accepted",
            Self::ApprovalDenied => "approval_denied",
            Self::UserInputNeeded => "user_input_needed",
            Self::UserInputSupplied => "user_input_supplied",
            Self::PermissionRequested => "permission_requested",
            Self::PermissionDecided => "permission_decided",
            Self::HandoverRequested => "handover_requested",
            Self::HandoverCompleted => "handover_completed",
            Self::HandoverExpired => "handover_expired",
            Self::UserPaused => "user_paused",
            Self::UserTookOver => "user_took_over",
            Self::BackgroundRestricted => "background_restricted",
            Self::SettlingComplete => "settling_complete",
            Self::ResumeRequested => "resume_requested",
            Self::StopRequested => "stop_requested",
            Self::Discarded => "discarded",
            Self::ResultCandidateReady => "result_candidate_ready",
            Self::ResultValidated => "result_validated",
            Self::ResultHasGaps => "result_has_gaps",
            Self::CorrectionRequested => "correction_requested",
            Self::TerminalFailure => "terminal_failure",
            Self::ProviderPaused => "provider_paused",
            Self::FollowUpAsked => "follow_up_asked",
        }
    }
}
/// Which consent the task is waiting on while it is in `AWAITING_CONSENT`.
///
/// The state is entered from `DRAFT` for the initial consent and from `RUNNING`
/// for an in-task approval, and the two leave it by different edges. Keeping
/// the distinction as data means the reducer never has to guess which one a
/// decision belongs to.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ConsentStage {
    /// The first consent, before any work has been done.
    Initial,
    /// An in-task approval for an exact action, data, or scope expansion.
    InTask,
}

impl ConsentStage {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Initial => "initial",
            Self::InTask => "in_task",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::TaskState;
    use crate::task::DisplayState;

    #[test]
    fn the_display_mapping_is_the_section_table_and_nothing_else() {
        let expected: &[(TaskState, Option<DisplayState>)] = &[
            (TaskState::Draft, None),
            (TaskState::AwaitingConsent, None),
            (TaskState::Queued, Some(DisplayState::Running)),
            (TaskState::Running, Some(DisplayState::Running)),
            (TaskState::Completing, Some(DisplayState::Running)),
            (TaskState::WaitingUser, Some(DisplayState::WaitingForYou)),
            (TaskState::Pausing, Some(DisplayState::Paused)),
            (TaskState::Paused, Some(DisplayState::Paused)),
            (TaskState::Completed, Some(DisplayState::Done)),
            (TaskState::Partial, Some(DisplayState::PartlyDone)),
            (TaskState::Cancelling, Some(DisplayState::Stopped)),
            (TaskState::Cancelled, Some(DisplayState::Stopped)),
            (TaskState::Failed, Some(DisplayState::Failed)),
        ];
        assert_eq!(expected.len(), TaskState::ALL.len());
        for (state, display) in expected {
            assert_eq!(state.display(), *display, "{}", state.label());
        }
    }

    #[test]
    fn every_user_visible_state_is_reachable_from_some_internal_state() {
        for display in DisplayState::ALL {
            assert!(
                TaskState::ALL
                    .iter()
                    .any(|state| state.display() == Some(*display)),
                "{}",
                display.label()
            );
        }
    }

    #[test]
    fn the_internal_name_is_never_the_user_visible_one() {
        for state in TaskState::ALL {
            if let Some(display) = state.display() {
                assert_ne!(state.label(), display.user_visible_text());
            }
        }
    }

    #[test]
    fn the_terminal_set_is_the_four_states_the_specification_names() {
        let terminal: Vec<&str> = TaskState::ALL
            .iter()
            .filter(|state| state.is_terminal())
            .map(|state| state.label())
            .collect();
        assert_eq!(
            terminal,
            vec!["CANCELLED", "COMPLETED", "PARTIAL", "FAILED"]
        );
        assert_eq!(terminal.len(), TaskState::TERMINAL.len());
    }
}
