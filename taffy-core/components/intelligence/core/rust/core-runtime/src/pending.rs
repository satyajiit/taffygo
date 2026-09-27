// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Exactly-once terminal completion, cancellation, deadline, and disconnect recovery.
//!
//! # Two tables, one ceiling
//!
//! Exactly-once needs two facts, and they have opposite lifetimes. `pending`
//! holds work that has not answered yet and shrinks every time something does.
//! `terminal` holds the identities that already answered, and it is what
//! refuses a second registration of one — so it only ever grew, once per
//! operation, for as long as a profile stayed open.
//!
//! The identities are opaque strings the browser mints, so there is no ordinal
//! floor to stand in for the ones dropped. What there is, is the *generation*:
//! every path that reads `terminal` checks [`ServiceGeneration`] first, so an
//! identity recorded under a generation the profile has left can never reach
//! the check that would have refused it. [`PendingOperations::disconnect`]
//! drops those, which makes the generation the cheaper monotone proof and
//! costs one comparison instead of one string per operation the profile ever
//! ran.
//!
//! Within one generation there is no such floor, so the two tables share a
//! ceiling — [`MAX_TRACKED_OPERATIONS`], counted by
//! [`PendingOperations::tracked_operation_count`] — and reaching it refuses
//! registration with [`PendingError::Saturated`]. It fails closed on purpose:
//! evicting a terminal identity is the one response that would let an
//! operation run twice.

use std::collections::{BTreeMap, BTreeSet};

use crate::contract::{
    CancellationReason, Completion, EffectRequest, EffectSemantics, OperationEnvelope, OperationId,
    RuntimeError, ServiceGeneration,
};

/// Hard cap on operations retained by one profile service runtime.
pub const MAX_PENDING_OPERATIONS: usize = 256;

/// Hard cap on the identity bookkeeping exactly-once needs, in one generation.
///
/// Counts the operations awaiting an answer plus the identities that already
/// produced one, because both are what a profile can grow and only their sum
/// is the memory a generation holds. A ceiling rather than an eviction policy:
/// see this module's header for why dropping a terminal identity is the one
/// response that would break the property the table exists for.
pub const MAX_TRACKED_OPERATIONS: usize = 8_192;

/// Why an effect could not enter the pending table.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PendingError {
    /// The operation belongs to a replaced service process.
    StaleGeneration,
    /// The operation was planned against a different task revision.
    StaleRevision,
    /// Its deadline has already arrived.
    DeadlineExceeded,
    /// The operation identity is already pending.
    DuplicateOperation,
    /// The operation identity already produced a terminal answer.
    AlreadyCompleted,
    /// The bounded table is full.
    Saturated,
}

impl PendingError {
    /// A compiled-in diagnostic label.
    pub const fn label(self) -> &'static str {
        match self {
            Self::StaleGeneration => "stale_generation",
            Self::StaleRevision => "stale_revision",
            Self::DeadlineExceeded => "deadline_exceeded",
            Self::DuplicateOperation => "duplicate_operation",
            Self::AlreadyCompleted => "already_completed",
            Self::Saturated => "saturated",
        }
    }
}

/// Why a terminal answer was not accepted.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompletionError {
    /// The completion belongs to a replaced service process.
    StaleGeneration,
    /// No pending operation has this identity.
    UnknownOperation,
    /// This operation already produced its one terminal answer.
    DuplicateTerminal,
    /// The task moved beyond the revision this operation observed.
    StaleRevision,
    /// Metadata disagreed with the registered request.
    MetadataMismatch,
}

impl CompletionError {
    /// A compiled-in diagnostic label.
    pub const fn label(self) -> &'static str {
        match self {
            Self::StaleGeneration => "stale_generation",
            Self::UnknownOperation => "unknown_operation",
            Self::DuplicateTerminal => "duplicate_terminal",
            Self::StaleRevision => "stale_revision",
            Self::MetadataMismatch => "metadata_mismatch",
        }
    }
}

/// Pending work for one profile's current service generation.
#[derive(Clone, Debug)]
pub struct PendingOperations {
    active_generation: ServiceGeneration,
    pending: BTreeMap<OperationId, OperationEnvelope<EffectRequest>>,
    terminal: BTreeSet<OperationId>,
}

