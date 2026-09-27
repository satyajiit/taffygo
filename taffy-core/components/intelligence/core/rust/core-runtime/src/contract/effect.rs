// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed effect families and their recovery semantics.

use bip_types::identity::{ActionId, FrameId, PageEpoch, TabId, TaskId};
use core_service_types::ObservationScope;
use policy_engine::{ActionClass, AuthoritySubject, MintedGrant};
use task_engine::ids::IdempotencyKey;

use crate::account::AccountEffect;

use super::{BoundedPayload, ToolCompletion, ToolEffect, MAX_PAYLOAD_BYTES};

/// A typed atomic append for the browser-owned Core Service journal.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StorageCommitEffect {
    task_id: TaskId,
    expected_revision: u64,
    resulting_revision: u64,
    transaction_batch: BoundedPayload,
    task_id_seed: [u8; 32],
    workspace: Option<WorkspaceCommit>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct WorkspaceCommit {
    workspace_id: String,
    expected_revision: u64,
    resulting_revision: u64,
    snapshot: Vec<u8>,
}

impl StorageCommitEffect {
    pub(crate) fn append_task_commit(
        task_id: TaskId,
        expected_revision: u64,
        resulting_revision: u64,
        transaction_batch: BoundedPayload,
        task_id_seed: [u8; 32],
    ) -> Self {
        Self {
            task_id,
            expected_revision,
            resulting_revision,
            transaction_batch,
            task_id_seed,
            workspace: None,
        }
    }

    /// Couples an independently versioned workspace write to this task append.
    pub(crate) fn with_workspace(
        mut self,
        workspace_id: String,
        expected_revision: u64,
        resulting_revision: u64,
        snapshot: Vec<u8>,
    ) -> Self {
        self.workspace = Some(WorkspaceCommit {
            workspace_id,
            expected_revision,
            resulting_revision,
            snapshot,
        });
        self
    }

    /// Projects the exact generated broker request with no service-glue defaults.
    pub fn to_wire(&self) -> core_service_types::StorageCommitEffect {
        core_service_types::StorageCommitEffect {
            operation_kind: core_service_types::StorageOperation::AppendTaskCommit,
            task_id: self.task_id.0.clone(),
            expected_revision: self.expected_revision,
            resulting_revision: self.resulting_revision,
            transaction_batch: self.transaction_batch.as_bytes().to_vec(),
            task_id_seed: self.task_id_seed,
            workspace: self.workspace.as_ref().map(|workspace| {
                core_service_types::WorkspacePersistEffect {
                    workspace_id: workspace.workspace_id.clone(),
                    snapshot: workspace.snapshot.clone(),
                    expected_revision: workspace.expected_revision,
                    resulting_revision: workspace.resulting_revision,
                }
            }),
            install_skill: None,
            skill_status: None,
            skill_run: None,
            forget_skill: None,
            source_deletion: None,
            assistant_configuration: None,
            workspace_deletion: None,
            library_entry: None,
            library_deletion: None,
            memory_record: None,
            memory_deletion: None,
        }
    }

    /// Revision the browser must report after the atomic append.
    pub const fn resulting_revision(&self) -> u64 {
        self.resulting_revision
    }
}

/// Why an observation effect could not be derived from a policy grant.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ObservationEffectError {
    /// Only an `ObservePage` grant may authorize page intelligence.
    WrongActionClass,
    /// A zero-byte observation cannot produce a protocol value.
    ZeroLimit,
    /// The requested observation crosses the service payload ceiling.
    LimitTooLarge,
    /// The policy grant carried an unsupported proposal digest.
    InvalidProposalDigest,
    /// Taskless direct observation is emitted only by the typed policy wire.
    DirectObservationRequiresPolicyWire,
}

/// A page-intelligence read bound to one Rust-minted policy grant.
///
/// Every browser/document identity is copied from the grant. The browser may
/// choose only the closed observation scope and a narrower byte ceiling; it
/// cannot substitute a task, action, document, digest, or capability.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PageObservationEffect {
    authority_subject: AuthoritySubject,
    action_id: ActionId,
    capability_id: policy_engine::CapabilityId,
    proposal_digest: String,
    idempotency_key: IdempotencyKey,
    tab_id: TabId,
    frame_id: FrameId,
    page_epoch: PageEpoch,
    expected_graph_revision: u64,
    scope: ObservationScope,
    max_bytes: u32,
}

