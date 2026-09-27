// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Immutable fact records crossing the reducer port.

use task_engine::{ActionClass, FailureReason, PlanStatus, TaskState, TaskTemplateId};

/// Bytes of browser-generated entropy persisted with one task creation.
pub const TASK_ID_ENTROPY_BYTES: usize = 32;

/// Replay-stable entropy from which Rust derives task-local identifiers.
#[derive(Clone, PartialEq, Eq)]
pub struct TaskIdEntropy([u8; TASK_ID_ENTROPY_BYTES]);

impl TaskIdEntropy {
    /// Accepts non-degenerate browser entropy.
    pub fn new(bytes: [u8; TASK_ID_ENTROPY_BYTES]) -> Result<Self, PortError> {
        let Some(first) = bytes.first().copied() else {
            return Err(PortError::InvalidInput);
        };
        if bytes.iter().all(|byte| *byte == first) {
            return Err(PortError::InvalidInput);
        }
        Ok(Self(bytes))
    }

    /// The entropy, for the id-source factory that consumes it.
    #[must_use]
    pub const fn as_bytes(&self) -> &[u8; TASK_ID_ENTROPY_BYTES] {
        &self.0
    }
}

impl core::fmt::Debug for TaskIdEntropy {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter.write_str("TaskIdEntropy(redacted)")
    }
}

/// Closed failures shared by deterministic composition adapters.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PortError {
    /// A required local component is not available.
    Unavailable,
    /// Expected durable revision disagreed with the single writer.
    Conflict,
    /// Generated input failed the receiving domain's validation.
    InvalidInput,
    /// The receiving domain refused the request.
    Rejected,
}

/// Immutable progress facts derived from the reducer's one current plan.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PlanProgressFacts {
    /// Lifecycle of the current durable plan revision.
    pub status: PlanStatus,
    /// Every step in the current plan.
    pub total_steps: u32,
    /// Steps in any durable final state, regardless of outcome.
    pub finished_steps: u32,
}

/// Facts for the one action whose exact proposal is awaiting a user decision.
///
/// `proposal_digest` is browser/service-private correlation. The generated
/// Core API projection deliberately strips it before any UI can observe the
/// action.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PendingActionFacts {
    /// Opaque reducer-minted action identity.
    pub action_id: String,
    /// Closed consequence class, used only to select a localized summary.
    pub action_class: ActionClass,
    /// A proposal represents one exact visible action.
    pub item_count: u32,
    /// Lowercase SHA-256 digest of the exact reducer-held proposal.
    pub proposal_digest: String,
}

/// Exact native permission request currently owned by the reducer.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PendingPermissionFacts {
    pub request_id: String,
    pub permission: task_engine::PlatformPermission,
    pub deadline_monotonic_ms: u64,
    pub deadline_utc_ms: u64,
    pub browser_session_id: String,
}

/// Immutable canonical facts needed to externalize one committed action effect.
#[derive(Clone, Debug, PartialEq)]
pub struct ActionEffectFacts {
    pub action_id: String,
    pub proposal: task_engine::ActionProposal,
    pub state: task_engine::ActionState,
    pub capability_id: Option<String>,
    pub dispatch_id: Option<String>,
    pub approval: Option<task_engine::ActionApproval>,
    pub control_mode: task_engine::ControlMode,
    pub policy_version: task_engine::PolicyVersion,
    pub skill_version_id: Option<String>,
}

/// Durable zero-source discovery facts projected from the canonical reducer.
///
/// The record deliberately contains no origin. A prepared blank tab is not a
/// source; it can authorize only the separately typed discovery search, whose
/// verified terminal contributes the first exact tuple source.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskDiscoveryAuthorityFacts {
    pub discovery_tab_id: String,
    pub browser_session_id: String,
    pub remaining_new_source_cap: u32,
}

/// Immutable built-in binding and the durable start facts it constrained.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BuiltinSkillBindingFacts {
    pub reference: task_engine::BuiltinSkillReference,
    pub template_id: TaskTemplateId,
    pub tool_allowlist: Vec<String>,
    pub milestone: task_engine::Milestone,
    pub initial_source_count: usize,
    pub source_discovery_enabled: bool,
    pub remaining_new_source_cap: u32,
}

/// Browser-only durable initial-consent facts reconstructed from the journal.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AcceptedTaskConsentFacts {
    pub accepted_revision: u64,
    pub browser_session_id: String,
    pub receipt_id: String,
    pub sources: Vec<task_engine::ConsentedSource>,
    pub source_discovery_enabled: bool,
    pub new_source_cap: u32,
    pub provider_route_id: Option<String>,
}

