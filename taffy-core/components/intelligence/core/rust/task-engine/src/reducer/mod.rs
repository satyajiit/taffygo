// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The task reducer: the only thing that commits a transition.
//!
//! # What it is
//!
//! A synchronous, deterministic fold of commands over a task. It reads no
//! clock, mints no identifier, and performs no effect of its own: time arrives
//! through an injected [`crate::time::Clock`], identifiers through an injected
//! [`crate::ids::IdSource`], and everything the world has to do comes back to
//! the caller as an ordered list of [`crate::effect::Effect`] values.
//!
//! # What it is not
//!
//! It is not an authority. Every side effect it wants is a proposal that
//! `policy-engine` has to authorize, and the decision comes back in as a
//! command. There is no path through this module that issues a capability,
//! widens a scope, or dispatches anything.
//!
//! # The five invariants it enforces
//!
//! 1. **Expected revision.** A command written against a revision the task has
//!    moved past is refused, not applied.
//! 2. **Cause and trace.** Every event names the command that caused it and
//!    carries that command's trace identifier.
//! 3. **Revoke before waiting.** Entering `PAUSING` or `CANCELLING` returns
//!    revocation as the first effect and the remote wait after it.
//! 4. **Complete means complete.** `COMPLETED` is only reachable with a result
//!    that has no unmet requirement, checked here rather than asserted by the
//!    caller.
//! 5. **Terminal tasks act no further.** A terminal task proposes nothing,
//!    dispatches nothing, and turns authority that arrives late into a
//!    cancelled action.
//!
//! # The two registers it bounds
//!
//! A reducer that only ever inserted would be a reducer whose memory, and
//! whose per-command projection cost, the *page* decides: nothing else limits
//! how many actions one task proposes or how many commands one caller sends.
//! Both registers are therefore capped, and both refuse rather than evict
//! something a consumer still needs.
//!
//! | Register | Bound | What happens at the ceiling |
//! |---|---|---|
//! | [`MAX_ACTIONS_PER_TASK`] | actions proposed by one task | [`RefusalReason::ActionRegisterFull`](crate::transition::RefusalReason::ActionRegisterFull); nothing is ever removed |
//! | [`MAX_COMMAND_RECEIPTS`] | receipts a duplicate could still reach | receipts below either floor are dropped first, then [`RefusalReason::ReceiptRegisterFull`](crate::transition::RefusalReason::ReceiptRegisterFull) |
//!
//! The asymmetry is the whole design. An action record is read by consumers
//! that have no other source for the fact — the duplicate check, the replay
//! verdict, the plan's verified set, the workflow's attempt count — and the
//! identifiers are opaque, so there is no ordinal floor that could stand in
//! for a record that was thrown away. A command receipt has two:
//!
//! - `revision` only ever increases, and [`Reducer::apply`] refuses a command
//!   written against a revision the task has moved past *before* it consults a
//!   receipt, so a receipt below that revision is unreachable;
//! - a command that moved no state, journalled no event and returned no effect
//!   has nothing a second delivery could do twice.
//!
//! Dropping a receipt on either ground costs nothing a caller could observe as
//! a repeated effect. [`Reducer::make_room_for_receipt`] says why both are
//! needed.
//!
//! # How this module is laid out
//!
//! The fold is here. Everything a command *does* lives in one of four
//! sibling modules, one per command family, and [`Reducer::execute`] is the
//! exhaustive match that dispatches to them. The match stays in one place on
//! purpose: it is the proof that every command has exactly one handler, and
//! adding a command is a compile error here until it has one.
//!
//! | Module | Owns |
//! |---|---|
//! | [`lifecycle`] | Consent, control, and settlement |
//! | [`planning`] | Plan revisions and step movement |
//! | [`actions`] | Proposal, policy decision, dispatch, outcome |
//! | [`model`] | Asking the model, and recording what came back or did not |
//! | [`results`] | Terminal results, corrections, artifacts |
//! | [`guards`] | Whether a guard the table names holds |
//! | [`replay`] | Rebuilding from the journal |

mod access;
mod actions;
mod activity;
mod apply;
mod errand;
pub use self::errand::{
    FRUITLESS_ERRAND_ARRIVAL_NUDGE, MAX_FRUITLESS_ERRAND_ARRIVALS, MAX_TURNS_ATTEMPTING_NOTHING,
    MAX_UNANSWERED_VALUE_ASKS, MAX_UNPRODUCTIVE_ERRAND_REPLIES, UNANSWERED_VALUE_ASK_NUDGE,
};
pub use self::model::MAX_CONSECUTIVE_REASKS;
pub use self::progress::{MAX_TURNS_WITHOUT_PROGRESS, TURNS_WITHOUT_PROGRESS_NUDGE};
mod guards;
mod lifecycle;
mod model;
mod outcome;
mod planning;
mod progress;
mod replay;
mod results;
mod seed;
mod verdict;