impl PageObservationEffect {
    /// Narrows a complete minted grant into a bounded observation request.
    pub fn from_grant(
        grant: &MintedGrant,
        scope: ObservationScope,
        max_bytes: u32,
    ) -> Result<Self, ObservationEffectError> {
        if grant.action_class != ActionClass::ObservePage {
            return Err(ObservationEffectError::WrongActionClass);
        }
        if !matches!(grant.authority_subject, AuthoritySubject::Task(_)) {
            return Err(ObservationEffectError::DirectObservationRequiresPolicyWire);
        }
        if max_bytes == 0 {
            return Err(ObservationEffectError::ZeroLimit);
        }
        let max_bytes_usize = usize::try_from(max_bytes).unwrap_or(usize::MAX);
        if max_bytes_usize > MAX_PAYLOAD_BYTES {
            return Err(ObservationEffectError::LimitTooLarge);
        }
        let digest = &grant.proposal_digest.value;
        if digest.len() != 64
            || !digest
                .bytes()
                .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
        {
            return Err(ObservationEffectError::InvalidProposalDigest);
        }
        Ok(Self {
            authority_subject: grant.authority_subject.clone(),
            action_id: grant.action_id.clone(),
            capability_id: grant.capability_id.clone(),
            proposal_digest: digest.clone(),
            idempotency_key: IdempotencyKey::new(grant.idempotency_key.as_str()),
            tab_id: grant.scope.tab_id.clone(),
            frame_id: grant.scope.frame_id.clone(),
            page_epoch: grant.scope.page_epoch.clone(),
            expected_graph_revision: grant.scope.required_graph_revision.0,
            scope,
            max_bytes,
        })
    }

    /// Task whose reducer proposed the observation.
    pub const fn task_id(&self) -> Option<&TaskId> {
        self.authority_subject.task_id()
    }

    /// Typed task/direct authority binding copied from the policy grant.
    pub const fn authority_subject(&self) -> &AuthoritySubject {
        &self.authority_subject
    }

    /// Action whose proposal digest was evaluated.
    pub const fn action_id(&self) -> &ActionId {
        &self.action_id
    }

    /// Browser-ledger reference to the policy-minted grant.
    pub const fn capability_id(&self) -> &policy_engine::CapabilityId {
        &self.capability_id
    }

    /// Lowercase SHA-256 proposal digest.
    pub fn proposal_digest(&self) -> &str {
        &self.proposal_digest
    }

    /// Exact effect identity bound into the grant.
    pub const fn idempotency_key(&self) -> &IdempotencyKey {
        &self.idempotency_key
    }

    /// Browser tab copied from the grant scope.
    pub const fn tab_id(&self) -> &TabId {
        &self.tab_id
    }

    /// Renderer frame copied from the grant scope.
    pub const fn frame_id(&self) -> &FrameId {
        &self.frame_id
    }

    /// Document epoch copied from the grant scope.
    pub const fn page_epoch(&self) -> &PageEpoch {
        &self.page_epoch
    }

    /// Exact graph revision covered by the policy-minted capability.
    pub const fn expected_graph_revision(&self) -> u64 {
        self.expected_graph_revision
    }

    /// Closed BIP observation scope.
    pub const fn scope(&self) -> ObservationScope {
        self.scope
    }

    /// Maximum generated observation bytes.
    pub const fn max_bytes(&self) -> u32 {
        self.max_bytes
    }

    /// Projects into the one generated Core Service schema without exposing
    /// any wider constructor to browser glue.
    pub fn to_wire(&self) -> core_service_types::PageObservationEffect {
        let authority_subject = match &self.authority_subject {
            AuthoritySubject::Task(task_id) => core_service_types::AuthoritySubject {
                kind: core_service_types::AuthoritySubjectKind::Task,
                authority_subject_id: task_id.0.clone(),
            },
            AuthoritySubject::DirectUserIntent(intent_id) => core_service_types::AuthoritySubject {
                kind: core_service_types::AuthoritySubjectKind::DirectUserIntent,
                authority_subject_id: intent_id.as_str().to_owned(),
            },
        };
        core_service_types::PageObservationEffect {
            tab_id: self.tab_id.0.clone(),
            frame_id: self.frame_id.0.clone(),
            page_epoch: self.page_epoch.0.clone(),
            scope: self.scope,
            max_bytes: self.max_bytes,
            task_id: self
                .authority_subject
                .task_id()
                .map_or_else(String::new, |task_id| task_id.0.clone()),
            action_id: self.action_id.0.clone(),
            capability_id: self.capability_id.as_str().to_owned(),
            proposal_digest: self.proposal_digest.clone(),
            idempotency_key: self.idempotency_key.as_str().to_owned(),
            authority_subject,
            max_nodes: 0,
            max_text_bytes: 0,
            max_frames: 0,
            deadline_ms: 0,
            expected_graph_revision: self.expected_graph_revision,
        }
    }
}

