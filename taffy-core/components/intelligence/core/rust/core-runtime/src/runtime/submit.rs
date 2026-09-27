// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Two-phase reducer submission and browser-owned durability completion.

mod registration;

use bip_types::identity::TaskId;
use taffy_storage::workspace::WorkspacePhase;
use task_engine::{Accepted, CommandEnvelope, JournalEntry};

use crate::contract::{Completion, Deadline, OperationEnvelope, OperationId};
use crate::ports::{WorkspacePersistRequest, WorkspacePort};

use super::{
    BeginSubmit, CommitCompletionError, CommitOutcome, CoreRuntime, SubmitError, TaskSession,
};

enum ApplyOutcome {
    Duplicate(Accepted),
    Applied {
        accepted: Accepted,
        journal_entries: Vec<JournalEntry>,
    },
}

enum AppliedSubmission {
    Duplicate(Accepted),
    Applied {
        accepted: Accepted,
        journal_entries: Vec<JournalEntry>,
        workspace_phase: WorkspacePhase,
    },
}

struct WorkspaceStage<'a> {
    workspace_id: Option<&'a str>,
    plan: Option<super::workspace_commit::PageIngestion>,
    phase: WorkspacePhase,
    operation_id: &'a OperationId,
    now_utc_millis: u64,
    in_memory_revision: u64,
}

struct SubmissionPreparation {
    previous_revision: u64,
    workspace_plan: Option<super::workspace_commit::PageIngestion>,
    workspace_id: Option<String>,
    preflight: OperationEnvelope<()>,
    command_for_commit: CommandEnvelope,
}

struct StagedSubmission {
    prepared: SubmissionPreparation,
    accepted: Accepted,
    appended_entries: Vec<JournalEntry>,
    workspace: Option<WorkspacePersistRequest>,
}

fn apply_task(
    session: &mut TaskSession,
    command: CommandEnvelope,
) -> Result<ApplyOutcome, SubmitError> {
    let previous_journal_len = session.task.journal().len();
    let accepted = session.task.apply(command).map_err(SubmitError::Task)?;
    if accepted.duplicate {
        if accepted.effects.is_empty() {
            return Ok(ApplyOutcome::Duplicate(accepted));
        }
        session.recovery_required = true;
        return Err(SubmitError::InvalidTaskResult {
            in_memory_revision: session.task.revision(),
        });
    }
    if accepted.revision != session.task.revision() {
        session.recovery_required = true;
        return Err(SubmitError::InvalidTaskResult {
            in_memory_revision: session.task.revision(),
        });
    }
    let Some(journal_entries) = session
        .task
        .journal()
        .entries()
        .get(previous_journal_len..)
        .map(<[JournalEntry]>::to_vec)
    else {
        session.recovery_required = true;
        return Err(SubmitError::InvalidTaskResult {
            in_memory_revision: session.task.revision(),
        });
    };
    if journal_entries.is_empty() {
        session.recovery_required = true;
        return Err(SubmitError::InvalidTaskResult {
            in_memory_revision: session.task.revision(),
        });
    }
    Ok(ApplyOutcome::Applied {
        accepted,
        journal_entries,
    })
}

fn apply_submission(
    session: &mut TaskSession,
    command: CommandEnvelope,
) -> Result<AppliedSubmission, SubmitError> {
    let applied = apply_task(session, command)?;
    let (accepted, journal_entries) = match applied {
        ApplyOutcome::Duplicate(accepted) => return Ok(AppliedSubmission::Duplicate(accepted)),
        ApplyOutcome::Applied {
            accepted,
            journal_entries,
        } => (accepted, journal_entries),
    };
    let Ok(facts) = session.task.view_facts() else {
        session.recovery_required = true;
        return Err(SubmitError::InvalidTaskResult {
            in_memory_revision: accepted.revision,
        });
    };
    Ok(AppliedSubmission::Applied {
        accepted,
        journal_entries,
        workspace_phase: super::workspace_commit::workspace_phase_for_task(facts.state),
    })
}