pub use self::seed::TaskSeed;
pub use self::verdict::{Accepted, HistoryAdmission, Recovery, Refusal, ReplayError};

use std::collections::{BTreeMap, BTreeSet};

use bip_types::identity::{ActionId, TabId};

use crate::action::ActionRecord;
use crate::agent::ModelTurn;
use crate::artifact::ArtifactRecord;
use crate::budget::{BudgetDefaults, BudgetLedger};
use crate::command::{CommandKind, PauseCause};
use crate::event::{EventKind, TaskEvent};
use crate::field_values::{FieldValueRequestId, SuppliedFieldValues};
use crate::handover::HandoverId;
use crate::ids::{IdSource, IdempotencyKey};
use crate::journal::TaskJournal;
use crate::permission::PermissionRequest;
use crate::plan::Plan;
use crate::task::{SourceScope, Task, TaskActivity, TaskState};
use crate::time::{Clock, TraceId};
use crate::tool::RefusalLedger;

/// How many actions one task may propose.
///
/// Every action a task proposes stays in the register for the life of the
/// task, because the consumers listed in this module's header read records a
/// proposal already ended. That is a deliberate trade: exactness for a
/// ceiling, rather than eviction for a guess. The number is a ceiling and not
/// a product limit — a task that reaches it is refused with
/// [`RefusalReason::ActionRegisterFull`](crate::transition::RefusalReason::ActionRegisterFull), which is an internal failure and
/// says so, where a budget the user set says something else entirely.
pub const MAX_ACTIONS_PER_TASK: usize = 512;

/// Maximum number of deterministic artifacts one task can retain.
pub const MAX_ARTIFACTS_PER_TASK: usize = 64;

/// Maximum number of verified tool outputs whose bytes one browser task may retain.
pub const MAX_RETAINED_TOOL_ARTIFACTS_PER_TASK: usize = 4;

/// Maximum verified tool-output bytes one browser task may retain.
pub const MAX_RETAINED_TOOL_ARTIFACT_BYTES_PER_TASK: u64 = 16 * 1024 * 1024;

/// How many command receipts one task keeps.
///
/// A receipt answers a retried command with the result its first delivery
/// produced. It stops being reachable the moment the task's revision moves
/// past the revision the command was written against, because
/// [`Reducer::apply`] checks the revision before it checks anything else the
/// command asks for. The register keeps at most this many receipts that are
/// still reachable, which is a bound on the *live* duplicate window rather
/// than on the task's history.
pub const MAX_COMMAND_RECEIPTS: usize = 256;

/// A committed command's result, with the revision that makes it reachable.
///
/// The revision is stored rather than recomputed because it is the floor:
/// dropping a receipt is safe exactly when a duplicate of its command would be
/// refused by the revision check anyway, and that question cannot be answered
/// from the [`Accepted`] alone.
#[derive(Clone, Debug)]
struct CommandReceipt {
    expected_revision: u64,
    accepted: Accepted,
}

/// The one request for values a task is waiting on, and the tab it named.
///
/// The tab is held beside the identity because it is where every field the
/// sheet shows stands, and the answer that closes the request says which
/// fields and not which tab (decision 0238). Rebuilt by replaying the request.
#[derive(Clone, Debug)]
struct PendingFieldValues {
    request_id: FieldValueRequestId,
    tab_id: TabId,
}

