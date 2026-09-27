// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Closed action and recovery-value projections.

use bip_types::ActionResultCode;
use core_service_types as wire;
use task_engine::{ActionClass, IdempotencyClass};

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
    action_class,
    unaction_class,
    ActionClass,
    wire::PersistedActionClass,
    ObservePage,
    ScrollIntoView,
    OpenLink,
    CreateTaskTab,
    SyntheticClick,
    MoveFocus,
    FillField,
    SelectOption,
    ToggleControl,
    SubmitForm,
    StartDownload,
    UploadFile,
    SendMessage,
    Purchase,
    ExtractCredential,
    BypassAccessControl,
    ExecuteToolJob,
    LibraryRead,
    LibraryWrite,
    MemoryRead,
    MemoryWrite,
    ControlTab,
    ProfileStoreRead
);
closed_pair!(
    tool_runtime,
    untool_runtime,
    task_engine::ToolRuntime,
    wire::PersistedToolRuntime,
    Media,
    Python,
    LocalModel,
    Wasm
);
closed_pair!(
    tool_job_status,
    untool_job_status,
    task_engine::ToolJobStatus,
    wire::PersistedToolJobStatus,
    Succeeded,
    Failed,
    Cancelled,
    OutcomeUnknown,
    Unavailable
);
closed_pair!(
    idempotency,
    unidempotency,
    IdempotencyClass,
    wire::PersistedIdempotencyClass,
    PureRead,
    IdempotentWrite,
    ConditionallyIdempotent,
    Consequential
);
closed_pair!(
    result_code,
    unresult_code,
    ActionResultCode,
    wire::PersistedActionResultCode,
    Verified,
    DeniedByPolicy,
    ApprovalRequired,
    ApprovalDenied,
    ActorLeaseMissing,
    CapabilityExpired,
    TabGone,
    FrameGone,
    DocumentInactive,
    StalePageEpoch,
    StaleGraph,
    NodeGone,
    OriginChanged,
    RoleOrActionChanged,
    NotVisible,
    Occluded,
    NotEnabled,
    NotEditable,
    SensitiveField,
    DestinationChanged,
    Unsupported,
    BudgetExceeded,
    DispatchFailed,
    NavigationStarted,
    PostconditionTimeout,
    PostconditionFailed,
    CancelledByUser,
    CancelledByNavigation,
    RendererCrashed,
    OutcomeUnknown,
    InternalError,
    EgressNotAuthorized,
    DestinationClassRestricted,
    UntrustedContentOrigin,
    PreparedEffectChanged,
    CommitWithoutPrepare,
    GraphMovedDuringPreflight,
    ValueReferenceUnknown
);
