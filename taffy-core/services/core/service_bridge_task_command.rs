// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed generated command decoding for one durable reducer submission.

use core_runtime::wire;
use core_runtime::{
    ActorLeaseId, ApprovalReceiptReference, ArtifactId, ArtifactKind, Command, CommandEnvelope,
    FieldValueRequestId, HandoverCompletion, HandoverId, IdempotencyKey, PauseCause,
    PermissionRequestId, PermissionResult, PersonInput, SuppliedValueCount, TaskControlKind,
    TaskId, TaskState, TraceId,
};

use crate::ffi;
use crate::service_bridge_task_support::{
    is_sha256, permission_decision_from_wire, permission_from_wire,
    supplied_field_node_ids_from_wire, supplied_outcome_from_wire, valid_identifier,
};

pub(crate) fn decode_command(
    runtime: &core_runtime::ProfileServiceRuntime,
    task_id: &TaskId,
    revision: u64,
    value: &ffi::BridgeTaskCommand,
    now_monotonic_ms: u64,
    now_utc_millis: u64,
) -> Option<Command> {
    let kind = wire::CoreServiceCommandKind::from_wire(u32::from(value.kind))?;
    match kind {
        wire::CoreServiceCommandKind::CancelTask => {
            if wire::CancelReason::from_wire(u32::from(value.cancel_reason))?
                != wire::CancelReason::User
            {
                return None;
            }
            if !admits_control(runtime, task_id, revision, TaskControlKind::Stop)? {
                return None;
            }
            Some(Command::CancelTask)
        }
        wire::CoreServiceCommandKind::PauseTask => {
            admits_control(runtime, task_id, revision, TaskControlKind::Pause)?.then_some(
                Command::PauseTask {
                    cause: PauseCause::User,
                },
            )
        }
        wire::CoreServiceCommandKind::ResumeTask => {
            admits_control(runtime, task_id, revision, TaskControlKind::Resume)?
                .then_some(Command::ResumeTask)
        }
        wire::CoreServiceCommandKind::TakeOver => {
            admits_control(runtime, task_id, revision, TaskControlKind::TakeOver)?
                .then_some(Command::TakeOver)
        }
        wire::CoreServiceCommandKind::UserDecision => {
            if !valid_identifier(&value.action_id)
                || !valid_identifier(&value.approval_receipt_id)
                || !is_sha256(&value.approval_digest)
            {
                return None;
            }
            let facts = runtime.core().task(task_id)?.view_facts().ok()?;
            let pending = facts.pending_action?;
            if facts.revision != revision
                || pending.action_id != value.action_id
                || pending.proposal_digest != value.approval_digest
            {
                return None;
            }
            let approval = ApprovalReceiptReference(value.approval_receipt_id.clone());
            match wire::UserDecisionKind::from_wire(u32::from(value.user_decision))? {
                wire::UserDecisionKind::Accept => {
                    if value.approval_expires_at_monotonic_ms <= now_monotonic_ms
                        || value.approval_expires_at_utc_ms <= now_utc_millis
                        || value.approval_expires_at_monotonic_ms
                            > value.operation.deadline_monotonic_ms
                        || value.browser_session_id != runtime.browser_session_id().as_str()
                    {
                        return None;
                    }
                    Some(Command::ApproveAction {
                        approval,
                        still_current: true,
                        expires_at_monotonic_ms: value.approval_expires_at_monotonic_ms,
                        expires_at_utc_ms: value.approval_expires_at_utc_ms,
                        browser_session_id: runtime.browser_session_id().clone(),
                    })
                }
                wire::UserDecisionKind::Deny | wire::UserDecisionKind::Dismiss => {
                    Some(Command::DenyAction { approval })
                }
            }
        }
        wire::CoreServiceCommandKind::PermissionResult => {
            let request_id = PermissionRequestId::new(value.request_id.clone()).ok()?;
            Some(Command::RecordPermissionResult(PermissionResult::new(
                request_id,
                permission_from_wire(value.permission)?,
                permission_decision_from_wire(value.permission_decision)?,
            )))
        }
        wire::CoreServiceCommandKind::CompleteHandover => {
            if !valid_identifier(&value.handover_id)
                || !valid_identifier(&value.lease_before)
                || !valid_identifier(&value.resumed_with)
            {
                return None;
            }
            let facts = runtime.core().task(task_id)?.view_facts().ok()?;
            if facts.revision != revision
                || facts.pending_handover.as_deref() != Some(value.handover_id.as_str())
            {
                return None;
            }
            Some(Command::CompleteHandover(
                HandoverCompletion::new(
                    HandoverId::new(value.handover_id.clone()).ok()?,
                    ActorLeaseId::new(value.lease_before.clone()),
                    ActorLeaseId::new(value.resumed_with.clone()),
                    PersonInput::observed(value.person_input),
                )
                .ok()?,
            ))
        }
        wire::CoreServiceCommandKind::ExpireHandover => {
            if !valid_identifier(&value.handover_id) {
                return None;
            }
            let facts = runtime.core().task(task_id)?.view_facts().ok()?;
            if facts.revision != revision
                || facts.pending_handover.as_deref() != Some(value.handover_id.as_str())
            {
                return None;
            }
            Some(Command::ExpireHandover {
                handover_id: HandoverId::new(value.handover_id.clone()).ok()?,
            })
        }
        wire::CoreServiceCommandKind::SupplyUserInput => {
            let facts = runtime.core().task(task_id)?.view_facts().ok()?;
            if facts.revision != revision
                || facts.state != TaskState::WaitingUser
                || facts.pending_handover.is_some()
                || facts.pending_permission.is_some()
            {
                return None;
            }
            core_runtime::classify_person_answer(&value.answer).ok()?;
            Some(Command::SupplyUserInput)
        }
        // A follow-up rides the person-answer seam (decision 0137): the words
        // arrive in the same bounded field, are classified the same way, and
        // are staged for the next compose rather than journaled. It is
        // admitted only from a finished task whose transcript this generation
        // still holds — a restored task has facts but no prose, and a
        // conversation composed over facts alone would be one the core made
        // up. The refusal reaches the surface as an invalid command, which
        // the box reads as "the conversation is gone" and starts afresh.
        wire::CoreServiceCommandKind::FollowUp => {
            let facts = runtime.core().task(task_id)?.view_facts().ok()?;
            if facts.revision != revision
                || !matches!(facts.state, TaskState::Completed | TaskState::Partial)
                || facts.pending_handover.is_some()
                || facts.pending_permission.is_some()
                || !runtime.holds_transcript(task_id.as_str())
            {
                return None;
            }
            core_runtime::classify_follow_up_question(&value.answer).ok()?;
            Some(Command::FollowUp)
        }
        wire::CoreServiceCommandKind::SupplyFieldValues => {
            if !valid_identifier(&value.request_id) {
                return None;
            }
            let facts = runtime.core().task(task_id)?.view_facts().ok()?;
            // The same three checks the handover completion makes, and for the
            // same reason: an answer to a request this task is not holding
            // open is a late or repeated crossing, and admitting one would
            // resume a task that has already moved on.
            if facts.revision != revision
                || facts.state != TaskState::WaitingUser
                || facts.pending_field_values.as_deref() != Some(value.request_id.as_str())
            {
                return None;
            }
            Some(Command::SupplyFieldValues {
                request_id: FieldValueRequestId::new(value.request_id.clone()).ok()?,
                // Bounded here rather than clamped. A count past the bound
                // means the browser and this process disagree about how many
                // values a request may carry, and the honest answer to that is
                // to refuse the crossing rather than to keep the first eight
                // of an answer neither side can explain.
                supplied: SuppliedValueCount::new(value.supplied).ok()?,
                // Refused on an unknown number for the same reason, and it is
                // the sharper of the two: defaulting would have this process
                // tell the next turn the person answered when the browser is
                // saying something it has no word for (decision 0215).
                outcome: Some(supplied_outcome_from_wire(value.supplied_outcome)?),
                // One field per value, or the crossing is refused, so the
                // task never places a value it cannot say the field of
                // (decision 0238).
                field_node_ids: Some(supplied_field_node_ids_from_wire(
                    &value.supplied_field_node_ids,
                    value.supplied,
                )?),
            })
        }
        wire::CoreServiceCommandKind::AcceptTaskArtifact => {
            if !valid_identifier(&value.artifact_id) {
                return None;
            }
            let facts = runtime.core().task(task_id)?.view_facts().ok()?;
            let artifact = facts
                .artifacts
                .iter()
                .find(|artifact| artifact.artifact_id == value.artifact_id)?;
            if facts.revision != revision || artifact.accepted {
                return None;
            }
            Some(Command::AcceptArtifact {
                artifact_id: ArtifactId::new(value.artifact_id.clone()),
            })
        }
        wire::CoreServiceCommandKind::ExportTaskArtifact => {
            if !valid_identifier(&value.artifact_id) {
                return None;
            }
            let format = artifact_kind_from_wire(value.artifact_kind)?;
            let facts = runtime.core().task(task_id)?.view_facts().ok()?;
            let artifact = facts
                .artifacts
                .iter()
                .find(|artifact| artifact.artifact_id == value.artifact_id)?;
            if facts.revision != revision || !artifact.accepted || artifact.kind != format {
                return None;
            }
            Some(Command::ExportArtifact {
                artifact_id: ArtifactId::new(value.artifact_id.clone()),
                format,
            })
        }
        wire::CoreServiceCommandKind::StartTask
        | wire::CoreServiceCommandKind::AuthCallback
        | wire::CoreServiceCommandKind::StartAuth
        | wire::CoreServiceCommandKind::RequestEmailLink
        | wire::CoreServiceCommandKind::SignOut
        | wire::CoreServiceCommandKind::AuthCredentialResult
        | wire::CoreServiceCommandKind::CorrectWorkspaceFact
        | wire::CoreServiceCommandKind::ExcludeWorkspaceSource
        | wire::CoreServiceCommandKind::RequestWorkspaceExport
        | wire::CoreServiceCommandKind::SaveWorkspace
        | wire::CoreServiceCommandKind::RenameWorkspace
        | wire::CoreServiceCommandKind::DeleteWorkspace
        | wire::CoreServiceCommandKind::DiscardWorkspace
        | wire::CoreServiceCommandKind::SearchLibrary
        | wire::CoreServiceCommandKind::SaveLibraryFact
        | wire::CoreServiceCommandKind::RemoveLibraryEntry
        | wire::CoreServiceCommandKind::RequestLibraryExport
        | wire::CoreServiceCommandKind::SearchMemory
        | wire::CoreServiceCommandKind::UpsertMemory
        | wire::CoreServiceCommandKind::DeleteMemory
        | wire::CoreServiceCommandKind::SetAssetDeliveryPolicy
        | wire::CoreServiceCommandKind::RequestAsset
        | wire::CoreServiceCommandKind::RemoveAsset
        | wire::CoreServiceCommandKind::SaveProviderCredential
        | wire::CoreServiceCommandKind::ForgetProviderCredential
        | wire::CoreServiceCommandKind::StartProviderAuth
        | wire::CoreServiceCommandKind::ProviderAuthCallback
        | wire::CoreServiceCommandKind::SaveCustomProvider
        | wire::CoreServiceCommandKind::RemoveCustomProvider
        | wire::CoreServiceCommandKind::SetProviderCredentialState
        | wire::CoreServiceCommandKind::ProbeProviderCredential
        | wire::CoreServiceCommandKind::SetProviderModelPreference
        | wire::CoreServiceCommandKind::ProbeCustomEndpoint
        | wire::CoreServiceCommandKind::RequestComposerCompletion
        | wire::CoreServiceCommandKind::CancelComposerCompletion
        | wire::CoreServiceCommandKind::SetAssistantConfiguration
        | wire::CoreServiceCommandKind::MutateSkill
        | wire::CoreServiceCommandKind::CancelProviderAuth
        | wire::CoreServiceCommandKind::ReplaceSavedDataSnapshot => None,
    }
}