/// A browser action the core may propose but never authorize or perform.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum BrowserActionEffect {
    /// Dispatch an action after the browser validates and spends its grant.
    Dispatch(ActionId),
    /// Reconcile a dispatch whose terminal outcome was not observed.
    Reconcile(ActionId),
    /// Close tabs owned by the task.
    ReleaseTaskTabs,
    /// Hand an already-generated artifact to the platform picker.
    ExportArtifact,
}

impl BrowserActionEffect {
    const fn semantics(&self) -> EffectSemantics {
        match self {
            Self::ReleaseTaskTabs => EffectSemantics::Idempotent,
            Self::Dispatch(_) | Self::Reconcile(_) | Self::ExportArtifact => {
                EffectSemantics::Consequential
            }
        }
    }
}

/// One asynchronous operation emitted by the ordered core.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EffectRequest {
    /// An existing deterministic task-machine effect during the migration.
    Task(task_engine::Effect),
    /// A typed request to the browser storage broker.
    Storage(StorageCommitEffect),
    /// A bounded renderer observation request.
    Observation(PageObservationEffect),
    /// A provider-neutral or local model request.
    Model(BoundedPayload),
    /// A browser action proposal.
    BrowserAction(BrowserActionEffect),
    /// A request to one isolated tool runtime.
    Tool(Box<ToolEffect>),
    /// A portable account protocol plan executed by the browser transport.
    Account(AccountEffect),
}

/// What recovery may conclude after losing an in-flight operation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum EffectSemantics {
    /// The request observes state and may be issued again after revalidation.
    PureRead,
    /// Repeating the same key is safe, but still requires committed replay.
    Idempotent,
    /// It may have changed state or spent a budget; never retry blindly.
    Consequential,
}

impl EffectRequest {
    /// Recovery semantics are owned by the request kind, never its caller.
    pub const fn semantics(&self) -> EffectSemantics {
        match self {
            Self::Storage(_) => EffectSemantics::Idempotent,
            Self::Observation(_) => EffectSemantics::PureRead,
            Self::Tool(tool) => match tool.runtime() {
                core_service_types::ToolRuntimeKind::Media => EffectSemantics::Idempotent,
                core_service_types::ToolRuntimeKind::Python
                | core_service_types::ToolRuntimeKind::LocalModel
                | core_service_types::ToolRuntimeKind::Wasm => EffectSemantics::PureRead,
            },
            Self::Model(_) | Self::Account(_) => EffectSemantics::Consequential,
            Self::BrowserAction(action) => action.semantics(),
            Self::Task(effect) => match effect {
                // A tool job is one authorized dispatch attempt, exactly as a
                // browser action's is: the journal has spent the authority by
                // the time this intent exists, so a blind repeat could run
                // the job twice against one grant.
                task_engine::Effect::DispatchAction { .. }
                | task_engine::Effect::RunToolJob { .. }
                | task_engine::Effect::ReconcileAction { .. }
                | task_engine::Effect::ExportArtifact { .. }
                // A model call spends money. Nothing about it is safe to
                // repeat on the runtime's own initiative, which is the whole
                // of what this class says.
                | task_engine::Effect::CallModel { .. } => EffectSemantics::Consequential,
                task_engine::Effect::RevokeAuthority { .. }
                | task_engine::Effect::AskPolicy { .. }
                | task_engine::Effect::RequestApproval { .. }
                | task_engine::Effect::RequestPermission { .. }
                | task_engine::Effect::AwaitInFlightWork { .. }
                // Opening the same handover twice opens it once: the identity
                // is derived from the call that asked, so a repeat names the
                // window that is already open rather than interrupting the
                // person a second time. What would not be idempotent is a
                // *second* handover, and that is a different identity and a
                // different effect.
                | task_engine::Effect::AwaitHandover { .. }
                // The bootstrap identity names one already task-owned tab;
                // replay prepares that same tab and never widens page scope.
                | task_engine::Effect::PrepareDiscoveryTab { .. }
                // Idempotent for the same reason a handover is: the request
                // identity is derived from the call that asked, so a repeat
                // re-opens the surface that is already open rather than asking
                // the person for the same values a second time.
                | task_engine::Effect::RequestFieldValues { .. }
                | task_engine::Effect::RunLibraryTool { .. }
                | task_engine::Effect::RunMemoryTool { .. }
                | task_engine::Effect::ReleaseTaskTabs
                | task_engine::Effect::GenerateArtifact { .. } => EffectSemantics::Idempotent,
            },
        }
    }