impl PendingOperations {
    /// Starts an empty table for one live utility-process incarnation.
    pub const fn new(active_generation: ServiceGeneration) -> Self {
        Self {
            active_generation,
            pending: BTreeMap::new(),
            terminal: BTreeSet::new(),
        }
    }

    /// The generation accepted by new requests and completions.
    pub const fn active_generation(&self) -> ServiceGeneration {
        self.active_generation
    }

    pub(crate) fn is_terminal(&self, operation_id: &OperationId) -> bool {
        self.terminal.contains(operation_id)
    }

    /// Registers work before the broker dispatches it.
    pub fn register(
        &mut self,
        envelope: OperationEnvelope<EffectRequest>,
        current_revision: u64,
        now_millis: u64,
    ) -> Result<(), PendingError> {
        self.validate_registration(&envelope, current_revision, now_millis)?;
        self.pending.insert(envelope.operation_id.clone(), envelope);
        Ok(())
    }

    /// Checks whether work can be registered without mutating the table.
    ///
    /// `CoreRuntime::begin_submit` uses this before applying a reducer command,
    /// so malformed metadata or queue pressure cannot create uncommitted state.
    pub fn validate_registration<T>(
        &self,
        envelope: &OperationEnvelope<T>,
        current_revision: u64,
        now_millis: u64,
    ) -> Result<(), PendingError> {
        if envelope.service_generation != self.active_generation {
            return Err(PendingError::StaleGeneration);
        }
        if envelope.task_revision != current_revision {
            return Err(PendingError::StaleRevision);
        }
        if envelope.deadline.is_expired_at(now_millis) {
            return Err(PendingError::DeadlineExceeded);
        }
        if self.terminal.contains(&envelope.operation_id) {
            return Err(PendingError::AlreadyCompleted);
        }
        if self.pending.contains_key(&envelope.operation_id) {
            return Err(PendingError::DuplicateOperation);
        }
        if self.pending.len() >= MAX_PENDING_OPERATIONS {
            return Err(PendingError::Saturated);
        }
        if self.tracked_operation_count() >= MAX_TRACKED_OPERATIONS {
            return Err(PendingError::Saturated);
        }
        Ok(())
    }

    /// Accepts one terminal answer and refuses every later one.
    pub fn complete(
        &mut self,
        completion: OperationEnvelope<Completion>,
        current_revision: u64,
        now_millis: u64,
    ) -> Result<OperationEnvelope<Completion>, CompletionError> {
        if completion.service_generation != self.active_generation {
            return Err(CompletionError::StaleGeneration);
        }
        if self.terminal.contains(&completion.operation_id) {
            return Err(CompletionError::DuplicateTerminal);
        }
        let Some(request) = self.pending.get(&completion.operation_id) else {
            return Err(CompletionError::UnknownOperation);
        };
        if request.task_revision != current_revision || completion.task_revision != current_revision
        {
            return Err(CompletionError::StaleRevision);
        }
        if completion.idempotency_key != request.idempotency_key
            || completion.deadline != request.deadline
            || completion.service_generation != request.service_generation
            || !request.body.accepts_completion(&completion.body)
        {
            return Err(CompletionError::MetadataMismatch);
        }
        let operation_id = completion.operation_id.clone();
        let Some(request) = self.pending.remove(&operation_id) else {
            return Err(CompletionError::UnknownOperation);
        };
        self.terminal.insert(operation_id);
        if request.deadline.is_expired_at(now_millis) {
            return Ok(request.with_body(Completion::Failed(RuntimeError::DeadlineExceeded)));
        }
        Ok(completion)
    }

    /// Cancels one operation locally before waiting for its worker to stop.
    pub fn cancel(
        &mut self,
        operation_id: &OperationId,
        reason: CancellationReason,
    ) -> Result<OperationEnvelope<Completion>, CompletionError> {
        if self.terminal.contains(operation_id) {
            return Err(CompletionError::DuplicateTerminal);
        }
        let Some(request) = self.pending.remove(operation_id) else {
            return Err(CompletionError::UnknownOperation);
        };
        self.terminal.insert(operation_id.clone());
        Ok(request.with_body(Completion::Cancelled(reason)))
    }

