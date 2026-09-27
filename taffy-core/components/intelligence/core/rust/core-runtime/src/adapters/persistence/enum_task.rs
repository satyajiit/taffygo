// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed task, plan, and budget enumeration projections.

use core_service_types as wire;
use task_engine::{
    ArtifactKind, BudgetKind, ControlMode, FailureReason, GapReason, Milestone, PauseCause,
    StateReason, StepKind, StepState, TaskState, TaskTemplateId,
};

pub(super) const fn budget(value: BudgetKind) -> wire::PersistedBudgetKind {
    match value {
        BudgetKind::MaxSources => wire::PersistedBudgetKind::MaxSources,
        BudgetKind::MaxWorkingTabs => wire::PersistedBudgetKind::MaxWorkingTabs,
        BudgetKind::MaxWallTimeMillis => wire::PersistedBudgetKind::MaxWallTimeMillis,
        BudgetKind::MaxModelRequests => wire::PersistedBudgetKind::MaxModelRequests,
        BudgetKind::MaxInputTokensOrBytes => wire::PersistedBudgetKind::MaxInputTokensOrBytes,
        BudgetKind::MaxOutputTokensOrBytes => wire::PersistedBudgetKind::MaxOutputTokensOrBytes,
        BudgetKind::MaxCost => wire::PersistedBudgetKind::MaxCost,
        BudgetKind::MaxNavigationDepth => wire::PersistedBudgetKind::MaxNavigationDepth,
        BudgetKind::MaxRetriesPerStep => wire::PersistedBudgetKind::MaxRetriesPerStep,
        BudgetKind::MaxArtifactBytes => wire::PersistedBudgetKind::MaxArtifactBytes,
    }
}

pub(super) const fn unbudget(value: wire::PersistedBudgetKind) -> BudgetKind {
    match value {
        wire::PersistedBudgetKind::MaxSources => BudgetKind::MaxSources,
        wire::PersistedBudgetKind::MaxWorkingTabs => BudgetKind::MaxWorkingTabs,
        wire::PersistedBudgetKind::MaxWallTimeMillis => BudgetKind::MaxWallTimeMillis,
        wire::PersistedBudgetKind::MaxModelRequests => BudgetKind::MaxModelRequests,
        wire::PersistedBudgetKind::MaxInputTokensOrBytes => BudgetKind::MaxInputTokensOrBytes,
        wire::PersistedBudgetKind::MaxOutputTokensOrBytes => BudgetKind::MaxOutputTokensOrBytes,
        wire::PersistedBudgetKind::MaxCost => BudgetKind::MaxCost,
        wire::PersistedBudgetKind::MaxNavigationDepth => BudgetKind::MaxNavigationDepth,
        wire::PersistedBudgetKind::MaxRetriesPerStep => BudgetKind::MaxRetriesPerStep,
        wire::PersistedBudgetKind::MaxArtifactBytes => BudgetKind::MaxArtifactBytes,
    }
}

pub(super) const fn control(value: ControlMode) -> wire::PersistedControlMode {
    match value {
        ControlMode::User => wire::PersistedControlMode::User,
        ControlMode::Shared => wire::PersistedControlMode::Shared,
        ControlMode::Assistant => wire::PersistedControlMode::Assistant,
    }
}

pub(super) const fn uncontrol(value: wire::PersistedControlMode) -> ControlMode {
    match value {
        wire::PersistedControlMode::User => ControlMode::User,
        wire::PersistedControlMode::Shared => ControlMode::Shared,
        wire::PersistedControlMode::Assistant => ControlMode::Assistant,
    }
}

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

closed_pair!(
    template,
    untemplate,
    TaskTemplateId,
    wire::PersistedTaskTemplateId,
    CompareProducts,
    SummarizeEvidence,
    BuildSourceTable,
    WebErrand
);
closed_pair!(
    milestone,
    unmilestone,
    Milestone,
    wire::PersistedMilestone,
    M0,
    M1,
    M2,
    M3,
    M4,
    M5,
    M6,
    M7,
    M8
);
closed_pair!(
    pause,
    unpause,
    PauseCause,
    wire::PersistedPauseCause,
    User,
    BackgroundRestricted,
    ProviderLimit,
    ProviderBusy,
    Offline,
    NoAnswer
);
closed_pair!(
    step_kind,
    unstep_kind,
    StepKind,
    wire::PersistedStepKind,
    Observe,
    Navigate,
    Extract,
    Infer,
    AskUser,
    Export
);
closed_pair!(
    step_state,
    unstep_state,
    StepState,
    wire::PersistedStepState,
    Pending,
    Ready,
    Running,
    Waiting,
    Succeeded,
    Skipped,
    Failed,
    Cancelled,
    Superseded
);
closed_pair!(
    task_state,
    untask_state,
    TaskState,
    wire::PersistedTaskState,
    Draft,
    AwaitingConsent,
    Queued,
    Running,
    WaitingUser,
    Pausing,
    Paused,
    Cancelling,
    Completing,
    Cancelled,
    Completed,
    Partial,
    Failed
);
closed_pair!(
    state_reason,
    unstate_reason,
    StateReason,
    wire::PersistedStateReason,
    PreviewReady,
    ScopeEdited,
    InitialConsentAccepted,
    ExecutorStarted,
    ScopeExpansionNeedsConsent,
    ApprovalAccepted,
    ApprovalDenied,
    UserInputNeeded,
    UserInputSupplied,
    UserPaused,
    UserTookOver,
    BackgroundRestricted,
    SettlingComplete,
    ResumeRequested,
    StopRequested,
    Discarded,
    ResultCandidateReady,
    ResultValidated,
    ResultHasGaps,
    CorrectionRequested,
    TerminalFailure,
    PermissionRequested,
    PermissionDecided,
    HandoverRequested,
    HandoverCompleted,
    HandoverExpired,
    ProviderPaused,
    FollowUpAsked
);
closed_pair!(
    failure,
    unfailure,
    FailureReason,
    wire::PersistedFailureReason,
    BudgetExhausted,
    ProviderUnavailable,
    SourcesUnavailable,
    UnverifiableAction,
    JournalUnusable,
    DeadlineExceeded,
    ProviderRefused,
    ProviderLimit,
    Offline,
    PolicyRefused
);
closed_pair!(
    artifact,
    unartifact,
    ArtifactKind,
    wire::PersistedArtifactKind,
    Markdown,
    Csv,
    Xlsx,
    Pdf,
    Docx,
    Pptx,
    WaveAudio,
    FrameArchive
);
closed_pair!(
    gap,
    ungap,
    GapReason,
    wire::PersistedGapReason,
    NotFoundInScope,
    ConflictUnresolved,
    BudgetReached,
    SourceUnavailable,
    StoppedByUser
);
