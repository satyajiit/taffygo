// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The workspace-plane composition port.
//!
//! A workspace revision becomes visible only after the browser-owned single
//! writer commits it. Implementations therefore encode bounded snapshots and
//! hold pending revisions; they never write, read, or index anything
//! themselves, exactly as [`StorageDomainPort`](super::StorageDomainPort) only
//! encodes.

use core_api_types::{WorkspaceExportFormat, WorkspaceExportView, WorkspaceViewState};
use taffy_storage::workspace::{
    MutationError, WorkspaceMediaProvenance, WorkspacePageFactScope, WorkspacePhase,
    WorkspaceSnapshot,
};

use crate::account::Sha256Port;

mod boxed;

/// One pending workspace revision awaiting a browser-owned physical commit.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspacePersistRequest {
    /// Correlation identity the completion must repeat exactly.
    pub operation_id: String,
    /// Aggregate identity being mutated.
    pub workspace_id: String,
    /// Revision the mutation was computed against.
    pub expected_revision: u64,
    /// Revision the mutation produces once durable.
    pub resulting_revision: u64,
    /// Complete encoded snapshot for the single writer.
    pub snapshot: Vec<u8>,
}

/// One already-redacted page fact to install with its exact cited source.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspacePageFact {
    /// Replay-stable opaque fact identity.
    pub fact_id: String,
    /// Stable plain-language field name used by every export shape.
    pub field: String,
    /// Bounded bytes that already crossed the page sensitivity gate.
    pub value: String,
    /// Present only for coordinate-bearing image, video, or PDF evidence.
    pub media_provenance: Option<WorkspaceMediaProvenance>,
}

impl WorkspacePageFact {
    pub const fn scope(&self) -> WorkspacePageFactScope {
        match &self.media_provenance {
            Some(provenance) => WorkspacePageFactScope::Media(provenance.media_kind),
            None => WorkspacePageFactScope::Dom,
        }
    }
}

/// One content-free row for the persistent workspace picker.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceListEntry {
    pub workspace_id: String,
    pub revision: u64,
    pub display_name: String,
    pub phase: WorkspacePhase,
    pub last_updated_epoch_ms: u64,
}

/// Exact local records a confirmed workspace deletion will remove.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct WorkspaceDeletionCounts {
    pub sources: u32,
    pub facts: u32,
    pub artifact_metadata: u32,
    pub derived_indexes: u32,
}

/// Content-free two-step deletion challenge bound to one current revision.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceDeletionPreview {
    pub workspace_id: String,
    pub expected_revision: u64,
    pub counts: WorkspaceDeletionCounts,
    pub confirmation_token: String,
}

/// One confirmed deletion awaiting the browser-owned atomic transaction.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct WorkspaceDeleteRequest {
    pub operation_id: String,
    pub workspace_id: String,
    pub expected_revision: u64,
    pub resulting_revision: u64,
    pub counts: WorkspaceDeletionCounts,
    pub confirmation_token: String,
}

/// Closed deterministic workspace-plane failures.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WorkspaceStoreError {
    InvalidSnapshot,
    DuplicateWorkspace,
    TooManyWorkspaces,
    TooManyWorkspaceBytes,
    RestoreBindingMismatch,
    UnknownWorkspace,
    InvalidIdentifier,
    OperationAlreadyPending,
    TooManyPendingMutations,
    Mutation(MutationError),
    WrongCompletion,
    ProjectionOverflow,
    StaleRevision,
    InvalidConfirmation,
    NotDiscardable,
    DigestUnavailable,
    RevisionOverflow,
}

/// Closed deterministic workspace-export failures.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum WorkspaceExportError {
    Store(WorkspaceStoreError),
    StaleRevision,
    InvalidEvidence,
    NoExportableFacts,
    RenderRefused,
}

/// Ordered workspace state whose durability the browser owns.
///
/// Implementations must not block, perform I/O, enter Mojo, or read a clock;
/// `updated_at_epoch_ms` is a browser-minted fact.
pub trait WorkspacePort {
    /// Restores the complete browser-owned snapshot set atomically.
    fn restore_encoded(&mut self, snapshots: &[Vec<u8>]) -> Result<(), WorkspaceStoreError>;

    /// Restores browser-indexed snapshots and refuses mismatched metadata.
    fn restore_bound_records(
        &mut self,
        records: &[(String, u64, Vec<u8>)],
    ) -> Result<(), WorkspaceStoreError>;

    /// Lists every durable workspace without exposing fact or source content.
    fn list_workspaces(&self) -> Vec<WorkspaceListEntry>;

    /// Borrows the current durable snapshot, including temporary task work.
    /// Pending writes remain invisible. The caller binds the identity to its
    /// own task; this is separate from the person's saved-work picker.
    fn retained_snapshot(&self, workspace_id: &str) -> Option<&WorkspaceSnapshot>;

    /// Reopens one exact current snapshot after list or process restart.
    fn reopen_workspace(
        &self,
        workspace_id: &str,
        expected_revision: u64,
    ) -> Result<WorkspaceSnapshot, WorkspaceStoreError>;

