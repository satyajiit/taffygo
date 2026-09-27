// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Fresh-task durability and committed-task replay.

use bip_types::identity::TaskId;

use crate::contract::{
    BoundedPayload, CancellationReason, Completion, Deadline, EffectRequest, OperationEnvelope,
    OperationId, StorageCommitEffect,
};
use crate::ports::{
    AuditPort, PortError, StorageDomainPort, TaskCreationAudit, TaskCreationCommit, TaskEngineLoad,
    TaskEnginePort, TaskIdEntropy, WorkspacePersistRequest, WorkspacePort,
};

use super::workspace_commit::initial_workspace_snapshot;
use super::{
    BeginOpenTask, CoreRuntime, OpenTaskCommitOutcome, OpenTaskCompletionError, OpenTaskError,
    OpenTaskOutcome, PendingOpenTask, ServiceRuntimeComponents, TaskCompletionError, TaskSession,
    TaskTerminal, MAX_TASK_SESSIONS_PER_PROFILE,
};

enum CreationEncodingError {
    Audit(PortError),
    Storage(PortError),
}

struct PendingOpenRegistration {
    task_id: TaskId,
    key: String,
    commit: OperationEnvelope<EffectRequest>,
    task: Box<dyn TaskEnginePort>,
    id_entropy: TaskIdEntropy,
    workspace_revision: Option<u64>,
    initial_effects: Vec<task_engine::Effect>,
}

const fn task_table_is_saturated(open_tasks: usize, pending_opens: usize) -> bool {
    open_tasks.saturating_add(pending_opens) >= MAX_TASK_SESSIONS_PER_PROFILE
}

fn encode_creation(
    components: &ServiceRuntimeComponents,
    commit: &TaskCreationCommit<'_>,
) -> Result<BoundedPayload, CreationEncodingError> {
    let audit_record = components
        .audit
        .encode_task_creation(TaskCreationAudit {
            task_id: &commit.seed.task_id,
            creation_key: commit.creation_key,
            trace_id: commit.trace_id,
            resulting_revision: commit.resulting_revision,
            journal_entries: commit.journal_entries,
        })
        .map_err(CreationEncodingError::Audit)?;
    components
        .storage
        .encode_task_creation(commit, &audit_record)
        .map_err(CreationEncodingError::Storage)
}

fn stage_initial_workspace(
    components: &mut ServiceRuntimeComponents,
    created_workspace: bool,
    operation_id: &OperationId,
    task: &dyn TaskEnginePort,
    now_utc_millis: u64,
) -> Result<Option<WorkspacePersistRequest>, OpenTaskError> {
    if !created_workspace {
        return Ok(None);
    }
    let snapshot =
        initial_workspace_snapshot(task, now_utc_millis).map_err(OpenTaskError::Workspace)?;
    components
        .workspaces
        .begin_creation(operation_id.as_str().to_owned(), snapshot)
        .map(Some)
        .map_err(|error| OpenTaskError::Workspace(super::WorkspaceCommitError::Store(error)))
}

