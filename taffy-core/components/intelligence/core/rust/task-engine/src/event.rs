// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the journal records about a transition (domain model section 17).
//!
//! # Every transition carries a cause and a trace
//!
//! An event names the command that caused it, the states it moved between, the
//! reason it moved, and the trace the command carried. That is what makes a
//! replay checkable: the reducer recomputes the state and the journal says what
//! the state was, and a disagreement is a corruption rather than a surprise.
//!
//! # Events carry identifiers and decisions, never content
//!
//! There is no free-text field here. A subject is an identifier, a reason is a
//! compiled-in name, and a count is a number, so no page text, prompt, model
//! output, file name, or selected text can travel in one. `audit-engine`
//! redacts again independently when it writes the audit record, because a
//! recorder that trusts its caller is not a control.

use bip_types::identity::{ActionId, TabId};

use crate::authority::{ActorLeaseId, CapabilityId};
use crate::command::CommandKind;
use crate::handover::HandoverId;
use crate::ids::{ArtifactId, PlanId, PlanStepId};
use crate::permission::PermissionRequestId;
use crate::records::{FactId, SourceId};
use crate::task::{StateReason, TaskState};

/// What happened.
///
/// Named after the section 17.2 vocabulary where one exists. The states the
/// section 9.2 machine passes through that section 17.2 does not name — queued,
/// settling, and the two validated results — have their own members, because a
/// transition the journal cannot name is a transition a replay cannot check.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum EventKind {
    /// The task was created.
    TaskCreated,
    /// The task's source scope was set.
    SourceScopeSet,
    /// A provider route was selected.
    ProviderRouteSelected,
    /// The user was asked to consent.
    ConsentRequested,
    /// The task entered the queue.
    TaskQueued,
    /// The browser prepared the bounded navigation context for a zero-source
    /// Web errand. This event grants no page authority.
    DiscoveryTabPrepared,
    /// A plan revision was created.
    PlanCreated,
    /// A plan revision was superseded.
    PlanSuperseded,
    /// A plan step moved.
    PlanStepAdvanced,
    /// The task started executing.
    TaskStarted,
    /// An action was proposed.
    ActionProposed,
    /// The user was asked to approve an action.
    ApprovalRequested,
    /// The user decided.
    ApprovalDecided,
    /// Policy issued a capability.
    CapabilityIssued,
    /// Policy refused a proposal.
    ActionRejected,
    /// A dispatch started. Recorded before the side effect.
    ActionDispatchStarted,
    /// An action's verification completed.
    ActionVerificationCompleted,
    /// The task stopped on a missing source or input.
    TaskWaitingUser,
    /// The user supplied what was missing.
    UserInputSupplied,
    /// A native permission request was committed before the platform surface.
    PermissionRequested,
    /// A native permission request received one terminal platform decision.
    PermissionDecided,
    /// The assistant stopped and gave the page to the person.
    HandoverRequested,
    /// The person came back and the assistant resumed under a new lease.
    HandoverCompleted,
    /// The handover window closed with nobody coming back.
    HandoverExpired,
    /// The user took over.
    UserTookOver,
    /// The task began revoking authority on the way to being held.
    TaskPausing,
    /// The task is held.
    TaskPaused,
    /// The task resumed, into the queue.
    TaskResumed,
    /// The task began revoking authority on the way to being stopped.
    TaskCancelling,
    /// The task stopped.
    TaskCancelled,
    /// A result candidate is being validated.
    TaskCompleting,
    /// The task finished with a validated complete result.
    TaskCompleted,
    /// The task finished with labelled gaps.
    TaskPartial,
    /// The task failed.
    TaskFailed,
    /// A fact was corrected.
    FactCorrected,
    /// A source was removed from scope.
    SourceExcluded,
    /// A bounded deterministic artifact became ready.
    ArtifactReady,
    /// An artifact was accepted.
    ArtifactAccepted,
    /// An artifact was exported.
    ArtifactExported,
    /// A budget was charged.
    BudgetCharged,
    /// The model answered a turn. The turn's content-free digest is on the
    /// record; the answer itself never is.
    ModelTurnRecorded,
    /// A model turn ended without an answer, and the gap is on the record.
    ModelTurnGapRecorded,
    /// The durable context-eviction boundary moved before the next turn.
    ContextEvicted,
    /// The person asked a follow-up of a finished task, which went back to
    /// work. The question itself is never on the record.
    FollowUpAsked,
}