    /// Stages a validated revision-one workspace without publishing it.
    fn begin_creation(
        &mut self,
        operation_id: String,
        snapshot: WorkspaceSnapshot,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError>;

    /// Stages one exact-source page-fact replacement without publishing it.
    ///
    /// `None` is the deliberate no-write result for an empty page or an
    /// excluded source.
    fn begin_page_ingestion(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        source_id: &str,
        facts: Vec<WorkspacePageFact>,
        updated_at_epoch_ms: u64,
    ) -> Result<Option<WorkspacePersistRequest>, WorkspaceStoreError>;

    /// Checks that no other revision of the task's workspace is in flight.
    ///
    /// Called before the task reducer mutates so an independent workspace edit
    /// cannot turn an otherwise valid task transition into an after-apply
    /// recovery case.
    fn preflight_task_update(&self, workspace_id: &str) -> Result<(), WorkspaceStoreError>;

    /// Stages the post-transition task phase and optional page facts together.
    /// New source metadata must come from the current accepted source of the
    /// same verified capture, including an explicitly empty fact list.
    fn begin_task_update(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        phase: WorkspacePhase,
        page_facts: Option<(&str, Vec<WorkspacePageFact>)>,
        observed_source: Option<taffy_storage::workspace::WorkspaceSource>,
        updated_at_epoch_ms: u64,
    ) -> Result<Option<WorkspacePersistRequest>, WorkspaceStoreError>;

    /// Explicitly promotes a finished temporary task workspace to saved.
    fn begin_save(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError>;

    /// Begins one correction without publishing it before physical commit.
    fn begin_correction(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        fact_id: &str,
        value: String,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError>;

    /// Begins one source exclusion without publishing it before commit.
    fn begin_exclusion(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        source_id: &str,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError>;

    /// Stages a display-name change without rewriting the immutable goal.
    fn begin_rename(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        display_name: String,
        updated_at_epoch_ms: u64,
    ) -> Result<WorkspacePersistRequest, WorkspaceStoreError>;

    /// Creates a content-free confirmation challenge for the exact current state.
    fn preview_deletion(
        &self,
        workspace_id: &str,
        digest: &dyn Sha256Port,
    ) -> Result<WorkspaceDeletionPreview, WorkspaceStoreError>;

    /// Stages deletion only if the challenge still names the exact current state.
    ///
    /// `now_utc_ms` is the browser's clock, carried because a staged deletion
    /// is latched and the core has no timer of its own: it is what lets one
    /// that was never answered be forgotten instead of refusing every later
    /// change to that workspace for ever.
    fn begin_deletion(
        &mut self,
        operation_id: String,
        preview: &WorkspaceDeletionPreview,
        digest: &dyn Sha256Port,
        now_utc_ms: u64,
    ) -> Result<WorkspaceDeleteRequest, WorkspaceStoreError>;

    /// Stages explicit cleanup of one exact terminal unsaved task workspace.
    fn begin_discard(
        &mut self,
        operation_id: String,
        workspace_id: &str,
        expected_revision: u64,
        digest: &dyn Sha256Port,
        now_utc_ms: u64,
    ) -> Result<WorkspaceDeleteRequest, WorkspaceStoreError>;

    /// Makes one pending revision visible after an exact browser commit.
    fn complete_persist(
        &mut self,
        operation_id: &str,
        committed_revision: u64,
    ) -> Result<(), WorkspaceStoreError>;

    /// Drops one pending revision after a terminal refusal.
    fn reject_persist(&mut self, operation_id: &str) -> bool;

    /// Publishes absence and drops local derived metadata after exact commit.
    fn complete_deletion(
        &mut self,
        operation_id: &str,
        committed_revision: u64,
    ) -> Result<(), WorkspaceStoreError>;

    /// Rolls back one staged deletion after refusal or ambiguous completion.
    fn reject_deletion(&mut self, operation_id: &str) -> bool;

    /// Renders one immutable revision through the artifact renderer.
    fn request_export(
        &mut self,
        request_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<WorkspaceExportView, WorkspaceExportError>;

    /// Renders one current revision without publishing it for export.
    fn prepare_artifact(
        &mut self,
        artifact_id: &str,
        workspace_id: &str,
        format: WorkspaceExportFormat,
    ) -> Result<u64, WorkspaceExportError>;

    /// Recreates one exact prepared revision after service replay.
    fn ensure_artifact(
        &mut self,
        artifact_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<(), WorkspaceExportError>;

    /// Publishes one accepted exact artifact to the Android export adapter.
    fn publish_artifact(
        &mut self,
        request_id: &str,
        artifact_id: &str,
        workspace_id: &str,
        expected_revision: u64,
        format: WorkspaceExportFormat,
    ) -> Result<WorkspaceExportView, WorkspaceExportError>;

    /// Projects every visible revision for the generated Core API state.
    fn project_core_api(&self) -> Result<Vec<WorkspaceViewState>, WorkspaceStoreError>;

    /// The one export produced since the last visible revision changed.
    fn latest_export(&self) -> Option<WorkspaceExportView>;
}
