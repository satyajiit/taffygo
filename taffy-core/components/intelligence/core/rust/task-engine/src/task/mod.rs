// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The task aggregate and its state machine (domain model sections 9.1 to 9.4).
//!
//! # Two taxonomies, one direction
//!
//! [`TaskState`] is the internal, durable taxonomy — thirteen states, exactly
//! the ones section 9.2 defines. [`DisplayState`] is the seven states a person
//! sees. The mapping runs one way, through [`TaskState::display`], and the
//! internal name is never shown: `PAUSING` and `PAUSED` are both "Paused",
//! `CANCELLING` and `CANCELLED` are both "Stopped", and a task in setup or
//! consent has no task state at all yet.
//!
//! # Transitions are committed in one place
//!
//! Nothing here changes a state. [`crate::reducer::Reducer`] is the only thing
//! that commits a transition, it does so against an expected revision, and
//! every transition it commits carries a cause event and a trace identifier.

mod activity;
mod display;
mod outcome;
mod scope;
mod snapshot;
mod state;

pub use self::activity::{
    host_of_origin, TaskActivity, TaskActivityKind, TaskActivityStep, MAX_TASK_ACTIVITY,
};
pub use self::display::DisplayState;
pub use self::outcome::{FailureReason, GapReason, TaskResult, UnmetRequirement};
pub use self::scope::SourceScope;
pub use self::snapshot::{
    BrowserSessionId, BrowserSessionIdError, BuiltinSkillId, BuiltinSkillReference,
    ConsentedSource, LibraryRefreshContext, LibraryRefreshSource, ScopePreview, TaskSnapshot,
    TaskTemplateId, MAX_BROWSER_SESSION_ID_BYTES, MAX_LIBRARY_REFRESH_LOCATOR_BYTES,
    MAX_LIBRARY_REFRESH_SOURCES, MAX_WEB_ERRAND_NEW_SOURCE_CAP,
};
pub use self::state::{ConsentStage, ExecutionPhase, StateReason, TaskState};

use bip_types::identity::{MonotonicMillis, ProfileId, TaskId};

use crate::authority::ControlMode;
use crate::budget::{BudgetLedger, TaskBudgets};
use crate::ids::ArtifactId;
use crate::records::WorkspaceId;
use crate::time::UtcMillis;
/// What kind of work the task is.
///
/// Two members. Domain model section 9.1 says other kinds arrive only when
/// they are explicitly approved, so the enumeration grows by decision, not by
/// a caller passing a string. The second arrived with decision 0136.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum TaskKind {
    /// Read-oriented research: the task ends on an answer built from facts.
    #[default]
    Research,
    /// An errand on a site: the task ends on an outcome the browser verified,
    /// and a reply with no facts behind it is complete rather than partial.
    Errand,
}

impl TaskKind {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Research => "research",
            Self::Errand => "errand",
        }
    }
}

/// The task aggregate (domain model section 9.1).
///
/// Every field a transition depends on lives here, and nothing here changes
/// except through [`crate::reducer::Reducer`].
#[allow(clippy::struct_field_names)]
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Task {
    pub(crate) task_id: TaskId,
    pub(crate) workspace_id: Option<WorkspaceId>,
    pub(crate) browser_profile_id: ProfileId,
    pub(crate) kind: TaskKind,
    pub(crate) state: TaskState,
    pub(crate) execution_phase: Option<ExecutionPhase>,
    pub(crate) state_reason: Option<StateReason>,
    pub(crate) consent_stage: Option<ConsentStage>,
    pub(crate) revision: u64,
    pub(crate) user_goal: String,
    pub(crate) control_mode: ControlMode,
    pub(crate) snapshot: TaskSnapshot,
    pub(crate) scope: SourceScope,
    /// Durable source authority accepted for this task. The snapshot keeps the
    /// initial consent immutable; this set may grow only through a journalled
    /// discovery terminal and is therefore rebuilt by replay.
    pub(crate) consented_sources: Vec<ConsentedSource>,
    pub(crate) budgets: TaskBudgets,
    pub(crate) ledger: BudgetLedger,
    pub(crate) terminal_result: Option<TaskResult>,
    pub(crate) terminal_failure: Option<FailureReason>,
    pub(crate) accepted_artifacts: Vec<ArtifactId>,
    /// What the task did, in order, bounded at [`MAX_TASK_ACTIVITY`]. Appended
    /// in the reducer handlers that commit the transitions it is about, so a
    /// replayed task has the steps the task that ran had (decision 0148).
    pub(crate) activity: TaskActivity,
    pub(crate) created_at: UtcMillis,
    pub(crate) updated_at: UtcMillis,
    pub(crate) deadline: Option<MonotonicMillis>,
    pub(crate) deadline_utc: Option<UtcMillis>,
    pub(crate) predecessor_task_id: Option<TaskId>,
}