    /// Ends every operation whose browser-owned deadline has arrived.
    pub fn expire_due(&mut self, now_millis: u64) -> Vec<OperationEnvelope<Completion>> {
        let due: Vec<OperationId> = self
            .pending
            .iter()
            .filter(|(_, request)| request.deadline.is_expired_at(now_millis))
            .map(|(operation_id, _)| operation_id.clone())
            .collect();
        let mut completions = Vec::with_capacity(due.len());
        for operation_id in due {
            if let Some(request) = self.pending.remove(&operation_id) {
                self.terminal.insert(operation_id);
                completions
                    .push(request.with_body(Completion::Failed(RuntimeError::DeadlineExceeded)));
            }
        }
        completions
    }

    /// Advances to the next service incarnation and resolves all old work.
    ///
    /// Consequential work becomes `OUTCOME_UNKNOWN`; every other request ends
    /// as `CORE_UNAVAILABLE`. Nothing is silently retried here.
    pub fn disconnect(
        &mut self,
        next_generation: ServiceGeneration,
    ) -> Result<Vec<OperationEnvelope<Completion>>, ServiceGeneration> {
        if self.active_generation.next() != Some(next_generation) {
            return Err(self.active_generation);
        }
        let old = core::mem::take(&mut self.pending);
        let mut completions = Vec::with_capacity(old.len());
        for request in old.into_values() {
            let terminal = if request.body.semantics() == EffectSemantics::Consequential {
                Completion::OutcomeUnknown
            } else {
                Completion::Failed(RuntimeError::CoreUnavailable)
            };
            completions.push(request.with_body(terminal));
        }
        // Every identity in `terminal` belongs to the generation being left,
        // and `register` and `complete` both refuse a stale generation before
        // they read this set. Keeping the strings would protect nothing and
        // would make the table grow for the life of the profile rather than
        // for the life of one service incarnation.
        self.terminal.clear();
        self.active_generation = next_generation;
        Ok(completions)
    }

    /// Resolves a clean profile shutdown without counting it as a crash.
    pub fn prepare_for_shutdown(&mut self) -> Vec<OperationEnvelope<Completion>> {
        let old = core::mem::take(&mut self.pending);
        let mut completions = Vec::with_capacity(old.len());
        for (operation_id, request) in old {
            self.terminal.insert(operation_id);
            completions.push(
                request.with_body(Completion::Cancelled(CancellationReason::ProfileShutdown)),
            );
        }
        completions
    }

    /// Number of operations that have not emitted a terminal answer.
    pub fn len(&self) -> usize {
        self.pending.len()
    }

    /// Whether no operation is awaiting a terminal answer.
    pub fn is_empty(&self) -> bool {
        self.pending.is_empty()
    }

    /// Earliest browser-owned deadline still awaiting a terminal answer.
    ///
    /// The table is capped at [`MAX_PENDING_OPERATIONS`], so deriving this
    /// only when the table changes keeps the implementation smaller and safer
    /// than a second index that every terminal path would have to maintain.
    /// The browser uses the answer to arm one timer; `None` means no wake-up.
    pub fn next_deadline_millis(&self) -> Option<u64> {
        self.pending
            .values()
            .map(|request| request.deadline.as_millis())
            .min()
    }

    /// The identity bookkeeping this generation holds, as one number: the
    /// operations awaiting an answer plus the identities that already gave
    /// one.
    ///
    /// This is the quantity [`MAX_TRACKED_OPERATIONS`] bounds, and it is
    /// public so a test can assert the bound itself rather than the mechanism
    /// that keeps it.
    pub fn tracked_operation_count(&self) -> usize {
        self.pending.len().saturating_add(self.terminal.len())
    }
}

#[cfg(test)]
mod tests {
    use proptest::prelude::{any, proptest};
    use task_engine::ids::IdempotencyKey;

    use super::{CompletionError, PendingError, PendingOperations, MAX_TRACKED_OPERATIONS};
    use crate::contract::{
        BoundedPayload, CancellationReason, Completion, Deadline, EffectRequest, OperationEnvelope,
        OperationId, ServiceGeneration, ToolEffect,
    };

