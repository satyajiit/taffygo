// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The reducer's input alphabet (domain model sections 18.1 and 18.2).
//!
//! # Where these names come from
//!
//! Section 18.1 names the durable user commands; [`CommandKind::is_user_command`]
//! marks exactly those. The rest are the runtime's own inputs — the executor
//! starting, settling finishing, a policy decision coming back, an outcome
//! arriving — which section 9.2 draws as transitions without naming a command
//! for each. Both kinds go through the same door, carry the same envelope, and
//! are journalled the same way, because a transition nobody journalled is a
//! transition nobody can replay.
//!
//! # Every command carries an expected revision and an idempotency key
//!
//! A command whose expected revision disagrees with the aggregate is refused
//! rather than applied to a state it was not written for. A command whose key
//! has already been applied returns the original result and performs no
//! effects, which is what makes a retry after process death safe.

use bip_types::identity::{ActionId, ApprovalReceiptReference, DispatchId, SemanticNodeId, TabId};

use crate::action::{ActionOutcome, ActionProposal};
use crate::agent::{ModelAttemptKind, TurnDigest, TurnGap};
use crate::artifact::ArtifactKind;
use crate::authority::ProposalDecision;
use crate::field_values::{
    FieldNodeIds, FieldValueAskOutcome, FieldValueRequestId, SuppliedValueCount,
};
use crate::handover::{HandoverCompletion, HandoverId};
use crate::ids::{ArtifactId, IdempotencyKey, ModelCallId, PlanStepId};
use crate::permission::{PermissionRequest, PermissionResult};
use crate::plan::{PlanDraft, StepState};
use crate::records::{FactId, SourceId};
use crate::task::{BrowserSessionId, FailureReason, ScopePreview, SourceScope, TaskResult};
use crate::time::TraceId;

mod idempotency;
mod pause;

pub use self::pause::PauseCause;