impl EventKind {
    /// Every kind, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::TaskCreated,
        Self::SourceScopeSet,
        Self::ProviderRouteSelected,
        Self::ConsentRequested,
        Self::TaskQueued,
        Self::DiscoveryTabPrepared,
        Self::PlanCreated,
        Self::PlanSuperseded,
        Self::PlanStepAdvanced,
        Self::TaskStarted,
        Self::ActionProposed,
        Self::ApprovalRequested,
        Self::ApprovalDecided,
        Self::CapabilityIssued,
        Self::ActionRejected,
        Self::ActionDispatchStarted,
        Self::ActionVerificationCompleted,
        Self::TaskWaitingUser,
        Self::UserInputSupplied,
        Self::PermissionRequested,
        Self::PermissionDecided,
        Self::HandoverRequested,
        Self::HandoverCompleted,
        Self::HandoverExpired,
        Self::UserTookOver,
        Self::TaskPausing,
        Self::TaskPaused,
        Self::TaskResumed,
        Self::TaskCancelling,
        Self::TaskCancelled,
        Self::TaskCompleting,
        Self::TaskCompleted,
        Self::TaskPartial,
        Self::TaskFailed,
        Self::FactCorrected,
        Self::SourceExcluded,
        Self::ArtifactReady,
        Self::ArtifactAccepted,
        Self::ArtifactExported,
        Self::BudgetCharged,
        Self::ModelTurnRecorded,
        Self::ModelTurnGapRecorded,
        Self::ContextEvicted,
        Self::FollowUpAsked,
    ];

    /// The compiled-in wire name.
    pub const fn wire(self) -> &'static str {
        match self {
            Self::TaskCreated => "TaskCreated",
            Self::SourceScopeSet => "SourceScopeSet",
            Self::ProviderRouteSelected => "ProviderRouteSelected",
            Self::ConsentRequested => "ConsentRequested",
            Self::TaskQueued => "TaskQueued",
            Self::DiscoveryTabPrepared => "DiscoveryTabPrepared",
            Self::PlanCreated => "PlanCreated",
            Self::PlanSuperseded => "PlanSuperseded",
            Self::PlanStepAdvanced => "PlanStepAdvanced",
            Self::TaskStarted => "TaskStarted",
            Self::ActionProposed => "ActionProposed",
            Self::ApprovalRequested => "ApprovalRequested",
            Self::ApprovalDecided => "ApprovalDecided",
            Self::CapabilityIssued => "CapabilityIssued",
            Self::ActionRejected => "ActionRejected",
            Self::ActionDispatchStarted => "ActionDispatchStarted",
            Self::ActionVerificationCompleted => "ActionVerificationCompleted",
            Self::TaskWaitingUser => "TaskWaitingUser",
            Self::UserInputSupplied => "UserInputSupplied",
            Self::PermissionRequested => "PermissionRequested",
            Self::PermissionDecided => "PermissionDecided",
            Self::HandoverRequested => "HandoverRequested",
            Self::HandoverCompleted => "HandoverCompleted",
            Self::HandoverExpired => "HandoverExpired",
            Self::UserTookOver => "UserTookOver",
            Self::TaskPausing => "TaskPausing",
            Self::TaskPaused => "TaskPaused",
            Self::TaskResumed => "TaskResumed",
            Self::TaskCancelling => "TaskCancelling",
            Self::TaskCancelled => "TaskCancelled",
            Self::TaskCompleting => "TaskCompleting",
            Self::TaskCompleted => "TaskCompleted",
            Self::TaskPartial => "TaskPartial",
            Self::TaskFailed => "TaskFailed",
            Self::FactCorrected => "FactCorrected",
            Self::SourceExcluded => "SourceExcluded",
            Self::ArtifactReady => "ArtifactReady",
            Self::ArtifactAccepted => "ArtifactAccepted",
            Self::ArtifactExported => "ArtifactExported",
            Self::BudgetCharged => "BudgetCharged",
            Self::ModelTurnRecorded => "ModelTurnRecorded",
            Self::ModelTurnGapRecorded => "ModelTurnGapRecorded",
            Self::ContextEvicted => "ContextEvicted",
            Self::FollowUpAsked => "FollowUpAsked",
        }
    }

    /// Parses a wire name. `None` means the value is outside the enumeration:
    /// the caller fails closed and never substitutes a known member.
    pub fn from_wire(value: &str) -> Option<Self> {
        Self::ALL.iter().copied().find(|kind| kind.wire() == value)
    }
}

/// What an event is about.
///
/// Every member is an identifier. There is no member that can carry text.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EventSubject {
    /// An action.
    Action(ActionId),
    /// A plan revision.
    Plan(PlanId),
    /// A plan step.
    PlanStep(PlanStepId),
    /// An artifact.
    Artifact(ArtifactId),
    /// A source.
    Source(SourceId),
    /// A task-owned discovery tab. It is deliberately distinct from a source:
    /// the tab alone authorizes no page read or action.
    DiscoveryTab(TabId),
    /// A fact.
    Fact(FactId),
    /// A capability.
    Capability(CapabilityId),
    /// A native permission request.
    PermissionRequest(PermissionRequestId),
    /// A handover.
    Handover(HandoverId),
    /// An actor lease held in the browser process.
    ///
    /// The one subject this crate never issues. It is here because a
    /// resumption after a handover has to be readable in the journal as a
    /// *different* stretch of assistant activity from the one before it, and
    /// the lease identity is what says so.
    ActorLease(ActorLeaseId),
}