/// Browser-only durable and not-yet-consumed action approval.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CommittedActionApprovalFacts {
    pub action_id: String,
    pub committed_revision: u64,
    pub receipt_id: String,
    pub proposal_digest: String,
    pub expires_at_monotonic_ms: u64,
    pub expires_at_utc_ms: u64,
    pub browser_session_id: String,
}

/// Content-free durable metadata for one generated artifact.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskArtifactFacts {
    pub artifact_id: String,
    pub kind: task_engine::ArtifactKind,
    pub workspace_revision: u64,
    pub accepted: bool,
}

/// One thing the task did, in the order it did it (decision 0148).
///
/// A closed kind, an optional host, a count and a time — and nothing else. What
/// the step *says* is composed on the surface from a compiled-in template, so
/// no sentence and no page text ever crosses this port.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskActivityFacts {
    /// Which step this is over the task's whole life, counted from one. It does
    /// not restart when the bounded record drops from the front.
    pub sequence: u64,
    pub kind: task_engine::TaskActivityKind,
    /// The host it happened to, where the kind is about a page. A host and
    /// never a full address.
    pub host: Option<String>,
    /// How many, where the kind counts something; nought where it does not.
    pub count: u32,
    pub at_epoch_ms: u64,
}

/// Complete immutable task facts needed by the generated Core API projection.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TaskViewFacts {
    pub task_id: String,
    pub revision: u64,
    pub state: TaskState,
    pub execution_phase: Option<task_engine::ExecutionPhase>,
    pub goal: String,
    pub template_id: TaskTemplateId,
    pub workspace_id: Option<String>,
    pub plan_progress: Option<PlanProgressFacts>,
    pub terminal_failure: Option<FailureReason>,
    /// Why the task is paused, while it is.
    pub pause_cause: Option<task_engine::PauseCause>,
    /// The paid attempt in flight beyond the first call, when there is one.
    pub model_attempt: Option<task_engine::ModelAttemptKind>,
    /// The last move was refused and nothing has been verified since.
    pub last_move_refused: bool,
    /// The turn in flight re-asks a question whose answer could not be used.
    pub reply_being_reasked: bool,
    pub pending_action: Option<PendingActionFacts>,
    pub pending_permission: Option<PendingPermissionFacts>,
    pub accepted_consent: Option<AcceptedTaskConsentFacts>,
    pub committed_action_approvals: Vec<CommittedActionApprovalFacts>,
    pub outcome_unknown_actions: u32,
    /// Generated artifacts in the reducer's stable identity order. File bytes
    /// are reproducible from these facts and never cross this port.
    pub artifacts: Vec<TaskArtifactFacts>,
    /// What the task did, oldest first, bounded by
    /// [`task_engine::MAX_TASK_ACTIVITY`] (decision 0148). Rebuilt by replay
    /// like every other task fact, so a restored task keeps its timeline.
    pub activity: Vec<TaskActivityFacts>,
    /// Ordered controls accepted as real state changes by the reducer now.
    pub allowed_controls: Vec<task_engine::TaskControlKind>,
    /// The open handover, when the wait is the person acting on the page.
    ///
    /// `WAITING_USER` is also how an approval or a permission wait is shown.
    /// This is the fact that tells a handover apart from those, and the one a
    /// "Waiting for you" surface reads so it does not ask for an OK the person
    /// is not being asked for. A pending permission is a separate wait: it
    /// must not share the ask-for-a-value line.
    pub pending_handover: Option<String>,
    /// The open request to fill a form in, when the wait is that (decision
    /// 0088).
    ///
    /// The fourth thing `WAITING_USER` can mean, and the one that reads most
    /// like the others while asking for something different: an approval wants
    /// a yes, a permission wants a platform grant, a handover wants the person
    /// to work the page themselves, and this wants them to type values Taffy
    /// will carry.
    ///
    /// It is an identity and nothing else. What the person is actually shown —
    /// which fields, of what kind, over which part of the page — is composed in
    /// the browser from the classification it re-reads there, and travels to
    /// the surface on a browser-process interface rather than in this status.
    /// That is deliberate: a status payload is projected and journalled, and
    /// what a challenge looks like is exactly the thing neither may hold.
    pub pending_field_values: Option<String>,
}

/// A reducer invariant required for a truthful UI projection did not hold.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TaskViewFactsError {
    CountOverflow,
    MissingPendingAction,
    PendingActionNotWaiting,
    PendingActionOutsideConsent,
    PendingActionCountMismatch,
    InvalidProposalDigest,
    PendingPermissionOutsideWait,
    PendingHandoverOutsideWait,
    PendingFieldValuesOutsideWait,
    MissingTerminalFailure,
    UnexpectedTerminalFailure,
    MissingAcceptedConsent,
    InvalidAcceptedConsent,
    InvalidCommittedApproval,
}

