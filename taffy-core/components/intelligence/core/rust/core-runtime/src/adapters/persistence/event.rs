// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Journal command/event record projections.

use bip_types::identity::{ActionId, TabId};
use core_service_types as wire;
use task_engine::{
    ArtifactId, CapabilityId, EventKind, EventSubject, FactId, IdempotencyKey, JournalEntry,
    PermissionRequestId, PlanId, PlanStepId, SourceId, TaskEvent, TaskState, TraceId, UtcMillis,
};

use super::command::{envelope, unenvelope};
use super::enum_journal::{command_kind, event_kind, uncommand_kind, unevent_kind};
use super::enum_task::{state_reason, task_state, unstate_reason, untask_state};
use super::ConversionError;

pub(super) fn journal_entry(
    value: &JournalEntry,
) -> Result<wire::PersistedJournalEntry, ConversionError> {
    Ok(match value {
        JournalEntry::Command(record) => wire::PersistedJournalEntry::Command {
            record: wire::PersistedCommandRecord {
                sequence: record.sequence,
                envelope: envelope(&record.envelope)?,
                recorded_at_utc_ms: record.recorded_at.0,
            },
        },
        JournalEntry::Event(record) => wire::PersistedJournalEntry::Event {
            record: wire::PersistedEventRecord {
                sequence: record.sequence,
                event: event(&record.event),
                revision: record.revision,
                trace_id: record.trace_id.as_str().to_owned(),
                causation_key: record.causation_key.as_str().to_owned(),
                recorded_at_utc_ms: record.recorded_at.0,
            },
        },
    })
}

pub(super) fn unjournal_entry(
    value: wire::PersistedJournalEntry,
) -> Result<JournalEntry, ConversionError> {
    Ok(match value {
        wire::PersistedJournalEntry::Command { record } => {
            JournalEntry::Command(task_engine::journal::CommandRecord {
                sequence: record.sequence,
                envelope: unenvelope(record.envelope)?,
                recorded_at: UtcMillis(record.recorded_at_utc_ms),
            })
        }
        wire::PersistedJournalEntry::Event { record } => {
            if record.trace_id.is_empty() || record.causation_key.is_empty() {
                return Err(ConversionError::InvalidIdentifier);
            }
            JournalEntry::Event(task_engine::journal::EventRecord {
                sequence: record.sequence,
                event: unevent(record.event)?,
                revision: record.revision,
                trace_id: TraceId::new(record.trace_id),
                causation_key: IdempotencyKey::new(record.causation_key),
                recorded_at: UtcMillis(record.recorded_at_utc_ms),
            })
        }
    })
}

fn event(value: &TaskEvent) -> wire::PersistedTaskEvent {
    wire::PersistedTaskEvent {
        kind: event_kind(value.kind),
        caused_by: command_kind(value.caused_by),
        from: value.from.map(task_state),
        to: value.to.map(task_state),
        reason: value.reason.map(state_reason),
        subject: value.subject.as_ref().map(subject),
    }
}

fn unevent(value: wire::PersistedTaskEvent) -> Result<TaskEvent, ConversionError> {
    let event = TaskEvent {
        kind: unevent_kind(value.kind),
        caused_by: uncommand_kind(value.caused_by)?,
        from: value.from.map(untask_state),
        to: value.to.map(untask_state),
        reason: value.reason.map(unstate_reason),
        subject: value.subject.map(unsubject).transpose()?,
    };
    let is_transition = event.from.is_some() && event.to.is_some() && event.reason.is_some();
    let is_creation = event.kind == EventKind::TaskCreated
        && event.from.is_none()
        && event.to == Some(TaskState::Draft)
        && event.reason.is_none();
    let is_content_free = event.from.is_none() && event.to.is_none() && event.reason.is_none();
    if !(is_transition || is_creation || is_content_free) {
        return Err(ConversionError::InvalidValue);
    }
    Ok(event)
}

