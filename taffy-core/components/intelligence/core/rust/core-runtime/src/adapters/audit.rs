// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Independent last-redaction projection for transactional task commits.

use core::fmt::Write as _;
use std::rc::Rc;

use audit_engine::redaction::{scrub_identifier, Scrubbed};
use audit_engine::{
    audit_record, project_committed, Actor, AggregateId, AggregateType, AuditRecord,
    CommittedEvent, EventDraft, EventId, EventPayload, EventType, FieldName, RedactionClass,
    SchemaVersion, Sequence, StreamId, TraceId as AuditTraceId,
};
use core_service_types::{
    PersistedAuditActor, PersistedAuditEventType, PersistedAuditRecord, PersistedAuditSubjectKind,
};
use task_engine::journal::EventRecord;
use task_engine::{CommandKind, EventKind, EventSubject, JournalEntry};

use crate::account::Sha256Port;
use crate::ports::{AuditPort, PortError, StorageCommit, TaskCreationAudit};

/// Canonical pure audit adapter. Physical persistence stays in the browser's
/// atomic storage effect.
#[derive(Clone)]
pub struct ProductionAudit {
    digest: Rc<dyn Sha256Port>,
}

impl ProductionAudit {
    /// Uses the reviewed digest primitive; UTC time comes from reducer-owned
    /// journal records written with the ordered service clock.
    pub fn new(digest: Rc<dyn Sha256Port>) -> Self {
        Self { digest }
    }

    fn encode_entries(
        &self,
        task_id: &bip_types::identity::TaskId,
        entries: &[JournalEntry],
    ) -> Result<Vec<PersistedAuditRecord>, PortError> {
        let mut records = Vec::new();
        for record in entries.iter().filter_map(JournalEntry::as_event) {
            let envelope = self.project_event(task_id, record)?;
            let redacted = audit_record(&envelope);
            records.push(persisted_record(record, &redacted)?);
        }
        Ok(records)
    }

    fn project_event(
        &self,
        task_id: &bip_types::identity::TaskId,
        record: &EventRecord,
    ) -> Result<audit_engine::EventEnvelope, PortError> {
        let revision = record.revision;
        let event = &record.event;
        let event_type = event_type(event.kind);
        let payload = subject_payload(
            EventPayload::new().with_identifier(FieldName::TaskId, task_id.as_str()),
            event.subject.as_ref(),
        );
        let stream = StreamId::new(AggregateType::Task, AggregateId::new(task_id.as_str()));
        let event_id = self.event_id(task_id.as_str(), revision, event_type)?;
        project_committed(CommittedEvent {
            draft: EventDraft {
                stream,
                expected_revision: revision.saturating_sub(1),
                event_type,
                schema_version: SchemaVersion(1),
                actor: actor(event.caused_by),
                task_id: Some(AggregateId::new(task_id.as_str())),
                trace_id: AuditTraceId::new(record.trace_id.as_str()),
                causation_event_id: None,
                correlation_id: None,
                redaction_class: RedactionClass::Decision,
                payload,
            },
            event_id,
            occurred_at_utc: audit_engine::UtcMillis(record.recorded_at.0),
            sequence: Sequence(record.sequence),
            aggregate_revision: revision,
        })
        .map_err(|_| PortError::InvalidInput)
    }

    fn event_id(
        &self,
        task_id: &str,
        revision: u64,
        event_type: EventType,
    ) -> Result<EventId, PortError> {
        let mut input = Vec::with_capacity(task_id.len().saturating_add(64));
        input.extend_from_slice(b"taffy/audit-event/v1");
        input.extend_from_slice(task_id.as_bytes());
        input.extend_from_slice(&revision.to_be_bytes());
        input.extend_from_slice(event_type.wire().as_bytes());
        let digest = self
            .digest
            .sha256(&input)
            .map_err(|_| PortError::Unavailable)?;
        let mut id = String::with_capacity(68);
        id.push_str("evt_");
        for byte in digest {
            write!(&mut id, "{byte:02x}").map_err(|_| PortError::Unavailable)?;
        }
        Ok(EventId::new(id))
    }
}