impl PortError {
    /// A compiled-in diagnostic label.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Unavailable => "unavailable",
            Self::Conflict => "conflict",
            Self::InvalidInput => "invalid_input",
            Self::Rejected => "rejected",
        }
    }
}

/// The immutable task facts one model turn is composed from.
///
/// Owned rather than borrowed because it crosses out of the reducer, and it
/// lives on the port rather than beside the composer so that the reducer half
/// of the seam does not depend on the production half. The transcript is the
/// one field carrying something a person wrote — it holds the goal, and the
/// shape of every turn since; the rest is a route identity, a name list and a
/// milestone.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ModelTurnFacts {
    /// Which task.
    pub task_id: String,
    /// Number of paid attempts durably started for the in-flight logical turn.
    pub attempts_started: u32,
    /// Candidate selected by its latest durably started attempt.
    pub candidate_ordinal: u32,
    /// Whether both the task-wide request budget and per-step retry bound
    /// admit one more attempt.
    pub can_afford_model_attempt: bool,
    /// The conversation so far, goal first, already cut to its bound.
    ///
    /// It replaces a bare goal field rather than sitting beside one. Two
    /// copies of the goal in one record is two things to keep in step, and the
    /// one that would go stale is whichever the composer stopped reading.
    pub transcript: crate::context::TaskTranscript,
    /// The route the task disclosed, exactly as it was consented to.
    pub provider_route_id: Option<String>,
    /// The names this task may propose, before any per-action check.
    pub tool_allowlist: Vec<String>,
    /// The milestone whose tool surface the task resolves names against.
    pub milestone: task_engine::Milestone,
    /// The reviewed workflow the task was started as. It decides which tool
    /// rows are offered before any activation and which opening the model
    /// reads.
    pub template_id: task_engine::TaskTemplateId,
    /// Sites the task holds so far, consented or discovered.
    pub source_count: usize,
    /// New sites the task may still discover. Zero for a research template
    /// whose sources were all consented up front.
    pub remaining_new_source_cap: u32,
    /// The exact task-owned tab a turn names when it has no page projection
    /// to derive one from.
    ///
    /// A turn is composed with no current page in two situations, and both
    /// need this: a zero-source discovery turn, which has read nothing yet,
    /// and a turn whose tab has just left every source it holds, which retires
    /// the page bytes that were there. Absent is only honest when the task
    /// owns no tab at all. Naming nothing is not: every call that designates
    /// no node takes this tab into its canonical intent, an empty identifier
    /// is refused by that encoding, and the refusal is not a refusal of the
    /// call but of the walk — the task then has no next command at all
    /// (decision 0178).
    pub empty_page_tab_id: Option<String>,
    /// The tab the browser opened for this task, for as long as the task
    /// lives.
    ///
    /// Where a **search** acts, and the same answer whether or not the task is
    /// currently reading a page. Distinct from [`Self::empty_page_tab_id`],
    /// which answers where a turn with **no page** acts and falls back here
    /// only when the task holds no source tab.
    ///
    /// Present for longer than the discovery authority that opened it. The
    /// authority is spent by the first landing; the tab goes on being the
    /// task's own, and every later search belongs in it exactly as the first
    /// did — a search routed anywhere else moves a tab the task does not own
    /// off the origin its consent named (decision 0224).
    pub discovery_tab_id: Option<String>,
    /// The tabs of the pages the person attached, while the task has a tab of
    /// its own to act in instead; empty otherwise.
    ///
    /// What the turn is told it may only read, and the same set the reducer
    /// refuses every other call on, because both are read from
    /// `Reducer::persons_pages` (decision 0237). Only ever compared, never
    /// put into an intent, so it needs none of the shape checks the two tab
    /// fields above get.
    pub persons_pages: task_engine::PersonsPages,
    /// Loop-local tool names this process has activated for the task.
    ///
    /// Live only. A restart loses them the same way it loses tool arguments,
    /// and the model searches again.
    pub activated: Vec<&'static str>,
    /// Nested-pass goal from `run.spawn`, when this process still holds it.
    pub nested_goal: Option<String>,
    /// The thinking rung the person asked this task's provider for.
    ///
    /// Absent means nobody chose and Taffy decides. A rung the chosen model
    /// does not offer is not refused here: the router clamps every request
    /// against that model's own ladder, so this field states a wish rather
    /// than a level the call will be sent at.
    pub thinking: Option<model_router::ThinkingLevel>,
}
