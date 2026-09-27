// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Everything a task needs at creation.
//!
//! Its own module because it is also everything a *rebuild* needs before it
//! can replay: `Reducer::create` and `Reducer::replay` take the same seed, and
//! that is the reason a rebuild can reach the same state without a second
//! implementation of what a task starts as.

use bip_types::identity::{MonotonicMillis, ProfileId, TaskId};

use crate::authority::ControlMode;
use crate::budget::TaskBudgets;
use crate::records::WorkspaceId;
use crate::task::{TaskKind, TaskSnapshot};
use crate::time::UtcMillis;
/// Everything a task needs at creation, which is also everything a rebuild
/// needs before it can replay.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskSeed {
    /// Identity.
    pub task_id: TaskId,
    /// The workspace it saves into, when it has one.
    pub workspace_id: Option<WorkspaceId>,
    /// The browser profile it belongs to.
    pub browser_profile_id: ProfileId,
    /// What kind of work it is.
    pub kind: TaskKind,
    /// What the user asked for.
    pub user_goal: String,
    /// Who is acting in the tab.
    pub control_mode: ControlMode,
    /// The configuration it freezes.
    pub snapshot: TaskSnapshot,
    /// The limits it states for itself.
    pub budgets: TaskBudgets,
    /// Its monotonic deadline, when it has one.
    pub deadline: Option<MonotonicMillis>,
    /// Absolute deadline retained for old-boot replay refusal.
    pub deadline_utc: Option<UtcMillis>,
    /// The task it reruns, when it reruns one.
    pub predecessor_task_id: Option<TaskId>,
}
