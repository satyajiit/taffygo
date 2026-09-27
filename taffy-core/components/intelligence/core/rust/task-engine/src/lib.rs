// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The durable task reducer: plans, budgets, journal, recovery, tools, and
//! deterministic exports.
//!
//! Authoritative specifications:
//! `docs/architecture/domain-model.md` sections 9 to 12, 16, 17, and 18, plus
//! the appendix "Tool namespace"; `docs/security/data-and-privacy.md` section
//! 15; decisions 0004 and 0009. Owning milestone: M3 (the assistant and
//! workspaces), work package WP-M0-03.
//!
//! # The one rule
//!
//! **This crate has no authority.** It decides what the assistant should try,
//! never what it may do. Every side effect it wants leaves as a proposal that
//! `policy-engine` must authorize, and the decision comes back in as a command.
//! There is no function here that issues a capability, holds a lease, widens a
//! scope, or reaches a page.
//!
//! # What lives here
//!
//! | Module | Owns |
//! |---|---|
//! | [`task`] | The thirteen durable states, the seven displayed ones, and the aggregate |
//! | [`transition`] | The table: every state times every command, with its outcome written down |
//! | [`command`] | The reducer's input alphabet and the envelope that makes it replayable |
//! | [`event`] | What the journal records about a transition |
//! | [`effect`] | What the runtime has to do, in an order that revokes before it waits |
//! | [`reducer`] | The fold, the revision check, and the rebuild from journal |
//! | [`journal`] | The append-only record of commands and events |
//! | [`plan`] | Plans and steps, which explain and authorize nothing |
//! | [`proposal`] | The one canonical identity of a proposal bound to a plan step |
//! | [`template`] | What a reviewed template says about itself in a person's words |
//! | [`action`] | Proposals, their lifecycle, and what an unknown outcome permits |
//! | [`budget`] | Limits, where a missing one is a policy default and never unlimited |
//! | [`agent`] | The assistant loop: the pure step that decides the next command, and the decision table it is |
//! | [`handover`] | Giving the page to the person, and the bounded evidence that they acted |
//! | [`tool`] | The typed tool namespace, where registration is not authorization: definitions, the effective set, argument validation, outcomes and the refusal loop guard |
//! | [`artifact`] | Byte-identical Markdown and comma-separated exports with fact-level citations |
//! | [`ids`] | Injected identifier sources |
//!
//! # Properties this crate holds
//!
//! - **Only the reducer commits a transition**, against an expected revision,
//!   and every transition it commits carries a cause event and a trace.
//! - **The internal state taxonomy is never displayed.** The seven user-visible
//!   states come from one pure function, [`task::TaskState::display`].
//! - **Pausing and stopping revoke authority before waiting on remote work**,
//!   and so does handing the page to a person. The effect list is built so,
//!   and [`effect::revocation_precedes_remote_wait`] is the predicate a test
//!   walks.
//! - **A handover does not recognise anything.** There is no field, anywhere,
//!   naming what the person is being asked to do. `BypassAccessControl` and
//!   `ExtractCredential` are prohibited by class, so a challenge and a
//!   one-time code are refused before anything looks at them, and the handover
//!   is what remains — see [`handover`].
//! - **A replay reconstructs and never repeats.** Rebuilding from the journal
//!   re-applies the same commands through the same deterministic fold, returns
//!   no effects while doing it, and ends an attempt that was in flight as
//!   `OUTCOME_UNKNOWN` rather than as a success or a retry.
//! - **`COMPLETED` requires a validated complete result.** A result with a
//!   labelled gap is `PARTIAL`, and the check is here rather than in the
//!   caller.
//! - **A terminal task acts no further.** It proposes nothing, dispatches
//!   nothing, and turns authority that arrives late into a cancelled action.
//! - **Registration is not authorization.** A tool name resolves to its owning
//!   milestone; whether an action may happen is still `policy-engine`'s
//!   decision, per action.
//! - **No recovery is an unqualified retry.** [`tool::Recovery`] has no bare
//!   `Retry` member, and the map from every result code to one of its five
//!   answers is total. An unqualified retry is how a runtime loops forever
//!   without anybody deciding it should.
//! - **Identical refusals are counted, not discouraged.**
//!   [`MAX_IDENTICAL_REFUSALS`] of the same call refused the same way ends the
//!   attempt. The count is reducer state, so a rebuild re-derives it.
//! - **No panics, no ambient clocks, no randomness.** Time arrives through
//!   [`Clock`], identifiers through [`ids::IdSource`], and every function is
//!   total.
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