fn continuing_submission(
    applied: AppliedSubmission,
) -> Result<(Accepted, Vec<JournalEntry>, WorkspacePhase), Accepted> {
    match applied {
        AppliedSubmission::Duplicate(accepted) => Err(accepted),
        AppliedSubmission::Applied {
            accepted,
            journal_entries,
            workspace_phase,
        } => Ok((accepted, journal_entries, workspace_phase)),
    }
}

impl CoreRuntime {
    fn submission_revision(&self, task_id: &TaskId) -> Result<u64, SubmitError> {
        let Some(session) = self.tasks.get(task_id.as_str()) else {
            return Err(SubmitError::UnknownTask);
        };
        if session.recovery_required {
            return Err(SubmitError::RecoveryRequired {
                in_memory_revision: session.task.revision(),
            });
        }
        if session.pending_submit.is_some() {
            return Err(SubmitError::CommitInFlight);
        }
        Ok(session.task.revision())
    }

    fn submission_preflight(
        &self,
        operation_id: OperationId,
        previous_revision: u64,
        deadline: Deadline,
        idempotency_key: task_engine::ids::IdempotencyKey,
        now_monotonic_millis: u64,
    ) -> Result<OperationEnvelope<()>, SubmitError> {
        let preflight = OperationEnvelope::new(
            operation_id,
            self.pending.active_generation(),
            previous_revision,
            deadline,
            idempotency_key,
            (),
        )
        .map_err(SubmitError::Envelope)?;
        self.pending
            .validate_registration(&preflight, previous_revision, now_monotonic_millis)
            .map_err(SubmitError::Pending)?;
        Ok(preflight)
    }

    fn stage_submit_workspace(
        &mut self,
        task_id: &TaskId,
        stage: WorkspaceStage<'_>,
    ) -> Result<Option<WorkspacePersistRequest>, SubmitError> {
        match self.stage_task_workspace_update(
            stage.workspace_id,
            stage.plan,
            stage.phase,
            stage.operation_id,
            stage.now_utc_millis,
        ) {
            Ok(workspace) => Ok(workspace),
            Err(error) => {
                if let Some(session) = self.tasks.get_mut(task_id.as_str()) {
                    session.recovery_required = true;
                }
                Err(SubmitError::WorkspaceAfterApply {
                    error,
                    in_memory_revision: stage.in_memory_revision,
                })
            }
        }
    }

    fn prepare_submission(
        &self,
        task_id: &TaskId,
        command: &CommandEnvelope,
        operation_id: OperationId,
        deadline: Deadline,
        now_monotonic_millis: u64,
    ) -> Result<SubmissionPreparation, SubmitError> {
        let previous_revision = self.submission_revision(task_id)?;
        let workspace_plan = self
            .prepare_page_ingestion(task_id, command)
            .map_err(SubmitError::Workspace)?;
        let workspace_id = self
            .tasks
            .get(task_id.as_str())
            .and_then(|session| session.task.workspace_id())
            .map(|workspace_id| workspace_id.to_text());
        let preflight = self.submission_preflight(
            operation_id,
            previous_revision,
            deadline,
            command.idempotency_key.clone(),
            now_monotonic_millis,
        )?;
        self.preflight_task_workspace_update(workspace_id.as_deref())
            .map_err(SubmitError::Workspace)?;
        Ok(SubmissionPreparation {
            previous_revision,
            workspace_plan,
            workspace_id,
            preflight,
            command_for_commit: command.clone(),
        })
    }

    /// Applies a command and emits one transactional commit intent.
    pub fn begin_submit(
        &mut self,
        task_id: &TaskId,
        command: CommandEnvelope,
        operation_id: OperationId,
        deadline: Deadline,
        now_monotonic_millis: u64,
        now_utc_millis: u64,
    ) -> Result<BeginSubmit, SubmitError> {
        let mut prepared = self.prepare_submission(
            task_id,
            &command,
            operation_id,
            deadline,
            now_monotonic_millis,
        )?;
        let applied = {
            let Some(session) = self.tasks.get_mut(task_id.as_str()) else {
                return Err(SubmitError::UnknownTask);
            };
            apply_submission(session, command)?
        };
        let continuing = match continuing_submission(applied) {
            Ok(continuing) => continuing,
            Err(accepted) => return Ok(BeginSubmit::Duplicate(accepted)),
        };
        let (accepted, appended_entries, workspace_phase) = continuing;
        let workspace = self.stage_submit_workspace(
            task_id,
            WorkspaceStage {
                workspace_id: prepared.workspace_id.as_deref(),
                plan: prepared.workspace_plan.take(),
                phase: workspace_phase,
                operation_id: &prepared.preflight.operation_id,
                now_utc_millis,
                in_memory_revision: accepted.revision,
            },
        )?;
        self.register_staged_submission(
            task_id,
            StagedSubmission {
                prepared,
                accepted,
                appended_entries,
                workspace,
            },
            now_monotonic_millis,
        )
    }

