// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The event envelope (domain model section 17.1).
//!
//! Every durable statement about a task is one of these. The envelope carries
//! identity, ordering, causation, and a redaction class; the payload carries
//! the decision facts. Three fields are not the caller's to set — the event
//! identifier, the wall-clock time, and the monotonic sequence — because a
//! caller that could set them could rewrite history rather than append to it.
//!
//! The enumerations here are closed and their wire names are compiled-in
//! strings. That is what lets an audit record and a telemetry record name an
//! event type without carrying a caller-supplied string.

use core::fmt;

use crate::clock::UtcMillis;
use crate::ids::{CorrelationId, EventId, TraceId};
use crate::payload::EventPayload;

/// The position of one event inside one stream.
///
/// Sequences are per stream and start at one. Nothing compares sequences from
/// different streams: two aggregates are two independent histories.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Sequence(pub u64);

impl Sequence {
    /// The sequence of the first event in a stream.
    pub const FIRST: Self = Self(1);

    /// The next sequence, or `None` once the counter is exhausted.
    pub fn next(self) -> Option<Self> {
        self.0.checked_add(1).map(Self)
    }
}

impl fmt::Display for Sequence {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}", self.0)
    }
}

/// The version of the event schema a record was written under.
///
/// An event type and version is immutable after release: a migration
/// transforms projections or appends corrective events, and never edits one
/// that is already written.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct SchemaVersion(pub u32);

impl fmt::Display for SchemaVersion {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}", self.0)
    }
}

/// Which aggregate a stream belongs to (domain model section 4).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum AggregateType {
    /// A task and everything it did.
    Task,
    /// One proposed action and its attempts.
    Action,
    /// A workspace and its source membership.
    Workspace,
    /// A produced artifact and its lineage.
    Artifact,
}

impl AggregateType {
    /// Every aggregate type, in declaration order.
    pub const ALL: &'static [Self] = &[Self::Task, Self::Action, Self::Workspace, Self::Artifact];

    /// A short, compiled-in name.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Task => "task",
            Self::Action => "action",
            Self::Workspace => "workspace",
            Self::Artifact => "artifact",
        }
    }
}

/// An opaque aggregate identifier.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct AggregateId(String);

impl AggregateId {
    /// Wraps an opaque identifier.
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for AggregateId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// The stream one event belongs to: an aggregate type plus an aggregate
/// identifier.
///
/// Sequences, revisions, and gaps are all per stream.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct StreamId {
    /// Which kind of aggregate.
    pub aggregate_type: AggregateType,
    /// Which one.
    pub aggregate_id: AggregateId,
}

impl StreamId {
    /// Builds a stream identifier.
    pub fn new(aggregate_type: AggregateType, aggregate_id: AggregateId) -> Self {
        Self {
            aggregate_type,
            aggregate_id,
        }
    }
}

impl fmt::Display for StreamId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "{}/{}",
            self.aggregate_type.label(),
            self.aggregate_id
        )
    }
}

/// Who caused an event (domain model section 17.1).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Actor {
    /// The person using the browser.
    User,
    /// The task reducer.
    TaskRuntime,
    /// The policy broker.
    Policy,
    /// The browser process.
    Browser,
    /// The runtime itself: startup, retention, recovery.
    System,
}

impl Actor {
    /// Every actor, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::User,
        Self::TaskRuntime,
        Self::Policy,
        Self::Browser,
        Self::System,
    ];

    /// A short, compiled-in name.
    pub const fn label(self) -> &'static str {
        match self {
            Self::User => "user",
            Self::TaskRuntime => "task_runtime",
            Self::Policy => "policy",
            Self::Browser => "browser",
            Self::System => "system",
        }
    }
}

/// How much a record is allowed to carry.
///
/// It is a ceiling, never a permission: a class of [`Self::RedactedSummary`]
/// does not make a field eligible that the field policy drops, and the two
/// gates are applied independently.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum RedactionClass {
    /// Counts and enumerated names only. Eligible for telemetry.
    Operational,
    /// Identifiers and decision facts. The ordinary class for an audit event.
    Decision,
    /// Decision facts plus a bounded, redacted summary.
    RedactedSummary,
}

impl RedactionClass {
    /// Every class, in declaration order.
    pub const ALL: &'static [Self] = &[Self::Operational, Self::Decision, Self::RedactedSummary];

