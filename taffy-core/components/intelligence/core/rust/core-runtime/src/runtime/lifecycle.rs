// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Policy/routing delegation and task-scoped effect recovery.

use bip_types::identity::TaskId;
use model_router::route::RouteRequest;
use model_router::{RoutePlan, RouteRefusal, TaskLedger};
use policy_engine::GrantRequest;
use task_engine::IdempotencyKey;

use crate::account::AccountSession;
use crate::contract::{
    CancellationReason, Completion, Deadline, EffectRequest, OperationEnvelope, OperationId,
    ServiceGeneration,
};
use crate::ports::{AccountPort, ModelRouterPort, PolicyEvaluation, PortError, TaskEnginePort};

use super::{
    AccountRestoreError, CoreRuntime, DisconnectError, StageError, TaskCompletionError,
    TaskTerminal,
};

impl CoreRuntime {
    /// Policy bundle compiled into the canonical profile grant minter.
    pub fn policy_version(&self) -> policy_engine::PolicyVersion {
        self.components.policy.policy_version()
    }

    /// Policy bundle frozen into one durable task seed.
    pub fn task_policy_version(&self, task_id: &TaskId) -> Option<task_engine::PolicyVersion> {
        self.tasks
            .get(task_id.as_str())
            .map(|session| session.task.capability_policy_version())
    }

    /// Delegates a proposal to the only component that may mint a grant.
    pub fn decide_policy(
        &mut self,
        request: &GrantRequest,
        now_millis: u64,
    ) -> Result<PolicyEvaluation, PortError> {
        self.components.policy.decide(request, now_millis)
    }

    /// Runs provider-neutral route planning without network I/O.
    pub fn route_model(
        &mut self,
        request: &RouteRequest,
        ledger: &TaskLedger,
    ) -> Result<RoutePlan, RouteRefusal> {
        self.components.models.route(request, ledger)
    }

    /// Registers an effect envelope before browser broker dispatch.
    pub fn stage_effect(
        &mut self,
        task_id: &TaskId,
        operation_id: OperationId,
        deadline: Deadline,
        idempotency_key: IdempotencyKey,
        effect: EffectRequest,
        now_millis: u64,
    ) -> Result<OperationEnvelope<EffectRequest>, StageError> {
        let Some(session) = self.tasks.get(task_id.as_str()) else {
            return Err(StageError::UnknownTask);
        };
        if session.recovery_required {
            return Err(StageError::RecoveryRequired);
        }
        if session.pending_submit.is_some() {
            return Err(StageError::CommitInFlight);
        }
        let envelope = OperationEnvelope::new(
            operation_id,
            self.pending.active_generation(),
            session.task.revision(),
            deadline,
            idempotency_key,
            effect,
        )
        .map_err(StageError::Envelope)?;
        self.pending
            .register(envelope.clone(), session.task.revision(), now_millis)
            .map_err(StageError::Pending)?;
        self.operation_tasks
            .insert(envelope.operation_id.clone(), task_id.as_str().to_owned());
        Ok(envelope)
    }

    /// Accepts exactly one terminal completion for non-commit work.
    pub fn complete_effect(
        &mut self,
        task_id: &TaskId,
        completion: OperationEnvelope<Completion>,
        now_millis: u64,
    ) -> Result<OperationEnvelope<Completion>, TaskCompletionError> {
        let Some(session) = self.tasks.get(task_id.as_str()) else {
            return Err(TaskCompletionError::UnknownTask);
        };
        if let Some(owner) = self.operation_tasks.get(&completion.operation_id) {
            if owner.as_str() != task_id.as_str() {
                return Err(TaskCompletionError::WrongTask);
            }
        }
        let terminal = self
            .pending
            .complete(completion, session.task.revision(), now_millis)
            .map_err(TaskCompletionError::Terminal)?;
        self.operation_tasks.remove(&terminal.operation_id);
        Ok(terminal)
    }

    /// Resolves cancellation locally before asking a worker to stop.
    pub fn cancel_effect(
        &mut self,
        task_id: &TaskId,
        operation_id: &OperationId,
        reason: CancellationReason,
    ) -> Result<OperationEnvelope<Completion>, TaskCompletionError> {
        if !self.tasks.contains_key(task_id.as_str()) {
            return Err(TaskCompletionError::UnknownTask);
        }
        if let Some(owner) = self.operation_tasks.get(operation_id) {
            if owner.as_str() != task_id.as_str() {
                return Err(TaskCompletionError::WrongTask);
            }
        }
        self.cancel_operation(operation_id, reason)
            .map(|terminal| terminal.completion)
    }

    /// Resolves one profile-owned operation without requiring its task identity.
    pub fn cancel_operation(
        &mut self,
        operation_id: &OperationId,
        reason: CancellationReason,
    ) -> Result<TaskTerminal, TaskCompletionError> {
        let completion = self
            .pending
            .cancel(operation_id, reason)
            .map_err(TaskCompletionError::Terminal)?;
        self.resolve_profile_terminal(completion, true)
            .ok_or(TaskCompletionError::UnknownTask)
    }

    /// Resolves operations whose monotonic deadlines arrived.
    pub fn expire_due(&mut self, now_millis: u64) -> Vec<TaskTerminal> {
        let terminals = self.pending.expire_due(now_millis);
        self.resolve_profile_terminals(terminals, true)
    }

