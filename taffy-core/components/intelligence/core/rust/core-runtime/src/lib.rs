// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Deterministic composition for the isolated Taffy core service.
//!
//! This crate owns no thread, executor, socket, database, filesystem path, or
//! browser object. Chromium drives it on one ordered sequence. Work that must
//! cross a process or blocking boundary leaves as a typed effect and returns as
//! exactly one typed completion.
//!
//! One runtime is profile-scoped. It owns shared account, policy, routing,
//! audit, storage-domain, and generation state plus a bounded deterministic map
//! of task reducers. Each task has independent commit and recovery state, so
//! one profile session can run multiple tasks without one transition blocking
//! another.
//!
//! [`task_engine`] proposes, [`policy_engine`] decides, [`audit_engine`]
//! records, [`model_router`] plans, and [`taffy_storage`] supplies storage
//! domain types. The traits in [`ports`] keep those roles explicit while the
//! browser remains the owner of physical storage, network, credentials, tabs,
//! and capability spending.
#![doc(html_no_source)]
#![cfg_attr(
    test,
    allow(
        clippy::unwrap_used,
        clippy::expect_used,
        clippy::panic,
        clippy::indexing_slicing
    )
)]

pub mod account;
pub mod assistant_configuration;
pub mod backup_planning;
pub mod backup_restore_protocol;
pub mod builtin_skills;
pub mod completion;
pub mod effect_identity;
pub mod entitlement_refresh;
pub mod probe;
/// The transient page/transcript context, owned by the loop kernel.
pub use loop_kernel::context;
pub mod adapters;
pub mod codec;
pub mod composition;
pub mod contract;
pub mod page_snapshot_export;
pub mod pending;
pub mod ports;
pub mod procedure_catalogue;
pub mod product_capabilities;
pub mod provider;
pub mod provider_listing;
pub mod runtime;
pub mod saved_data;

/// Canonical task identity used by the profile service bridge.
pub use bip_types::identity::{
    ActionId, ApprovalReceiptReference, DispatchId, FrameId, GraphRevision, MonotonicMillis,
    PageEpoch, SemanticNodeId, TabId, TaskId,
};
pub use bip_types::ActionResultCode;
/// Closed export family shared by the service bridge and Core API status.
pub use core_api_types::WorkspaceExportFormat;
/// Generated authority-neutral Core Service DTOs consumed by service glue.
pub use core_service_types as wire;
/// The one record family for an authored skill and a learned procedure.
///
/// Re-exported rather than re-declared. The core service owns durable state, so
/// a procedure is the core's record; this crate is where the service bridge
/// reads a core-owned type from, and a second name for one record is exactly
/// what decision 0055 refuses.
///
/// Nothing here matches, narrows or replays yet — storage and recording are
/// separate changes. What the seam settles now is that there is one place to
/// reach it from.
pub use procedure_engine as procedure;
/// Closed deterministic workspace mutation failures exposed to service glue.
pub use taffy_storage::workspace::MutationError as WorkspaceMutationError;
pub use task_engine::action::{
    ActionIntent, BrowserIntent, LibraryIntent, MediaOperation, MediaToolIntent, MemoryIntent,
    StoreIntent, StoreKind, TaskTabTarget, DEFAULT_STORE_RESULTS, MAX_STORE_RESULTS,
};
pub use task_engine::job_id_for_action;
/// Canonical bounded retry identity used by generated operation envelopes.
pub use task_engine::{
    Accepted, ActionClass, ActionOutcome, ActorLeaseId, AgentError, ArtifactCustody, ArtifactId,
    ArtifactKind, Authorization, BrowserSessionId, CapabilityId, Command, CommandEnvelope,
    CommandKind, ConsentedSource, ControlMode, Denial, Effect, FailureReason, FieldNodeIds,
    FieldValueAskOutcome, FieldValueRequestId, HandleTable, HandoverCompletion, HandoverId,
    HistoryAdmission, IdempotencyKey, MediaProbeResultError, MediaProbeTranscriptOutcome,
    Milestone, ModelCallId, ModelReply, PageObservationEvidence, PageReadability, PauseCause,
    PermissionDecision, PermissionRequestId, PermissionResult, PersonInput, PlatformPermission,
    ProposalDecision, RecoveryRule, RefusalReason, RenderShape, RevocationReason, SourceId,
    SuppliedValueCount, TaskControlKind, TaskDownloadActionResult, TaskDownloadDirectoryClass,
    TaskDownloadHandleTable, TaskDownloadMediaType, TaskDownloadResultError, TaskDownloadSnapshot,
    TaskDownloadState, TaskDownloadTranscriptEntry, TaskDownloadTranscriptOutcome,
    TaskLibraryCitation, TaskLibrarySearchEntry, TaskLibrarySearchTranscriptOutcome,
    TaskMemorySearchEntry, TaskMemorySearchTranscriptOutcome, TaskState, TaskStoreActionResult,
    TaskStoreResultError, TaskStoreRow, TaskStoreTranscriptOutcome, TaskTabActionResult,
    TaskTabHandleTable, TaskTabResultError, TaskTabSnapshot, TaskTabTranscriptEntry,
    TaskTabTranscriptOutcome, ToolJobId, ToolJobOutcome, ToolJobStatus, ToolRuntime, TraceId,
    TurnGap, TurnPage, TurnResidency, MAX_LIBRARY_TRANSCRIPT_RESULT_BYTES,
    MAX_MEDIA_PROBE_TRANSCRIPT_RESULT_BYTES, MAX_STORE_ROW_FIELD_BYTES, MAX_TASK_DOWNLOAD_RESULTS,
    MAX_TASK_STORE_RESULTS, MAX_TASK_TAB_RESULTS, MAX_WEB_ERRAND_NEW_SOURCE_CAP,
    REVIEWED_NO_MODEL_ROUTE_ID, REVIEWED_OBSERVATION_TOOL,
};