    /// A short, compiled-in name.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Operational => "operational",
            Self::Decision => "decision",
            Self::RedactedSummary => "redacted_summary",
        }
    }

    /// Whether an event of this class may reach operational telemetry at all.
    ///
    /// Only [`Self::Operational`] may. Product audit and operational telemetry
    /// are separate systems with separate consent and retention, and telemetry
    /// does not reuse the audit payload by convenience (domain model section
    /// 22).
    pub const fn is_telemetry_eligible(self) -> bool {
        matches!(self, Self::Operational)
    }
}

/// What happened (domain model section 17.2).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum EventType {
    /// A task was created.
    TaskCreated,
    /// The task's source scope was set.
    SourceScopeSet,
    /// A provider route was selected.
    ProviderRouteSelected,
    /// The task requested initial consent.
    ConsentRequested,
    /// The task entered the execution queue.
    TaskQueued,
    /// A plan was created.
    PlanCreated,
    /// A plan was superseded.
    PlanSuperseded,
    /// One plan step changed state.
    PlanStepAdvanced,
    /// The task started.
    TaskStarted,
    /// The browser prepared the exact task-owned tab used for bounded source discovery.
    DiscoveryTabPrepared,
    /// An actor lease was issued.
    ActorLeaseIssued,
    /// An observation was captured.
    ObservationCaptured,
    /// A model invocation started.
    ModelInvocationStarted,
    /// A model invocation completed.
    ModelInvocationCompleted,
    /// An action was proposed.
    ActionProposed,
    /// An approval was requested.
    ApprovalRequested,
    /// An approval was decided.
    ApprovalDecided,
    /// A capability was issued.
    CapabilityIssued,
    /// Policy refused a proposed action.
    ActionRejected,
    /// A dispatch started. Recorded before the side effect.
    ActionDispatchStarted,
    /// Verification of an action completed.
    ActionVerificationCompleted,
    /// The user took over.
    UserTookOver,
    /// The task began revoking authority before pausing.
    TaskPausing,
    /// The task paused.
    TaskPaused,
    /// The task resumed.
    TaskResumed,
    /// The task is waiting for user input or a source.
    TaskWaitingUser,
    /// The user supplied requested task input.
    UserInputSupplied,
    /// The task began revoking authority before cancellation.
    TaskCancelling,
    /// The task began validating a result.
    TaskCompleting,
    /// The task completed with labelled gaps.
    TaskPartial,
    /// A fact was accepted.
    FactAccepted,
    /// A fact was corrected.
    FactCorrected,
    /// A source left the task scope.
    SourceExcluded,
    /// A conflict was detected.
    ConflictDetected,
    /// An artifact became ready.
    ArtifactReady,
    /// An artifact was accepted.
    ArtifactAccepted,
    /// An artifact was exported.
    ArtifactExported,
    /// A task budget was charged.
    BudgetCharged,
    /// The task completed.
    TaskCompleted,
    /// The task was cancelled.
    TaskCancelled,
    /// The task failed.
    TaskFailed,
    /// Deletion was requested.
    DeletionRequested,
    /// Deletion completed.
    DeletionCompleted,
    /// A visible native permission request was committed before presentation.
    PermissionRequested,
    /// One native permission request received an exact terminal decision.
    PermissionDecided,
    /// The assistant stopped and gave the page to the person.
    HandoverRequested,
    /// The person came back and the assistant resumed under a new lease.
    HandoverCompleted,
    /// The handover window closed with nobody coming back.
    HandoverExpired,
    /// A model invocation ended without an answer.
    ModelInvocationFailed,
    /// The durable context-eviction boundary moved before the next turn.
    ContextEvicted,
}

