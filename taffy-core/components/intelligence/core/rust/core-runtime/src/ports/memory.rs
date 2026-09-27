// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The durable Memory composition port.

use core_api_types::MemoryViewState;
use core_service_types::MemoryRecord;
use task_engine::TaskMemorySearchTranscriptOutcome;

use crate::account::Sha256Port;

/// Display-safe exact workspace chosen as Memory scope or attribution.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryWorkspaceInput {
    pub workspace_id: String,
    pub display_name: String,
}

/// Visible future-task scope selected for a Memory statement.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum MemoryScopeInput {
    AllTasks,
    Workspace(MemoryWorkspaceInput),
}

/// Closed handling class selected by a person.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MemorySensitivityInput {
    Standard,
    Sensitive,
}

/// One direct person-authored create or update.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryUserUpsert {
    pub memory_id: Option<String>,
    pub statement: String,
    pub scope: MemoryScopeInput,
    pub sensitivity: MemorySensitivityInput,
    pub expected_memory_revision: u64,
    pub expected_record_revision: u64,
    pub expires_at_epoch_ms: Option<u64>,
    pub approved_at_epoch_ms: u64,
}

/// One task suggestion the person explicitly approved.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryTaskSave {
    pub task_id: String,
    pub source_workspace: Option<MemoryWorkspaceInput>,
    pub statement: String,
    pub scope: MemoryScopeInput,
    pub expected_memory_revision: u64,
    pub expires_at_epoch_ms: Option<u64>,
    pub approved_at_epoch_ms: u64,
}

/// One exact record revision the person explicitly approved changing.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryTaskUpdate {
    pub memory_id: String,
    pub expected_record_revision: u64,
    pub statement: String,
    pub scope: MemoryScopeInput,
    pub expected_memory_revision: u64,
    pub expires_at_epoch_ms: Option<u64>,
    pub approved_at_epoch_ms: u64,
}

/// A staged Memory upsert awaiting one browser-owned atomic transaction.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemorySaveRequest {
    pub operation_id: String,
    pub expected_memory_revision: u64,
    pub resulting_memory_revision: u64,
    pub expected_record_revision: u64,
    pub record: MemoryRecord,
}

/// A staged content-removing Memory mutation.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MemoryDeleteRequest {
    pub operation_id: String,
    pub expected_memory_revision: u64,
    pub resulting_memory_revision: u64,
    pub memory_id: String,
    pub expected_record_revision: u64,
    pub resulting_record_revision: u64,
    pub deleted_at_epoch_ms: u64,
}

/// Closed Memory-plane failures.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MemoryStoreError {
    PrivateProfile,
    InvalidIdentifier,
    InvalidRecord,
    InvalidQuery,
    UnknownRecord,
    UnknownWorkspace,
    StaleMemoryRevision,
    StaleRecordRevision,
    OperationAlreadyPending,
    TooManyRecords,
    TooManyPendingMutations,
    DigestUnavailable,
    WrongCompletion,
}

pub trait MemoryPort {
    fn restore(
        &mut self,
        revision: u64,
        records: Vec<MemoryRecord>,
    ) -> Result<(), MemoryStoreError>;

    fn search_for_person(
        &mut self,
        request_id: &str,
        query: &str,
        limit: u32,
        now_epoch_ms: u64,
    ) -> Result<(), MemoryStoreError>;

    fn search_for_task(
        &self,
        query: &str,
        limit: u32,
        workspace_id: Option<&str>,
        now_epoch_ms: u64,
    ) -> Result<TaskMemorySearchTranscriptOutcome, MemoryStoreError>;

    fn begin_user_upsert(
        &mut self,
        operation_id: String,
        input: MemoryUserUpsert,
        digest: &dyn Sha256Port,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError>;

    fn begin_task_save(
        &mut self,
        operation_id: String,
        input: MemoryTaskSave,
        digest: &dyn Sha256Port,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError>;

    fn begin_task_update(
        &mut self,
        operation_id: String,
        input: MemoryTaskUpdate,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError>;

    fn begin_delete(
        &mut self,
        operation_id: String,
        memory_id: &str,
        expected_memory_revision: u64,
        expected_record_revision: u64,
        deleted_at_epoch_ms: u64,
    ) -> Result<MemoryDeleteRequest, MemoryStoreError>;

    fn complete(
        &mut self,
        operation_id: &str,
        committed_memory_revision: u64,
    ) -> Result<(), MemoryStoreError>;

    fn reject(&mut self, operation_id: &str) -> bool;

    fn project_core_api(&self) -> MemoryViewState;
}

impl<T> MemoryPort for Box<T>
where
    T: MemoryPort + ?Sized,
{
    fn restore(
        &mut self,
        revision: u64,
        records: Vec<MemoryRecord>,
    ) -> Result<(), MemoryStoreError> {
        self.as_mut().restore(revision, records)
    }

    fn search_for_person(
        &mut self,
        request_id: &str,
        query: &str,
        limit: u32,
        now_epoch_ms: u64,
    ) -> Result<(), MemoryStoreError> {
        self.as_mut()
            .search_for_person(request_id, query, limit, now_epoch_ms)
    }

    fn search_for_task(
        &self,
        query: &str,
        limit: u32,
        workspace_id: Option<&str>,
        now_epoch_ms: u64,
    ) -> Result<TaskMemorySearchTranscriptOutcome, MemoryStoreError> {
        self.as_ref()
            .search_for_task(query, limit, workspace_id, now_epoch_ms)
    }

    fn begin_user_upsert(
        &mut self,
        operation_id: String,
        input: MemoryUserUpsert,
        digest: &dyn Sha256Port,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError> {
        self.as_mut().begin_user_upsert(operation_id, input, digest)
    }

    fn begin_task_save(
        &mut self,
        operation_id: String,
        input: MemoryTaskSave,
        digest: &dyn Sha256Port,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError> {
        self.as_mut().begin_task_save(operation_id, input, digest)
    }

    fn begin_task_update(
        &mut self,
        operation_id: String,
        input: MemoryTaskUpdate,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError> {
        self.as_mut().begin_task_update(operation_id, input)
    }

    fn begin_delete(
        &mut self,
        operation_id: String,
        memory_id: &str,
        expected_memory_revision: u64,
        expected_record_revision: u64,
        deleted_at_epoch_ms: u64,
    ) -> Result<MemoryDeleteRequest, MemoryStoreError> {
        self.as_mut().begin_delete(
            operation_id,
            memory_id,
            expected_memory_revision,
            expected_record_revision,
            deleted_at_epoch_ms,
        )
    }

    fn complete(
        &mut self,
        operation_id: &str,
        committed_memory_revision: u64,
    ) -> Result<(), MemoryStoreError> {
        self.as_mut()
            .complete(operation_id, committed_memory_revision)
    }

    fn reject(&mut self, operation_id: &str) -> bool {
        self.as_mut().reject(operation_id)
    }

    fn project_core_api(&self) -> MemoryViewState {
        self.as_ref().project_core_api()
    }
}