fn artifact_kind_from_wire(value: u8) -> Option<ArtifactKind> {
    match wire::TaskArtifactKind::from_wire(u32::from(value))? {
        wire::TaskArtifactKind::Markdown => Some(ArtifactKind::Markdown),
        wire::TaskArtifactKind::Csv => Some(ArtifactKind::Csv),
        wire::TaskArtifactKind::Xlsx => Some(ArtifactKind::Xlsx),
        wire::TaskArtifactKind::Pdf => Some(ArtifactKind::Pdf),
        wire::TaskArtifactKind::Docx => Some(ArtifactKind::Docx),
        wire::TaskArtifactKind::Pptx => Some(ArtifactKind::Pptx),
        wire::TaskArtifactKind::WaveAudio => Some(ArtifactKind::WaveAudio),
        wire::TaskArtifactKind::FrameArchive => Some(ArtifactKind::FrameArchive),
    }
}

fn admits_control(
    runtime: &core_runtime::ProfileServiceRuntime,
    task_id: &TaskId,
    revision: u64,
    control: TaskControlKind,
) -> Option<bool> {
    let facts = runtime.core().task(task_id)?.view_facts().ok()?;
    Some(facts.revision == revision && facts.allowed_controls.contains(&control))
}

pub(crate) fn command_envelope(
    operation: &wire::OperationEnvelope,
    trace_id: String,
    command: Command,
) -> Option<CommandEnvelope> {
    if !valid_identifier(&trace_id) {
        return None;
    }
    Some(CommandEnvelope::new(
        IdempotencyKey::new(operation.idempotency_key.clone()),
        operation.task_revision,
        TraceId::new(trace_id),
        command,
    ))
}