    fn request(
        id: &str,
        generation: ServiceGeneration,
        revision: u64,
        deadline: u64,
        effect: EffectRequest,
    ) -> OperationEnvelope<EffectRequest> {
        OperationEnvelope::new(
            OperationId::new(id).unwrap_or_else(|_| unreachable!()),
            generation,
            revision,
            Deadline::from_millis(deadline),
            IdempotencyKey::new(format!("key-{id}")),
            effect,
        )
        .unwrap_or_else(|_| unreachable!())
    }

    fn pure_read() -> EffectRequest {
        let job = core_service_types::ToolJobEffect {
            job_id: "job-read".to_owned(),
            runtime: core_service_types::ToolRuntimeKind::Python,
            tool_id: "python.read".to_owned(),
            tool_version: "v1".to_owned(),
            operation_kind: core_service_types::ToolOperation::RunBundledPythonModule,
            budget: core_service_types::ToolResourceBudget {
                max_input_bytes: 0,
                max_output_bytes: 0,
                max_memory_bytes: 1,
                max_cpu_ms: 1,
                max_temporary_bytes: 0,
                max_output_chunks: 0,
            },
            bundled_python: Some(core_service_types::BundledPythonArguments {
                entrypoint_id: "document.build".to_owned(),
                input: Vec::new(),
            }),
            local_model: None,
            media_probe: None,
            audio_extract: None,
            frame_sample: None,
            transcode: None,
            signed_wasm: None,
            local_embedding: None,
            task_id: "task-read".to_owned(),
        };
        EffectRequest::Tool(Box::new(
            ToolEffect::new(job).unwrap_or_else(|_| unreachable!()),
        ))
    }

    #[test]
    fn one_operation_emits_exactly_one_terminal_completion() {
        let generation = ServiceGeneration::INITIAL;
        let mut pending = PendingOperations::new(generation);
        let request = request("observe-1", generation, 4, 100, pure_read());
        assert!(pending.register(request.clone(), 4, 1).is_ok());
        let completion = request.with_body(Completion::Failed(
            crate::contract::RuntimeError::ToolFailed,
        ));
        assert!(pending.complete(completion.clone(), 4, 2).is_ok());
        assert_eq!(
            pending.complete(completion, 4, 3),
            Err(CompletionError::DuplicateTerminal)
        );
    }

    #[test]
    fn nearest_deadline_tracks_registration_completion_and_cancellation() {
        let generation = ServiceGeneration::INITIAL;
        let mut pending = PendingOperations::new(generation);
        let later = request("observe-later", generation, 4, 300, pure_read());
        let earlier = request("observe-earlier", generation, 4, 100, pure_read());

        assert_eq!(pending.next_deadline_millis(), None);
        assert!(pending.register(later.clone(), 4, 1).is_ok());
        assert_eq!(pending.next_deadline_millis(), Some(300));
        assert!(pending.register(earlier.clone(), 4, 1).is_ok());
        assert_eq!(pending.next_deadline_millis(), Some(100));

        assert!(pending
            .cancel(&earlier.operation_id, CancellationReason::User)
            .is_ok());
        assert_eq!(pending.next_deadline_millis(), Some(300));

        let completion = later.with_body(Completion::Failed(
            crate::contract::RuntimeError::ToolFailed,
        ));
        assert!(pending.complete(completion, 4, 2).is_ok());
        assert_eq!(pending.next_deadline_millis(), None);
    }

    #[test]
    fn disconnect_never_retries_consequential_work() {
        let generation = ServiceGeneration::INITIAL;
        let mut pending = PendingOperations::new(generation);
        let consequential = request(
            "model-1",
            generation,
            2,
            100,
            EffectRequest::Model(BoundedPayload::empty()),
        );
        let read = request("observe-1", generation, 2, 100, pure_read());
        assert!(pending.register(consequential, 2, 1).is_ok());
        assert!(pending.register(read, 2, 1).is_ok());
        let next = generation.next().unwrap_or_else(|| unreachable!());
        let terminals = pending.disconnect(next).unwrap_or_default();
        assert!(terminals
            .iter()
            .any(|terminal| terminal.body == Completion::OutcomeUnknown));
        assert!(terminals.iter().any(|terminal| {
            terminal.body == Completion::Failed(crate::contract::RuntimeError::CoreUnavailable)
        }));
        assert!(pending.is_empty());
    }

