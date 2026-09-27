// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Read-only views of reducer state.

use std::collections::BTreeSet;

use bip_types::identity::ActionId;

use super::{Reducer, MAX_ACTIONS_PER_TASK, MAX_COMMAND_RECEIPTS};
use crate::action::ActionRecord;
use crate::artifact::ArtifactRecord;
use crate::field_values::{FieldValueRequestId, SuppliedFieldValues};
use crate::handover::HandoverId;
use crate::ids::{ArtifactId, IdSource, IdempotencyKey};
use crate::journal::TaskJournal;
use crate::permission::PermissionRequest;
use crate::plan::Plan;
use crate::task::Task;
use crate::time::Clock;
use crate::tool::RefusalLedger;

impl<C: Clock, I: IdSource> Reducer<C, I> {
    /// The task.
    pub const fn task(&self) -> &Task {
        &self.task
    }

    /// The plan the task is working from, when there is one.
    pub const fn plan(&self) -> Option<&Plan> {
        self.plan.as_ref()
    }

    /// One action.
    pub fn action(&self, action_id: &ActionId) -> Option<&ActionRecord> {
        self.actions.get(action_id.as_str())
    }

    /// Every action, in identifier order.
    pub fn actions(&self) -> impl Iterator<Item = &ActionRecord> {
        self.actions.values()
    }

    /// Whether the last move was refused and nothing has been verified since.
    pub const fn last_move_refused(&self) -> bool {
        self.last_move_refused
    }

    /// One successfully rendered artifact.
    pub fn artifact(&self, artifact_id: &ArtifactId) -> Option<&ArtifactRecord> {
        self.artifacts.get(artifact_id.as_str())
    }

    /// Every successfully rendered artifact, in identifier order.
    pub fn artifacts(&self) -> impl Iterator<Item = &ArtifactRecord> {
        self.artifacts.values()
    }

    /// The journal.
    pub const fn journal(&self) -> &TaskJournal {
        &self.journal
    }

    /// Every refusal this task has repeated, as counts.
    ///
    /// Part of the task's own state, so a rebuild re-derives it by re-applying
    /// the journalled commands rather than by restoring a snapshot of it. It
    /// holds numbers only — a fingerprint, a compiled-in result code and a
    /// count — so no page or model text travels in it (decision 0054 section
    /// 5).
    pub const fn refusals(&self) -> &RefusalLedger {
        &self.refusals
    }

    /// The action the task is waiting on an approval for.
    pub const fn pending_action(&self) -> Option<&ActionId> {
        self.pending_action.as_ref()
    }

    /// The exact native permission request awaiting a terminal platform result.
    pub const fn pending_permission(&self) -> Option<&PermissionRequest> {
        self.pending_permission.as_ref()
    }

    /// The open handover, while the page belongs to the person.
    pub const fn pending_handover(&self) -> Option<&HandoverId> {
        self.pending_handover.as_ref()
    }

    /// The one field-value request the task is waiting on, when it is waiting on one.
    pub const fn pending_field_values(&self) -> Option<&FieldValueRequestId> {
        match &self.pending_field_values {
            Some(pending) => Some(&pending.request_id),
            None => None,
        }
    }

    /// The latest content-free field-value answer available to the next turn.
    pub const fn supplied_field_values(&self) -> Option<&SuppliedFieldValues> {
        self.supplied_field_values.as_ref()
    }

    /// The idempotency keys that have been dispatched.
    pub const fn dispatched_keys(&self) -> &BTreeSet<IdempotencyKey> {
        &self.dispatched_keys
    }

    /// How many actions the task has proposed. Never exceeds the bound.
    pub fn action_count(&self) -> usize {
        debug_assert!(self.actions.len() <= MAX_ACTIONS_PER_TASK);
        self.actions.len()
    }

    /// How many command receipts are retained. Never exceeds the bound.
    pub fn receipt_count(&self) -> usize {
        debug_assert!(self.receipts.len() <= MAX_COMMAND_RECEIPTS);
        self.receipts.len()
    }
}