/// What the reducer was asked to do.
#[derive(Clone, Debug, PartialEq)]
pub enum Command {
    /// Create the task. Never accepted by a reducer that already has one;
    /// [`crate::reducer::Reducer::create`] is the constructor.
    CreateTask,
    /// Change the initial scope while the task is still being set up.
    EditScope(SourceScope),
    /// Ask to start, carrying the scope and provider preview the user will
    /// consent to. Section 9.2's `DRAFT` exit needs that preview to exist, so
    /// the command carries it rather than assuming it.
    StartTask(ScopePreview),
    /// The user accepted the initial consent.
    AcceptInitialConsent(ApprovalReceiptReference),
    /// Record the exact task-owned blank/search tab prepared for a zero-source
    /// Web errand.
    ///
    /// This command binds only a navigation context. It carries no origin or
    /// source identity and therefore cannot grant page authority.
    RecordDiscoveryTab {
        /// Exact browser-issued tab identity.
        discovery_tab_id: TabId,
        /// Browser session that minted the tab.
        browser_session_id: BrowserSessionId,
    },
    /// The user accepted an in-task approval.
    ApproveAction {
        /// The receipt the surface recorded.
        approval: ApprovalReceiptReference,
        /// Whether the approval still describes the current world. Approval is
        /// invalidated when input, target, destination, page epoch, risk,
        /// provider route, or disclosed data changes.
        still_current: bool,
        /// Browser-bounded monotonic expiry for this exact receipt.
        expires_at_monotonic_ms: u64,
        /// Absolute expiry persisted for old-boot replay refusal.
        expires_at_utc_ms: u64,
        /// Browser-process session in which approval occurred.
        browser_session_id: BrowserSessionId,
    },
    /// The user denied or dismissed an approval.
    DenyAction {
        /// The receipt the surface recorded.
        approval: ApprovalReceiptReference,
    },
    /// Hold the task.
    PauseTask {
        /// Why.
        cause: PauseCause,
    },
    /// The user took over the tab.
    TakeOver,
    /// Authority is revoked and in-flight work has settled.
    PauseSettled,
    /// Resume, which re-enters the queue for revalidation.
    ResumeTask,
    /// Stop the task.
    CancelTask,
    /// Authority is revoked and the journal has settled.
    CancelSettled,
    /// An executor picked the task up.
    ExecutorStarted,
    /// Replace the current plan with a new revision.
    SetPlan(PlanDraft),
    /// Move a plan step.
    AdvanceStep {
        /// Which step.
        plan_step_id: PlanStepId,
        /// Where to.
        to: StepState,
    },
    /// An exact action, data, or scope expansion needs the user's consent.
    RequestApproval {
        /// The action the consent is for.
        action_id: ActionId,
    },
    /// A source or an input is missing.
    RequestUserInput,
    /// The user supplied it.
    SupplyUserInput,
    /// The person is asked to fill in a form's fields (decision 0088).
    ///
    /// Beside [`Self::RequestUserInput`] rather than instead of it: that one
    /// asks a question the assistant then reads, this one asks for values the
    /// assistant never sees. The identity is derived from the turn and the
    /// call, like a handover's, so a replay reaches the same request.
    RequestFieldValues {
        /// Which request.
        request_id: FieldValueRequestId,
        /// The tab the form is in.
        tab_id: TabId,
        /// The form the person is being asked to fill in.
        node_id: SemanticNodeId,
        /// The other fields on the same page only the person can supply,
        /// asked about on the same sheet when `node_id` names a field rather
        /// than a form (decision 0238). Empty on a command rebuilt from the
        /// journal, which does not record them.
        companion_node_ids: FieldNodeIds,
    },
    /// The person answered, or the browser gave up asking.
    ///
    /// Carries a count, what became of the ask, and which field each held
    /// value was minted for, and nothing else. The values were minted into
    /// the browser's vault and never enter this process, so there is nothing
    /// here to hold — see [`crate::field_values`]. The outcome and the fields
    /// are `None` only on a command rebuilt from the journal, which records
    /// neither (decisions 0215 and 0238).
    SupplyFieldValues {
        /// Which request this answers.
        request_id: FieldValueRequestId,
        /// How many values the person supplied.
        supplied: SuppliedValueCount,
        /// What became of the ask, or `None` when this was restored.
        outcome: Option<FieldValueAskOutcome>,
        /// The field each held value was minted for, in position order, or
        /// `None` when this was restored. When present it names exactly
        /// `supplied` fields, or the answer is refused.
        field_node_ids: Option<FieldNodeIds>,
    },
    /// Open one exact native permission surface after this intent is durable.
    RequestPermission(PermissionRequest),
    /// Record the terminal decision for the exact pending permission request.
    RecordPermissionResult(PermissionResult),
    /// Stop and give the page to the person.
    ///
    /// The identity is carried rather than minted, like a model call's, so a
    /// replay reaches the same handover instead of opening a second one.
    RequestHandover {
        /// Which handover.
        handover_id: HandoverId,
    },
    /// The person came back and the assistant may resume.
    ///
    /// Reported by the surface the person pressed, the way
    /// [`Command::SupplyUserInput`] is: it is not one of section 18.1's user
    /// commands, because the durable fact is what the browser observed rather
    /// than a button.
    CompleteHandover(HandoverCompletion),
    /// The handover window closed with nobody coming back.
    ExpireHandover {
        /// Which handover.
        handover_id: HandoverId,
    },
    /// Propose an action. Proposing grants nothing.
    ProposeAction(Box<ActionProposal>),
    /// Record what `policy-engine` decided about a proposal.
    RecordPolicyDecision {
        /// Which action.
        action_id: ActionId,
        /// What policy decided.
        decision: Box<ProposalDecision>,
        /// Exact attempt identity committed atomically with an authorization.
        /// It is absent for approval-required and denied decisions.
        dispatch_id: Option<DispatchId>,
    },
    /// Journal the intent and spend the authority.
    DispatchAction {
        /// Which action.
        action_id: ActionId,
        /// The dispatch the broker assigned.
        dispatch_id: DispatchId,
    },
    /// Record what an executor reported.
    RecordActionOutcome {
        /// Which action.
        action_id: ActionId,
        /// What came back.
        outcome: Box<ActionOutcome>,
    },
    /// Record what one tool job produced, and settle its action.
    ///
    /// Apart from [`Command::RecordActionOutcome`] because the outcomes speak
    /// different languages: an action outcome is a page-protocol result code
    /// with optional observation evidence, and a tool job has neither — its
    /// durable record is a closed status, an output digest and counts.
    RecordToolJobOutcome {
        /// Which action.
        action_id: ActionId,
        /// Which job. Named so a completion for a job this action never
        /// dispatched is refused rather than recorded.
        job_id: crate::ids::ToolJobId,
        /// What the job produced.
        outcome: Box<crate::tool::ToolJobOutcome>,
    },
    /// Ask the model. The one command that spends money, and the reason it
    /// carries the call identity rather than minting one: a replay has to
    /// reach the same identity so the browser's effect journal can refuse a
    /// call it has already made.
    RequestModelTurn {
        /// Which call.
        call_id: ModelCallId,
    },
    /// Durably charge and authorize one router-approved sub-attempt.
    ///
    /// The exact provider request stays in the live runtime. These ordinals
    /// are the content-free proof that the logical turn advanced once and
    /// that replay must not invent another paid route.
    RequestModelAttempt {
        /// Logical model call shared by every attempt.
        call_id: ModelCallId,
        /// Zero-based paid attempt; the initial request is zero.
        attempt_ordinal: u32,
        /// Zero-based candidate in the immutable route plan.
        candidate_ordinal: u32,
        /// Whether this repeats a candidate or advances to its substitute.
        kind: ModelAttemptKind,
    },
    /// Record the shape of a reply. Counts and closed enumerations only — the
    /// text stays in the arena and never reaches the journal.
    RecordModelTurn {
        /// Which call it answers.
        call_id: ModelCallId,
        /// What the reply was, as shape.
        digest: Box<TurnDigest>,
    },
    /// Record that a turn produced no readable reply.
    ///
    /// A positive fact, not an absence. A cancelled or unsettled turn leaves a
    /// recorded gap rather than a conversation that ends on an unanswered
    /// request, because money may already have been spent.
    RecordModelTurnGap {
        /// Which call.
        call_id: ModelCallId,
        /// Why there is no reply.
        gap: TurnGap,
    },
    /// Record that the oldest turns no longer fit the context budget.
    ///
    /// The one durable fact context management leaves. The transcript builder
    /// drops every exchange up to and including `through_turn` before the
    /// byte ladder runs, so a replay composes the same shrunken conversation
    /// this run composed — and an audit can derive exactly what the model
    /// could no longer see, because the boundary is in the journal rather
    /// than in a projection that ran once and left nothing behind.
    RecordContextEviction {
        /// The highest turn ordinal evicted; every earlier turn goes with it.
        through_turn: u64,
    },
    /// A result candidate is ready for validation.
    ResultCandidateReady,
    /// Validation and persistence succeeded with no unmet requirement.
    CompleteResultValidated(TaskResult),
    /// Validation succeeded and the result has labelled gaps.
    PartialResultValidated(TaskResult),
    /// Go back to work to correct or retry something.
    ResumeForCorrection,
    /// The person asked a follow-up of a finished task (decision 0137).
    ///
    /// A unit, like [`Self::SupplyUserInput`]: the words are staged by the
    /// runtime and never journaled. The task keeps its scope, its sources, its
    /// refusal ledger and its budgets, and goes back to work.
    FollowUp,
    /// The task cannot be finished.
    FailTask {
        /// Why.
        reason: FailureReason,
    },
    /// The user corrected a fact.
    CorrectFact {
        /// The fact the correction supersedes.
        fact_id: FactId,
    },
    /// The user removed a source from scope.
    ExcludeSource {
        /// Which source.
        source_id: SourceId,
    },
    /// Record that the deterministic renderer prepared one exact workspace
    /// revision from accepted cited facts.
    RequestArtifact {
        /// Replay-stable identity derived from the model turn and call.
        artifact_id: ArtifactId,
        /// Closed reviewed output format.
        format: ArtifactKind,
        /// Exact workspace revision whose accepted facts were rendered.
        workspace_revision: u64,
    },
    /// The user accepted an artifact.
    AcceptArtifact {
        /// Which artifact.
        artifact_id: ArtifactId,
    },
    /// The user exported an accepted artifact.
    ExportArtifact {
        /// Which artifact.
        artifact_id: ArtifactId,
        /// Which format it was generated in.
        format: crate::artifact::ArtifactKind,
    },
}