    /// Accepts a browser-owned transactional commit completion.
    pub fn complete_commit(
        &mut self,
        task_id: &TaskId,
        completion: OperationEnvelope<Completion>,
        now_millis: u64,
    ) -> Result<CommitOutcome, CommitCompletionError> {
        let Some(session) = self.tasks.get_mut(task_id.as_str()) else {
            return Err(CommitCompletionError::UnknownTask);
        };
        let Some(pending_submit) = session.pending_submit.as_ref() else {
            return Err(CommitCompletionError::NoCommitPending);
        };
        if pending_submit.operation_id != completion.operation_id
            || self
                .operation_tasks
                .get(&completion.operation_id)
                .is_none_or(|task| task != task_id.as_str())
        {
            return Err(CommitCompletionError::WrongOperation);
        }
        let terminal = self
            .pending
            .complete(completion, session.task.revision(), now_millis)
            .map_err(CommitCompletionError::Terminal)?;
        self.operation_tasks.remove(&terminal.operation_id);
        let Some(mut pending_submit) = session.pending_submit.take() else {
            return Err(CommitCompletionError::NoCommitPending);
        };
        if matches!(terminal.body, Completion::StorageCommitted { .. }) {
            if let Some(workspace_revision) = pending_submit.workspace_revision {
                if let Err(error) = self
                    .components
                    .workspaces
                    .complete_persist(terminal.operation_id.as_str(), workspace_revision)
                {
                    session.recovery_required = true;
                    self.components
                        .workspaces
                        .reject_persist(terminal.operation_id.as_str());
                    return Err(CommitCompletionError::Workspace(error));
                }
            }
            // Observation is post-hoc by contract (decision 0073): the commit
            // is durable, the outcome is already decided, and an observer
            // returns nothing — so this loop cannot change what the caller
            // receives, only attest that it happened.
            let fact = crate::ports::SettledFact::CommandCommitted {
                task_id: task_id.as_str(),
                from: pending_submit.accepted.from,
                to: pending_submit.accepted.to,
                revision: pending_submit.accepted.revision,
            };
            for observer in &mut self.components.observers {
                observer.observe(&fact);
            }
            let state = self
                .loop_states
                .entry(task_id.as_str().to_owned())
                .or_default();
            if let Some(task_library_search) = pending_submit.task_library_search.take() {
                task_library_search.install(state);
            }
            if let Some(task_memory_search) = pending_submit.task_memory_search.take() {
                task_memory_search.install(state);
            }
            if let Some(task_tab) = pending_submit.task_tab.take() {
                task_tab.install(state);
            }
            if let Some(task_store) = pending_submit.task_store.take() {
                task_store.install(state);
            }
            if let Some(task_download) = pending_submit.task_download.take() {
                task_download.install(state);
            }
            if let Some(media_probe) = pending_submit.media_probe.take() {
                media_probe.install(state);
            }
            self.record_committed_steps(task_id, &pending_submit.accepted.events);
            self.invalidate_changed_page(task_id, &pending_submit.accepted.events);
            return Ok(CommitOutcome::Committed(pending_submit.accepted));
        }
        if pending_submit.workspace_revision.is_some() {
            self.components
                .workspaces
                .reject_persist(terminal.operation_id.as_str());
        }
        session.recovery_required = true;
        Ok(CommitOutcome::RecoveryRequired {
            in_memory_revision: pending_submit.accepted.revision,
            terminal,
        })
    }
}
