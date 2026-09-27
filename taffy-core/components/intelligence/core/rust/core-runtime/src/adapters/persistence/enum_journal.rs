// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed command, event, and effect enumeration projections.

use core_service_types as wire;
use task_engine::{CommandKind, EventKind, RecoveryRule, RevocationReason};

use super::ConversionError;

macro_rules! closed_pair {
    ($encode:ident, $decode:ident, $source:ty, $target:ty, $($variant:ident),+ $(,)?) => {
        pub(super) const fn $encode(value: $source) -> $target {
            match value { $(<$source>::$variant => <$target>::$variant),+ }
        }
        pub(super) const fn $decode(value: $target) -> $source {
            match value { $(<$target>::$variant => <$source>::$variant),+ }
        }
    };
}

/// The command vocabulary, closed on both sides.
///
/// The persisted layout carried `RequestModelTurn`, `RecordModelTurn` and
/// `RecordModelTurnGap` from transaction version 7, before any reducer could
/// produce them, and the decode direction refused all three rather than
/// restoring a paid model call as the nearest command that did exist. The
/// reducer that emits them has brought its arms with it, so the window is
/// closed and the pair is total again — and `uncommand_kind` keeps its
/// `Result` because callers depend on the shape, not because any member can
/// still fail.
macro_rules! closed_command_pair {
    (
        $encode:ident, $decode:ident, $source:ty, $target:ty,
        [$($variant:ident),+ $(,)?] $(,)?
    ) => {
        pub(super) const fn $encode(value: $source) -> $target {
            match value { $(<$source>::$variant => <$target>::$variant),+ }
        }
        pub(super) const fn $decode(value: $target) -> Result<$source, ConversionError> {
            match value {
                $(<$target>::$variant => Ok(<$source>::$variant),)+
            }
        }
    };
}

closed_command_pair!(
    command_kind,
    uncommand_kind,
    CommandKind,
    wire::PersistedCommandKind,
    [
        CreateTask,
        EditScope,
        StartTask,
        AcceptInitialConsent,
        RecordDiscoveryTab,
        ApproveAction,
        DenyAction,
        PauseTask,
        TakeOver,
        PauseSettled,
        ResumeTask,
        CancelTask,
        CancelSettled,
        ExecutorStarted,
        SetPlan,
        AdvanceStep,
        RequestApproval,
        RequestUserInput,
        SupplyUserInput,
        ProposeAction,
        RecordPolicyDecision,
        DispatchAction,
        RecordActionOutcome,
        ResultCandidateReady,
        CompleteResultValidated,
        PartialResultValidated,
        ResumeForCorrection,
        FailTask,
        CorrectFact,
        ExcludeSource,
        RequestArtifact,
        AcceptArtifact,
        ExportArtifact,
        RequestPermission,
        RecordPermissionResult,
        RequestModelTurn,
        RequestModelAttempt,
        RecordModelTurn,
        RecordModelTurnGap,
        RecordContextEviction,
        RecordToolJobOutcome,
        RequestHandover,
        RequestFieldValues,
        SupplyFieldValues,
        CompleteHandover,
        ExpireHandover,
        FollowUp,
    ],
);
closed_pair!(
    event_kind,
    unevent_kind,
    EventKind,
    wire::PersistedEventKind,
    TaskCreated,
    SourceScopeSet,
    ProviderRouteSelected,
    ConsentRequested,
    TaskQueued,
    DiscoveryTabPrepared,
    PlanCreated,
    PlanSuperseded,
    PlanStepAdvanced,
    TaskStarted,
    ActionProposed,
    ApprovalRequested,
    ApprovalDecided,
    CapabilityIssued,
    ActionRejected,
    ActionDispatchStarted,
    ActionVerificationCompleted,
    TaskWaitingUser,
    UserInputSupplied,
    UserTookOver,
    TaskPausing,
    TaskPaused,
    TaskResumed,
    TaskCancelling,
    TaskCancelled,
    TaskCompleting,
    TaskCompleted,
    TaskPartial,
    TaskFailed,
    FactCorrected,
    SourceExcluded,
    ArtifactReady,
    ArtifactAccepted,
    ArtifactExported,
    BudgetCharged,
    PermissionRequested,
    PermissionDecided,
    HandoverRequested,
    HandoverCompleted,
    HandoverExpired,
    ModelTurnRecorded,
    ModelTurnGapRecorded,
    ContextEvicted,
    FollowUpAsked
);
closed_pair!(
    revocation,
    unrevocation,
    RevocationReason,
    wire::PersistedRevocationReason,
    UserTookOver,
    DirectUserInput,
    TaskCancelled,
    PolicyRevoked,
    TabClosed
);
closed_pair!(
    recovery,
    unrecovery,
    RecoveryRule,
    wire::PersistedRecoveryRule,
    RetryWithinEpochAndBudget,
    RetryAfterStateCheck,
    ReconcileFirst,
    NeverAutomatically
);
