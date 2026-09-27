// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Ordered Memory orchestration for direct commands and approved task tools.

mod task_search;

use bip_types::identity::{ActionId, TaskId};
use core_service_types::MemoryRecord;
use task_engine::action::{ActionIntent, MemoryIntent, MemoryScopeIntent};
use task_engine::{ActionProposal, ActionState, TaskMemorySearchTranscriptOutcome};

use crate::ports::{
    MemoryDeleteRequest, MemorySaveRequest, MemoryScopeInput, MemoryStoreError, MemoryTaskSave,
    MemoryTaskUpdate, MemoryUserUpsert, MemoryWorkspaceInput,
};

pub(super) use self::task_search::PendingTaskMemorySearchSettlement;
use super::CoreRuntime;

/// One authorized Memory action after exact reducer/runtime validation.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum TaskMemoryExecution {
    /// A bounded task-safe search ran; its result awaits action-outcome commit.
    Search(TaskMemorySearchTranscriptOutcome),
    /// The exact requested record was already current.
    AlreadyCurrent,
    /// Browser storage must atomically persist this record and revision.
    Save(Box<MemorySaveRequest>),
    /// Browser storage must atomically delete this record and scrub its bytes.
    Delete(MemoryDeleteRequest),
}

/// Why a durable Memory effect could not be executed as authored.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskMemoryExecutionError {
    UnknownTask,
    UnknownAction,
    WrongActionState,
    WrongIntent,
    MissingResidentOperand,
    Store(MemoryStoreError),
}

struct TaskMemoryMutationContext {
    operation_id: String,
    memory_revision: u64,
    now_epoch_ms: u64,
    expires_at_epoch_ms: Option<u64>,
}

impl CoreRuntime {
    /// Restores the complete browser-owned Memory aggregate before commands
    /// are admitted. A private profile accepts only the empty revision-zero
    /// bootstrap enforced by the Memory port.
    pub fn restore_memory(
        &mut self,
        revision: u64,
        records: Vec<MemoryRecord>,
    ) -> Result<(), MemoryStoreError> {
        self.components.memory.restore(revision, records)
    }

    /// Runs one bounded person-visible search and publishes only record IDs in
    /// `CoreStatus`; full statements remain in the already-visible record list.
    pub fn search_memory_for_person(
        &mut self,
        request_id: &str,
        query: &str,
        limit: u32,
        now_epoch_ms: u64,
    ) -> Result<(), MemoryStoreError> {
        self.components
            .memory
            .search_for_person(request_id, query, limit, now_epoch_ms)
    }

