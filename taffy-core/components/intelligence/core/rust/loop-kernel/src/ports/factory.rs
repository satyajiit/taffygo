// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Opening, replaying and consenting one task reducer.

use bip_types::identity::TaskId;
use task_engine::{
    BudgetDefaults, Clock, CommandEnvelope, IdSource, IdempotencyKey, Recovery, Reducer,
    ScopePreview, TaskJournal, TaskSeed, TaskState, TraceId,
};

use super::{PortError, TaskEnginePort, TaskIdEntropy};

/// Exact visible initial-consent commands committed with task creation.
#[derive(Clone, Debug)]
pub struct InitialConsentAdmission {
    pub preview: ScopePreview,
    pub receipt: bip_types::identity::ApprovalReceiptReference,
    pub start_key: IdempotencyKey,
    pub start_trace: TraceId,
    pub consent_key: IdempotencyKey,
    pub consent_trace: TraceId,
}

/// Canonical source for opening one task aggregate inside a profile runtime.
///
/// Fresh and replayed tasks use the same [`TaskSeed`]. Replay additionally
/// carries the browser-owned committed journal. The request deliberately has
/// no opaque byte variant, so service glue cannot invent a second task format.
pub enum TaskEngineLoad {
    /// Construct a new aggregate and its deterministic creation record.
    Fresh {
        /// Immutable creation inputs.
        seed: Box<TaskSeed>,
        /// Idempotency identity of the creation record.
        creation_key: IdempotencyKey,
        /// Trace identity of the creation record.
        trace_id: TraceId,
        /// Browser entropy committed with the creation record for replay.
        id_entropy: TaskIdEntropy,
        /// Exact preview and browser receipt committed in the same batch.
        initial_consent: Box<InitialConsentAdmission>,
    },
    /// Rebuild an aggregate from a browser-owned committed journal.
    Replay {
        /// The same immutable inputs used for original creation.
        seed: Box<TaskSeed>,
        /// Validated journal records read through the browser storage broker.
        journal: TaskJournal,
        /// The exact entropy committed with original creation.
        id_entropy: TaskIdEntropy,
    },
}

impl TaskEngineLoad {
    /// Builds a fresh-task request.
    pub fn fresh(
        seed: TaskSeed,
        creation_key: IdempotencyKey,
        trace_id: TraceId,
        id_entropy: TaskIdEntropy,
        initial_consent: InitialConsentAdmission,
    ) -> Self {
        Self::Fresh {
            seed: Box::new(seed),
            creation_key,
            trace_id,
            id_entropy,
            initial_consent: Box::new(initial_consent),
        }
    }

    /// Builds a replay request from a committed journal.
    pub fn replay(seed: TaskSeed, journal: TaskJournal, id_entropy: TaskIdEntropy) -> Self {
        Self::Replay {
            seed: Box::new(seed),
            journal,
            id_entropy,
        }
    }

    /// The aggregate identity that must match the factory result.
    pub const fn task_id(&self) -> &TaskId {
        match self {
            Self::Fresh { seed, .. } | Self::Replay { seed, .. } => &seed.task_id,
        }
    }

    /// Replay-stable identifier entropy committed with the task seed.
    pub const fn id_entropy(&self) -> &TaskIdEntropy {
        match self {
            Self::Fresh { id_entropy, .. } | Self::Replay { id_entropy, .. } => id_entropy,
        }
    }

    /// Immutable selected procedure version carried by this task seed.
    pub fn skill_version_id(&self) -> Option<&str> {
        match self {
            Self::Fresh { seed, .. } | Self::Replay { seed, .. } => seed
                .snapshot
                .skill_version_id
                .as_ref()
                .map(|value| value.0.as_str()),
        }
    }

    /// Immutable compiled built-in binding carried by this task seed.
    pub const fn builtin_skill_reference(&self) -> Option<task_engine::BuiltinSkillReference> {
        match self {
            Self::Fresh { seed, .. } | Self::Replay { seed, .. } => seed.snapshot.builtin_skill,
        }
    }
}

impl core::fmt::Debug for TaskEngineLoad {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::Fresh { seed, .. } => formatter
                .debug_struct("Fresh")
                .field("task_id", &seed.task_id)
                .finish_non_exhaustive(),
            Self::Replay { seed, journal, .. } => formatter
                .debug_struct("Replay")
                .field("task_id", &seed.task_id)
                .field("journal_entries", &journal.entries().len())
                .finish_non_exhaustive(),
        }
    }
}

/// One factory result plus recovery facts produced during replay.
pub struct OpenedTaskEngine {
    task: Box<dyn TaskEnginePort>,
    pub(in crate::ports) recovery: Option<Recovery>,
    initial_effects: Vec<task_engine::Effect>,
}

impl OpenedTaskEngine {
    /// Wraps a custom task implementation selected by service composition.
    pub fn new(
        task: Box<dyn TaskEnginePort>,
        recovery: Option<Recovery>,
        initial_effects: Vec<task_engine::Effect>,
    ) -> Self {
        Self {
            task,
            recovery,
            initial_effects,
        }
    }

