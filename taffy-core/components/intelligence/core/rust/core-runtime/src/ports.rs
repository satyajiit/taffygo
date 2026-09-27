// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Synchronous composition ports driven on the core's one ordered sequence.
//!
//! A port is not permission to perform ambient I/O. Policy and routing stay
//! local. Storage and audit ports only encode generated, bounded commit
//! payloads; the browser performs their I/O later through an emitted effect.
//!
//! Every port here has one shape: a trait, a `Box<dyn _>` blanket
//! implementation, and exactly one canonical `Production*` adapter in
//! [`crate::adapters`]. A subsystem the runtime owns is a port, so it can be
//! substituted in a test without a build feature.

mod account;
mod assets;
mod composition;
mod library;
mod memory;
mod observer;
mod status;
mod workspace;

pub use account::AccountPort;
pub use assets::{
    AssetDeliveryError, AssetDeliveryPort, AssetDeliveryPosture, AssetInstallationView,
};
pub use composition::{
    AuditPort, PolicyEvaluation, PolicyPort, StorageCommit, StorageDomainPort, TaskCreationAudit,
    TaskCreationCommit,
};
pub use library::{LibraryPort, LibraryRemoveRequest, LibrarySaveRequest, LibraryStoreError};
pub(crate) use loop_kernel::ports::apply_initial_consent;
pub use loop_kernel::ports::{
    AcceptedTaskConsentFacts, ActionEffectFacts, BuiltinSkillBindingFacts, InitialConsentAdmission,
    ModelRouterPort, ModelTurnFacts, OpenedTaskEngine, PendingActionFacts, PendingPermissionFacts,
    PlanProgressFacts, PortError, ReducerFactory, TaskDiscoveryAuthorityFacts, TaskEngineFactory,
    TaskEngineLoad, TaskEnginePort, TaskIdEntropy, TaskIdSourceFactory, TaskViewFacts,
    TaskViewFactsError, TASK_ID_ENTROPY_BYTES,
};
pub use memory::{
    MemoryDeleteRequest, MemoryPort, MemorySaveRequest, MemoryScopeInput, MemorySensitivityInput,
    MemoryStoreError, MemoryTaskSave, MemoryTaskUpdate, MemoryUserUpsert, MemoryWorkspaceInput,
};
pub use observer::{SettledFact, TurnObserverPort};
pub use status::{StatusContributionFacts, StatusContributorPort};
pub use workspace::{
    WorkspaceDeleteRequest, WorkspaceDeletionCounts, WorkspaceDeletionPreview,
    WorkspaceExportError, WorkspaceListEntry, WorkspacePageFact, WorkspacePersistRequest,
    WorkspacePort, WorkspaceStoreError,
};
