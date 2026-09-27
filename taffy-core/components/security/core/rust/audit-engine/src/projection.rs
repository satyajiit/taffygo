// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Projections rebuilt from the journal (domain model section 17.3).
//!
//! A projection is derived state. It is never the authority for anything, and
//! it must be reconstructible: "projection corruption must be recoverable from
//! the journal". That makes replay a property rather than a feature — folding
//! the journal has to produce exactly what applying each event as it arrived
//! produced, and a test asserts it rather than assuming it.
//!
//! [`TaskProjection`] is deliberately small. It is the task timeline and the
//! action audit view of section 17.3, enough to prove the replay property
//! without inventing product state that the task reducer in `task-engine` owns.

use crate::envelope::{AggregateId, EventEnvelope, EventType, Sequence};
use crate::journal::find_sequence_gaps;
use crate::payload::{FieldName, FieldValue};

/// Where a task has got to.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum TaskState {
    /// No task event has been seen.
    #[default]
    Unknown,
    /// Created, not yet started.
    Created,
    /// Running.
    Running,
    /// Paused.
    Paused,
    /// Finished successfully.
    Completed,
    /// Cancelled.
    Cancelled,
    /// Failed.
    Failed,
}

impl TaskState {
    /// A short, compiled-in name.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Unknown => "unknown",
            Self::Created => "created",
            Self::Running => "running",
            Self::Paused => "paused",
            Self::Completed => "completed",
            Self::Cancelled => "cancelled",
            Self::Failed => "failed",
        }
    }

    /// Whether the task has finished.
    pub const fn is_terminal(self) -> bool {
        matches!(self, Self::Completed | Self::Cancelled | Self::Failed)
    }
}

/// The task timeline and action audit view, rebuilt from events.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TaskProjection {
    /// The task, once an event has named one.
    pub task_id: Option<AggregateId>,
    /// Where the task has got to.
    pub state: TaskState,
    /// The revision of the last event applied.
    pub revision: u64,
    /// The sequence of the last event applied.
    pub last_sequence: Option<Sequence>,
    /// How many actions were proposed.
    pub actions_proposed: u64,
    /// How many capabilities were issued.
    pub capabilities_issued: u64,
    /// How many dispatches started.
    pub dispatches_started: u64,
    /// How many verifications completed.
    pub verifications_completed: u64,
    /// How many approvals were requested.
    pub approvals_requested: u64,
    /// How many approvals were decided.
    pub approvals_decided: u64,
    /// How many times the user took over.
    pub take_overs: u64,
    /// The last protocol result code recorded, as a compiled-in name.
    pub last_result_code: Option<&'static str>,
}

impl TaskProjection {
    /// An empty projection.
    pub const fn new() -> Self {
        Self {
            task_id: None,
            state: TaskState::Unknown,
            revision: 0,
            last_sequence: None,
            actions_proposed: 0,
            capabilities_issued: 0,
            dispatches_started: 0,
            verifications_completed: 0,
            approvals_requested: 0,
            approvals_decided: 0,
            take_overs: 0,
            last_result_code: None,
        }
    }

    /// Applies one event.
    ///
    /// Total: an event this projection has no interest in advances the
    /// revision and changes nothing else. It never fails, because a projection
    /// that refused an event would be claiming authority the journal holds.
    pub fn apply(&mut self, event: &EventEnvelope) {
        self.revision = event.aggregate_revision();
        self.last_sequence = Some(event.monotonic_sequence());
        if self.task_id.is_none() {
            self.task_id = event.task_id().cloned();
        }
        if let Some(FieldValue::Enumerated(code)) = event.payload().get(FieldName::ResultCode) {
            self.last_result_code = Some(code);
        }

        match event.event_type() {
            EventType::TaskCreated => self.state = TaskState::Created,
            EventType::TaskStarted | EventType::TaskResumed => self.state = TaskState::Running,
            EventType::TaskPaused => self.state = TaskState::Paused,
            EventType::TaskCompleted => self.state = TaskState::Completed,
            EventType::TaskCancelled => self.state = TaskState::Cancelled,
            EventType::TaskFailed => self.state = TaskState::Failed,
            EventType::ActionProposed => {
                self.actions_proposed = self.actions_proposed.saturating_add(1);
            }
            EventType::CapabilityIssued => {
                self.capabilities_issued = self.capabilities_issued.saturating_add(1);
            }
            EventType::ActionDispatchStarted => {
                self.dispatches_started = self.dispatches_started.saturating_add(1);
            }
            EventType::ActionVerificationCompleted => {
                self.verifications_completed = self.verifications_completed.saturating_add(1);
            }
            EventType::ApprovalRequested => {
                self.approvals_requested = self.approvals_requested.saturating_add(1);
            }
            EventType::ApprovalDecided => {
                self.approvals_decided = self.approvals_decided.saturating_add(1);
            }
            EventType::UserTookOver => self.take_overs = self.take_overs.saturating_add(1),
            _ => {}
        }
    }
}

/// Why a replay could not produce a projection.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ReplayError {
    /// The events are missing one, repeat one, or arrive out of order.
    ///
    /// The projection is not built: a projection over a broken journal would
    /// look like an answer.
    SequenceBroken(Vec<crate::journal::SequenceGap>),
}

/// Rebuilds a projection from a journal.
///
/// The sequence is checked first. A journal with a gap yields
/// [`ReplayError::SequenceBroken`] rather than a projection built from what
/// happened to survive.
pub fn replay(events: &[EventEnvelope]) -> Result<TaskProjection, ReplayError> {
    let gaps = find_sequence_gaps(events);
    if !gaps.is_empty() {
        return Err(ReplayError::SequenceBroken(gaps));
    }

    let mut projection = TaskProjection::new();
    for event in events {
        projection.apply(event);
    }
    Ok(projection)
}
