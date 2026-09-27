// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Canonical portable Memory adapter.

mod conversion;

use core_api_types::{MemoryAvailability, MemorySearchHitView, MemorySearchView, MemoryViewState};
use core_service_types::MemoryRecord as WireRecord;
use taffy_storage::ids::{MemoryId, WorkspaceId};
use taffy_storage::memory::{
    MemoryMutation, MemoryMutationPlan, MemoryQuery, MemoryRecord, MemorySearchAudience,
    MemorySensitivity, MemorySource, MemoryStore,
};
use task_engine::TaskMemorySearchTranscriptOutcome;

use crate::account::Sha256Port;
use crate::ports::{
    MemoryDeleteRequest, MemoryPort, MemorySaveRequest, MemoryStoreError, MemoryTaskSave,
    MemoryTaskUpdate, MemoryUserUpsert,
};

use self::conversion::{
    map_error, record_from_wire, record_to_view, record_to_wire, scope_from_input,
    sensitivity_from_input, stable_memory_id, transcript_entry, workspace_from_input,
};

#[derive(Clone, Debug)]
pub struct ProductionMemory {
    store: MemoryStore,
    private_profile: bool,
    latest_search: Option<MemorySearchView>,
}

impl ProductionMemory {
    pub const fn new(private_profile: bool) -> Self {
        Self {
            store: MemoryStore::new(),
            private_profile,
            latest_search: None,
        }
    }

    fn require_available(&self) -> Result<(), MemoryStoreError> {
        if self.private_profile {
            Err(MemoryStoreError::PrivateProfile)
        } else {
            Ok(())
        }
    }

    fn stage_record(
        &mut self,
        operation_id: String,
        expected_memory_revision: u64,
        expected_record_revision: u64,
        record: MemoryRecord,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError> {
        let plan = self
            .store
            .begin_save(
                operation_id,
                expected_memory_revision,
                expected_record_revision,
                record,
            )
            .map_err(map_error)?;
        match plan {
            MemoryMutationPlan::AlreadyCurrent => Ok(None),
            MemoryMutationPlan::Persist(request) => {
                let MemoryMutation::Save(record) = request.mutation else {
                    return Err(MemoryStoreError::InvalidRecord);
                };
                Ok(Some(MemorySaveRequest {
                    operation_id: request.operation_id,
                    expected_memory_revision: request.expected_memory_revision,
                    resulting_memory_revision: request.resulting_memory_revision,
                    expected_record_revision,
                    record: record_to_wire(&record),
                }))
            }
        }
    }
}

impl MemoryPort for ProductionMemory {
    fn restore(&mut self, revision: u64, records: Vec<WireRecord>) -> Result<(), MemoryStoreError> {
        if self.private_profile {
            return if revision == 0 && records.is_empty() {
                self.latest_search = None;
                Ok(())
            } else {
                Err(MemoryStoreError::PrivateProfile)
            };
        }
        let records = records
            .into_iter()
            .map(record_from_wire)
            .collect::<Result<Vec<_>, _>>()?;
        self.store.restore(revision, records).map_err(map_error)?;
        self.latest_search = None;
        Ok(())
    }

    fn search_for_person(
        &mut self,
        request_id: &str,
        query: &str,
        limit: u32,
        now_epoch_ms: u64,
    ) -> Result<(), MemoryStoreError> {
        self.require_available()?;
        if request_id.is_empty() || request_id.len() > core_service_types::MAX_IDENTIFIER_BYTES {
            return Err(MemoryStoreError::InvalidIdentifier);
        }
        let parsed = MemoryQuery::new(query, limit).map_err(map_error)?;
        let hits = self
            .store
            .search(&parsed, MemorySearchAudience::PersonReview, now_epoch_ms)
            .into_iter()
            .map(|hit| MemorySearchHitView {
                memory_id: hit.record.memory_id.to_text(),
            })
            .collect();
        self.latest_search = Some(MemorySearchView {
            request_id: request_id.to_owned(),
            query: query.to_owned(),
            memory_revision: self.store.revision(),
            hits,
        });
        Ok(())
    }

    fn search_for_task(
        &self,
        query: &str,
        limit: u32,
        workspace_id: Option<&str>,
        now_epoch_ms: u64,
    ) -> Result<TaskMemorySearchTranscriptOutcome, MemoryStoreError> {
        self.require_available()?;
        let workspace_id = workspace_id
            .map(WorkspaceId::parse)
            .transpose()
            .map_err(|_| MemoryStoreError::InvalidIdentifier)?;
        let parsed = MemoryQuery::new(query, limit).map_err(map_error)?;
        let entries = self
            .store
            .search(
                &parsed,
                MemorySearchAudience::Task { workspace_id },
                now_epoch_ms,
            )
            .into_iter()
            .map(|hit| transcript_entry(&hit.record).ok_or(MemoryStoreError::InvalidRecord))
            .collect::<Result<Vec<_>, _>>()?;
        Ok(TaskMemorySearchTranscriptOutcome::bounded(entries))
    }