/// The command discriminant, with no payload.
///
/// The transition table is a function of this and the state, so the table can
/// be walked exhaustively without constructing a payload for every cell.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum CommandKind {
    /// [`Command::CreateTask`].
    CreateTask,
    /// [`Command::EditScope`].
    EditScope,
    /// [`Command::StartTask`].
    StartTask,
    /// [`Command::AcceptInitialConsent`].
    AcceptInitialConsent,
    /// [`Command::RecordDiscoveryTab`].
    RecordDiscoveryTab,
    /// [`Command::ApproveAction`].
    ApproveAction,
    /// [`Command::DenyAction`].
    DenyAction,
    /// [`Command::PauseTask`].
    PauseTask,
    /// [`Command::TakeOver`].
    TakeOver,
    /// [`Command::PauseSettled`].
    PauseSettled,
    /// [`Command::ResumeTask`].
    ResumeTask,
    /// [`Command::CancelTask`].
    CancelTask,
    /// [`Command::CancelSettled`].
    CancelSettled,
    /// [`Command::ExecutorStarted`].
    ExecutorStarted,
    /// [`Command::SetPlan`].
    SetPlan,
    /// [`Command::AdvanceStep`].
    AdvanceStep,
    /// [`Command::RequestApproval`].
    RequestApproval,
    /// [`Command::RequestUserInput`].
    RequestUserInput,
    /// [`Command::SupplyUserInput`].
    SupplyUserInput,
    /// [`Command::RequestFieldValues`].
    RequestFieldValues,
    /// [`Command::SupplyFieldValues`].
    SupplyFieldValues,
    /// [`Command::RequestPermission`].
    RequestPermission,
    /// [`Command::RecordPermissionResult`].
    RecordPermissionResult,
    /// [`Command::RequestHandover`].
    RequestHandover,
    /// [`Command::CompleteHandover`].
    CompleteHandover,
    /// [`Command::ExpireHandover`].
    ExpireHandover,
    /// [`Command::ProposeAction`].
    ProposeAction,
    /// [`Command::RecordPolicyDecision`].
    RecordPolicyDecision,
    /// [`Command::DispatchAction`].
    DispatchAction,
    /// [`Command::RecordActionOutcome`].
    RecordActionOutcome,
    /// [`Command::RecordToolJobOutcome`].
    RecordToolJobOutcome,
    /// [`Command::RequestModelTurn`].
    RequestModelTurn,
    /// [`Command::RequestModelAttempt`].
    RequestModelAttempt,
    /// [`Command::RecordModelTurn`].
    RecordModelTurn,
    /// [`Command::RecordModelTurnGap`].
    RecordModelTurnGap,
    /// [`Command::RecordContextEviction`].
    RecordContextEviction,
    /// [`Command::ResultCandidateReady`].
    ResultCandidateReady,
    /// [`Command::CompleteResultValidated`].
    CompleteResultValidated,
    /// [`Command::PartialResultValidated`].
    PartialResultValidated,
    /// [`Command::ResumeForCorrection`].
    ResumeForCorrection,
    /// [`Command::FollowUp`].
    FollowUp,
    /// [`Command::FailTask`].
    FailTask,
    /// [`Command::CorrectFact`].
    CorrectFact,
    /// [`Command::ExcludeSource`].
    ExcludeSource,
    /// [`Command::RequestArtifact`].
    RequestArtifact,
    /// [`Command::AcceptArtifact`].
    AcceptArtifact,
    /// [`Command::ExportArtifact`].
    ExportArtifact,
}