/// The neutral types this crate's public interface is written in.
///
/// A consumer that only wants to drive a task should not have to name
/// a policy, audit, model-routing, or storage implementation in its own
/// manifest. Everything re-exported here appears in a signature or field of
/// this crate; nothing is re-exported for convenience.
pub mod deps {
    pub use crate::authority::{
        ActionClass, ActorLeaseId, Authorization, CapabilityId, ControlMode, Denial, PolicyVersion,
        ProposalDecision, RevocationReason,
    };
    pub use crate::records::{
        Fact, FactId, ProvenanceId, ProvenanceLocator, Source, SourceId, Timestamp, WorkspaceId,
    };
    pub use crate::route::ProviderRouteId;
    pub use crate::time::{Clock, ManualClock, TraceId, UtcMillis};
    pub use bip_types::identity::{
        ActionId, ApprovalReceiptReference, ContentDigest, DigestAlgorithm, DispatchId,
        MonotonicMillis, ProfileId, SemanticNodeId, SkillVersionId, TabId, TaskId,
    };
    pub use bip_types::ActionResultCode;
}

pub mod action;
pub mod agent;
pub mod artifact;
pub mod authority;
pub mod budget;
pub mod command;
mod control;
pub mod effect;
pub mod event;
pub mod field_values;
pub mod handle;
pub mod handover;
pub mod ids;
pub mod journal;
pub mod observation;
pub mod permission;
pub mod plan;
pub mod proposal;
pub mod records;
pub mod reducer;
pub mod route;
pub mod task;
pub mod template;
pub mod time;
pub mod tool;
pub mod transition;
pub mod workflow;