    #[test]
    fn a_completed_identity_is_never_registrable_again_while_its_generation_stands() {
        let generation = ServiceGeneration::INITIAL;
        let mut pending = PendingOperations::new(generation);
        let envelope = request("observe-1", generation, 4, 100, pure_read());
        assert!(pending.register(envelope.clone(), 4, 1).is_ok());
        let completion = envelope.with_body(Completion::Failed(
            crate::contract::RuntimeError::ToolFailed,
        ));
        assert!(pending.complete(completion, 4, 2).is_ok());
        assert_eq!(
            pending.register(envelope, 4, 3).err(),
            Some(PendingError::AlreadyCompleted)
        );
    }

    #[test]
    fn a_generation_change_drops_the_terminal_set_without_admitting_its_work() {
        // The retirement, and the proof that nothing was lost by it. The
        // identities are opaque strings, so there is no ordinal floor to sit
        // below; what there is, is the generation, and every path that reads
        // `terminal` checks it first. Dropping the strings therefore costs
        // nothing a caller could observe as a second registration.
        let generation = ServiceGeneration::INITIAL;
        let mut pending = PendingOperations::new(generation);
        let envelope = request("observe-1", generation, 4, 100, pure_read());
        assert!(pending.register(envelope.clone(), 4, 1).is_ok());
        let completion = envelope.with_body(Completion::Failed(
            crate::contract::RuntimeError::ToolFailed,
        ));
        assert!(pending.complete(completion, 4, 2).is_ok());
        assert_eq!(pending.tracked_operation_count(), 1);

        let Some(next) = generation.next() else {
            unreachable!("the initial generation has a successor")
        };
        assert!(pending.disconnect(next).is_ok());
        assert_eq!(pending.tracked_operation_count(), 0);

        // The identity the dropped set was standing in for is still refused,
        // by the cheaper check that was always running ahead of it.
        assert_eq!(
            pending.register(envelope, 4, 3).err(),
            Some(PendingError::StaleGeneration)
        );
    }

    #[test]
    fn the_tracked_identity_count_never_passes_its_ceiling() {
        let generation = ServiceGeneration::INITIAL;
        let mut pending = PendingOperations::new(generation);
        let mut refusal = None;
        // One more than the ceiling, so the loop reaches the refusal rather
        // than ending on a number this test would then be restating.
        for step in 0..=MAX_TRACKED_OPERATIONS {
            let envelope = request(
                &format!("operation-{step}"),
                generation,
                7,
                1_000_000,
                pure_read(),
            );
            let operation_id = envelope.operation_id.clone();
            match pending.register(envelope, 7, 1) {
                Ok(()) => {
                    assert!(pending
                        .cancel(&operation_id, CancellationReason::User)
                        .is_ok());
                }
                Err(error) => {
                    refusal = Some(error);
                    break;
                }
            }
            assert!(pending.tracked_operation_count() <= MAX_TRACKED_OPERATIONS);
        }
        // Fails closed and says which ceiling it hit, rather than quietly
        // forgetting an operation that already ran.
        assert_eq!(refusal, Some(PendingError::Saturated));
        assert!(pending.tracked_operation_count() <= MAX_TRACKED_OPERATIONS);
    }

    proptest! {
        #[test]
        fn cancellation_is_terminal_for_every_nonempty_numeric_identity(value in any::<u64>()) {
            let generation = ServiceGeneration::INITIAL;
            let mut pending = PendingOperations::new(generation);
            let id = format!("operation-{value}");
            let request = request(
                &id,
                generation,
                7,
                100,
                pure_read(),
            );
            let operation_id = request.operation_id.clone();
            assert!(pending.register(request, 7, 1).is_ok());
            let terminal = pending.cancel(&operation_id, CancellationReason::User);
            assert!(matches!(terminal, Ok(envelope) if matches!(envelope.body, Completion::Cancelled(CancellationReason::User))));
            assert_eq!(
                pending.cancel(&operation_id, CancellationReason::User),
                Err(CompletionError::DuplicateTerminal)
            );
        }
    }
}
