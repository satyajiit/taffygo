// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The task controls one current reducer state can honestly offer.
//!
//! This projection is owned beside the transition table because the table is
//! the authority. A surface receives the resulting closed list; it never
//! reconstructs task legality from display phases or labels.

use crate::authority::ControlMode;
use crate::command::CommandKind;
use crate::event::EventKind;
use crate::ids::IdSource;
use crate::reducer::Reducer;
use crate::task::{ConsentStage, StateReason, TaskState};
use crate::time::Clock;
use crate::transition::disposition;

/// One user-visible control the reducer can accept as a state-changing command.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum TaskControlKind {
    /// Hold the task after revoking its authority.
    Pause,
    /// Re-enter the queue and revalidate after a user-requested hold.
    Resume,
    /// Revoke assistant/shared control and give the page to the person.
    TakeOver,
    /// End the task without reporting a failure.
    Stop,
}

impl TaskControlKind {
    /// Stable compiled-in label for analytics and contract projections.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Pause => "pause",
            Self::Resume => "resume",
            Self::TakeOver => "take_over",
            Self::Stop => "stop",
        }
    }
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Ordered controls that would make a real state transition now.
    ///
    /// Recorded duplicate commands are deliberately absent: they are safe to
    /// replay, but they are not truthful controls to offer again. A resume is
    /// offered after every hold a person can lift: one they initiated, one the
    /// provider's limit or the network caused, and one the platform imposed
    /// while the app was in the background. Only an automatic settlement stays
    /// held, because there is nothing for the person to have changed.
    ///
    /// The platform hold is the one this list was wrong about, and the shape of
    /// the mistake is worth keeping written down. `BackgroundRestricted` is
    /// raised when Android stops the work because the app is not in front of
    /// the person; by the time anybody can read a Paused pill, that condition
    /// has ended, because they are looking at it. Leaving it off this list gave
    /// the one pause whose cause is always already over the one state with no
    /// way out of it — a task held forever behind a pill that said "Paused" and
    /// offered nothing.
    #[must_use]
    pub fn allowed_task_controls(&self) -> Vec<TaskControlKind> {
        let user_pause_settled = self.task().state() == TaskState::Paused
            && self
                .journal()
                .events()
                .rev()
                .find(|record| record.event.kind == EventKind::TaskPausing)
                .and_then(|record| record.event.reason)
                .is_some_and(is_resumable_pause_reason);
        controls_for(
            self.task().state(),
            self.task().control_mode(),
            user_pause_settled,
            self.task().consent_stage() == Some(ConsentStage::Initial),
        )
    }
}

const fn is_resumable_pause_reason(reason: StateReason) -> bool {
    matches!(
        reason,
        StateReason::UserPaused
            | StateReason::UserTookOver
            | StateReason::ProviderPaused
            | StateReason::BackgroundRestricted
    )
}

fn controls_for(
    state: TaskState,
    control_mode: ControlMode,
    user_pause_settled: bool,
    initial_consent_pending: bool,
) -> Vec<TaskControlKind> {
    let mut controls = Vec::with_capacity(4);
    if !initial_consent_pending
        && disposition(state, CommandKind::PauseTask)
            .targets()
            .contains(&TaskState::Pausing)
    {
        controls.push(TaskControlKind::Pause);
    }
    if user_pause_settled
        && disposition(state, CommandKind::ResumeTask)
            .targets()
            .contains(&TaskState::Queued)
    {
        controls.push(TaskControlKind::Resume);
    }
    if !initial_consent_pending
        && control_mode != ControlMode::User
        && disposition(state, CommandKind::TakeOver)
            .targets()
            .contains(&TaskState::Pausing)
    {
        controls.push(TaskControlKind::TakeOver);
    }
    if disposition(state, CommandKind::CancelTask)
        .targets()
        .iter()
        .any(|target| matches!(target, TaskState::Cancelling | TaskState::Cancelled))
    {
        controls.push(TaskControlKind::Stop);
    }
    controls
}

#[cfg(test)]
mod tests {
    use super::{controls_for, TaskControlKind};
    use crate::{ControlMode, TaskState};

    const PAUSE: TaskControlKind = TaskControlKind::Pause;
    const RESUME: TaskControlKind = TaskControlKind::Resume;
    const TAKE_OVER: TaskControlKind = TaskControlKind::TakeOver;
    const STOP: TaskControlKind = TaskControlKind::Stop;

    #[test]
    fn controls_are_exhaustive_over_every_state_and_control_mode() {
        for state in TaskState::ALL {
            for mode in [
                ControlMode::User,
                ControlMode::Shared,
                ControlMode::Assistant,
            ] {
                let actual = controls_for(*state, mode, *state == TaskState::Paused, false);
                let expected: &[TaskControlKind] = match state {
                    TaskState::Draft | TaskState::Queued | TaskState::Completing => &[STOP],
                    TaskState::AwaitingConsent | TaskState::Running | TaskState::WaitingUser => {
                        match mode {
                            ControlMode::User => &[PAUSE, STOP],
                            ControlMode::Shared | ControlMode::Assistant => {
                                &[PAUSE, TAKE_OVER, STOP]
                            }
                        }
                    }
                    TaskState::Paused => &[RESUME, STOP],
                    TaskState::Pausing
                    | TaskState::Cancelling
                    | TaskState::Cancelled
                    | TaskState::Completed
                    | TaskState::Partial
                    | TaskState::Failed => &[],
                };
                assert_eq!(actual, expected, "{} x {mode:?}", state.label());
                assert!(actual.windows(2).all(|pair| pair[0] != pair[1]));
            }
        }
    }

    #[test]
    fn initial_consent_offers_only_stop() {
        for mode in [
            ControlMode::User,
            ControlMode::Shared,
            ControlMode::Assistant,
        ] {
            assert_eq!(
                controls_for(TaskState::AwaitingConsent, mode, false, true),
                [STOP]
            );
        }
    }

    #[test]
    fn only_a_user_initiated_settled_pause_can_resume() {
        assert_eq!(
            controls_for(TaskState::Paused, ControlMode::Assistant, true, false),
            [RESUME, STOP]
        );
        assert_eq!(
            controls_for(TaskState::Paused, ControlMode::Assistant, false, false),
            [STOP]
        );
    }

    #[test]
    fn labels_are_closed_and_content_free() {
        assert_eq!(PAUSE.label(), "pause");
        assert_eq!(RESUME.label(), "resume");
        assert_eq!(TAKE_OVER.label(), "take_over");
        assert_eq!(STOP.label(), "stop");
    }
}
