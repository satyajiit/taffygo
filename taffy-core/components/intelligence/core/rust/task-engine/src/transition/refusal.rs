// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Why a command was refused.
//!
//! A closed enumeration of compiled-in reasons, in its own module so that the
//! set a person can be shown is readable in one screen and grows only by
//! deliberate addition. No page text and no model text ever reaches a refusal.
/// Why a command was refused.
///
/// A closed enumeration of compiled-in reasons, so a refusal shown to a person
/// is composed from a trusted local template and never from page or model text.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RefusalReason {
    /// The reducer already has a task; creation is its constructor.
    TaskAlreadyExists,
    /// The command was written against a different revision of the task.
    RevisionConflict,
    /// The task has ended. A rerun is a new task.
    TaskIsTerminal,
    /// The task has not been started yet.
    NotStarted,
    /// The task has already been started.
    AlreadyStarted,
    /// A consent decision is already pending.
    ConsentDecisionPending,
    /// Nothing is waiting on the user's decision.
    NotAwaitingConsent,
    /// The task is waiting on an in-task approval, not the initial consent.
    NotAwaitingInitialConsent,
    /// The task is waiting on the initial consent, not an in-task approval.
    NotAwaitingInTaskApproval,
    /// The approval no longer describes the current world.
    ApprovalNoLongerCurrent,
    /// The task is not queued.
    NotQueued,
    /// A discovery-tab completion did not match the task's exact zero-source
    /// Web-errand authority, browser session, or remaining cap.
    DiscoveryAuthorityMismatch,
    /// The task is not running.
    NotRunning,
    /// The task is not waiting for the user.
    NotWaitingForUser,
    /// The task is waiting for a different kind of user input.
    NotWaitingForUserInput,
    /// No exact pending permission request matches this result.
    PermissionResultMismatch,
    /// No open handover matches this completion or expiry.
    HandoverMismatch,
    /// The answer names a field-value request that is not the open one.
    FieldValueRequestMismatch,
    /// The resumption named the lease the handover had already revoked.
    HandoverLeaseReused,
    /// The task is not paused.
    NotPaused,
    /// There is no pause edge from this state.
    NotPausable,
    /// No actor lease can exist in this state, so there is nothing to take
    /// over.
    NoActorLeaseHere,
    /// The task is already revoking authority and settling.
    AlreadySettling,
    /// The task is not settling a pause.
    NotSettlingAPause,
    /// The task is not settling a cancellation.
    NotSettlingACancel,
    /// The task is not validating a result.
    NotCompleting,
    /// A result is being validated and cannot be changed underneath it.
    ResultIsBeingValidated,
    /// The candidate result has gaps, so it is partial and not complete.
    ResultHasUnmetRequirements,
    /// A dispatched action has not returned one terminal result yet.
    ActionWorkStillInFlight,
    /// A consequential action may have happened and is not resolved.
    ActionOutcomeUnresolved,
    /// There is no result to accept or export yet.
    NoResultToAccept,
    /// The artifact has not been accepted.
    ArtifactNotAccepted,
    /// No successful bounded render produced this artifact.
    ArtifactNotReady,
    /// This exact artifact identity already names a successful render.
    ArtifactAlreadyReady,
    /// The scope is frozen at this point in the task's life.
    ScopeIsFrozen,
    /// Nothing has produced a fact yet.
    NoFactsYet,
    /// The tool name is unregistered, owned by a later milestone, or excluded.
    ToolUnavailable,
    /// The proposal repeats a key that has already been dispatched.
    ProposalAlreadyDispatched,
    /// A budget has no room for the work.
    BudgetExhausted,
    /// The action identifier does not belong to this task.
    UnknownAction,
    /// Policy has not authorized the action.
    ActionNotAuthorized,
    /// The terminal did not match the action's exact in-flight attempt or
    /// carried invalid observation evidence.
    ActionOutcomeMismatch,
    /// The step is unknown, already final, or missing a verified outcome.
    StepCannotMove,
    /// The plan draft was rejected.
    PlanRejected,
    /// The identifier source is exhausted.
    IdSourceExhausted,
    /// The journal refused the append.
    JournalRefused,
    /// The task's action register is full. It holds every action the task ever
    /// proposed, because every later consumer — the duplicate check, the
    /// replay verdict, the plan's verified set — reads records that a proposal
    /// already ended. Refusing the proposal is what keeps that register a
    /// bounded structure rather than one a long task grows forever.
    ActionRegisterFull,
    /// The task retained the maximum number of artifact identities.
    ArtifactRegisterFull,
    /// The command receipt register is full of receipts a duplicate could
    /// still reach, so this command cannot be admitted without losing the
    /// answer another one owes its caller.
    ReceiptRegisterFull,
    /// This exact call has already been refused
    /// [`crate::tool::MAX_IDENTICAL_REFUSALS`] times, so the attempt is over.
    /// Counting is the only thing that sees a loop, because each request in one
    /// looks locally reasonable (decision 0054 section 5).
    RepeatedRefusalsAbandoned,
    /// A model call is outstanding. Asking for a second one, or settling as
    /// though nothing were in flight, would leave a paid call with nobody
    /// waiting for it.
    ModelTurnInFlight,
    /// The call identity is not the one this task's next turn would carry.
    /// Identities are derived rather than supplied so that a replay reaches
    /// the same one; a caller that invents one is refused rather than
    /// obliged.
    ModelCallNotNext,
    /// The completion does not name the call this task is waiting on.
    /// Recording it would attribute one turn's reply to another turn's
    /// request.
    ModelTurnMismatch,
    /// The eviction boundary is not later than the last one, or names a turn
    /// this task has not started. Either way the conversation it describes is
    /// not this task's.
    EvictionOutOfRange,
    /// A follow-up was asked of a task that has not finished. It is not
    /// terminal, so it is not `TaskIsTerminal`; it is still working.
    NotFinished,
}