impl core::fmt::Debug for ProductionAudit {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("ProductionAudit")
            .finish_non_exhaustive()
    }
}

impl AuditPort for ProductionAudit {
    fn encode_task_creation(
        &self,
        creation: TaskCreationAudit<'_>,
    ) -> Result<Vec<PersistedAuditRecord>, PortError> {
        self.encode_entries(creation.task_id, creation.journal_entries)
    }

    fn encode_record(
        &self,
        commit: &StorageCommit<'_>,
    ) -> Result<Vec<PersistedAuditRecord>, PortError> {
        self.encode_entries(commit.task_id, commit.journal_entries)
    }
}

fn persisted_record(
    source: &EventRecord,
    redacted: &AuditRecord,
) -> Result<PersistedAuditRecord, PortError> {
    let task_id = redacted.task_id.clone().ok_or(PortError::InvalidInput)?;
    let (subject_kind, subject_id) = persisted_subject(source, redacted);
    Ok(PersistedAuditRecord {
        event_id: redacted.event_id.clone(),
        event_type: persisted_event_type(source.event.kind),
        actor: persisted_actor(actor(source.event.caused_by)),
        task_id,
        subject_kind,
        subject_id,
        revision: source.revision,
        sequence: source.sequence,
        trace_id: redacted.trace_id.clone(),
        occurred_at_utc_ms: redacted.occurred_at_utc,
        dropped_field_count: redacted.dropped_field_count,
        content_values_retained: redacted.content_values_retained,
    })
}

fn persisted_subject(
    source: &EventRecord,
    _redacted: &AuditRecord,
) -> (Option<PersistedAuditSubjectKind>, Option<String>) {
    let Some(subject) = source.event.subject.as_ref() else {
        return (None, None);
    };
    let kind = match subject {
        EventSubject::Action(_) => PersistedAuditSubjectKind::Action,
        EventSubject::Capability(_) => PersistedAuditSubjectKind::Capability,
        EventSubject::Plan(_) => PersistedAuditSubjectKind::Plan,
        EventSubject::PlanStep(_) => PersistedAuditSubjectKind::PlanStep,
        EventSubject::Artifact(_) => PersistedAuditSubjectKind::Artifact,
        EventSubject::Source(_) => PersistedAuditSubjectKind::Source,
        EventSubject::Fact(_) => PersistedAuditSubjectKind::Fact,
        EventSubject::PermissionRequest(_) => PersistedAuditSubjectKind::PermissionRequest,
        EventSubject::Handover(_) => PersistedAuditSubjectKind::Handover,
        EventSubject::ActorLease(_) => PersistedAuditSubjectKind::ActorLease,
        EventSubject::DiscoveryTab(_) => PersistedAuditSubjectKind::DiscoveryTab,
    };
    let identifier = match scrub_identifier(&subject.identifier()) {
        Scrubbed::Dropped => None,
        other => other.value().map(str::to_owned),
    };
    match identifier {
        Some(identifier) => (Some(kind), Some(identifier)),
        None => (None, None),
    }
}

const fn persisted_actor(actor: Actor) -> PersistedAuditActor {
    match actor {
        Actor::User => PersistedAuditActor::User,
        Actor::TaskRuntime => PersistedAuditActor::TaskRuntime,
        Actor::Policy => PersistedAuditActor::Policy,
        Actor::Browser => PersistedAuditActor::Browser,
        Actor::System => PersistedAuditActor::System,
    }
}

const fn actor(command: CommandKind) -> Actor {
    match command {
        CommandKind::RecordPolicyDecision => Actor::Policy,
        CommandKind::DispatchAction | CommandKind::RecordActionOutcome => Actor::Browser,
        command if command.is_user_command() => Actor::User,
        _ => Actor::TaskRuntime,
    }
}