    /// Splits the opened engine for the runtime that owns both halves.
    #[must_use]
    pub fn into_parts(
        self,
    ) -> (
        Box<dyn TaskEnginePort>,
        Option<Recovery>,
        Vec<task_engine::Effect>,
    ) {
        (self.task, self.recovery, self.initial_effects)
    }
}

impl core::fmt::Debug for OpenedTaskEngine {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("OpenedTaskEngine")
            .field("task_id", self.task.task_id())
            .field("revision", &self.task.revision())
            .field("recovery", &self.recovery)
            .field("initial_effects", &self.initial_effects)
            .finish()
    }
}

pub fn apply_initial_consent<C, I>(
    reducer: &mut Reducer<C, I>,
    admission: InitialConsentAdmission,
) -> Result<Vec<task_engine::Effect>, PortError>
where
    C: Clock,
    I: IdSource,
{
    let start_revision = reducer.task().revision();
    let started = reducer
        .apply(CommandEnvelope::new(
            admission.start_key,
            start_revision,
            admission.start_trace,
            task_engine::Command::StartTask(admission.preview),
        ))
        .map_err(|_| PortError::Rejected)?;
    if started.duplicate || !started.effects.is_empty() {
        return Err(PortError::Rejected);
    }
    let consented = reducer
        .apply(CommandEnvelope::new(
            admission.consent_key,
            reducer.task().revision(),
            admission.consent_trace,
            task_engine::Command::AcceptInitialConsent(admission.receipt),
        ))
        .map_err(|_| PortError::Rejected)?;
    if consented.duplicate || reducer.task().state() != TaskState::Queued {
        return Err(PortError::Rejected);
    }
    Ok(consented.effects)
}

/// Factory for fresh and replayed task reducers within one profile.
pub trait TaskEngineFactory {
    /// Opens exactly one aggregate from a closed, typed load request.
    fn open(&mut self, load: TaskEngineLoad) -> Result<OpenedTaskEngine, PortError>;
}

impl<T> TaskEngineFactory for Box<T>
where
    T: TaskEngineFactory + ?Sized,
{
    fn open(&mut self, load: TaskEngineLoad) -> Result<OpenedTaskEngine, PortError> {
        self.as_mut().open(load)
    }
}

/// Creates a deterministic, profile-unique identifier stream for one task.
///
/// A replay must reconstruct the same stream for the same task. Streams for
/// different task identities must remain disjoint because policy state is
/// shared at profile scope.
pub trait TaskIdSourceFactory {
    /// Creates or reconstructs one task's identifier stream.
    fn for_task(&mut self, task_id: &TaskId) -> Result<Box<dyn IdSource>, PortError>;
}

impl<F> TaskIdSourceFactory for F
where
    F: FnMut(&TaskId) -> Result<Box<dyn IdSource>, PortError>,
{
    fn for_task(&mut self, task_id: &TaskId) -> Result<Box<dyn IdSource>, PortError> {
        self(task_id)
    }
}

/// Canonical reducer factory with injected deterministic time and identifiers.
///
/// Chromium supplies adapters for [`Clock`] and [`IdSource`]. Opening and
/// replay stay synchronous and local; no implementation may perform I/O here.
#[derive(Clone, Debug)]
pub struct ReducerFactory<C, F> {
    defaults: BudgetDefaults,
    clock: C,
    id_sources: F,
}

impl<C, F> ReducerFactory<C, F> {
    /// Freezes the profile's budget defaults and deterministic sources.
    pub const fn new(defaults: BudgetDefaults, clock: C, id_sources: F) -> Self {
        Self {
            defaults,
            clock,
            id_sources,
        }
    }
}

impl<C, F> TaskEngineFactory for ReducerFactory<C, F>
where
    C: Clock + Clone + 'static,
    F: TaskIdSourceFactory,
{
    fn open(&mut self, load: TaskEngineLoad) -> Result<OpenedTaskEngine, PortError> {
        match load {
            TaskEngineLoad::Fresh {
                seed,
                creation_key,
                trace_id,
                id_entropy: _,
                initial_consent,
            } => {
                let ids = self.id_sources.for_task(&seed.task_id)?;
                let mut reducer = Reducer::create(
                    *seed,
                    self.defaults.clone(),
                    self.clock.clone(),
                    ids,
                    creation_key,
                    trace_id,
                );
                let initial_effects = apply_initial_consent(&mut reducer, *initial_consent)?;
                Ok(OpenedTaskEngine::new(
                    Box::new(reducer),
                    None,
                    initial_effects,
                ))
            }
            TaskEngineLoad::Replay {
                seed,
                journal,
                id_entropy: _,
            } => {
                let ids = self.id_sources.for_task(&seed.task_id)?;
                let (task, recovery) = Reducer::replay(
                    *seed,
                    self.defaults.clone(),
                    self.clock.clone(),
                    ids,
                    &journal,
                )
                .map_err(|_| PortError::Rejected)?;
                Ok(OpenedTaskEngine::new(
                    Box::new(task),
                    Some(recovery),
                    Vec::new(),
                ))
            }
        }
    }
}
