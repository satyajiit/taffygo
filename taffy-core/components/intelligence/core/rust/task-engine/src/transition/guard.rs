// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The conditions a cell of the transition table depends on.
//!
//! Guards are the part of a decision that depends on the task's *data* rather
//! than on its state. They live apart from the table because they are the
//! table's vocabulary, not its content: adding a guard is a change to what the
//! reducer may consult, and it is meant to be visible as one.

use super::refusal::RefusalReason;
/// A condition the reducer checks before it commits a cell's transition.
///
/// Guards are the part of a decision that depends on the task's data rather
/// than on its state. Naming them here keeps the table honest: a cell with a
/// guard is a cell whose refusal path a test has to exercise as well.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Guard {
    /// The task is waiting on the initial consent, not on an in-task approval.
    InitialConsentPending,
    /// The task is waiting on an in-task approval.
    InTaskApprovalPending,
    /// The approval still describes the current world.
    ApprovalStillCurrent,
    /// The candidate result has no unmet requirement.
    ResultIsComplete,
    /// No dispatched action is still waiting for a terminal result.
    NoActionWorkInFlight,
    /// No action has an ambiguous consequential outcome.
    NoUnresolvedActionOutcomes,
    /// The tool name is registered and the milestone has reached it.
    ToolAvailable,
    /// The proposal's idempotency key has not already been dispatched.
    ProposalNotAlreadyDispatched,
    /// Every budget the work draws on still has room.
    WithinBudget,
    /// The returned tab belongs to this exact zero-source discovery bootstrap.
    DiscoveryAuthorityMatches,
    /// The action identifier belongs to this task.
    ActionKnown,
    /// The terminal belongs to the exact in-flight attempt and any observation
    /// is a valid accepted source.
    ActionOutcomeMatches,
    /// Policy issued authority for exactly this action.
    ActionAuthorized,
    /// The step exists, is not already final, and has its required outcomes.
    StepMayMove,
    /// The artifact was accepted before it is exported.
    ArtifactAccepted,
    /// A bounded render produced this artifact and the terminal result names it.
    ArtifactReady,
    /// The plan draft's dependencies point backwards and it has steps.
    PlanIsWellFormed,
    /// A generic user-input completion cannot consume a permission prompt.
    UserInputPending,
    /// The permission result exactly matches the one pending native prompt.
    PendingPermissionMatches,
    /// The completion or expiry names the one handover that is open.
    PendingHandoverMatches,
    /// The answer names the one field-value request that is open.
    ///
    /// The same shape as [`Self::PendingHandoverMatches`] and for the same
    /// reason: an answer to a request that is no longer pending must not
    /// resume the task, or a surface the person abandoned could restart work
    /// they had stopped.
    FieldValueRequestMatches,
    /// The resumption acquired a lease identity of its own.
    ///
    /// Reusing the lease the handover revoked would leave an audit unable to
    /// separate what the assistant did before the handover from what it does
    /// after — and everything in between was the person's.
    ResumptionLeaseIsNew,
    /// This exact call has not already been refused its way to the ceiling.
    NotLoopingOnRefusals,
    /// No model call is outstanding.
    ///
    /// It guards two different things for one reason. A second turn while the
    /// first is unanswered would spend twice for one question; a settlement
    /// while a turn is unanswered would leave a paid call with nobody left to
    /// receive it. Both are "somebody is still holding a call this task paid
    /// for".
    NoModelTurnInFlight,
    /// The call identity is the one this task's next turn would derive.
    ModelCallIsNext,
    /// The completion names the exact call this task is waiting on.
    ModelTurnMatches,
    /// The eviction boundary moves forward over turns that exist.
    ///
    /// Monotone and bounded: it must name a turn later than any already
    /// evicted, and earlier than the next turn this task would start. A
    /// boundary that moved backwards would resurrect turns a past compose
    /// already dropped, and one past the newest turn would evict a
    /// conversation that has not happened.
    EvictionAdvances,
}