/// The reducer.
#[derive(Clone, Debug)]
pub struct Reducer<C, I> {
    task: Task,
    plan: Option<Plan>,
    plan_revision: u32,
    actions: BTreeMap<String, ActionRecord>,
    artifacts: BTreeMap<String, ArtifactRecord>,
    retained_tool_artifact_count: usize,
    retained_tool_artifact_bytes: u64,
    dispatched_keys: BTreeSet<IdempotencyKey>,
    receipts: BTreeMap<IdempotencyKey, CommandReceipt>,
    pending_action: Option<ActionId>,
    pending_permission: Option<PermissionRequest>,
    pending_handover: Option<HandoverId>,
    pending_field_values: Option<PendingFieldValues>,
    supplied_field_values: Option<SuppliedFieldValues>,
    turn: Option<ModelTurn>,
    turns_started: u64,
    evicted_through: Option<u64>,
    unproductive_replies: u8,
    turns_attempting_nothing: u8,
    /// Whether a hand-back to the person has completed on this task.
    ///
    /// Decision 0136 section 5 names a completed handover beside a verified
    /// download and a verified form outcome as something an errand's prose
    /// reply may report. Only the download arm was ever read, and the form arm
    /// is unreachable — `browser.form.submit` is withheld from every template
    /// allowlist — so a started download was in practice the only way an
    /// errand could finish at all, and an errand that ends in a sign-in or an
    /// OTP could not finish by construction. Derived, rebuilt by replay.
    ///
    /// A count rather than a flag: an errand may hand the page back more than
    /// once, and how many times it did is the more useful fact to hold.
    handovers_completed: u8,
    /// Verified navigating moves that arrived somewhere the task already held.
    ///
    /// "It went where it had already been", counted from the one place that
    /// already knows: source discovery answers `AlreadyBound` for exactly that
    /// (decision 0054 section 5 extended from identical refusals to identical
    /// arrivals). Refusal counting cannot see this — a fingerprint is recorded
    /// only for a terminal fail-closed outcome, and a search fingerprints as
    /// its tab, so three different queries look like one repeat. Derived.
    fruitless_arrivals: u8,
    /// Asks for values in a row that came back with nothing; the loop the
    /// other three counters cannot see, argued on [`MAX_UNANSWERED_VALUE_ASKS`]
    /// (decision 0216). Advanced and reset by `SupplyFieldValues` alone, from
    /// the count and never the outcome, so a replay rebuilds it. Derived.
    unanswered_value_asks: u8,
    /// Recorded turns in a row that changed nothing, each closed by the
    /// `RequestModelTurn` that replaced it, and whether the turn now open has
    /// changed anything: the loop the four counters above cannot see because
    /// it alternates two of their shapes, argued on
    /// [`MAX_TURNS_WITHOUT_PROGRESS`] (decision 0233). Derived.
    progress: progress::ProgressRun,
    /// Why the task is paused, from the command that paused it; cleared by
    /// the resume. Derived, so a replay rebuilds it from the journal.
    pause_cause: Option<PauseCause>,
    /// The turn in flight was asked for because the one before it could not
    /// be read or was cut off. Derived; cleared when the turn settles.
    reply_being_reasked: bool,
    /// How many turns in a row were asked for only because the one before
    /// could not be read or was cut off. Advanced and reset by
    /// `RequestModelTurn` alone, so replay rebuilds it. Derived.
    consecutive_reasks: u8,
    /// The last move the task made was refused — by policy at the proposal,
    /// or by the browser at dispatch — and nothing has been verified since.
    /// Derived from the journaled decisions and outcomes.
    last_move_refused: bool,
    pub(crate) refusals: RefusalLedger,
    journal: TaskJournal,
    defaults: BudgetDefaults,
    clock: C,
    ids: I,
    replaying: bool,
}

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// Creates a task in `DRAFT` and journals its creation.
    pub fn create(
        seed: TaskSeed,
        defaults: BudgetDefaults,
        clock: C,
        ids: I,
        creation_key: IdempotencyKey,
        trace_id: TraceId,
    ) -> Self {
        let now = clock.now_utc();
        let task = Task {
            task_id: seed.task_id,
            workspace_id: seed.workspace_id,
            browser_profile_id: seed.browser_profile_id,
            kind: seed.kind,
            state: TaskState::Draft,
            execution_phase: None,
            state_reason: None,
            consent_stage: None,
            revision: 0,
            user_goal: seed.user_goal,
            control_mode: seed.control_mode,
            snapshot: seed.snapshot,
            scope: SourceScope::new(),
            consented_sources: Vec::new(),
            activity: TaskActivity::default(),
            budgets: seed.budgets,
            ledger: BudgetLedger::new(),
            terminal_result: None,
            terminal_failure: None,
            accepted_artifacts: Vec::new(),
            created_at: now,
            updated_at: now,
            deadline: seed.deadline,
            deadline_utc: seed.deadline_utc,
            predecessor_task_id: seed.predecessor_task_id,
        };
        let mut reducer = Self {
            task,
            plan: None,
            plan_revision: 0,
            actions: BTreeMap::new(),
            artifacts: BTreeMap::new(),
            retained_tool_artifact_count: 0,
            retained_tool_artifact_bytes: 0,
            dispatched_keys: BTreeSet::new(),
            receipts: BTreeMap::new(),
            pending_action: None,
            pending_permission: None,
            pending_handover: None,
            pending_field_values: None,
            supplied_field_values: None,
            turn: None,
            turns_started: 0,
            evicted_through: None,
            unproductive_replies: 0,
            turns_attempting_nothing: 0,
            handovers_completed: 0,
            fruitless_arrivals: 0,
            unanswered_value_asks: 0,
            progress: progress::ProgressRun::new(),
            pause_cause: None,
            reply_being_reasked: false,
            consecutive_reasks: 0,
            last_move_refused: false,
            refusals: RefusalLedger::new(),
            journal: TaskJournal::new(),
            defaults,
            clock,
            ids,
            replaying: false,
        };
        let event = TaskEvent {
            kind: EventKind::TaskCreated,
            caused_by: CommandKind::CreateTask,
            from: None,
            to: Some(TaskState::Draft),
            reason: None,
            subject: None,
        };
        reducer.task.revision = reducer
            .journal
            .append_event(event, trace_id, creation_key, now);
        reducer
    }
}