fn subject(value: &EventSubject) -> wire::PersistedEventSubject {
    match value {
        EventSubject::Action(id) => wire::PersistedEventSubject::Action { id: id.0.clone() },
        EventSubject::Plan(id) => wire::PersistedEventSubject::Plan {
            id: id.as_str().to_owned(),
        },
        EventSubject::PlanStep(id) => wire::PersistedEventSubject::PlanStep {
            id: id.as_str().to_owned(),
        },
        EventSubject::Artifact(id) => wire::PersistedEventSubject::Artifact {
            id: id.as_str().to_owned(),
        },
        EventSubject::Source(id) => wire::PersistedEventSubject::Source { id: id.to_text() },
        EventSubject::DiscoveryTab(id) => wire::PersistedEventSubject::DiscoveryTab {
            id: id.as_str().to_owned(),
        },
        EventSubject::Fact(id) => wire::PersistedEventSubject::Fact { id: id.to_text() },
        EventSubject::Capability(id) => wire::PersistedEventSubject::Capability {
            id: id.as_str().to_owned(),
        },
        EventSubject::PermissionRequest(id) => wire::PersistedEventSubject::PermissionRequest {
            id: id.as_str().to_owned(),
        },
        EventSubject::Handover(id) => wire::PersistedEventSubject::Handover {
            id: id.as_str().to_owned(),
        },
        EventSubject::ActorLease(id) => wire::PersistedEventSubject::ActorLease {
            id: id.as_str().to_owned(),
        },
    }
}

fn unsubject(value: wire::PersistedEventSubject) -> Result<EventSubject, ConversionError> {
    Ok(match value {
        wire::PersistedEventSubject::Action { id } => {
            validate_opaque(&id)?;
            EventSubject::Action(ActionId(id))
        }
        wire::PersistedEventSubject::Plan { id } => {
            validate_opaque(&id)?;
            EventSubject::Plan(PlanId::new(id))
        }
        wire::PersistedEventSubject::PlanStep { id } => {
            validate_opaque(&id)?;
            EventSubject::PlanStep(PlanStepId::new(id))
        }
        wire::PersistedEventSubject::Artifact { id } => {
            validate_opaque(&id)?;
            EventSubject::Artifact(ArtifactId::new(id))
        }
        wire::PersistedEventSubject::Source { id } => EventSubject::Source(
            SourceId::parse(&id).map_err(|_| ConversionError::InvalidIdentifier)?,
        ),
        wire::PersistedEventSubject::DiscoveryTab { id } => {
            super::consent::valid_tab_id(&id)
                .then_some(())
                .ok_or(ConversionError::InvalidIdentifier)?;
            EventSubject::DiscoveryTab(TabId::new(id))
        }
        wire::PersistedEventSubject::Fact { id } => {
            EventSubject::Fact(FactId::parse(&id).map_err(|_| ConversionError::InvalidIdentifier)?)
        }
        wire::PersistedEventSubject::Capability { id } => {
            validate_opaque(&id)?;
            EventSubject::Capability(CapabilityId::new(id))
        }
        wire::PersistedEventSubject::PermissionRequest { id } => EventSubject::PermissionRequest(
            PermissionRequestId::new(id).map_err(|_| ConversionError::InvalidIdentifier)?,
        ),
        wire::PersistedEventSubject::Handover { id } => EventSubject::Handover(
            task_engine::HandoverId::new(id).map_err(|_| ConversionError::InvalidIdentifier)?,
        ),
        wire::PersistedEventSubject::ActorLease { id } => {
            validate_opaque(&id)?;
            EventSubject::ActorLease(task_engine::ActorLeaseId::new(id))
        }
    })
}

fn validate_opaque(value: &str) -> Result<(), ConversionError> {
    if value.is_empty() {
        return Err(ConversionError::InvalidIdentifier);
    }
    Ok(())
}