impl CommandKind {
    /// Every command kind, in declaration order.
    ///
    /// The transition-table test walks this list against [`crate::task::TaskState::ALL`],
    /// so a command added without a table entry fails rather than being
    /// skipped.
    pub const ALL: &'static [Self] = &[
        Self::CreateTask,
        Self::EditScope,
        Self::StartTask,
        Self::AcceptInitialConsent,
        Self::RecordDiscoveryTab,
        Self::ApproveAction,
        Self::DenyAction,
        Self::PauseTask,
        Self::TakeOver,
        Self::PauseSettled,
        Self::ResumeTask,
        Self::CancelTask,
        Self::CancelSettled,
        Self::ExecutorStarted,
        Self::SetPlan,
        Self::AdvanceStep,
        Self::RequestApproval,
        Self::RequestUserInput,
        Self::SupplyUserInput,
        Self::RequestFieldValues,
        Self::SupplyFieldValues,
        Self::RequestPermission,
        Self::RecordPermissionResult,
        Self::RequestHandover,
        Self::CompleteHandover,
        Self::ExpireHandover,
        Self::ProposeAction,
        Self::RecordPolicyDecision,
        Self::DispatchAction,
        Self::RecordActionOutcome,
        Self::RecordToolJobOutcome,
        Self::RequestModelTurn,
        Self::RequestModelAttempt,
        Self::RecordModelTurn,
        Self::RecordModelTurnGap,
        Self::RecordContextEviction,
        Self::ResultCandidateReady,
        Self::CompleteResultValidated,
        Self::PartialResultValidated,
        Self::ResumeForCorrection,
        Self::FollowUp,
        Self::FailTask,
        Self::CorrectFact,
        Self::ExcludeSource,
        Self::RequestArtifact,
        Self::AcceptArtifact,
        Self::ExportArtifact,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::CreateTask => "CreateTask",
            Self::EditScope => "EditScope",
            Self::StartTask => "StartTask",
            Self::AcceptInitialConsent => "AcceptInitialConsent",
            Self::RecordDiscoveryTab => "RecordDiscoveryTab",
            Self::ApproveAction => "ApproveAction",
            Self::DenyAction => "DenyAction",
            Self::PauseTask => "PauseTask",
            Self::TakeOver => "TakeOver",
            Self::PauseSettled => "PauseSettled",
            Self::ResumeTask => "ResumeTask",
            Self::CancelTask => "CancelTask",
            Self::CancelSettled => "CancelSettled",
            Self::ExecutorStarted => "ExecutorStarted",
            Self::SetPlan => "SetPlan",
            Self::AdvanceStep => "AdvanceStep",
            Self::RequestApproval => "RequestApproval",
            Self::RequestUserInput => "RequestUserInput",
            Self::RequestFieldValues => "RequestFieldValues",
            Self::SupplyFieldValues => "SupplyFieldValues",
            Self::SupplyUserInput => "SupplyUserInput",
            Self::RequestPermission => "RequestPermission",
            Self::RecordPermissionResult => "RecordPermissionResult",
            Self::RequestHandover => "RequestHandover",
            Self::CompleteHandover => "CompleteHandover",
            Self::ExpireHandover => "ExpireHandover",
            Self::ProposeAction => "ProposeAction",
            Self::RecordPolicyDecision => "RecordPolicyDecision",
            Self::DispatchAction => "DispatchAction",
            Self::RecordActionOutcome => "RecordActionOutcome",
            Self::RecordToolJobOutcome => "RecordToolJobOutcome",
            Self::RequestModelTurn => "RequestModelTurn",
            Self::RequestModelAttempt => "RequestModelAttempt",
            Self::RecordModelTurn => "RecordModelTurn",
            Self::RecordModelTurnGap => "RecordModelTurnGap",
            Self::RecordContextEviction => "RecordContextEviction",
            Self::ResultCandidateReady => "ResultCandidateReady",
            Self::CompleteResultValidated => "CompleteResultValidated",
            Self::PartialResultValidated => "PartialResultValidated",
            Self::ResumeForCorrection => "ResumeForCorrection",
            Self::FollowUp => "FollowUp",
            Self::FailTask => "FailTask",
            Self::CorrectFact => "CorrectFact",
            Self::ExcludeSource => "ExcludeSource",
            Self::RequestArtifact => "RequestArtifact",
            Self::AcceptArtifact => "AcceptArtifact",
            Self::ExportArtifact => "ExportArtifact",
        }
    }

    /// Whether section 18.1 names this command as one the user issues.
    ///
    /// `CreateWorkspace`, `AddWorkspaceSource`, and `DeleteWorkspace` belong to
    /// the workspace aggregate and are deliberately absent from this reducer.
    pub const fn is_user_command(self) -> bool {
        matches!(
            self,
            Self::CreateTask
                | Self::StartTask
                | Self::PauseTask
                | Self::ResumeTask
                | Self::CancelTask
                | Self::TakeOver
                | Self::ApproveAction
                | Self::DenyAction
                | Self::CorrectFact
                | Self::ExcludeSource
                | Self::AcceptArtifact
                | Self::ExportArtifact
                | Self::FollowUp
        )
    }
}

