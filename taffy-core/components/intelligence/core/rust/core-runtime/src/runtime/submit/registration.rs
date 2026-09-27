// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Commit encoding and pending-operation registration for a staged submit.

use bip_types::identity::TaskId;

use crate::contract::{BoundedPayload, EffectRequest, OperationId, StorageCommitEffect};
use crate::ports::{
    AuditPort, PortError, StorageCommit, StorageDomainPort, WorkspacePersistRequest, WorkspacePort,
};

use super::super::{BeginSubmit, CoreRuntime, PendingSubmit, SubmitError};
use super::StagedSubmission;

enum CommitEncodingError {
    Audit(PortError),
    Storage(PortError),
}

fn encode_commit(
    runtime: &CoreRuntime,
    commit: &StorageCommit<'_>,
) -> Result<BoundedPayload, CommitEncodingError> {
    let audit_record = runtime
        .components
        .audit
        .encode_record(commit)
        .map_err(CommitEncodingError::Audit)?;
    runtime
        .components
        .storage
        .encode_commit(commit, &audit_record)
        .map_err(CommitEncodingError::Storage)
}

fn encode_submission(
    runtime: &CoreRuntime,
    commit: &StorageCommit<'_>,
    resulting_revision: u64,
) -> Result<BoundedPayload, SubmitError> {
    match encode_commit(runtime, commit) {
        Ok(payload) => Ok(payload),
        Err(CommitEncodingError::Audit(error)) => Err(SubmitError::AuditEncoding {
            error,
            in_memory_revision: resulting_revision,
        }),
        Err(CommitEncodingError::Storage(error)) => Err(SubmitError::StorageEncoding {
            error,
            in_memory_revision: resulting_revision,
        }),
    }
}

fn submission_storage_effect(
    task_id: TaskId,
    previous_revision: u64,
    resulting_revision: u64,
    payload: BoundedPayload,
    task_id_seed: [u8; 32],
    workspace: Option<&WorkspacePersistRequest>,
) -> StorageCommitEffect {
    let storage = StorageCommitEffect::append_task_commit(
        task_id,
        previous_revision,
        resulting_revision,
        payload,
        task_id_seed,
    );
    if let Some(workspace) = workspace {
        storage.with_workspace(
            workspace.workspace_id.clone(),
            workspace.expected_revision,
            workspace.resulting_revision,
            workspace.snapshot.clone(),
        )
    } else {
        storage
    }
}

fn reject_workspace(
    runtime: &mut CoreRuntime,
    operation_id: &OperationId,
    workspace: Option<&WorkspacePersistRequest>,
) {
    if workspace.is_some() {
        runtime
            .components
            .workspaces
            .reject_persist(operation_id.as_str());
    }
}

impl CoreRuntime {
    pub(super) fn register_staged_submission(
        &mut self,
        task_id: &TaskId,
        staged: StagedSubmission,
        now_monotonic_millis: u64,
    ) -> Result<BeginSubmit, SubmitError> {
        let StagedSubmission {
            prepared,
            accepted,
            appended_entries,
            workspace,
        } = staged;
        if !self.tasks.contains_key(task_id.as_str()) {
            reject_workspace(self, &prepared.preflight.operation_id, workspace.as_ref());
            return Err(SubmitError::UnknownTask);
        }
        let payload = {
            let Some(session) = self.tasks.get(task_id.as_str()) else {
                return Err(SubmitError::UnknownTask);
            };
            let commit = StorageCommit {
                task_id: session.task.task_id(),
                workspace_id: session.task.workspace_id(),
                command: &prepared.command_for_commit,
                previous_revision: prepared.previous_revision,
                resulting_revision: accepted.revision,
                journal_entries: &appended_entries,
                events: &accepted.events,
                effect_intents: &accepted.effects,
                idempotency_key: &prepared.command_for_commit.idempotency_key,
                trace_id: &prepared.command_for_commit.trace_id,
            };
            encode_submission(self, &commit, accepted.revision)
        };
        let payload = match payload {
            Ok(payload) => payload,
            Err(error) => {
                reject_workspace(self, &prepared.preflight.operation_id, workspace.as_ref());
                if let Some(session) = self.tasks.get_mut(task_id.as_str()) {
                    session.recovery_required = true;
                }
                return Err(error);
            }
        };
        let Some(session) = self.tasks.get_mut(task_id.as_str()) else {
            reject_workspace(self, &prepared.preflight.operation_id, workspace.as_ref());
            return Err(SubmitError::UnknownTask);
        };
        let storage = submission_storage_effect(
            task_id.clone(),
            prepared.previous_revision,
            accepted.revision,
            payload,
            *session.id_entropy.as_bytes(),
            workspace.as_ref(),
        );
        let envelope = crate::contract::OperationEnvelope {
            operation_id: prepared.preflight.operation_id,
            service_generation: prepared.preflight.service_generation,
            task_revision: accepted.revision,
            deadline: prepared.preflight.deadline,
            idempotency_key: prepared.preflight.idempotency_key,
            body: EffectRequest::Storage(storage),
        };
        if let Err(error) =
            self.pending
                .register(envelope.clone(), accepted.revision, now_monotonic_millis)
        {
            reject_workspace(self, &envelope.operation_id, workspace.as_ref());
            if let Some(session) = self.tasks.get_mut(task_id.as_str()) {
                session.recovery_required = true;
            }
            return Err(SubmitError::PendingAfterApply {
                error,
                in_memory_revision: accepted.revision,
            });
        }
        self.operation_tasks
            .insert(envelope.operation_id.clone(), task_id.as_str().to_owned());
        let Some(session) = self.tasks.get_mut(task_id.as_str()) else {
            return Err(SubmitError::UnknownTask);
        };
        session.pending_submit = Some(PendingSubmit {
            operation_id: envelope.operation_id.clone(),
            accepted,
            workspace_revision: workspace.map(|value| value.resulting_revision),
            task_library_search: None,
            task_memory_search: None,
            task_tab: None,
            task_store: None,
            task_download: None,
            media_probe: None,
        });
        Ok(BeginSubmit::AwaitingCommit(Box::new(envelope)))
    }
}
