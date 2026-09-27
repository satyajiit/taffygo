// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The record of every question shown and every answer given (domain model
//! section 12.5).
//!
//! The book is the broker's own record. A message can present an identifier; it
//! can never present an answer, because the answer is only ever read from here.
//!
//! One answer authorizes one action. [`ApprovalBook::consume`] runs every gate
//! — the task, the answer, the expiry, prior use, the repeat scope, the gesture
//! receipt, and the binding — and a binding that disagrees marks the approval
//! invalidated in the book, so the same answer cannot be presented again later
//! against a binding that happens to match.

use bip_types::identity::{ApprovalReceiptReference, MonotonicMillis, TaskId};
use std::collections::BTreeMap;

use crate::approval::binding::{ApprovalBinding, ApprovalInvalidation};
use crate::approval::{
    ApprovalDecision, ApprovalError, ApprovalId, RepeatScope, UserGestureReceipt,
};
use crate::time::{IdKind, IdSource};

/// How many approvals one broker session may present.
///
/// The book is what refuses a second use of one answer, so an entry it dropped
/// would be an answer that could authorize a second action. Nothing is
/// dropped; the register has a ceiling instead, and reaching it refuses to
/// present with [`ApprovalError::BookFull`].
pub const MAX_APPROVALS_PER_SESSION: usize = 4_096;

/// One question and its answer.
#[derive(Clone, Debug, PartialEq)]
pub struct Approval {
    id: ApprovalId,
    task_id: TaskId,
    binding: ApprovalBinding,
    repeat_scope: RepeatScope,
    decision: ApprovalDecision,
    invalidation: Option<ApprovalInvalidation>,
    gesture: Option<UserGestureReceipt>,
    presented_at: MonotonicMillis,
    decided_at: Option<MonotonicMillis>,
    expires_at: MonotonicMillis,
    used_at: Option<MonotonicMillis>,
}

impl Approval {
    /// The approval identifier.
    pub fn approval_id(&self) -> &ApprovalId {
        &self.id
    }

    /// The task the question belongs to.
    pub fn task_id(&self) -> &TaskId {
        &self.task_id
    }

    /// Everything the sheet showed.
    pub fn binding(&self) -> &ApprovalBinding {
        &self.binding
    }

    /// How often the answer may be used.
    pub fn repeat_scope(&self) -> RepeatScope {
        self.repeat_scope
    }

    /// Why the approval was invalidated, when it was.
    pub fn invalidation(&self) -> Option<ApprovalInvalidation> {
        self.invalidation
    }

    /// When the question was shown.
    pub fn presented_at(&self) -> MonotonicMillis {
        self.presented_at
    }

    /// When it was answered.
    pub fn decided_at(&self) -> Option<MonotonicMillis> {
        self.decided_at
    }

    /// When the answer was used.
    pub fn used_at(&self) -> Option<MonotonicMillis> {
        self.used_at
    }

    /// The answer as of `now`.
    ///
    /// Expiry is computed rather than stored, so an unanswered question never
    /// looks answerable because nobody ran a timer. An answered question keeps
    /// its answer: expiry does not overwrite a denial.
    pub fn decision_at(&self, now: MonotonicMillis) -> ApprovalDecision {
        match self.decision {
            ApprovalDecision::Pending | ApprovalDecision::Approved
                if now.0 >= self.expires_at.0 =>
            {
                ApprovalDecision::Expired
            }
            other => other,
        }
    }

    /// Whether the answer has already been used.
    pub fn is_used(&self) -> bool {
        self.used_at.is_some()
    }
}

/// Every approval this session presented.
///
/// The book is the broker's own record. A message can present an identifier; it
/// can never present an answer, because the answer is only ever read from here.
/// It holds at most [`MAX_APPROVALS_PER_SESSION`] entries and never removes
/// one.
#[derive(Clone, Debug, Default)]
pub struct ApprovalBook {
    entries: BTreeMap<ApprovalId, Approval>,
}

impl ApprovalBook {
    /// An empty book.
    pub const fn new() -> Self {
        Self {
            entries: BTreeMap::new(),
        }
    }