    /// Whether a terminal body belongs to this exact effect family.
    pub(crate) fn accepts_completion(&self, completion: &Completion) -> bool {
        match completion {
            Completion::Failed(_) | Completion::Cancelled(_) | Completion::OutcomeUnknown => true,
            Completion::StorageCommitted { committed_revision } => matches!(
                self,
                Self::Storage(commit) if commit.resulting_revision() == *committed_revision
            ),
            Completion::Tool(result) => matches!(
                self,
                Self::Tool(effect) if effect.job_id() == result.job_id()
            ),
            Completion::Succeeded(_) => matches!(
                self,
                Self::Task(_)
                    | Self::Observation(_)
                    | Self::Model(_)
                    | Self::BrowserAction(_)
                    | Self::Account(_)
            ),
        }
    }
}

/// Why the core or a broker ended an operation without a value.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RuntimeError {
    CoreUnavailable,
    DeadlineExceeded,
    InvalidPayload,
    StaleGeneration,
    StaleRevision,
    PortUnavailable,
    StorageConflict,
    PolicyDenied,
    ModelUnavailable,
    ToolFailed,
    BrowserActionFailed,
    ProtocolViolation,
}

impl RuntimeError {
    /// A compiled-in diagnostic label safe for content-free telemetry.
    pub const fn label(self) -> &'static str {
        match self {
            Self::CoreUnavailable => "core_unavailable",
            Self::DeadlineExceeded => "deadline_exceeded",
            Self::InvalidPayload => "invalid_payload",
            Self::StaleGeneration => "stale_generation",
            Self::StaleRevision => "stale_revision",
            Self::PortUnavailable => "port_unavailable",
            Self::StorageConflict => "storage_conflict",
            Self::PolicyDenied => "policy_denied",
            Self::ModelUnavailable => "model_unavailable",
            Self::ToolFailed => "tool_failed",
            Self::BrowserActionFailed => "browser_action_failed",
            Self::ProtocolViolation => "protocol_violation",
        }
    }
}

/// Why a pending operation was cancelled before a terminal result.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum CancellationReason {
    User,
    TaskSettled,
    ProfileShutdown,
}

/// The exactly-one terminal answer for an effect.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Completion {
    /// Browser-owned atomic append reached this exact durable revision.
    StorageCommitted {
        committed_revision: u64,
    },
    Succeeded(BoundedPayload),
    /// A runtime-specific, typed, exactly-one terminal tool result.
    Tool(Box<ToolCompletion>),
    Failed(RuntimeError),
    Cancelled(CancellationReason),
    OutcomeUnknown,
}

impl Completion {
    /// A compiled-in terminal label.
    pub const fn label(&self) -> &'static str {
        match self {
            Self::StorageCommitted { .. } | Self::Succeeded(_) | Self::Tool(_) => "succeeded",
            Self::Failed(_) => "failed",
            Self::Cancelled(_) => "cancelled",
            Self::OutcomeUnknown => "outcome_unknown",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{BrowserActionEffect, EffectRequest, EffectSemantics};

    #[test]
    fn recovery_semantics_are_owned_by_the_closed_operation_kind() {
        let read = EffectRequest::BrowserAction(BrowserActionEffect::ReleaseTaskTabs);
        let deletion = EffectRequest::BrowserAction(BrowserActionEffect::ExportArtifact);
        assert_eq!(read.semantics(), EffectSemantics::Idempotent);
        assert_eq!(deletion.semantics(), EffectSemantics::Consequential);
    }
}