fn creation_storage_effect(
    task_id: TaskId,
    resulting_revision: u64,
    payload: BoundedPayload,
    task_id_seed: [u8; 32],
    workspace: Option<&WorkspacePersistRequest>,
) -> StorageCommitEffect {
    let storage = StorageCommitEffect::append_task_commit(
        task_id,
        0,
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

impl CoreRuntime {
    fn validate_open_slot(&self, task_key: &str) -> Result<(), OpenTaskError> {
        if self.tasks.contains_key(task_key) || self.pending_task_opens.contains_key(task_key) {
            return Err(OpenTaskError::AlreadyOpen);
        }
        if task_table_is_saturated(self.tasks.len(), self.pending_task_opens.len()) {
            return Err(OpenTaskError::Saturated);
        }
        Ok(())
    }

    fn register_pending_open(
        &mut self,
        registration: PendingOpenRegistration,
        now_monotonic_millis: u64,
    ) -> Result<BeginOpenTask, OpenTaskError> {
        if let Err(error) = self.pending.register(
            registration.commit.clone(),
            registration.task.revision(),
            now_monotonic_millis,
        ) {
            if registration.workspace_revision.is_some() {
                self.components
                    .workspaces
                    .reject_persist(registration.commit.operation_id.as_str());
            }
            return Err(OpenTaskError::Pending(error));
        }
        self.operation_tasks.insert(
            registration.commit.operation_id.clone(),
            registration.key.clone(),
        );
        self.pending_task_opens.insert(
            registration.key,
            PendingOpenTask {
                operation_id: registration.commit.operation_id.clone(),
                task: registration.task,
                id_entropy: registration.id_entropy,
                workspace_revision: registration.workspace_revision,
                initial_effects: registration.initial_effects,
            },
        );
        Ok(BeginOpenTask {
            task_id: registration.task_id,
            commit: registration.commit,
        })
    }

    /// Restores one already-committed task through canonical replay.
    pub fn restore_task(&mut self, load: TaskEngineLoad) -> Result<OpenTaskOutcome, OpenTaskError> {
        if !matches!(load, TaskEngineLoad::Replay { .. }) {
            return Err(OpenTaskError::WrongLoadKind);
        }
        let requested_id = load.task_id().clone();
        let id_entropy = load.id_entropy().clone();
        let key = requested_id.as_str().to_owned();
        self.validate_open_slot(&key)?;
        let opened = self
            .components
            .task_factory
            .open(load)
            .map_err(OpenTaskError::Factory)?;
        let (task, recovery, initial_effects) = opened.into_parts();
        if task.task_id() != &requested_id {
            return Err(OpenTaskError::IdentityMismatch);
        }
        if recovery.is_none() || !initial_effects.is_empty() {
            return Err(OpenTaskError::InvalidFactoryResult);
        }
        let revision = task.revision();
        let terminal_on_restore = task.is_terminal();
        self.tasks.insert(
            key,
            TaskSession {
                task,
                id_entropy,
                pending_submit: None,
                recovery_required: false,
                terminal_on_restore,
            },
        );
        Ok(OpenTaskOutcome {
            task_id: requested_id,
            revision,
            recovery,
            effects: Vec::new(),
        })
    }

    /// Constructs a fresh task and emits its initial transactional commit.
    pub fn begin_open_task(
        &mut self,
        mut load: TaskEngineLoad,
        operation_id: OperationId,
        deadline: Deadline,
        now_monotonic_millis: u64,
        now_utc_millis: u64,
    ) -> Result<BeginOpenTask, OpenTaskError> {
        let (requested_id, creation_key) = match &load {
            TaskEngineLoad::Fresh {
                seed, creation_key, ..
            } => (seed.task_id.clone(), creation_key.clone()),
            TaskEngineLoad::Replay { .. } => return Err(OpenTaskError::WrongLoadKind),
        };
        let key = requested_id.as_str().to_owned();
        self.validate_open_slot(&key)?;
        let preflight = OperationEnvelope::new(
            operation_id,
            self.pending.active_generation(),
            0,
            deadline,
            creation_key.clone(),
            (),
        )
        .map_err(OpenTaskError::Envelope)?;
        self.pending
            .validate_registration(&preflight, 0, now_monotonic_millis)
            .map_err(OpenTaskError::Pending)?;
        let created_workspace = self
            .attach_derived_workspace(&mut load)
            .map_err(OpenTaskError::Workspace)?;
        let (seed, trace_id, id_entropy) = match &load {
            TaskEngineLoad::Fresh {
                seed,
                trace_id,
                id_entropy,
                ..
            } => (seed.clone(), trace_id.clone(), id_entropy.clone()),
            TaskEngineLoad::Replay { .. } => return Err(OpenTaskError::WrongLoadKind),
        };
        let opened = self
            .components
            .task_factory
            .open(load)
            .map_err(OpenTaskError::Factory)?;
        let (task, recovery, initial_effects) = opened.into_parts();
        if task.task_id() != &requested_id {
            return Err(OpenTaskError::IdentityMismatch);
        }
        if recovery.is_some() {
            return Err(OpenTaskError::InvalidFactoryResult);
        }
        let resulting_revision = task.revision();
        let creation_entries = task.journal().entries().to_vec();
        let creation = TaskCreationCommit {
            seed: &seed,
            creation_key: &creation_key,
            trace_id: &trace_id,
            id_entropy: &id_entropy,
            resulting_revision,
            journal_entries: &creation_entries,
            effect_intents: &initial_effects,
        };
        let payload = match encode_creation(&self.components, &creation) {
            Ok(payload) => payload,
            Err(CreationEncodingError::Audit(error)) => {
                return Err(OpenTaskError::AuditEncoding(error));
            }
            Err(CreationEncodingError::Storage(error)) => {
                return Err(OpenTaskError::StorageEncoding(error));
            }
        };
        let workspace = stage_initial_workspace(
            &mut self.components,
            created_workspace,
            &preflight.operation_id,
            task.as_ref(),
            now_utc_millis,
        )?;
        let storage = creation_storage_effect(
            requested_id.clone(),
            resulting_revision,
            payload,
            *id_entropy.as_bytes(),
            workspace.as_ref(),
        );
        let commit = OperationEnvelope {
            operation_id: preflight.operation_id,
            service_generation: preflight.service_generation,
            task_revision: resulting_revision,
            deadline: preflight.deadline,
            idempotency_key: preflight.idempotency_key,
            body: EffectRequest::Storage(storage),
        };
        self.register_pending_open(
            PendingOpenRegistration {
                task_id: requested_id,
                key,
                commit,
                task,
                id_entropy,
                workspace_revision: workspace.map(|value| value.resulting_revision),
                initial_effects,
            },
            now_monotonic_millis,
        )
    }

    /// Makes a fresh task addressable only after durable success.
    pub fn complete_open_task(
        &mut self,
        task_id: &TaskId,
        completion: OperationEnvelope<Completion>,
        now_millis: u64,
    ) -> Result<OpenTaskCommitOutcome, OpenTaskCompletionError> {
        let Some(pending_open) = self.pending_task_opens.get(task_id.as_str()) else {
            return Err(OpenTaskCompletionError::NoOpenPending);
        };
        if pending_open.operation_id != completion.operation_id {
            return Err(OpenTaskCompletionError::WrongOperation);
        }
        let terminal = self
            .pending
            .complete(completion, pending_open.task.revision(), now_millis)
            .map_err(OpenTaskCompletionError::Terminal)?;
        self.operation_tasks.remove(&terminal.operation_id);
        let Some(pending_open) = self.pending_task_opens.remove(task_id.as_str()) else {
            return Err(OpenTaskCompletionError::NoOpenPending);
        };
        if !matches!(terminal.body, Completion::StorageCommitted { .. }) {
            if pending_open.workspace_revision.is_some() {
                self.components
                    .workspaces
                    .reject_persist(terminal.operation_id.as_str());
            }
            return Ok(OpenTaskCommitOutcome::NotOpened(terminal));
        }
        if let Some(workspace_revision) = pending_open.workspace_revision {
            if let Err(error) = self
                .components
                .workspaces
                .complete_persist(terminal.operation_id.as_str(), workspace_revision)
            {
                self.components
                    .workspaces
                    .reject_persist(terminal.operation_id.as_str());
                return Err(OpenTaskCompletionError::Workspace(error));
            }
        }
        let revision = pending_open.task.revision();
        self.tasks.insert(
            task_id.as_str().to_owned(),
            TaskSession {
                task: pending_open.task,
                id_entropy: pending_open.id_entropy,
                pending_submit: None,
                recovery_required: false,
                // A task opened here is this run's own, whatever it becomes.
                terminal_on_restore: false,
            },
        );
        self.begin_flow_recording(task_id);
        Ok(OpenTaskCommitOutcome::Opened(OpenTaskOutcome {
            task_id: task_id.clone(),
            revision,
            recovery: None,
            effects: pending_open.initial_effects,
        }))
    }

    /// Closes one task and resolves its pending operations as cancellation.
    pub fn close_task(
        &mut self,
        task_id: &TaskId,
    ) -> Result<Vec<TaskTerminal>, TaskCompletionError> {
        if !self.tasks.contains_key(task_id.as_str()) {
            return Err(TaskCompletionError::UnknownTask);
        }
        let operation_ids: Vec<OperationId> = self
            .operation_tasks
            .iter()
            .filter(|(_, owner)| owner.as_str() == task_id.as_str())
            .map(|(operation_id, _)| operation_id.clone())
            .collect();
        let mut terminals = Vec::with_capacity(operation_ids.len());
        for operation_id in operation_ids {
            let completion = self
                .pending
                .cancel(&operation_id, CancellationReason::TaskSettled)
                .map_err(TaskCompletionError::Terminal)?;
            self.operation_tasks.remove(&operation_id);
            self.components
                .workspaces
                .reject_persist(operation_id.as_str());
            terminals.push(TaskTerminal {
                task_id: task_id.clone(),
                completion,
            });
        }
        self.tasks.remove(task_id.as_str());
        self.drop_loop_state(task_id.as_str());
        Ok(terminals)
    }
}