pub use crate::action::{
    ActionApproval, ActionOutcome, ActionProposal, ActionRecord, ActionState, LibraryIntent,
    MemoryIntent, MemoryScopeIntent, TaskTabTarget, DEFAULT_LIBRARY_SEARCH_RESULTS,
    DEFAULT_MEMORY_SEARCH_RESULTS, MAX_LIBRARY_SEARCH_RESULTS, MAX_MEMORY_SEARCH_RESULTS,
};
pub use crate::agent::{
    ask_view_key, call_id_for_turn, held_value_fill_key, loop_tool_result, turn_call_of,
    AgentError, CallDisposition, CallVerdict, CurrentDocument, HeldValuesPlaced,
    LiveSourceObservation, LoopOutcome, MediaProbeResultError, MediaProbeTranscriptOutcome,
    ModelAttemptKind, ModelReply, ModelStopReason, ModelToolCall, ModelTurn, NotAttempted,
    PageReadability, PersonsPages, PreModelObservation, RenderShape, TaskDownloadActionResult,
    TaskDownloadDirectoryClass, TaskDownloadHandleTable, TaskDownloadMediaType,
    TaskDownloadResultError, TaskDownloadSnapshot, TaskDownloadState, TaskDownloadTranscriptEntry,
    TaskDownloadTranscriptOutcome, TaskLibraryCitation, TaskLibrarySearchEntry,
    TaskLibrarySearchTranscriptOutcome, TaskMemorySearchEntry, TaskMemorySearchTranscriptOutcome,
    TaskStoreActionResult, TaskStoreResultError, TaskStoreRow, TaskStoreTranscriptOutcome,
    TaskTabActionResult, TaskTabHandleTable, TaskTabResultError, TaskTabSnapshot,
    TaskTabTranscriptEntry, TaskTabTranscriptOutcome, TurnDigest, TurnGap, TurnOverflow, TurnPage,
    TurnPhase, TurnResidency, TurnUsage, MAX_LIBRARY_TRANSCRIPT_RESULT_BYTES,
    MAX_LOOP_RESULT_BYTES, MAX_LOOP_RESULT_PIECES, MAX_MEDIA_PROBE_TRANSCRIPT_RESULT_BYTES,
    MAX_MEMORY_TRANSCRIPT_RESULT_BYTES, MAX_PARALLEL_SOURCE_READS, MAX_SOURCE_BOOTSTRAP_READS,
    MAX_STORE_ROW_FIELD_BYTES, MAX_STORE_TRANSCRIPT_RESULT_BYTES, MAX_TASK_DOWNLOAD_RESULTS,
    MAX_TASK_STORE_RESULTS, MAX_TASK_TAB_RESULTS, MAX_TURN_TOOL_CALLS, STORE_TOOLS,
};
pub use crate::artifact::{
    generate, generate_within, suggested_filename, Artifact, ArtifactCustody, ArtifactError,
    ArtifactKind, ArtifactRecord, ArtifactRequest, CitedFact, ContentChecksum,
};
pub use crate::authority::{
    ActionClass, ActorLeaseId, Authorization, CapabilityId, ControlMode, Denial, PolicyVersion,
    ProposalDecision, RevocationReason,
};
pub use crate::budget::{BudgetDefaults, BudgetDraw, BudgetKind, BudgetLedger, TaskBudgets};
pub use crate::command::{Command, CommandEnvelope, CommandKind, PauseCause};
pub use crate::control::TaskControlKind;
pub use crate::effect::{revocation_precedes_remote_wait, Effect};
pub use crate::event::{EventKind, EventSubject, TaskEvent};
pub use crate::field_values::{
    field_value_request_id_for_call, FieldNodeIds, FieldValueAskOutcome, FieldValueFactError,
    FieldValueRequestId, HeldValuePlacement, SuppliedFieldValues, SuppliedValueCount,
    MAX_FIELD_NODE_ID_BYTES, MAX_FIELD_VALUE_REQUEST_ID_BYTES, MAX_REQUESTED_FIELDS,
};
pub use crate::handle::{
    HandleBinding, HandleTable, ModelHandle, ValueTarget, MAX_RETAINED_BINDINGS,
};
pub use crate::handover::{
    handover_id_for_call, HandoverCompletion, HandoverFactError, HandoverId, PersonInput,
    HANDOVER_WINDOW_MS, MAX_HANDOVER_ID_BYTES,
};
pub use crate::ids::{
    ArtifactId, IdKind, IdSource, IdempotencyKey, ModelCallId, PlanId, PlanStepId, SequentialIds,
    ToolJobId,
};
pub use crate::journal::{JournalEntry, JournalError, TaskJournal};
pub use crate::observation::{
    ObservationCompleteness, ObservationGraphSummary, PageObservationEvidence,
    MAX_OBSERVATION_IDENTIFIER_BYTES, MAX_OBSERVATION_ORIGIN_BYTES,
    MAX_OBSERVATION_SCHEMA_VERSION_BYTES,
};
pub use crate::permission::{
    PermissionDecision, PermissionFactError, PermissionRequest, PermissionRequestId,
    PermissionResult, PlatformPermission, MAX_PERMISSION_REQUEST_ID_BYTES,
};
pub use crate::plan::{Plan, PlanDraft, PlanStatus, PlanStep, StepDraft, StepKind, StepState};
pub use crate::proposal::{
    hex_digest, plan_step_key, plan_step_material, PLAN_STEP_PROPOSAL_DOMAIN,
};
pub use crate::records::{
    DeletionState, Fact, FactClassification, FactId, FactStatus, ObservationId, Ownership,
    ProvenanceId, ProvenanceKind, ProvenanceLocator, Sensitivity, Source, SourceId, SourceKind,
    Timestamp, WorkspaceId,
};
pub use crate::reducer::{
    Accepted, HistoryAdmission, Recovery, Reducer, Refusal, ReplayError, TaskSeed,
    FRUITLESS_ERRAND_ARRIVAL_NUDGE, MAX_ACTIONS_PER_TASK, MAX_COMMAND_RECEIPTS,
    MAX_CONSECUTIVE_REASKS, MAX_FRUITLESS_ERRAND_ARRIVALS, MAX_RETAINED_TOOL_ARTIFACTS_PER_TASK,
    MAX_RETAINED_TOOL_ARTIFACT_BYTES_PER_TASK, MAX_TURNS_ATTEMPTING_NOTHING,
    MAX_TURNS_WITHOUT_PROGRESS, MAX_UNANSWERED_VALUE_ASKS, MAX_UNPRODUCTIVE_ERRAND_REPLIES,
    TURNS_WITHOUT_PROGRESS_NUDGE, UNANSWERED_VALUE_ASK_NUDGE,
};
pub use crate::route::{ProviderRouteId, ProviderRouteIdError};
pub use crate::task::{
    host_of_origin, BrowserSessionId, BrowserSessionIdError, BuiltinSkillId, BuiltinSkillReference,
    ConsentedSource, DisplayState, ExecutionPhase, FailureReason, GapReason, LibraryRefreshContext,
    LibraryRefreshSource, ScopePreview, SourceScope, StateReason, Task, TaskActivity,
    TaskActivityKind, TaskActivityStep, TaskKind, TaskResult, TaskSnapshot, TaskState,
    TaskTemplateId, UnmetRequirement, MAX_BROWSER_SESSION_ID_BYTES,
    MAX_LIBRARY_REFRESH_LOCATOR_BYTES, MAX_LIBRARY_REFRESH_SOURCES, MAX_TASK_ACTIVITY,
    MAX_WEB_ERRAND_NEW_SOURCE_CAP,
};
pub use crate::time::{Clock, ManualClock, TraceId, UtcMillis};
pub use crate::tool::LibraryTool;
// `tool::Recovery` is deliberately absent from this list. `Recovery` at the
// crate root is `reducer::Recovery`, which is what a rebuild from the journal
// concluded — a different subject that has held the name since before the tool
// vocabulary existed. Two types called `Recovery` here would be one import away
// from a caller that read the wrong one and compiled. It is reachable as
// `task_engine::tool::Recovery`.
pub use crate::tool::{
    available_at, job_id_for_action, permits_unattended_attempt, recovery_for, resolve, validate,
    AbandonReason, ArgumentRefusal, ArgumentRefusalReason, ArgumentValue, CallFingerprint,
    EffectiveToolSet, EmptyReason, IdempotencyClass, Milestone, Parameter, ParameterType,
    RecoveryRule, RefusalLedger, RepeatVerdict, SuppliedArgument, ToolAvailability, ToolDefinition,
    ToolDispatch, ToolEntry, ToolJobOutcome, ToolJobStatus, ToolLoading, ToolLookup, ToolOutcome,
    ToolRuntime, ACTIVATE_TOOL, ASK_TOOL, HANDOVER_TOOL, MAX_ARGUMENT_VALUE_BYTES,
    MAX_IDENTICAL_REFUSALS, MAX_NESTED_DEPTH, MAX_SUPPLIED_ARGUMENTS, MAX_TRACKED_REFUSALS,
    REGISTRY, SEARCH_TOOLS, SPAWN_RUN, TABLE_RESHAPE_TOOL,
};
pub use crate::transition::{disposition, Disposition, Guard, RefusalReason};
pub use crate::workflow::{
    WorkflowDigest, WorkflowError, REVIEWED_EFFECT_NAMESPACE, REVIEWED_NO_MODEL_ROUTE_ID,
    REVIEWED_OBSERVATION_TOOL,
};