    /// Stages one direct, explicitly submitted person-authored create/update.
    pub fn begin_user_memory_upsert(
        &mut self,
        operation_id: String,
        input: MemoryUserUpsert,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError> {
        let digest = self.components.digest.clone();
        self.components
            .memory
            .begin_user_upsert(operation_id, input, digest.as_ref())
    }

    /// Stages one direct, exact-revision deletion.
    pub fn begin_memory_delete(
        &mut self,
        operation_id: String,
        memory_id: &str,
        expected_memory_revision: u64,
        expected_record_revision: u64,
        deleted_at_epoch_ms: u64,
    ) -> Result<MemoryDeleteRequest, MemoryStoreError> {
        self.components.memory.begin_delete(
            operation_id,
            memory_id,
            expected_memory_revision,
            expected_record_revision,
            deleted_at_epoch_ms,
        )
    }

    /// Executes one reducer-authorized Memory tool. Memory writes reach this
    /// method only after policy and person approval put the action in
    /// `Dispatching`; mutations remain staged until browser durability.
    pub fn begin_task_memory_tool(
        &mut self,
        task_id: &TaskId,
        action_id: &ActionId,
        operation_id: String,
        now_epoch_ms: u64,
    ) -> Result<TaskMemoryExecution, TaskMemoryExecutionError> {
        let facts = self
            .tasks
            .get(task_id.as_str())
            .ok_or(TaskMemoryExecutionError::UnknownTask)?
            .task
            .action_effect_facts(action_id)
            .ok_or(TaskMemoryExecutionError::UnknownAction)?;
        if facts.state != ActionState::Dispatching || facts.dispatch_id.is_none() {
            return Err(TaskMemoryExecutionError::WrongActionState);
        }
        let ActionIntent::Memory(intent) = facts.proposal.intent().clone() else {
            return Err(TaskMemoryExecutionError::WrongIntent);
        };
        let memory_revision = self.components.memory.project_core_api().revision;
        match intent {
            MemoryIntent::Search { limit, .. } => {
                self.begin_task_memory_search(task_id, &facts.proposal, limit, now_epoch_ms)
            }
            MemoryIntent::Save {
                scope,
                expires_at_epoch_ms,
                ..
            } => self.begin_task_memory_save(
                task_id,
                &facts.proposal,
                scope,
                TaskMemoryMutationContext {
                    operation_id,
                    memory_revision,
                    now_epoch_ms,
                    expires_at_epoch_ms,
                },
            ),
            MemoryIntent::Update {
                memory_id,
                record_revision,
                scope,
                expires_at_epoch_ms,
                ..
            } => self.begin_task_memory_update(
                task_id,
                &facts.proposal,
                memory_id,
                record_revision,
                scope,
                TaskMemoryMutationContext {
                    operation_id,
                    memory_revision,
                    now_epoch_ms,
                    expires_at_epoch_ms,
                },
            ),
            MemoryIntent::Delete {
                memory_id,
                record_revision,
                ..
            } => self
                .begin_memory_delete(
                    operation_id,
                    &memory_id,
                    memory_revision,
                    record_revision,
                    now_epoch_ms,
                )
                .map(TaskMemoryExecution::Delete)
                .map_err(TaskMemoryExecutionError::Store),
        }
    }

    fn begin_task_memory_search(
        &mut self,
        task_id: &TaskId,
        proposal: &ActionProposal,
        limit: u32,
        now_epoch_ms: u64,
    ) -> Result<TaskMemoryExecution, TaskMemoryExecutionError> {
        let query = self
            .transient_memory_query(task_id.as_str(), proposal)
            .ok_or(TaskMemoryExecutionError::MissingResidentOperand)?
            .to_owned();
        let workspace_id = self
            .tasks
            .get(task_id.as_str())
            .and_then(|session| session.task.workspace_id())
            .map(|value| value.to_text());
        self.components
            .memory
            .search_for_task(&query, limit, workspace_id.as_deref(), now_epoch_ms)
            .map(TaskMemoryExecution::Search)
            .map_err(TaskMemoryExecutionError::Store)
    }

    fn begin_task_memory_save(
        &mut self,
        task_id: &TaskId,
        proposal: &ActionProposal,
        scope: MemoryScopeIntent,
        context: TaskMemoryMutationContext,
    ) -> Result<TaskMemoryExecution, TaskMemoryExecutionError> {
        let statement = self
            .transient_memory_statement(task_id.as_str(), proposal)
            .ok_or(TaskMemoryExecutionError::MissingResidentOperand)?
            .to_owned();
        let source_workspace = self.task_memory_source_workspace(task_id)?;
        let scope = self.memory_scope_input(scope)?;
        let digest = self.components.digest.clone();
        self.components
            .memory
            .begin_task_save(
                context.operation_id,
                MemoryTaskSave {
                    task_id: task_id.as_str().to_owned(),
                    source_workspace,
                    statement,
                    scope,
                    expected_memory_revision: context.memory_revision,
                    expires_at_epoch_ms: context.expires_at_epoch_ms,
                    approved_at_epoch_ms: context.now_epoch_ms,
                },
                digest.as_ref(),
            )
            .map(task_save_execution)
            .map_err(TaskMemoryExecutionError::Store)
    }

    fn begin_task_memory_update(
        &mut self,
        task_id: &TaskId,
        proposal: &ActionProposal,
        memory_id: String,
        record_revision: u64,
        scope: MemoryScopeIntent,
        context: TaskMemoryMutationContext,
    ) -> Result<TaskMemoryExecution, TaskMemoryExecutionError> {
        let statement = self
            .transient_memory_statement(task_id.as_str(), proposal)
            .ok_or(TaskMemoryExecutionError::MissingResidentOperand)?
            .to_owned();
        let scope = self.memory_scope_input(scope)?;
        self.components
            .memory
            .begin_task_update(
                context.operation_id,
                MemoryTaskUpdate {
                    memory_id,
                    expected_record_revision: record_revision,
                    statement,
                    scope,
                    expected_memory_revision: context.memory_revision,
                    expires_at_epoch_ms: context.expires_at_epoch_ms,
                    approved_at_epoch_ms: context.now_epoch_ms,
                },
            )
            .map(task_save_execution)
            .map_err(TaskMemoryExecutionError::Store)
    }

    /// Publishes one staged Memory mutation after exact browser durability.
    pub fn complete_memory_mutation(
        &mut self,
        operation_id: &str,
        committed_memory_revision: u64,
    ) -> Result<(), MemoryStoreError> {
        self.components
            .memory
            .complete(operation_id, committed_memory_revision)
    }

    /// Rejects one staged mutation without changing visible state.
    pub fn reject_memory_mutation(&mut self, operation_id: &str) -> bool {
        self.components.memory.reject(operation_id)
    }

    fn task_memory_source_workspace(
        &self,
        task_id: &TaskId,
    ) -> Result<Option<MemoryWorkspaceInput>, TaskMemoryExecutionError> {
        let workspace_id = self
            .tasks
            .get(task_id.as_str())
            .ok_or(TaskMemoryExecutionError::UnknownTask)?
            .task
            .workspace_id()
            .map(|value| value.to_text());
        workspace_id
            .map(|value| self.memory_workspace_input(&value))
            .transpose()
            .map_err(TaskMemoryExecutionError::Store)
    }

    fn memory_scope_input(
        &self,
        scope: MemoryScopeIntent,
    ) -> Result<MemoryScopeInput, TaskMemoryExecutionError> {
        match scope {
            MemoryScopeIntent::AllTasks => Ok(MemoryScopeInput::AllTasks),
            MemoryScopeIntent::Workspace { workspace_id } => self
                .memory_workspace_input(&workspace_id)
                .map(MemoryScopeInput::Workspace)
                .map_err(TaskMemoryExecutionError::Store),
        }
    }

    fn memory_workspace_input(
        &self,
        workspace_id: &str,
    ) -> Result<MemoryWorkspaceInput, MemoryStoreError> {
        self.components
            .workspaces
            .list_workspaces()
            .into_iter()
            .find(|workspace| workspace.workspace_id == workspace_id)
            .map(|workspace| MemoryWorkspaceInput {
                workspace_id: workspace.workspace_id,
                display_name: workspace.display_name,
            })
            .ok_or(MemoryStoreError::UnknownWorkspace)
    }
}

fn task_save_execution(request: Option<MemorySaveRequest>) -> TaskMemoryExecution {
    request.map_or(TaskMemoryExecution::AlreadyCurrent, |value| {
        TaskMemoryExecution::Save(Box::new(value))
    })
}