fn subject_payload(payload: EventPayload, subject: Option<&EventSubject>) -> EventPayload {
    match subject {
        Some(EventSubject::Action(id)) => payload.with_identifier(FieldName::ActionId, id.as_str()),
        Some(EventSubject::Capability(id)) => {
            payload.with_identifier(FieldName::CapabilityId, id.as_str())
        }
        // A handover and the lease that resumes it carry no field of their
        // own in the payload vocabulary. The subject kind and the scrubbed
        // identifier above already say which handover and which lease, and
        // inventing a field name here would put a value into a record shape
        // nothing else knows how to redact.
        Some(
            EventSubject::Plan(_)
            | EventSubject::PlanStep(_)
            | EventSubject::Artifact(_)
            | EventSubject::Source(_)
            | EventSubject::Fact(_)
            | EventSubject::PermissionRequest(_)
            | EventSubject::Handover(_)
            | EventSubject::ActorLease(_)
            | EventSubject::DiscoveryTab(_),
        )
        | None => payload,
    }
}

const fn event_type(kind: EventKind) -> EventType {
    match kind {
        EventKind::TaskCreated => EventType::TaskCreated,
        EventKind::SourceScopeSet => EventType::SourceScopeSet,
        EventKind::ProviderRouteSelected => EventType::ProviderRouteSelected,
        EventKind::ConsentRequested => EventType::ConsentRequested,
        EventKind::TaskQueued => EventType::TaskQueued,
        EventKind::PlanCreated => EventType::PlanCreated,
        EventKind::PlanSuperseded => EventType::PlanSuperseded,
        EventKind::PlanStepAdvanced => EventType::PlanStepAdvanced,
        EventKind::TaskStarted => EventType::TaskStarted,
        EventKind::DiscoveryTabPrepared => EventType::DiscoveryTabPrepared,
        EventKind::ActionProposed => EventType::ActionProposed,
        EventKind::ApprovalRequested => EventType::ApprovalRequested,
        EventKind::ApprovalDecided => EventType::ApprovalDecided,
        EventKind::CapabilityIssued => EventType::CapabilityIssued,
        EventKind::ActionRejected => EventType::ActionRejected,
        EventKind::ActionDispatchStarted => EventType::ActionDispatchStarted,
        EventKind::ActionVerificationCompleted => EventType::ActionVerificationCompleted,
        EventKind::TaskWaitingUser => EventType::TaskWaitingUser,
        // A follow-up is the person supplying the next thing the task works
        // from, and the audit vocabulary already has the type for that. The
        // record's causing command is `FollowUp`, which is what tells the two
        // apart; the question itself is never in either.
        EventKind::UserInputSupplied | EventKind::FollowUpAsked => EventType::UserInputSupplied,
        EventKind::UserTookOver => EventType::UserTookOver,
        EventKind::TaskPausing => EventType::TaskPausing,
        EventKind::TaskPaused => EventType::TaskPaused,
        EventKind::TaskResumed => EventType::TaskResumed,
        EventKind::TaskCancelling => EventType::TaskCancelling,
        EventKind::TaskCancelled => EventType::TaskCancelled,
        EventKind::TaskCompleting => EventType::TaskCompleting,
        EventKind::TaskCompleted => EventType::TaskCompleted,
        EventKind::TaskPartial => EventType::TaskPartial,
        EventKind::TaskFailed => EventType::TaskFailed,
        EventKind::FactCorrected => EventType::FactCorrected,
        EventKind::SourceExcluded => EventType::SourceExcluded,
        EventKind::ArtifactReady => EventType::ArtifactReady,
        EventKind::ArtifactAccepted => EventType::ArtifactAccepted,
        EventKind::ArtifactExported => EventType::ArtifactExported,
        EventKind::BudgetCharged => EventType::BudgetCharged,
        EventKind::PermissionRequested => EventType::PermissionRequested,
        EventKind::PermissionDecided => EventType::PermissionDecided,
        EventKind::HandoverRequested => EventType::HandoverRequested,
        EventKind::HandoverCompleted => EventType::HandoverCompleted,
        EventKind::HandoverExpired => EventType::HandoverExpired,
        EventKind::ModelTurnRecorded => EventType::ModelInvocationCompleted,
        EventKind::ModelTurnGapRecorded => EventType::ModelInvocationFailed,
        EventKind::ContextEvicted => EventType::ContextEvicted,
    }
}