impl RefusalReason {
    /// Every reason, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::TaskAlreadyExists,
        Self::RevisionConflict,
        Self::TaskIsTerminal,
        Self::NotStarted,
        Self::AlreadyStarted,
        Self::ConsentDecisionPending,
        Self::NotAwaitingConsent,
        Self::NotAwaitingInitialConsent,
        Self::NotAwaitingInTaskApproval,
        Self::ApprovalNoLongerCurrent,
        Self::NotQueued,
        Self::DiscoveryAuthorityMismatch,
        Self::NotRunning,
        Self::NotWaitingForUser,
        Self::NotWaitingForUserInput,
        Self::PermissionResultMismatch,
        Self::HandoverMismatch,
        Self::FieldValueRequestMismatch,
        Self::HandoverLeaseReused,
        Self::NotPaused,
        Self::NotPausable,
        Self::NoActorLeaseHere,
        Self::AlreadySettling,
        Self::NotSettlingAPause,
        Self::NotSettlingACancel,
        Self::NotCompleting,
        Self::ResultIsBeingValidated,
        Self::ResultHasUnmetRequirements,
        Self::ActionWorkStillInFlight,
        Self::ActionOutcomeUnresolved,
        Self::NoResultToAccept,
        Self::ArtifactNotAccepted,
        Self::ArtifactNotReady,
        Self::ArtifactAlreadyReady,
        Self::ScopeIsFrozen,
        Self::NoFactsYet,
        Self::ToolUnavailable,
        Self::ProposalAlreadyDispatched,
        Self::BudgetExhausted,
        Self::UnknownAction,
        Self::ActionNotAuthorized,
        Self::ActionOutcomeMismatch,
        Self::StepCannotMove,
        Self::PlanRejected,
        Self::IdSourceExhausted,
        Self::JournalRefused,
        Self::ActionRegisterFull,
        Self::ArtifactRegisterFull,
        Self::ReceiptRegisterFull,
        Self::RepeatedRefusalsAbandoned,
        Self::ModelTurnInFlight,
        Self::ModelCallNotNext,
        Self::ModelTurnMismatch,
        Self::EvictionOutOfRange,
        Self::NotFinished,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::TaskAlreadyExists => "task_already_exists",
            Self::RevisionConflict => "revision_conflict",
            Self::TaskIsTerminal => "task_is_terminal",
            Self::NotStarted => "not_started",
            Self::AlreadyStarted => "already_started",
            Self::ConsentDecisionPending => "consent_decision_pending",
            Self::NotAwaitingConsent => "not_awaiting_consent",
            Self::NotAwaitingInitialConsent => "not_awaiting_initial_consent",
            Self::NotAwaitingInTaskApproval => "not_awaiting_in_task_approval",
            Self::ApprovalNoLongerCurrent => "approval_no_longer_current",
            Self::NotQueued => "not_queued",
            Self::DiscoveryAuthorityMismatch => "discovery_authority_mismatch",
            Self::NotRunning => "not_running",
            Self::NotWaitingForUser => "not_waiting_for_user",
            Self::NotWaitingForUserInput => "not_waiting_for_user_input",
            Self::PermissionResultMismatch => "permission_result_mismatch",
            Self::HandoverMismatch => "handover_mismatch",
            Self::FieldValueRequestMismatch => "field_value_request_mismatch",
            Self::HandoverLeaseReused => "handover_lease_reused",
            Self::NotPaused => "not_paused",
            Self::NotPausable => "not_pausable",
            Self::NoActorLeaseHere => "no_actor_lease_here",
            Self::AlreadySettling => "already_settling",
            Self::NotSettlingAPause => "not_settling_a_pause",
            Self::NotSettlingACancel => "not_settling_a_cancel",
            Self::NotCompleting => "not_completing",
            Self::ResultIsBeingValidated => "result_is_being_validated",
            Self::ResultHasUnmetRequirements => "result_has_unmet_requirements",
            Self::ActionWorkStillInFlight => "action_work_still_in_flight",
            Self::ActionOutcomeUnresolved => "action_outcome_unresolved",
            Self::NoResultToAccept => "no_result_to_accept",
            Self::ArtifactNotAccepted => "artifact_not_accepted",
            Self::ArtifactNotReady => "artifact_not_ready",
            Self::ArtifactAlreadyReady => "artifact_already_ready",
            Self::ScopeIsFrozen => "scope_is_frozen",
            Self::NoFactsYet => "no_facts_yet",
            Self::ToolUnavailable => "tool_unavailable",
            Self::ProposalAlreadyDispatched => "proposal_already_dispatched",
            Self::BudgetExhausted => "budget_exhausted",
            Self::UnknownAction => "unknown_action",
            Self::ActionNotAuthorized => "action_not_authorized",
            Self::ActionOutcomeMismatch => "action_outcome_mismatch",
            Self::StepCannotMove => "step_cannot_move",
            Self::PlanRejected => "plan_rejected",
            Self::IdSourceExhausted => "id_source_exhausted",
            Self::JournalRefused => "journal_refused",
            Self::ActionRegisterFull => "action_register_full",
            Self::ArtifactRegisterFull => "artifact_register_full",
            Self::ReceiptRegisterFull => "receipt_register_full",
            Self::RepeatedRefusalsAbandoned => "repeated_refusals_abandoned",
            Self::ModelTurnInFlight => "model_turn_in_flight",
            Self::ModelCallNotNext => "model_call_not_next",
            Self::ModelTurnMismatch => "model_turn_mismatch",
            Self::EvictionOutOfRange => "eviction_out_of_range",
            Self::NotFinished => "not_finished",
        }
    }

    /// The sentence shown to a person, from a trusted local template.
    ///
    /// Exhaustive: a refusal reason added later would otherwise inherit the
    /// generic sentence silently, and the generic sentence is the one this
    /// function exists to avoid. Every reason that shares a sentence says so
    /// by naming itself in that arm.
    pub const fn user_visible_reason(self) -> &'static str {
        match self {
            Self::TaskIsTerminal => "This task has finished. Start a new one to try again.",
            Self::NotFinished => "Taffy is still working on this. Wait for it to finish first.",
            Self::ApprovalNoLongerCurrent => "The page changed, so Taffy is asking again.",
            Self::ResultHasUnmetRequirements => "Taffy could not finish every part of this.",
            Self::BudgetExhausted => "This task reached the limit you set.",
            Self::ToolUnavailable => "Taffy cannot do that yet.",
            Self::ProposalAlreadyDispatched => "Taffy already did that once.",
            Self::ScopeIsFrozen => "The sources are fixed while this task runs.",
            Self::NoResultToAccept => "There is nothing to save yet.",
            Self::ArtifactNotAccepted => "Save this result before exporting it.",
            Self::ArtifactNotReady => "There is no finished file to save yet.",
            // Its own sentence, and not an internal failure. Nothing went
            // wrong inside TaffyGo: the same request was refused three times,
            // and stopping is the correct outcome. A person reading this should
            // know the attempt ended on purpose.
            Self::RepeatedRefusalsAbandoned => {
                "Taffy tried this several times and kept being refused, so it stopped."
            }
            // One sentence, because from a person's point of view these are one
            // situation: the control they used does not apply to the task as it
            // is right now. Splitting them would name reducer states a person
            // has no way to observe.
            Self::TaskAlreadyExists
            | Self::RevisionConflict
            | Self::NotStarted
            | Self::AlreadyStarted
            | Self::ConsentDecisionPending
            | Self::NotAwaitingConsent
            | Self::NotAwaitingInitialConsent
            | Self::NotAwaitingInTaskApproval
            | Self::NotQueued
            | Self::DiscoveryAuthorityMismatch
            | Self::NotRunning
            | Self::NotWaitingForUser
            | Self::NotWaitingForUserInput
            | Self::PermissionResultMismatch
            | Self::HandoverMismatch
            | Self::FieldValueRequestMismatch
            | Self::NotPaused
            | Self::NotPausable
            | Self::NoActorLeaseHere
            | Self::AlreadySettling
            | Self::NotSettlingAPause
            | Self::NotSettlingACancel
            | Self::NotCompleting
            | Self::ResultIsBeingValidated
            | Self::ActionWorkStillInFlight
            | Self::ActionOutcomeUnresolved
            | Self::NoFactsYet
            | Self::ArtifactAlreadyReady
            // The three model-turn refusals belong in this group and not among
            // the internal failures. Each of them means the reducer was asked
            // about a call that is not the one it is holding, which from a
            // person's point of view is the same "not right now" as every
            // other member here. Naming a paid call in a sentence a person
            // reads would describe machinery they cannot see and cannot act
            // on.
            | Self::ModelTurnInFlight
            | Self::ModelCallNotNext
            | Self::ModelTurnMismatch
            | Self::EvictionOutOfRange => "Taffy cannot do that right now.",
            // The action a control referred to is gone, which reads to a
            // person as the control being stale rather than as an error.
            Self::UnknownAction => "That step is no longer part of this task.",
            Self::ActionNotAuthorized => "Taffy has not been given permission for that.",
            Self::ActionOutcomeMismatch => "That page result no longer matches this task.",
            Self::StepCannotMove => "That step cannot continue from where it is.",
            Self::PlanRejected => "Taffy could not make a workable plan for this.",
            // Five internal failures. They say so, because a sentence that
            // implied the person did something wrong would be a lie, and one
            // that said nothing would leave them with no next move. The two
            // register refusals belong here rather than beside
            // `BudgetExhausted`: a limit the person set is a fact they can act
            // on, and a compiled-in ceiling they never chose is not. A reused
            // resumption lease belongs here for the same reason and one more:
            // the person did exactly what was asked of them, and the thing
            // that went wrong is that the browser offered back the authority
            // it had already given up.
            Self::IdSourceExhausted
            | Self::JournalRefused
            | Self::ActionRegisterFull
            | Self::ArtifactRegisterFull
            | Self::HandoverLeaseReused
            | Self::ReceiptRegisterFull => {
                "Something went wrong inside TaffyGo. This task was not changed."
            }
        }
    }
}