impl EventSubject {
    /// A short, compiled-in name for what kind of thing this is.
    pub const fn kind_label(&self) -> &'static str {
        match self {
            Self::Action(_) => "action",
            Self::Plan(_) => "plan",
            Self::PlanStep(_) => "plan_step",
            Self::Artifact(_) => "artifact",
            Self::Source(_) => "source",
            Self::DiscoveryTab(_) => "discovery_tab",
            Self::Fact(_) => "fact",
            Self::Capability(_) => "capability",
            Self::PermissionRequest(_) => "permission_request",
            Self::Handover(_) => "handover",
            Self::ActorLease(_) => "actor_lease",
        }
    }

    /// The opaque identifier, for equality and audit correlation only.
    pub fn identifier(&self) -> String {
        match self {
            Self::Action(id) => id.as_str().to_owned(),
            Self::Plan(id) => id.as_str().to_owned(),
            Self::PlanStep(id) => id.as_str().to_owned(),
            Self::Artifact(id) => id.as_str().to_owned(),
            Self::Source(id) => id.to_text(),
            Self::DiscoveryTab(id) => id.as_str().to_owned(),
            Self::Fact(id) => id.to_text(),
            Self::Capability(id) => id.as_str().to_owned(),
            Self::PermissionRequest(id) => id.as_str().to_owned(),
            Self::Handover(id) => id.as_str().to_owned(),
            Self::ActorLease(id) => id.as_str().to_owned(),
        }
    }
}

/// One thing the journal records.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskEvent {
    /// What happened.
    pub kind: EventKind,
    /// The command that caused it.
    pub caused_by: CommandKind,
    /// The state the task was in, for a transition.
    pub from: Option<TaskState>,
    /// The state it reached, for a transition.
    pub to: Option<TaskState>,
    /// Why it moved.
    pub reason: Option<StateReason>,
    /// What it is about.
    pub subject: Option<EventSubject>,
}

impl TaskEvent {
    /// An event that records a transition.
    pub const fn transition(
        kind: EventKind,
        caused_by: CommandKind,
        from: TaskState,
        to: TaskState,
        reason: StateReason,
    ) -> Self {
        Self {
            kind,
            caused_by,
            from: Some(from),
            to: Some(to),
            reason: Some(reason),
            subject: None,
        }
    }

    /// An event that records something other than a transition.
    pub const fn record(kind: EventKind, caused_by: CommandKind) -> Self {
        Self {
            kind,
            caused_by,
            from: None,
            to: None,
            reason: None,
            subject: None,
        }
    }

    /// The same event, about `subject`.
    #[must_use]
    pub fn about(mut self, subject: EventSubject) -> Self {
        self.subject = Some(subject);
        self
    }

    /// Whether the event records a state change.
    pub const fn is_transition(&self) -> bool {
        self.to.is_some()
    }
}

#[cfg(test)]
mod tests {
    use super::{EventKind, EventSubject, TaskEvent};
    use crate::command::CommandKind;
    use crate::ids::PlanId;
    use crate::task::{StateReason, TaskState};

    #[test]
    fn every_event_kind_round_trips_its_wire_name() {
        for kind in EventKind::ALL {
            assert_eq!(EventKind::from_wire(kind.wire()), Some(*kind));
        }
    }

    #[test]
    fn an_unknown_wire_name_fails_closed() {
        for value in ["TaskDone", "", "taskcreated", "TaskCreated "] {
            assert_eq!(EventKind::from_wire(value), None, "{value}");
        }
    }

    #[test]
    fn every_wire_name_is_distinct() {
        let mut seen: Vec<&str> = Vec::new();
        for kind in EventKind::ALL {
            assert!(!seen.contains(&kind.wire()), "{}", kind.wire());
            seen.push(kind.wire());
        }
    }

    #[test]
    fn a_transition_event_carries_where_it_came_from_and_why() {
        let event = TaskEvent::transition(
            EventKind::TaskStarted,
            CommandKind::ExecutorStarted,
            TaskState::Queued,
            TaskState::Running,
            StateReason::ExecutorStarted,
        );
        assert!(event.is_transition());
        assert_eq!(event.from, Some(TaskState::Queued));
        assert_eq!(event.to, Some(TaskState::Running));
        assert_eq!(event.reason, Some(StateReason::ExecutorStarted));
    }

    #[test]
    fn a_subject_is_an_identifier_and_never_free_text() {
        let event = TaskEvent::record(EventKind::PlanCreated, CommandKind::SetPlan)
            .about(EventSubject::Plan(PlanId::new("plan_0")));
        assert!(!event.is_transition());
        assert_eq!(
            event.subject.as_ref().map(EventSubject::kind_label),
            Some("plan")
        );
        assert_eq!(
            event.subject.map(|subject| subject.identifier()),
            Some("plan_0".to_owned())
        );
    }
}