    fn begin_user_upsert(
        &mut self,
        operation_id: String,
        input: MemoryUserUpsert,
        digest: &dyn Sha256Port,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError> {
        self.require_available()?;
        let memory_id = input.memory_id.as_deref().map_or_else(
            || stable_memory_id(&operation_id, digest),
            |value| MemoryId::parse(value).map_err(|_| MemoryStoreError::InvalidIdentifier),
        )?;
        let current = self.store.record(memory_id);
        let source = current.map_or(MemorySource::UserEntered, |record| record.source.clone());
        let created_at_epoch_ms = current.map_or(input.approved_at_epoch_ms, |record| {
            record.created_at_epoch_ms
        });
        let reviewed_at_epoch_ms = match source {
            MemorySource::UserEntered => None,
            MemorySource::AcceptedTaskSuggestion { .. } => Some(input.approved_at_epoch_ms),
        };
        self.stage_record(
            operation_id,
            input.expected_memory_revision,
            input.expected_record_revision,
            MemoryRecord {
                memory_id,
                revision: input
                    .expected_record_revision
                    .checked_add(1)
                    .ok_or(MemoryStoreError::StaleRecordRevision)?,
                statement: input.statement,
                source,
                scope: scope_from_input(input.scope)?,
                sensitivity: sensitivity_from_input(input.sensitivity),
                created_at_epoch_ms,
                updated_at_epoch_ms: input.approved_at_epoch_ms,
                reviewed_at_epoch_ms,
                expires_at_epoch_ms: input.expires_at_epoch_ms,
            },
        )
    }

    fn begin_task_save(
        &mut self,
        operation_id: String,
        input: MemoryTaskSave,
        digest: &dyn Sha256Port,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError> {
        self.require_available()?;
        let memory_id = stable_memory_id(&operation_id, digest)?;
        self.stage_record(
            operation_id,
            input.expected_memory_revision,
            0,
            MemoryRecord {
                memory_id,
                revision: 1,
                statement: input.statement,
                source: MemorySource::AcceptedTaskSuggestion {
                    task_id: input.task_id,
                    workspace: input
                        .source_workspace
                        .map(workspace_from_input)
                        .transpose()?,
                },
                scope: scope_from_input(input.scope)?,
                sensitivity: MemorySensitivity::Standard,
                created_at_epoch_ms: input.approved_at_epoch_ms,
                updated_at_epoch_ms: input.approved_at_epoch_ms,
                reviewed_at_epoch_ms: Some(input.approved_at_epoch_ms),
                expires_at_epoch_ms: input.expires_at_epoch_ms,
            },
        )
    }

    fn begin_task_update(
        &mut self,
        operation_id: String,
        input: MemoryTaskUpdate,
    ) -> Result<Option<MemorySaveRequest>, MemoryStoreError> {
        self.require_available()?;
        let memory_id =
            MemoryId::parse(&input.memory_id).map_err(|_| MemoryStoreError::InvalidIdentifier)?;
        let current = self
            .store
            .record(memory_id)
            .ok_or(MemoryStoreError::UnknownRecord)?
            .clone();
        self.stage_record(
            operation_id,
            input.expected_memory_revision,
            input.expected_record_revision,
            MemoryRecord {
                memory_id,
                revision: input
                    .expected_record_revision
                    .checked_add(1)
                    .ok_or(MemoryStoreError::StaleRecordRevision)?,
                statement: input.statement,
                source: current.source,
                scope: scope_from_input(input.scope)?,
                sensitivity: current.sensitivity,
                created_at_epoch_ms: current.created_at_epoch_ms,
                updated_at_epoch_ms: input.approved_at_epoch_ms,
                reviewed_at_epoch_ms: Some(input.approved_at_epoch_ms),
                expires_at_epoch_ms: input.expires_at_epoch_ms,
            },
        )
    }

    fn begin_delete(
        &mut self,
        operation_id: String,
        memory_id: &str,
        expected_memory_revision: u64,
        expected_record_revision: u64,
        deleted_at_epoch_ms: u64,
    ) -> Result<MemoryDeleteRequest, MemoryStoreError> {
        self.require_available()?;
        let memory_id =
            MemoryId::parse(memory_id).map_err(|_| MemoryStoreError::InvalidIdentifier)?;
        let request = self
            .store
            .begin_delete(
                operation_id,
                expected_memory_revision,
                memory_id,
                expected_record_revision,
            )
            .map_err(map_error)?;
        let MemoryMutation::Delete {
            memory_id,
            expected_record_revision,
            resulting_record_revision,
        } = request.mutation
        else {
            return Err(MemoryStoreError::InvalidRecord);
        };
        Ok(MemoryDeleteRequest {
            operation_id: request.operation_id,
            expected_memory_revision: request.expected_memory_revision,
            resulting_memory_revision: request.resulting_memory_revision,
            memory_id: memory_id.to_text(),
            expected_record_revision,
            resulting_record_revision,
            deleted_at_epoch_ms,
        })
    }

    fn complete(
        &mut self,
        operation_id: &str,
        committed_memory_revision: u64,
    ) -> Result<(), MemoryStoreError> {
        self.require_available()?;
        self.store
            .complete(operation_id, committed_memory_revision)
            .map_err(map_error)?;
        self.latest_search = None;
        Ok(())
    }

    fn reject(&mut self, operation_id: &str) -> bool {
        self.store.reject(operation_id)
    }

    fn project_core_api(&self) -> MemoryViewState {
        MemoryViewState {
            availability: if self.private_profile {
                MemoryAvailability::PrivateProfile
            } else {
                MemoryAvailability::Available
            },
            revision: self.store.revision(),
            records: if self.private_profile {
                Vec::new()
            } else {
                self.store.records().map(record_to_view).collect()
            },
            search: if self.private_profile {
                None
            } else {
                self.latest_search.clone()
            },
        }
    }
}