impl Task {
    /// Identity.
    pub const fn task_id(&self) -> &TaskId {
        &self.task_id
    }

    /// The workspace the task saves into, when it has one.
    pub const fn workspace_id(&self) -> Option<&WorkspaceId> {
        self.workspace_id.as_ref()
    }

    /// The browser profile the task belongs to.
    pub const fn browser_profile_id(&self) -> &ProfileId {
        &self.browser_profile_id
    }

    /// What kind of work it is.
    pub const fn kind(&self) -> TaskKind {
        self.kind
    }

    /// The durable state.
    pub const fn state(&self) -> TaskState {
        self.state
    }

    /// What a person sees, or `None` while the task is still in setup and
    /// consent.
    pub const fn display_state(&self) -> Option<DisplayState> {
        self.state.display()
    }

    /// What the runtime is doing, while it is running.
    pub const fn execution_phase(&self) -> Option<ExecutionPhase> {
        self.execution_phase
    }

    /// Why the task is in the state it is in.
    pub const fn state_reason(&self) -> Option<StateReason> {
        self.state_reason
    }

    /// Which consent the task is waiting on, while it is waiting on one.
    pub const fn consent_stage(&self) -> Option<ConsentStage> {
        self.consent_stage
    }

    /// The aggregate revision every command is checked against.
    pub const fn revision(&self) -> u64 {
        self.revision
    }

    /// What the user asked for.
    pub fn user_goal(&self) -> &str {
        &self.user_goal
    }

    /// Who is acting in the tab.
    pub const fn control_mode(&self) -> ControlMode {
        self.control_mode
    }

    /// The configuration the task froze.
    pub const fn snapshot(&self) -> &TaskSnapshot {
        &self.snapshot
    }

    /// The sources in and out of scope.
    pub const fn scope(&self) -> &SourceScope {
        &self.scope
    }

    /// Exact browser-issued sources currently accepted for this task.
    pub fn consented_sources(&self) -> &[ConsentedSource] {
        &self.consented_sources
    }

    /// What the task did, in order, oldest first (decision 0148).
    pub const fn activity(&self) -> &TaskActivity {
        &self.activity
    }

    /// The limits the task stated for itself.
    pub const fn budgets(&self) -> &TaskBudgets {
        &self.budgets
    }

    /// What the task has spent.
    pub const fn ledger(&self) -> &BudgetLedger {
        &self.ledger
    }

    /// The validated result, once there is one.
    pub const fn terminal_result(&self) -> Option<&TaskResult> {
        self.terminal_result.as_ref()
    }

    /// The closed reason a failed task ended, when it failed.
    pub const fn terminal_failure(&self) -> Option<FailureReason> {
        self.terminal_failure
    }

    /// The artifacts the user accepted.
    pub fn accepted_artifacts(&self) -> &[ArtifactId] {
        &self.accepted_artifacts
    }

    /// When the task was created.
    pub const fn created_at(&self) -> UtcMillis {
        self.created_at
    }

    /// When it last changed.
    pub const fn updated_at(&self) -> UtcMillis {
        self.updated_at
    }

    /// The monotonic deadline, when it has one.
    pub const fn deadline(&self) -> Option<MonotonicMillis> {
        self.deadline
    }

    /// Absolute task deadline used only to refuse expired restored work.
    pub const fn deadline_utc(&self) -> Option<UtcMillis> {
        self.deadline_utc
    }

    /// The task this one reruns, when it reruns one.
    pub const fn predecessor_task_id(&self) -> Option<&TaskId> {
        self.predecessor_task_id.as_ref()
    }

    /// Whether the task has ended.
    pub const fn is_terminal(&self) -> bool {
        self.state.is_terminal()
    }
}