    /// Earliest browser-owned deadline still awaiting a terminal answer.
    ///
    /// This is the one scheduling authority exported to the process that owns
    /// the clock. The sandboxed runtime still reads no clock and owns no
    /// timer; it only reports the exact wire value already in its table.
    pub fn next_operation_deadline_monotonic_ms(&self) -> Option<u64> {
        self.pending.next_deadline_millis()
    }

    /// Advances generation and resolves old operations once.
    pub fn disconnected(
        &mut self,
        next_generation: ServiceGeneration,
    ) -> Result<Vec<TaskTerminal>, DisconnectError> {
        let terminals = self
            .pending
            .disconnect(next_generation)
            .map_err(|active| DisconnectError::GenerationOutOfSequence { active })?;
        Ok(self.resolve_profile_terminals(terminals, true))
    }

    /// Clean profile shutdown is cancellation, not a crash event.
    pub fn prepare_for_shutdown(&mut self) -> Vec<TaskTerminal> {
        let terminals = self.pending.prepare_for_shutdown();
        self.resolve_profile_terminals(terminals, false)
    }

    /// One current deterministic task domain.
    pub fn task(&self, task_id: &TaskId) -> Option<&dyn TaskEnginePort> {
        self.tasks
            .get(task_id.as_str())
            .map(|session| session.task.as_ref())
    }

    /// Number of task aggregates resident in this profile.
    pub fn task_count(&self) -> usize {
        self.tasks.len()
    }

    /// Portable handle-only account protocol state.
    pub fn account(&self) -> &dyn AccountPort {
        self.components.account.as_ref()
    }

    /// Mutates account state on the ordered core sequence.
    pub fn account_mut(&mut self) -> &mut dyn AccountPort {
        self.components.account.as_mut()
    }

    /// Mutates the router's standing configuration on the ordered sequence.
    ///
    /// Routing is a pure function of what is installed, so a provider change
    /// that never reaches this port leaves the router answering from the state
    /// it was built with: the person saved a key, the key is genuinely on the
    /// disk, and the route is still refused as `CredentialMissing`. That is
    /// the defect decision 0049 was written about, and it is invisible — every
    /// component behaves exactly as written.
    pub fn models_mut(&mut self) -> &mut dyn ModelRouterPort {
        self.components.models.as_mut()
    }

    /// Installs browser-committed handle-only account state during bootstrap.
    pub fn restore_account_session(
        &mut self,
        session: Option<AccountSession>,
    ) -> Result<(), AccountRestoreError> {
        if !self.tasks.is_empty() || !self.pending_task_opens.is_empty() || !self.pending.is_empty()
        {
            return Err(AccountRestoreError::RuntimeAlreadyActive);
        }
        if self.components.account.pending_count() != 0
            || self.components.account.session().is_some()
        {
            return Err(AccountRestoreError::AccountAlreadyActive);
        }
        self.components.account.restore_session(session);
        Ok(())
    }

    /// Current utility-process incarnation.
    pub const fn service_generation(&self) -> ServiceGeneration {
        self.pending.active_generation()
    }

    /// Number of effects without a terminal completion.
    pub fn pending_len(&self) -> usize {
        self.pending.len()
    }

    /// Whether this generation has already received one terminal answer for an
    /// operation. The bridge uses this before state-shaped command decoding so
    /// an exact retry remains a duplicate after the task has moved on.
    pub fn operation_is_terminal(&self, operation_id: &OperationId) -> bool {
        self.pending.is_terminal(operation_id)
    }

    /// Whether a task transition is waiting for durability.
    pub fn commit_in_flight(&self, task_id: &TaskId) -> Option<bool> {
        self.tasks
            .get(task_id.as_str())
            .map(|session| session.pending_submit.is_some())
    }

    /// Whether this task instance must be discarded and replayed.
    pub fn recovery_required(&self, task_id: &TaskId) -> Option<bool> {
        self.tasks
            .get(task_id.as_str())
            .map(|session| session.recovery_required)
    }

    fn resolve_profile_terminals(
        &mut self,
        completions: Vec<OperationEnvelope<Completion>>,
        recover_commits: bool,
    ) -> Vec<TaskTerminal> {
        let mut terminals = Vec::with_capacity(completions.len());
        for completion in completions {
            if let Some(terminal) = self.resolve_profile_terminal(completion, recover_commits) {
                terminals.push(terminal);
            }
        }
        terminals
    }

    fn resolve_profile_terminal(
        &mut self,
        completion: OperationEnvelope<Completion>,
        recover_commits: bool,
    ) -> Option<TaskTerminal> {
        let owner = self.operation_tasks.remove(&completion.operation_id)?;
        if let Some(pending_open) = self.pending_task_opens.remove(&owner) {
            if pending_open.workspace_revision.is_some() {
                self.components
                    .workspaces
                    .reject_persist(completion.operation_id.as_str());
            }
            return Some(TaskTerminal {
                task_id: pending_open.task.task_id().clone(),
                completion,
            });
        }
        let session = self.tasks.get_mut(&owner)?;
        if session
            .pending_submit
            .as_ref()
            .is_some_and(|submit| submit.operation_id == completion.operation_id)
        {
            let pending_submit = session.pending_submit.take();
            if pending_submit
                .as_ref()
                .is_some_and(|submit| submit.workspace_revision.is_some())
            {
                self.components
                    .workspaces
                    .reject_persist(completion.operation_id.as_str());
            }
            if recover_commits {
                session.recovery_required = true;
            }
        }
        Some(TaskTerminal {
            task_id: session.task.task_id().clone(),
            completion,
        })
    }
}