const fn persisted_event_type(kind: EventKind) -> PersistedAuditEventType {
    match kind {
        EventKind::TaskCreated => PersistedAuditEventType::TaskCreated,
        EventKind::SourceScopeSet => PersistedAuditEventType::SourceScopeSet,
        EventKind::ProviderRouteSelected => PersistedAuditEventType::ProviderRouteSelected,
        EventKind::ConsentRequested => PersistedAuditEventType::ConsentRequested,
        EventKind::TaskQueued => PersistedAuditEventType::TaskQueued,
        EventKind::PlanCreated => PersistedAuditEventType::PlanCreated,
        EventKind::PlanSuperseded => PersistedAuditEventType::PlanSuperseded,
        EventKind::PlanStepAdvanced => PersistedAuditEventType::PlanStepAdvanced,
        EventKind::TaskStarted => PersistedAuditEventType::TaskStarted,
        EventKind::DiscoveryTabPrepared => PersistedAuditEventType::DiscoveryTabPrepared,
        EventKind::ActionProposed => PersistedAuditEventType::ActionProposed,
        EventKind::ApprovalRequested => PersistedAuditEventType::ApprovalRequested,
        EventKind::ApprovalDecided => PersistedAuditEventType::ApprovalDecided,
        EventKind::CapabilityIssued => PersistedAuditEventType::CapabilityIssued,
        EventKind::ActionRejected => PersistedAuditEventType::ActionRejected,
        EventKind::ActionDispatchStarted => PersistedAuditEventType::ActionDispatchStarted,
        EventKind::ActionVerificationCompleted => {
            PersistedAuditEventType::ActionVerificationCompleted
        }
        EventKind::TaskWaitingUser => PersistedAuditEventType::TaskWaitingUser,
        EventKind::UserInputSupplied | EventKind::FollowUpAsked => {
            PersistedAuditEventType::UserInputSupplied
        }
        EventKind::UserTookOver => PersistedAuditEventType::UserTookOver,
        EventKind::TaskPausing => PersistedAuditEventType::TaskPausing,
        EventKind::TaskPaused => PersistedAuditEventType::TaskPaused,
        EventKind::TaskResumed => PersistedAuditEventType::TaskResumed,
        EventKind::TaskCancelling => PersistedAuditEventType::TaskCancelling,
        EventKind::TaskCancelled => PersistedAuditEventType::TaskCancelled,
        EventKind::TaskCompleting => PersistedAuditEventType::TaskCompleting,
        EventKind::TaskCompleted => PersistedAuditEventType::TaskCompleted,
        EventKind::TaskPartial => PersistedAuditEventType::TaskPartial,
        EventKind::TaskFailed => PersistedAuditEventType::TaskFailed,
        EventKind::FactCorrected => PersistedAuditEventType::FactCorrected,
        EventKind::SourceExcluded => PersistedAuditEventType::SourceExcluded,
        EventKind::ArtifactReady => PersistedAuditEventType::ArtifactReady,
        EventKind::ArtifactAccepted => PersistedAuditEventType::ArtifactAccepted,
        EventKind::ArtifactExported => PersistedAuditEventType::ArtifactExported,
        EventKind::BudgetCharged => PersistedAuditEventType::BudgetCharged,
        EventKind::PermissionRequested => PersistedAuditEventType::PermissionRequested,
        EventKind::PermissionDecided => PersistedAuditEventType::PermissionDecided,
        EventKind::HandoverRequested => PersistedAuditEventType::HandoverRequested,
        EventKind::HandoverCompleted => PersistedAuditEventType::HandoverCompleted,
        EventKind::HandoverExpired => PersistedAuditEventType::HandoverExpired,
        EventKind::ModelTurnRecorded => PersistedAuditEventType::ModelInvocationCompleted,
        EventKind::ModelTurnGapRecorded => PersistedAuditEventType::ModelInvocationFailed,
        EventKind::ContextEvicted => PersistedAuditEventType::ContextEvicted,
    }
}