    /// Records that a question was shown.
    pub fn present(
        &mut self,
        request: &ApprovalRequest,
        ids: &mut impl IdSource,
        now: MonotonicMillis,
    ) -> Result<ApprovalId, ApprovalError> {
        if !request.repeat_scope.is_authorized_today() {
            return Err(ApprovalError::RepeatScopeNotAuthorized);
        }
        if request.expires_at.0 <= now.0 {
            return Err(ApprovalError::ExpiryNotInFuture);
        }
        // Before the identifier is spent, so a refused presentation leaves the
        // book and the identifier source exactly where it found them.
        if self.entries.len() >= MAX_APPROVALS_PER_SESSION {
            return Err(ApprovalError::BookFull);
        }
        let approval_id = ids
            .next_id(IdKind::Approval)
            .map(ApprovalId::new)
            .ok_or(ApprovalError::IdSourceExhausted)?;
        self.entries.insert(
            approval_id.clone(),
            Approval {
                id: approval_id.clone(),
                task_id: request.task_id.clone(),
                binding: request.binding.clone(),
                repeat_scope: request.repeat_scope,
                decision: ApprovalDecision::Pending,
                invalidation: None,
                gesture: None,
                presented_at: now,
                decided_at: None,
                expires_at: request.expires_at,
                used_at: None,
            },
        );
        Ok(approval_id)
    }

    /// Records the answer a person gave.
    ///
    /// A yes without a gesture receipt is refused here rather than at use time,
    /// so an answer that no person gave never enters the book as a yes.
    pub fn decide(
        &mut self,
        approval_id: &ApprovalId,
        decision: ApprovalDecision,
        gesture: Option<UserGestureReceipt>,
        now: MonotonicMillis,
    ) -> Result<(), ApprovalError> {
        if decision == ApprovalDecision::Approved && gesture.is_none() {
            return Err(ApprovalError::MissingUserGesture);
        }
        let approval = self
            .entries
            .get_mut(approval_id)
            .ok_or(ApprovalError::Unknown)?;
        if approval.decision.is_final() {
            return Err(ApprovalError::AlreadyDecided);
        }
        if now.0 >= approval.expires_at.0 {
            approval.decision = ApprovalDecision::Expired;
            approval.decided_at = Some(now);
            return Err(ApprovalError::Expired);
        }
        approval.decision = decision;
        approval.gesture = gesture;
        approval.decided_at = Some(now);
        Ok(())
    }

    /// Spends an approval for one exact action.
    ///
    /// Every gate runs: the task, the answer, the expiry, prior use, the
    /// repeat scope, the gesture, and the binding. A binding that disagrees
    /// marks the approval invalidated in the book, so the same answer cannot be
    /// presented again against a binding that happens to match.
    pub fn consume(
        &mut self,
        approval_id: &ApprovalId,
        task_id: &TaskId,
        current: &ApprovalBinding,
        now: MonotonicMillis,
    ) -> Result<ApprovalReceiptReference, ApprovalError> {
        let approval = self
            .entries
            .get_mut(approval_id)
            .ok_or(ApprovalError::Unknown)?;
        if approval.task_id != *task_id {
            return Err(ApprovalError::TaskMismatch);
        }
        if approval.is_used() {
            return Err(ApprovalError::AlreadyUsed);
        }
        let decision = approval.decision_at(now);
        if decision == ApprovalDecision::Expired {
            approval.decision = ApprovalDecision::Expired;
            return Err(ApprovalError::Expired);
        }
        if !decision.grants_authority() {
            return Err(ApprovalError::NotGranted(decision));
        }
        if !approval.repeat_scope.is_authorized_today() {
            return Err(ApprovalError::RepeatScopeNotAuthorized);
        }
        if approval.gesture.is_none() {
            return Err(ApprovalError::MissingUserGesture);
        }
        if let Some(reason) = approval.binding.invalidation_against(current) {
            approval.decision = ApprovalDecision::Invalidated;
            approval.invalidation = Some(reason);
            return Err(ApprovalError::Invalidated(reason));
        }
        approval.used_at = Some(now);
        Ok(approval.id.to_receipt())
    }

    /// The approval with this identifier.
    pub fn get(&self, approval_id: &ApprovalId) -> Option<&Approval> {
        self.entries.get(approval_id)
    }

    /// How many approvals the book holds.
    ///
    /// Public so a test can assert the bound itself rather than the mechanism
    /// that keeps it. Never exceeds [`MAX_APPROVALS_PER_SESSION`].
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether the book has presented nothing.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Every approval, in deterministic identifier order.
    pub fn entries(&self) -> impl Iterator<Item = &Approval> {
        self.entries.values()
    }
}

/// What the trusted approval surface asks for.
#[derive(Clone, Debug, PartialEq)]
pub struct ApprovalRequest {
    /// The task the question belongs to.
    pub task_id: TaskId,
    /// Everything the sheet shows.
    pub binding: ApprovalBinding,
    /// How often the answer may be used.
    pub repeat_scope: RepeatScope,
    /// When an unanswered request stops being answerable.
    pub expires_at: MonotonicMillis,
}
