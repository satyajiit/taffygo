// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Profile runtime state and closed public outcomes.

use std::collections::BTreeMap;
use std::rc::Rc;

use bip_types::identity::TaskId;
use task_engine::{Accepted, Recovery, Refusal};

use crate::account::Sha256Port;
use crate::contract::{
    Completion, EffectRequest, EnvelopeError, OperationEnvelope, OperationId, ServiceGeneration,
};
use crate::pending::{CompletionError, PendingError, PendingOperations};
use crate::ports::{
    AccountPort, AssetDeliveryPort, AuditPort, LibraryPort, MemoryPort, ModelRouterPort,
    PolicyPort, PortError, StorageDomainPort, TaskEngineFactory, TaskEnginePort, TurnObserverPort,
    WorkspacePort, WorkspaceStoreError,
};

/// Hard cap on task aggregates resident in one profile utility service.
pub const MAX_TASK_SESSIONS_PER_PROFILE: usize = 64;

/// Why a command did not reach asynchronous durability.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SubmitError {
    /// No open task has this identity.
    UnknownTask,
    /// The in-memory transition must be discarded and replayed.
    RecoveryRequired { in_memory_revision: u64 },
    /// A transition commit is already in flight for this task.
    CommitInFlight,
    /// Boundary metadata was invalid.
    Envelope(EnvelopeError),
    /// Pending-operation state refused registration.
    Pending(PendingError),
    /// Workspace derivation or staging failed before task durability.
    Workspace(WorkspaceCommitError),
    /// Workspace staging failed after the reducer changed in memory. Replay is required.
    WorkspaceAfterApply {
        error: WorkspaceCommitError,
        in_memory_revision: u64,
    },
    /// The deterministic task reducer refused the command.
    Task(Refusal),
    /// A task port violated the reducer result contract after mutation.
    InvalidTaskResult { in_memory_revision: u64 },
    /// Pure audit encoding failed after reducer mutation.
    AuditEncoding {
        error: PortError,
        in_memory_revision: u64,
    },
    /// Pure storage encoding failed after reducer mutation.
    StorageEncoding {
        error: PortError,
        in_memory_revision: u64,
    },
    /// Pending state changed after reducer mutation.
    PendingAfterApply {
        error: PendingError,
        in_memory_revision: u64,
    },
}

/// First phase of command submission.
#[derive(Clone, Debug, PartialEq)]
pub enum BeginSubmit {
    /// The idempotency key was already durably applied.
    Duplicate(Accepted),
    /// Browser storage must commit before task effects are released.
    AwaitingCommit(Box<OperationEnvelope<EffectRequest>>),
}

/// Result of a browser-owned commit completion.
#[derive(Clone, Debug, PartialEq)]
pub enum CommitOutcome {
    /// The transition and effect intents are durable.
    Committed(Accepted),
    /// Durability failed or is ambiguous; discard and replay.
    RecoveryRequired {
        in_memory_revision: u64,
        terminal: OperationEnvelope<Completion>,
    },
}

/// Why a commit completion did not match a pending transition.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CommitCompletionError {
    UnknownTask,
    NoCommitPending,
    WrongOperation,
    Terminal(CompletionError),
    Workspace(WorkspaceStoreError),
}

/// Why effect staging failed before broker dispatch.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StageError {
    UnknownTask,
    RecoveryRequired,
    CommitInFlight,
    Envelope(EnvelopeError),
    Pending(PendingError),
}

/// Why a disconnect transition was not accepted.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DisconnectError {
    GenerationOutOfSequence { active: ServiceGeneration },
}

/// Why durable account state could not be installed during bootstrap.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AccountRestoreError {
    /// Bootstrap ended: tasks or asynchronous operations are already active.
    RuntimeAlreadyActive,
    /// An account flow already changed the empty protocol state.
    AccountAlreadyActive,
    /// Private profiles never restore a durable account session.
    PrivateProfile,
}

/// Why a task aggregate could not enter the profile runtime.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OpenTaskError {
    AlreadyOpen,
    Saturated,
    Factory(PortError),
    WrongLoadKind,
    IdentityMismatch,
    InvalidFactoryResult,
    Envelope(EnvelopeError),
    Pending(PendingError),
    AuditEncoding(PortError),
    StorageEncoding(PortError),
    Workspace(WorkspaceCommitError),
}

