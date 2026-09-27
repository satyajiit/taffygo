// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Policy, audit, storage, and model composition ports.

use bip_types::identity::TaskId;
use core_service_types::PersistedAuditRecord;
use policy_engine::{GrantRequest, MintedGrant, PolicyVersion};
use task_engine::{
    CommandEnvelope, Effect, IdempotencyKey, ProposalDecision, TaskEvent, TaskSeed, TraceId,
    WorkspaceId,
};

use crate::contract::BoundedPayload;

use super::TaskIdEntropy;

/// The high-level policy decision surface.
pub trait PolicyPort {
    /// Policy bundle compiled into the one canonical grant minter.
    fn policy_version(&self) -> PolicyVersion;

    /// Decides complete typed context and may mint one browser-ledger grant.
    fn decide(
        &mut self,
        request: &GrantRequest,
        now_millis: u64,
    ) -> Result<PolicyEvaluation, super::PortError>;
}

impl<T> PolicyPort for Box<T>
where
    T: PolicyPort + ?Sized,
{
    fn policy_version(&self) -> PolicyVersion {
        self.as_ref().policy_version()
    }

    fn decide(
        &mut self,
        request: &GrantRequest,
        now_millis: u64,
    ) -> Result<PolicyEvaluation, super::PortError> {
        self.as_mut().decide(request, now_millis)
    }
}

/// Policy result for both reducer state and the browser-owned grant ledger.
#[derive(Clone, Debug, PartialEq)]
pub struct PolicyEvaluation {
    /// Narrow decision the task journal records.
    pub task_decision: ProposalDecision,
    /// Full grant registered only in the browser ledger.
    pub minted_grant: Option<MintedGrant>,
}

/// A durable transition batch sent to the browser-owned single writer.
#[derive(Clone, Debug)]
pub struct StorageCommit<'a> {
    /// Aggregate identity.
    pub task_id: &'a TaskId,
    /// Optional workspace projection owner.
    pub workspace_id: Option<&'a WorkspaceId>,
    /// The complete command record, including its expected revision.
    pub command: &'a CommandEnvelope,
    /// Aggregate revision before applying the command.
    pub previous_revision: u64,
    /// Aggregate revision after applying the command.
    pub resulting_revision: u64,
    /// Exact command and event records appended by the reducer.
    pub journal_entries: &'a [task_engine::JournalEntry],
    /// Events committed by the task reducer.
    pub events: &'a [TaskEvent],
    /// Effect intents committed before any corresponding dispatch.
    pub effect_intents: &'a [Effect],
    /// Command identity for replay and effect intent correlation.
    pub idempotency_key: &'a IdempotencyKey,
    /// Trace propagated from the command envelope.
    pub trace_id: &'a TraceId,
}

/// Initial task seed and creation record committed before a task becomes open.
pub struct TaskCreationCommit<'a> {
    /// Complete immutable seed needed for canonical replay.
    pub seed: &'a TaskSeed,
    /// Creation record identity used for idempotent append.
    pub creation_key: &'a IdempotencyKey,
    /// Trace propagated from the visible start-task command.
    pub trace_id: &'a TraceId,
    /// Replay-stable entropy committed atomically with the seed.
    pub id_entropy: &'a TaskIdEntropy,
    /// Revision produced by deterministic reducer construction.
    pub resulting_revision: u64,
    /// Exact creation command and event records produced by the reducer.
    pub journal_entries: &'a [task_engine::JournalEntry],
    /// Initial effect intents committed before the fresh task becomes open.
    pub effect_intents: &'a [Effect],
}

impl core::fmt::Debug for TaskCreationCommit<'_> {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("TaskCreationCommit")
            .field("task_id", &self.seed.task_id)
            .field("workspace_id", &self.seed.workspace_id)
            .field("resulting_revision", &self.resulting_revision)
            .finish_non_exhaustive()
    }
}

/// Content-free task-creation facts eligible for independent audit encoding.
#[derive(Clone, Copy, Debug)]
pub struct TaskCreationAudit<'a> {
    /// Aggregate identity.
    pub task_id: &'a TaskId,
    /// Creation record identity.
    pub creation_key: &'a IdempotencyKey,
    /// Trace propagated from the visible start-task command.
    pub trace_id: &'a TraceId,
    /// Revision produced by deterministic reducer construction.
    pub resulting_revision: u64,
    /// Exact reducer-owned creation records used for sequence and UTC time.
    pub journal_entries: &'a [task_engine::JournalEntry],
}

/// Pure storage-domain encoding; the browser remains the only physical writer.
///
/// Implementations must not block, perform I/O, enter Mojo, or retain `commit`.
pub trait StorageDomainPort {
    /// Encodes task seed, creation event, and audit as one initial transaction.
    fn encode_task_creation(
        &self,
        commit: &TaskCreationCommit<'_>,
        audit_records: &[PersistedAuditRecord],
    ) -> Result<BoundedPayload, super::PortError>;

    /// Encodes one transactional command/event/effect-intent append.
    ///
    /// Audit is embedded in the same browser-owned commit so a crash cannot
    /// persist task state while silently losing its audit projection.
    fn encode_commit(
        &self,
        commit: &StorageCommit<'_>,
        audit_records: &[PersistedAuditRecord],
    ) -> Result<BoundedPayload, super::PortError>;
}

impl<T> StorageDomainPort for Box<T>
where
    T: StorageDomainPort + ?Sized,
{
    fn encode_task_creation(
        &self,
        commit: &TaskCreationCommit<'_>,
        audit_records: &[PersistedAuditRecord],
    ) -> Result<BoundedPayload, super::PortError> {
        self.as_ref().encode_task_creation(commit, audit_records)
    }

    fn encode_commit(
        &self,
        commit: &StorageCommit<'_>,
        audit_records: &[PersistedAuditRecord],
    ) -> Result<BoundedPayload, super::PortError> {
        self.as_ref().encode_commit(commit, audit_records)
    }
}

/// Pure independent last-redaction and product-audit encoding.
///
/// Implementations must not block, perform I/O, enter Mojo, or retain `commit`.
pub trait AuditPort {
    /// Encodes a content-free creation record without receiving the task goal.
    fn encode_task_creation(
        &self,
        creation: TaskCreationAudit<'_>,
    ) -> Result<Vec<PersistedAuditRecord>, super::PortError>;

    /// Encodes the audit projection included in the transactional commit.
    fn encode_record(
        &self,
        commit: &StorageCommit<'_>,
    ) -> Result<Vec<PersistedAuditRecord>, super::PortError>;
}

impl<T> AuditPort for Box<T>
where
    T: AuditPort + ?Sized,
{
    fn encode_task_creation(
        &self,
        creation: TaskCreationAudit<'_>,
    ) -> Result<Vec<PersistedAuditRecord>, super::PortError> {
        self.as_ref().encode_task_creation(creation)
    }

    fn encode_record(
        &self,
        commit: &StorageCommit<'_>,
    ) -> Result<Vec<PersistedAuditRecord>, super::PortError> {
        self.as_ref().encode_record(commit)
    }
}