pub use crate::account::{
    classify_token_status, validate_account_token_response, AccountAuthMethod, AccountEffect,
    AccountEmail, AccountEndpointId, AccountError, AccountProtocol, AccountProvider, AccountScope,
    AccountSession, AccountSubjectId, AuthFlowId, AuthorizationCodeHandle, AuthorizationEntropy,
    AuthorizationIntent, AuthorizationRequestPlan, DigestError, EmailLinkPlan, GoogleNonceEntropy,
    GoogleNonceHash, GoogleRawNonceMaterial, NativeCredentialExchangePlan, NativeCredentialOutcome,
    PkceChallenge, PkceVerifierMaterial, RedirectBindingId, RedirectOutcome, RedirectReceipt,
    RedirectState, RefreshPlan, SecretHandle, SessionHandle, SessionReceipt, Sha256Port,
    TokenExchangePlan, TokenHttpDisposition, AUTHORIZATION_ENTROPY_BYTES,
    GOOGLE_NONCE_ENTROPY_BYTES, MAX_AUTHORIZATION_CODE_LIFETIME_MILLIS, REFRESH_AHEAD_MILLIS,
};
pub use crate::adapters::account::{
    AccountOperationRuntime, AccountServiceError, AccountServiceStep, ProductionAccount,
};
pub use crate::adapters::assets::{GenerationJitter, ProductionAssetDelivery};
pub use crate::adapters::audit::ProductionAudit;
pub use crate::adapters::ids::{GenerationEntropy, GENERATION_ENTROPY_BYTES};
pub use crate::adapters::library::ProductionLibrary;
pub use crate::adapters::memory::ProductionMemory;
pub use crate::adapters::model::{ModelRouterBuildError, ProductionModelRouter};
pub use crate::adapters::persistence::{
    decode_task_restore, DecodedTaskRestore, ProductionStorage, RestoreDecodeError,
    RestoredTaskEffectBatch,
};
pub use crate::adapters::policy::{ProductionPolicy, COMPILED_POLICY_VERSION};
pub use crate::adapters::provider::{ProviderServiceError, ProviderServiceStep};
pub use crate::adapters::task::ProductionTaskFactory;
pub use crate::adapters::time::ServiceClock;
pub use crate::adapters::workspace::ProductionWorkspaces;
pub use crate::assistant_configuration::{
    prepare_configured_start, AssistantConfiguration, AssistantConfigurationError,
};
pub use crate::codec::account_wire::{
    decode_account_session, decode_auth_callback, decode_email_link, decode_native_credential,
    decode_session_receipt, decode_start_auth, encode_account_effect, AccountStart,
    AccountWireError,
};
pub use crate::codec::action_result_code::{
    action_result_code_from_wire, action_result_code_to_wire,
};
pub use crate::codec::completion_wire::{decode_storage_completion, CompletionWireError};
pub use crate::codec::grant_wire::{GrantWireError, MintedGrantWire};
pub use crate::codec::observation_wire::{
    decode_media_observation, decode_page_observation, decode_page_observation_evidence,
    ObservationEffectView, ObservationWireError,
};
pub use crate::codec::policy_wire::{evaluate_policy_request, PolicyWireError};
pub use crate::codec::start_shape::StartTaskDecodeError;
pub use crate::codec::start_task::decode_start_task;
pub use crate::composition::profile::probe::ProbeCommandError;
pub use crate::composition::profile::{
    create_profile_service_runtime, CoreStatusEncodingError, EncodedCoreStatus,
    ModelAttemptDispatch, ModelCompletionOutcome, ModelFailureOutcome, PlannedTurn,
    ProfileRuntimeBuildError, ProfileRuntimeConfiguration, ProfileServiceRuntime,
};
pub use crate::contract::{
    BoundedPayload, BrowserActionEffect, CancellationReason, Completion, Deadline, EffectRequest,
    EffectSemantics, EnvelopeError, ObservationEffectError, OperationEnvelope, OperationId,
    PageObservationEffect, PayloadError, PayloadLimit, RuntimeError, ServiceGeneration,
    StorageCommitEffect, ToolCompletion, ToolContractError, ToolEffect, MAX_OPERATION_ID_BYTES,
    MAX_PAYLOAD_BYTES,
};
pub use crate::page_snapshot_export::PageSnapshotExporter;
pub use crate::pending::{CompletionError, PendingError, PendingOperations};
pub use crate::ports::{
    AccountPort, AuditPort, InitialConsentAdmission, LibraryPort, LibraryRemoveRequest,
    LibrarySaveRequest, LibraryStoreError, MemoryDeleteRequest, MemoryPort, MemorySaveRequest,
    MemoryScopeInput, MemorySensitivityInput, MemoryStoreError, MemoryTaskSave, MemoryTaskUpdate,
    MemoryUserUpsert, MemoryWorkspaceInput, ModelRouterPort, OpenedTaskEngine, PolicyEvaluation,
    PolicyPort, PortError, ReducerFactory, StorageCommit, StorageDomainPort, TaskCreationAudit,
    TaskCreationCommit, TaskEngineFactory, TaskEngineLoad, TaskEnginePort, TaskIdEntropy,
    TaskIdSourceFactory, WorkspaceDeleteRequest, WorkspaceDeletionCounts, WorkspaceDeletionPreview,
    WorkspaceExportError, WorkspaceListEntry, WorkspacePageFact, WorkspacePersistRequest,
    WorkspacePort, WorkspaceStoreError, TASK_ID_ENTROPY_BYTES,
};
pub use crate::procedure_catalogue::{
    PreparedSkillMutation, ProcedureCatalogue, ProcedureCatalogueError, ProcedureOffer,
    SkillMutationError, SkillMutationPersistence, SkillStartError,
};
pub use crate::runtime::{
    create_service_runtime, AccountRestoreError, BeginOpenTask, BeginSubmit, BoxedCoreRuntime,
    CommitCompletionError, CommitOutcome, CoreRuntime, DisconnectError, OpenTaskCommitOutcome,
    OpenTaskCompletionError, OpenTaskError, OpenTaskOutcome, ServiceRuntimeComponents, StageError,
    SubmitError, TaskCompletionError, TaskLibraryExecution, TaskLibraryExecutionError,
    TaskMemoryExecution, TaskMemoryExecutionError, TaskResultSubmission, TaskTerminal,
    WorkspaceCommitError, MAX_TASK_SESSIONS_PER_PROFILE,
};
pub use crate::runtime::{TerminalTaskBindingFacts, TerminalTaskKind};
pub use crate::saved_data::{SavedDataSnapshotError, SavedDataState};
pub use loop_kernel::turn::{
    ask_subject_from_residency, classify_ask_subject, classify_follow_up_question,
    classify_person_answer, compose_configured_model_turn, compose_model_turn, managed_request_id,
    model_request_id, read_model_reply, ComposedModelTurn, ModelReplyReading, ModelReplyStream,
    ModelResponseStyle, ModelTurnError, PersonAnswerRefusal, ReplyWire, MAX_PERSON_ANSWER_BYTES,
};
pub use loop_kernel::walk::{
    next_agent_command_envelope, next_durable_command_envelope,
    next_durable_command_envelope_with_procedure, next_procedure_command_envelope,
    next_reviewed_command_envelope, workflow_for_provider_route, AgentWorkflowError,
    DurableWorkflowError, ProcedureWorkflowError, ReviewedWorkflowError, TaskWorkflow, WalkError,
};
pub use model_router::ErrorClass as ModelErrorClass;
pub use policy_engine::{
    ActorLeaseFact, ApprovalFact, GrantIdempotencyKey, GrantIdempotencyKeyError, GrantRequest,
    MintedGrant,
};

/// Paid attempts, including the first, allowed for one logical model turn.
pub const MAX_MODEL_ATTEMPTS_PER_TURN: u32 = model_router::defaults::SEMANTIC_RETRY_ATTEMPTS;