impl OpenTaskError {
    /// A short, compiled-in name for this refusal.
    ///
    /// One admission status crosses the wire, so every one of these reaches a
    /// person as the same sentence. The label names the branch and carries no
    /// value from the command, so it is safe to print beside the status the
    /// browser already logs. The three wrapped port errors keep their own
    /// name rather than the wrapper's, because "the audit port refused" and
    /// "the storage port refused" are different faults.
    pub const fn label(self) -> &'static str {
        match self {
            Self::AlreadyOpen => "open_already_open",
            Self::Saturated => "open_task_table_saturated",
            Self::Factory(_) => "open_factory_port",
            Self::WrongLoadKind => "open_wrong_load_kind",
            Self::IdentityMismatch => "open_identity_mismatch",
            Self::InvalidFactoryResult => "open_factory_result",
            Self::Envelope(_) => "open_envelope",
            Self::Pending(_) => "open_pending",
            Self::AuditEncoding(_) => "open_audit_encoding",
            Self::StorageEncoding(_) => "open_storage_encoding",
            Self::Workspace(_) => "open_workspace_commit",
        }
    }
}

/// Fresh task waiting for its creation commit.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BeginOpenTask {
    pub task_id: TaskId,
    pub commit: OperationEnvelope<EffectRequest>,
}

/// Facts returned after a task is opened.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OpenTaskOutcome {
    pub task_id: TaskId,
    pub revision: u64,
    pub recovery: Option<Recovery>,
    /// Effects released only after the creation transaction committed.
    pub effects: Vec<task_engine::Effect>,
}

/// Terminal outcome of a fresh task creation commit.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum OpenTaskCommitOutcome {
    Opened(OpenTaskOutcome),
    NotOpened(OperationEnvelope<Completion>),
}

/// Why a creation completion did not match a pending fresh task.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OpenTaskCompletionError {
    NoOpenPending,
    WrongOperation,
    Terminal(CompletionError),
    Workspace(WorkspaceStoreError),
}

/// Why an automatic workspace could not be derived or staged.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WorkspaceCommitError {
    DigestUnavailable,
    InvalidTaskFacts,
    InvalidOrigin,
    InvalidIdentifier,
    Store(WorkspaceStoreError),
}

/// Why a task-scoped terminal operation was refused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskCompletionError {
    UnknownTask,
    WrongTask,
    Terminal(CompletionError),
}

/// One task identity paired with a terminal completion.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskTerminal {
    pub task_id: TaskId,
    pub completion: OperationEnvelope<Completion>,
}

#[derive(Debug)]
pub(super) struct PendingSubmit {
    pub(super) operation_id: OperationId,
    pub(super) accepted: Accepted,
    pub(super) workspace_revision: Option<u64>,
    pub(super) task_library_search: Option<super::PendingTaskLibrarySearchSettlement>,
    pub(super) task_memory_search: Option<super::PendingTaskMemorySearchSettlement>,
    pub(super) task_tab: Option<super::PendingTaskTabSettlement>,
    pub(super) task_store: Option<super::PendingTaskStoreSettlement>,
    pub(super) task_download: Option<super::PendingTaskDownloadSettlement>,
    pub(super) media_probe: Option<super::PendingMediaProbeSettlement>,
}

/// Correlated command and clock facts shared by typed native action results.
/// Grouping them prevents each result family from growing a second wide
/// submission API while retaining the exact values used by the durable commit.
#[derive(Clone, Debug, PartialEq)]
pub struct TaskResultSubmission {
    pub command: task_engine::CommandEnvelope,
    pub operation_id: crate::OperationId,
    pub deadline: crate::Deadline,
    pub now_monotonic_millis: u64,
    pub now_utc_millis: u64,
}

pub(super) struct PendingOpenTask {
    pub(super) operation_id: OperationId,
    pub(super) task: Box<dyn TaskEnginePort>,
    pub(super) id_entropy: crate::ports::TaskIdEntropy,
    pub(super) workspace_revision: Option<u64>,
    pub(super) initial_effects: Vec<task_engine::Effect>,
}

impl core::fmt::Debug for PendingOpenTask {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("PendingOpenTask")
            .field("operation_id", &self.operation_id)
            .field("task_id", self.task.task_id())
            .field("revision", &self.task.revision())
            .field("id_entropy", &self.id_entropy)
            .field("workspace_revision", &self.workspace_revision)
            .field("initial_effects", &self.initial_effects)
            .finish()
    }
}