impl Guard {
    /// Every guard, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::InitialConsentPending,
        Self::InTaskApprovalPending,
        Self::ApprovalStillCurrent,
        Self::ResultIsComplete,
        Self::NoActionWorkInFlight,
        Self::NoUnresolvedActionOutcomes,
        Self::ToolAvailable,
        Self::ProposalNotAlreadyDispatched,
        Self::WithinBudget,
        Self::DiscoveryAuthorityMatches,
        Self::ActionKnown,
        Self::ActionOutcomeMatches,
        Self::ActionAuthorized,
        Self::StepMayMove,
        Self::ArtifactAccepted,
        Self::ArtifactReady,
        Self::PlanIsWellFormed,
        Self::UserInputPending,
        Self::PendingPermissionMatches,
        Self::PendingHandoverMatches,
        Self::FieldValueRequestMatches,
        Self::ResumptionLeaseIsNew,
        Self::NotLoopingOnRefusals,
        Self::NoModelTurnInFlight,
        Self::ModelCallIsNext,
        Self::ModelTurnMatches,
        Self::EvictionAdvances,
    ];

    /// The refusal a caller gets when this guard does not hold.
    pub const fn refusal(self) -> RefusalReason {
        match self {
            Self::InitialConsentPending => RefusalReason::NotAwaitingInitialConsent,
            Self::InTaskApprovalPending => RefusalReason::NotAwaitingInTaskApproval,
            Self::ApprovalStillCurrent => RefusalReason::ApprovalNoLongerCurrent,
            Self::ResultIsComplete => RefusalReason::ResultHasUnmetRequirements,
            Self::NoActionWorkInFlight => RefusalReason::ActionWorkStillInFlight,
            Self::NoUnresolvedActionOutcomes => RefusalReason::ActionOutcomeUnresolved,
            Self::ToolAvailable => RefusalReason::ToolUnavailable,
            Self::ProposalNotAlreadyDispatched => RefusalReason::ProposalAlreadyDispatched,
            Self::WithinBudget => RefusalReason::BudgetExhausted,
            Self::DiscoveryAuthorityMatches => RefusalReason::DiscoveryAuthorityMismatch,
            Self::ActionKnown => RefusalReason::UnknownAction,
            Self::ActionOutcomeMatches => RefusalReason::ActionOutcomeMismatch,
            Self::ActionAuthorized => RefusalReason::ActionNotAuthorized,
            Self::StepMayMove => RefusalReason::StepCannotMove,
            Self::ArtifactAccepted => RefusalReason::ArtifactNotAccepted,
            Self::ArtifactReady => RefusalReason::ArtifactNotReady,
            Self::PlanIsWellFormed => RefusalReason::PlanRejected,
            Self::UserInputPending => RefusalReason::NotWaitingForUserInput,
            Self::PendingPermissionMatches => RefusalReason::PermissionResultMismatch,
            Self::PendingHandoverMatches => RefusalReason::HandoverMismatch,
            Self::FieldValueRequestMatches => RefusalReason::FieldValueRequestMismatch,
            Self::ResumptionLeaseIsNew => RefusalReason::HandoverLeaseReused,
            Self::NotLoopingOnRefusals => RefusalReason::RepeatedRefusalsAbandoned,
            Self::NoModelTurnInFlight => RefusalReason::ModelTurnInFlight,
            Self::ModelCallIsNext => RefusalReason::ModelCallNotNext,
            Self::ModelTurnMatches => RefusalReason::ModelTurnMismatch,
            Self::EvictionAdvances => RefusalReason::EvictionOutOfRange,
        }
    }

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::InitialConsentPending => "initial_consent_pending",
            Self::InTaskApprovalPending => "in_task_approval_pending",
            Self::ApprovalStillCurrent => "approval_still_current",
            Self::ResultIsComplete => "result_is_complete",
            Self::NoActionWorkInFlight => "no_action_work_in_flight",
            Self::NoUnresolvedActionOutcomes => "no_unresolved_action_outcomes",
            Self::ToolAvailable => "tool_available",
            Self::ProposalNotAlreadyDispatched => "proposal_not_already_dispatched",
            Self::WithinBudget => "within_budget",
            Self::DiscoveryAuthorityMatches => "discovery_authority_matches",
            Self::ActionKnown => "action_known",
            Self::ActionOutcomeMatches => "action_outcome_matches",
            Self::ActionAuthorized => "action_authorized",
            Self::StepMayMove => "step_may_move",
            Self::ArtifactAccepted => "artifact_accepted",
            Self::ArtifactReady => "artifact_ready",
            Self::PlanIsWellFormed => "plan_is_well_formed",
            Self::UserInputPending => "user_input_pending",
            Self::PendingPermissionMatches => "pending_permission_matches",
            Self::PendingHandoverMatches => "pending_handover_matches",
            Self::FieldValueRequestMatches => "field_value_request_matches",
            Self::ResumptionLeaseIsNew => "resumption_lease_is_new",
            Self::NotLoopingOnRefusals => "not_looping_on_refusals",
            Self::NoModelTurnInFlight => "no_model_turn_in_flight",
            Self::ModelCallIsNext => "model_call_is_next",
            Self::ModelTurnMatches => "model_turn_matches",
            Self::EvictionAdvances => "eviction_advances",
        }
    }
}
