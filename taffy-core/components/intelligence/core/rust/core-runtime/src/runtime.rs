// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Profile-scoped deterministic composition and task lifecycle.

mod assets;
mod library;
mod lifecycle;
mod r#loop;
mod media_probe;
mod memory;
mod open;
mod recording;
mod research_result;
mod status;
mod submit;
mod task_downloads;
mod task_stores;
mod task_tabs;
mod types;
mod workspace;
mod workspace_commit;

pub use self::library::{TaskLibraryExecution, TaskLibraryExecutionError};
pub use self::memory::{TaskMemoryExecution, TaskMemoryExecutionError};

use self::library::PendingTaskLibrarySearchSettlement;
use self::media_probe::PendingMediaProbeSettlement;
use self::memory::PendingTaskMemorySearchSettlement;
pub use self::status::{
    project_account_view, AcceptedTaskConsentBindingFacts, CommittedActionApprovalBindingFacts,
    CoreStatusProjection, CoreStatusProjectionGap, PendingApprovalBindingFacts,
    PendingPermissionBindingFacts, TaskRevisionBindingFacts, TaskSettlementBindingFacts,
    TaskSettlementKind, TerminalTaskBindingFacts, TerminalTaskKind,
};
use self::task_downloads::PendingTaskDownloadSettlement;
use self::task_stores::PendingTaskStoreSettlement;
use self::task_tabs::PendingTaskTabSettlement;
pub use self::types::{
    create_service_runtime, AccountRestoreError, BeginOpenTask, BeginSubmit, BoxedCoreRuntime,
    CommitCompletionError, CommitOutcome, CoreRuntime, DisconnectError, OpenTaskCommitOutcome,
    OpenTaskCompletionError, OpenTaskError, OpenTaskOutcome, ServiceRuntimeComponents, StageError,
    SubmitError, TaskCompletionError, TaskResultSubmission, TaskTerminal, WorkspaceCommitError,
    MAX_TASK_SESSIONS_PER_PROFILE,
};
use self::types::{PendingOpenTask, PendingSubmit, TaskSession};

#[cfg(test)]
mod tests;