impl EventType {
    /// Every event type, in the order the domain model lists them.
    pub const ALL: &'static [Self] = &[
        Self::TaskCreated,
        Self::SourceScopeSet,
        Self::ProviderRouteSelected,
        Self::ConsentRequested,
        Self::TaskQueued,
        Self::PlanCreated,
        Self::PlanSuperseded,
        Self::PlanStepAdvanced,
        Self::TaskStarted,
        Self::DiscoveryTabPrepared,
        Self::ActorLeaseIssued,
        Self::ObservationCaptured,
        Self::ModelInvocationStarted,
        Self::ModelInvocationCompleted,
        Self::ActionProposed,
        Self::ApprovalRequested,
        Self::ApprovalDecided,
        Self::CapabilityIssued,
        Self::ActionRejected,
        Self::ActionDispatchStarted,
        Self::ActionVerificationCompleted,
        Self::UserTookOver,
        Self::TaskPausing,
        Self::TaskPaused,
        Self::TaskResumed,
        Self::TaskWaitingUser,
        Self::UserInputSupplied,
        Self::TaskCancelling,
        Self::TaskCompleting,
        Self::TaskPartial,
        Self::FactAccepted,
        Self::FactCorrected,
        Self::SourceExcluded,
        Self::ConflictDetected,
        Self::ArtifactReady,
        Self::ArtifactAccepted,
        Self::ArtifactExported,
        Self::BudgetCharged,
        Self::TaskCompleted,
        Self::TaskCancelled,
        Self::TaskFailed,
        Self::DeletionRequested,
        Self::DeletionCompleted,
        Self::PermissionRequested,
        Self::PermissionDecided,
        Self::HandoverRequested,
        Self::HandoverCompleted,
        Self::HandoverExpired,
        Self::ModelInvocationFailed,
        Self::ContextEvicted,
    ];

    /// The compiled-in wire name.
    pub const fn wire(self) -> &'static str {
        match self {
            Self::TaskCreated => "TaskCreated",
            Self::SourceScopeSet => "SourceScopeSet",
            Self::ProviderRouteSelected => "ProviderRouteSelected",
            Self::ConsentRequested => "ConsentRequested",
            Self::TaskQueued => "TaskQueued",
            Self::PlanCreated => "PlanCreated",
            Self::PlanSuperseded => "PlanSuperseded",
            Self::PlanStepAdvanced => "PlanStepAdvanced",
            Self::TaskStarted => "TaskStarted",
            Self::DiscoveryTabPrepared => "DiscoveryTabPrepared",
            Self::ActorLeaseIssued => "ActorLeaseIssued",
            Self::ObservationCaptured => "ObservationCaptured",
            Self::ModelInvocationStarted => "ModelInvocationStarted",
            Self::ModelInvocationCompleted => "ModelInvocationCompleted",
            Self::ActionProposed => "ActionProposed",
            Self::ApprovalRequested => "ApprovalRequested",
            Self::ApprovalDecided => "ApprovalDecided",
            Self::CapabilityIssued => "CapabilityIssued",
            Self::ActionRejected => "ActionRejected",
            Self::ActionDispatchStarted => "ActionDispatchStarted",
            Self::ActionVerificationCompleted => "ActionVerificationCompleted",
            Self::UserTookOver => "UserTookOver",
            Self::TaskPausing => "TaskPausing",
            Self::TaskPaused => "TaskPaused",
            Self::TaskResumed => "TaskResumed",
            Self::TaskWaitingUser => "TaskWaitingUser",
            Self::UserInputSupplied => "UserInputSupplied",
            Self::TaskCancelling => "TaskCancelling",
            Self::TaskCompleting => "TaskCompleting",
            Self::TaskPartial => "TaskPartial",
            Self::FactAccepted => "FactAccepted",
            Self::FactCorrected => "FactCorrected",
            Self::SourceExcluded => "SourceExcluded",
            Self::ConflictDetected => "ConflictDetected",
            Self::ArtifactReady => "ArtifactReady",
            Self::ArtifactAccepted => "ArtifactAccepted",
            Self::ArtifactExported => "ArtifactExported",
            Self::BudgetCharged => "BudgetCharged",
            Self::TaskCompleted => "TaskCompleted",
            Self::TaskCancelled => "TaskCancelled",
            Self::TaskFailed => "TaskFailed",
            Self::DeletionRequested => "DeletionRequested",
            Self::DeletionCompleted => "DeletionCompleted",
            Self::PermissionRequested => "PermissionRequested",
            Self::PermissionDecided => "PermissionDecided",
            Self::HandoverRequested => "HandoverRequested",
            Self::HandoverCompleted => "HandoverCompleted",
            Self::HandoverExpired => "HandoverExpired",
            Self::ModelInvocationFailed => "ModelInvocationFailed",
            Self::ContextEvicted => "ContextEvicted",
        }
    }

    /// Parses a wire name. `None` means the value is outside the enumeration:
    /// the caller fails closed and never substitutes a known member.
    pub fn from_wire(value: &str) -> Option<Self> {
        Self::ALL.iter().copied().find(|kind| kind.wire() == value)
    }
}