pub(super) struct TaskSession {
    pub(super) task: Box<dyn TaskEnginePort>,
    pub(super) id_entropy: crate::ports::TaskIdEntropy,
    pub(super) pending_submit: Option<PendingSubmit>,
    pub(super) recovery_required: bool,
    /// Whether this task came back from the journal already ended.
    ///
    /// The difference between a task that finished while the person was
    /// watching and one that finished before they last closed the browser, and
    /// the only place that difference is knowable: both are `Completed` to
    /// every other question anybody can ask. A task that ended in an earlier
    /// browser run is not what a bar should be saying anything about — its
    /// results live in the workspace list, which is restored separately and
    /// keeps them — so it is neither projected nor kept in this table.
    ///
    /// False for every task opened in this generation, including one restored
    /// mid-flight that has since ended: that one *is* this run's news.
    pub(super) terminal_on_restore: bool,
}

impl core::fmt::Debug for TaskSession {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("TaskSession")
            .field("task_id", self.task.task_id())
            .field("revision", &self.task.revision())
            .field("id_entropy", &self.id_entropy)
            .field("pending_submit", &self.pending_submit)
            .field("recovery_required", &self.recovery_required)
            .field("terminal_on_restore", &self.terminal_on_restore)
            .finish()
    }
}

/// One ordered, profile-scoped runtime with independent bounded tasks.
///
/// The runtime is deliberately not generic. Every port it drives is installed
/// as `Box<dyn _>` by the one production composition root, so a type parameter
/// per subsystem bought no devirtualization in the shipping build and made
/// every new subsystem a signature change in five files. Subsystems live in
/// [`ServiceRuntimeComponents`], where adding one is an added field.
pub struct CoreRuntime {
    pub(super) components: ServiceRuntimeComponents,
    pub(super) tasks: BTreeMap<String, TaskSession>,
    /// Everything transient the loop holds per task (decision 0072). Beside
    /// `tasks`, never inside a session: a restore builds sessions and no
    /// loop state, which is the proof nothing transient survives.
    pub(super) loop_states: BTreeMap<String, loop_kernel::state::LoopState>,
    pub(super) pending_task_opens: BTreeMap<String, PendingOpenTask>,
    pub(super) pending: PendingOperations,
    pub(super) operation_tasks: BTreeMap<OperationId, String>,
}

impl core::fmt::Debug for CoreRuntime {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("CoreRuntime")
            .field("task_count", &self.tasks.len())
            .field("pending_task_open_count", &self.pending_task_opens.len())
            .field("pending", &self.pending)
            .field("operation_owner_count", &self.operation_tasks.len())
            .finish_non_exhaustive()
    }
}

/// The service-owned runtime shape, retained as the name service glue uses.
pub type BoxedCoreRuntime = CoreRuntime;

/// Required production components. There are no default or no-op ports.
///
/// One field per subsystem the ordered runtime owns. A new subsystem is a new
/// field and a new port; it is never a new type parameter.
///
/// `observers` is the one deliberately plural field: observation cannot alter
/// control flow (decision 0073), so any number of observers — including none —
/// composes without a decision about precedence.
pub struct ServiceRuntimeComponents {
    pub digest: Rc<dyn Sha256Port>,
    pub task_factory: Box<dyn TaskEngineFactory>,
    pub policy: Box<dyn PolicyPort>,
    pub audit: Box<dyn AuditPort>,
    pub models: Box<dyn ModelRouterPort>,
    pub storage: Box<dyn StorageDomainPort>,
    pub account: Box<dyn AccountPort>,
    pub workspaces: Box<dyn WorkspacePort>,
    pub library: Box<dyn LibraryPort>,
    pub memory: Box<dyn MemoryPort>,
    pub assets: Box<dyn AssetDeliveryPort>,
    pub observers: Vec<Box<dyn TurnObserverPort>>,
}

impl core::fmt::Debug for ServiceRuntimeComponents {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter
            .debug_struct("ServiceRuntimeComponents")
            .finish_non_exhaustive()
    }
}

/// Creates the canonical runtime owned by one profile utility service.
pub const fn create_service_runtime(
    generation: ServiceGeneration,
    components: ServiceRuntimeComponents,
) -> CoreRuntime {
    CoreRuntime::new(generation, components)
}

impl CoreRuntime {
    /// Composes pure domains for one profile utility-process incarnation.
    pub const fn new(generation: ServiceGeneration, components: ServiceRuntimeComponents) -> Self {
        Self {
            components,
            tasks: BTreeMap::new(),
            loop_states: BTreeMap::new(),
            pending_task_opens: BTreeMap::new(),
            pending: PendingOperations::new(generation),
            operation_tasks: BTreeMap::new(),
        }
    }
}