impl Command {
    /// The discriminant of this command.
    pub const fn kind(&self) -> CommandKind {
        match self {
            Self::CreateTask => CommandKind::CreateTask,
            Self::EditScope(_) => CommandKind::EditScope,
            Self::StartTask(_) => CommandKind::StartTask,
            Self::AcceptInitialConsent(_) => CommandKind::AcceptInitialConsent,
            Self::RecordDiscoveryTab { .. } => CommandKind::RecordDiscoveryTab,
            Self::ApproveAction { .. } => CommandKind::ApproveAction,
            Self::DenyAction { .. } => CommandKind::DenyAction,
            Self::PauseTask { .. } => CommandKind::PauseTask,
            Self::TakeOver => CommandKind::TakeOver,
            Self::PauseSettled => CommandKind::PauseSettled,
            Self::ResumeTask => CommandKind::ResumeTask,
            Self::CancelTask => CommandKind::CancelTask,
            Self::CancelSettled => CommandKind::CancelSettled,
            Self::ExecutorStarted => CommandKind::ExecutorStarted,
            Self::SetPlan(_) => CommandKind::SetPlan,
            Self::AdvanceStep { .. } => CommandKind::AdvanceStep,
            Self::RequestApproval { .. } => CommandKind::RequestApproval,
            Self::RequestUserInput => CommandKind::RequestUserInput,
            Self::SupplyUserInput => CommandKind::SupplyUserInput,
            Self::RequestPermission(_) => CommandKind::RequestPermission,
            Self::RecordPermissionResult(_) => CommandKind::RecordPermissionResult,
            Self::RequestHandover { .. } => CommandKind::RequestHandover,
            Self::RequestFieldValues { .. } => CommandKind::RequestFieldValues,
            Self::SupplyFieldValues { .. } => CommandKind::SupplyFieldValues,
            Self::CompleteHandover(_) => CommandKind::CompleteHandover,
            Self::ExpireHandover { .. } => CommandKind::ExpireHandover,
            Self::ProposeAction(_) => CommandKind::ProposeAction,
            Self::RecordPolicyDecision { .. } => CommandKind::RecordPolicyDecision,
            Self::DispatchAction { .. } => CommandKind::DispatchAction,
            Self::RecordActionOutcome { .. } => CommandKind::RecordActionOutcome,
            Self::RecordToolJobOutcome { .. } => CommandKind::RecordToolJobOutcome,
            Self::RequestModelTurn { .. } => CommandKind::RequestModelTurn,
            Self::RequestModelAttempt { .. } => CommandKind::RequestModelAttempt,
            Self::RecordModelTurn { .. } => CommandKind::RecordModelTurn,
            Self::RecordModelTurnGap { .. } => CommandKind::RecordModelTurnGap,
            Self::RecordContextEviction { .. } => CommandKind::RecordContextEviction,
            Self::ResultCandidateReady => CommandKind::ResultCandidateReady,
            Self::CompleteResultValidated(_) => CommandKind::CompleteResultValidated,
            Self::PartialResultValidated(_) => CommandKind::PartialResultValidated,
            Self::ResumeForCorrection => CommandKind::ResumeForCorrection,
            Self::FollowUp => CommandKind::FollowUp,
            Self::FailTask { .. } => CommandKind::FailTask,
            Self::CorrectFact { .. } => CommandKind::CorrectFact,
            Self::ExcludeSource { .. } => CommandKind::ExcludeSource,
            Self::RequestArtifact { .. } => CommandKind::RequestArtifact,
            Self::AcceptArtifact { .. } => CommandKind::AcceptArtifact,
            Self::ExportArtifact { .. } => CommandKind::ExportArtifact,
        }
    }
}

/// A command with everything the journal needs to make it replayable.
#[derive(Clone, Debug, PartialEq)]
pub struct CommandEnvelope {
    /// The key that makes a repeat recognizable.
    pub idempotency_key: IdempotencyKey,
    /// The aggregate revision the caller believes the task is at.
    pub expected_revision: u64,
    /// The trace every event this command causes carries.
    pub trace_id: TraceId,
    /// What to do.
    pub command: Command,
}

impl CommandEnvelope {
    /// Builds an envelope.
    pub fn new(
        idempotency_key: IdempotencyKey,
        expected_revision: u64,
        trace_id: TraceId,
        command: Command,
    ) -> Self {
        Self {
            idempotency_key,
            expected_revision,
            trace_id,
            command,
        }
    }

    /// The discriminant of the command inside.
    pub const fn kind(&self) -> CommandKind {
        self.command.kind()
    }
}

#[cfg(test)]
mod tests;