/// What a caller asks to append.
///
/// It is the envelope minus the three fields the journal owns. There is no
/// constructor that lets a caller supply an event identifier, a wall-clock
/// time, or a sequence, which is how "append-only" is enforced rather than
/// documented.
#[derive(Clone, Debug, PartialEq)]
pub struct EventDraft {
    /// The stream to append to.
    pub stream: StreamId,
    /// The revision the caller believes the aggregate is at.
    ///
    /// The append is refused when it disagrees, so two writers conflict
    /// visibly rather than one silently overwriting the other.
    pub expected_revision: u64,
    /// What happened.
    pub event_type: EventType,
    /// The schema version this event was written under.
    pub schema_version: SchemaVersion,
    /// Who caused it.
    pub actor: Actor,
    /// The task, when the event belongs to one.
    pub task_id: Option<AggregateId>,
    /// The unit of work this event belongs to.
    pub trace_id: TraceId,
    /// The event that caused this one.
    pub causation_event_id: Option<EventId>,
    /// A correlation across streams.
    pub correlation_id: Option<CorrelationId>,
    /// How much this record may carry.
    pub redaction_class: RedactionClass,
    /// The decision facts.
    pub payload: EventPayload,
}

/// One durable statement about one aggregate.
///
/// Immutable once built. Every field is readable and none is writable, so an
/// event that has been appended cannot be edited through this type.
#[derive(Clone, Debug, PartialEq)]
pub struct EventEnvelope {
    event_id: EventId,
    stream: StreamId,
    aggregate_revision: u64,
    event_type: EventType,
    schema_version: SchemaVersion,
    occurred_at_utc: UtcMillis,
    monotonic_sequence: Sequence,
    actor: Actor,
    task_id: Option<AggregateId>,
    trace_id: TraceId,
    causation_event_id: Option<EventId>,
    correlation_id: Option<CorrelationId>,
    redaction_class: RedactionClass,
    payload: EventPayload,
}

impl EventEnvelope {
    /// Builds an envelope. Only the journal calls this.
    pub(crate) fn seal(
        draft: EventDraft,
        event_id: EventId,
        occurred_at_utc: UtcMillis,
        monotonic_sequence: Sequence,
        aggregate_revision: u64,
    ) -> Self {
        Self {
            event_id,
            stream: draft.stream,
            aggregate_revision,
            event_type: draft.event_type,
            schema_version: draft.schema_version,
            occurred_at_utc,
            monotonic_sequence,
            actor: draft.actor,
            task_id: draft.task_id,
            trace_id: draft.trace_id,
            causation_event_id: draft.causation_event_id,
            correlation_id: draft.correlation_id,
            redaction_class: draft.redaction_class,
            payload: draft.payload,
        }
    }

    /// The event identifier, minted by the journal.
    pub fn event_id(&self) -> &EventId {
        &self.event_id
    }

    /// The stream this event belongs to.
    pub fn stream(&self) -> &StreamId {
        &self.stream
    }

    /// The aggregate revision this event produced.
    pub fn aggregate_revision(&self) -> u64 {
        self.aggregate_revision
    }

    /// What happened.
    pub fn event_type(&self) -> EventType {
        self.event_type
    }

    /// The schema version this event was written under.
    pub fn schema_version(&self) -> SchemaVersion {
        self.schema_version
    }

    /// When it happened, on the wall clock.
    pub fn occurred_at_utc(&self) -> UtcMillis {
        self.occurred_at_utc
    }

    /// Where it sits in its stream.
    pub fn monotonic_sequence(&self) -> Sequence {
        self.monotonic_sequence
    }

    /// Who caused it.
    pub fn actor(&self) -> Actor {
        self.actor
    }

    /// The task, when the event belongs to one.
    pub fn task_id(&self) -> Option<&AggregateId> {
        self.task_id.as_ref()
    }

    /// The unit of work.
    pub fn trace_id(&self) -> &TraceId {
        &self.trace_id
    }

    /// The event that caused this one.
    pub fn causation_event_id(&self) -> Option<&EventId> {
        self.causation_event_id.as_ref()
    }

    /// The correlation across streams.
    pub fn correlation_id(&self) -> Option<&CorrelationId> {
        self.correlation_id.as_ref()
    }

    /// How much this record may carry.
    pub fn redaction_class(&self) -> RedactionClass {
        self.redaction_class
    }

    /// The decision facts, as the caller supplied them.
    ///
    /// Reading them is not the same as serializing them: both serializers
    /// redact this payload independently, and neither trusts it.
    pub fn payload(&self) -> &EventPayload {
        &self.payload
    }
}
