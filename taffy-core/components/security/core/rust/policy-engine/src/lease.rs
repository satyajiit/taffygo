// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Control modes and actor leases (domain model sections 9.3 and 12.3, threat
//! model invariant I-04 and section 8.3).
//!
//! A lease answers one question: is the assistant currently the actor in this
//! tab? It is not permission to do anything. Every effect additionally needs a
//! capability bound to the exact action, and a capability may only be issued
//! while a lease is standing, so the two together are the "lease plus
//! capability" invariant.
//!
//! Four rules are encoded:
//!
//! - at most one lease per tab, so two mutations can never interleave;
//! - `USER` control grants no lease at all, so the read-only mode cannot be
//!   turned into an acting mode by any other parameter;
//! - direct user input and take over preempt the lease synchronously — the
//!   revocation is a local state change that waits for no page, model, or
//!   network;
//! - a lease is session-local authority. It is never restored after process
//!   death, which is why nothing here serializes one.

use core::fmt;
use std::collections::btree_map::Entry;
use std::collections::BTreeMap;

use bip_types::identity::{MonotonicMillis, TabId, TaskId};

use crate::time::{IdKind, IdSource};

/// Who is acting in the tab (domain model section 9.3).
///
/// The user-facing wording lives in the experience specification; these are the
/// internal names. Control mode is not a blanket capability and not a privacy
/// setting: remote transmission, persistence, and action risk stay
/// independently governed.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ControlMode {
    /// The person browsing is the only actor. No assistant actor lease
    /// exists; the assistant may read and suggest within the task's data scope
    /// and may act on nothing.
    User,
    /// The user and the assistant share the tab. A bounded lease may cover the
    /// step currently under review, and each step is reviewed.
    Shared,
    /// The assistant is the actor. A task-scoped lease may cover the
    /// authorized action classes until it expires or is preempted.
    Assistant,
}

impl ControlMode {
    /// Every mode, in declaration order.
    pub const ALL: &'static [Self] = &[Self::User, Self::Shared, Self::Assistant];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::User => "user",
            Self::Shared => "shared",
            Self::Assistant => "assistant",
        }
    }

    /// Whether this mode can hold an actor lease at all.
    pub const fn grants_lease(self) -> bool {
        matches!(self, Self::Shared | Self::Assistant)
    }

    /// Whether every step needs its own approval before authority is issued.
    ///
    /// Shared control means the user is reviewing each step, so a capability
    /// issued under it carries an approval receipt or it is not issued.
    pub const fn requires_step_approval(self) -> bool {
        matches!(self, Self::Shared)
    }
}

/// Opaque actor lease identifier.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ActorLeaseId(String);

impl ActorLeaseId {
    /// Wraps an identifier minted by an [`IdSource`].
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value, for equality and for audit correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for ActorLeaseId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Lease lifecycle (domain model section 12.3).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LeaseState {
    /// Standing and inside its expiry.
    Active,
    /// A preemption has begun; no new authority is issued under it.
    Preempting,
    /// Ended by the user, by policy, or by task cancellation.
    Revoked,
    /// Past its expiry.
    Expired,
    /// Ended by the task runtime because the work finished.
    Released,
}

impl LeaseState {
    /// Whether a capability may still be issued under a lease in this state.
    pub const fn may_issue_authority(self) -> bool {
        matches!(self, Self::Active)
    }
}

/// Why a lease ended before its expiry.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum RevocationReason {
    /// The user pressed take over.
    UserTookOver,
    /// The user typed, tapped, or scrolled in the tab.
    DirectUserInput,
    /// The task was cancelled.
    TaskCancelled,
    /// A policy change withdrew the authority.
    PolicyRevoked,
    /// The tab or its document went away.
    TabClosed,
}

impl RevocationReason {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::UserTookOver => "user_took_over",
            Self::DirectUserInput => "direct_user_input",
            Self::TaskCancelled => "task_cancelled",
            Self::PolicyRevoked => "policy_revoked",
            Self::TabClosed => "tab_closed",
        }
    }
}

/// What a caller asks for when it wants the assistant to become the actor in a
/// tab.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LeaseRequest {
    /// The task the lease belongs to.
    pub task_id: TaskId,
    /// The tab the lease covers. One tab holds at most one lease.
    pub tab_id: TabId,
    /// Who is acting in the tab.
    pub control_mode: ControlMode,
    /// When the lease stops standing, on the monotonic clock.
    pub expires_at: MonotonicMillis,
}

/// A standing claim to be the actor in one tab.
///
/// The record is deliberately not serializable. A lease is process-local
/// authority; writing one to storage would invite restoring it after a crash,
/// and a lease that survives process death is a lease nobody is watching.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ActorLease {
    lease_id: ActorLeaseId,
    task_id: TaskId,
    tab_id: TabId,
    control_mode: ControlMode,
    issued_at: MonotonicMillis,
    expires_at: MonotonicMillis,
    state: LeaseState,
    revocation_reason: Option<RevocationReason>,
}

impl ActorLease {
    /// The lease identifier.
    pub fn lease_id(&self) -> &ActorLeaseId {
        &self.lease_id
    }

    /// The task this lease belongs to.
    pub fn task_id(&self) -> &TaskId {
        &self.task_id
    }

    /// The tab this lease covers.
    pub fn tab_id(&self) -> &TabId {
        &self.tab_id
    }

    /// Who is acting in the tab.
    pub fn control_mode(&self) -> ControlMode {
        self.control_mode
    }

    /// When the lease was issued.
    pub fn issued_at(&self) -> MonotonicMillis {
        self.issued_at
    }

    /// When the lease stops standing.
    pub fn expires_at(&self) -> MonotonicMillis {
        self.expires_at
    }

    /// Why the lease ended, when it ended early.
    pub fn revocation_reason(&self) -> Option<RevocationReason> {
        self.revocation_reason
    }

    /// The lease's state as of `now`.
    ///
    /// Expiry is computed rather than stored, so a lease cannot look active
    /// because nobody ran a timer. A lease that already ended keeps the reason
    /// it ended for: expiry does not overwrite a revocation.
    pub fn state_at(&self, now: MonotonicMillis) -> LeaseState {
        match self.state {
            LeaseState::Active if now.0 >= self.expires_at.0 => LeaseState::Expired,
            other => other,
        }
    }

    /// Whether the lease is standing as of `now`.
    pub fn is_active_at(&self, now: MonotonicMillis) -> bool {
        self.state_at(now) == LeaseState::Active
    }

    /// Whether a capability for a mutating action may be issued under this
    /// lease as of `now`.
    ///
    /// `USER` control never reaches here, because it is refused at issue time
    /// and holds no lease to ask.
    pub fn may_authorize_mutation_at(&self, now: MonotonicMillis) -> bool {
        self.control_mode.grants_lease() && self.is_active_at(now)
    }

    /// Ends the lease immediately.
    ///
    /// Returns `false` when the lease had already ended, so a second take over
    /// is not reported as a second revocation. The state change is local and
    /// complete when this returns: nothing is queued, and no page, model, or
    /// network participates.
    pub fn revoke(&mut self, reason: RevocationReason) -> bool {
        match self.state {
            LeaseState::Active | LeaseState::Preempting => {
                self.state = LeaseState::Revoked;
                self.revocation_reason = Some(reason);
                true
            }
            LeaseState::Revoked | LeaseState::Expired | LeaseState::Released => false,
        }
    }

    /// Marks the lease as preempting, so no further authority is issued under
    /// it while the take-over completes.
    pub fn begin_preemption(&mut self) -> bool {
        if self.state == LeaseState::Active {
            self.state = LeaseState::Preempting;
            true
        } else {
            false
        }
    }

    /// Ends the lease because the work finished.
    pub fn release(&mut self) -> bool {
        match self.state {
            LeaseState::Active | LeaseState::Preempting => {
                self.state = LeaseState::Released;
                true
            }
            LeaseState::Revoked | LeaseState::Expired | LeaseState::Released => false,
        }
    }
}

/// Why a lease could not be issued.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LeaseError {
    /// `USER` control holds no actor lease.
    ControlModeGrantsNoLease,
    /// The tab already has a standing lease. At most one exists per tab.
    TabAlreadyLeased,
    /// The requested expiry is now or in the past.
    ExpiryNotInFuture,
    /// The identifier source is exhausted.
    IdSourceExhausted,
}

impl fmt::Display for LeaseError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let text = match self {
            Self::ControlModeGrantsNoLease => "user control holds no actor lease",
            Self::TabAlreadyLeased => "a tab holds at most one actor lease",
            Self::ExpiryNotInFuture => "a lease expiry must be in the future",
            Self::IdSourceExhausted => "the identifier source is exhausted",
        };
        formatter.write_str(text)
    }
}

/// The leases held right now, at most one per tab.
///
/// Ended leases are kept so that a capability issued under one can still be
/// resolved to the reason its authority went away, and so that an audit record
/// can be written after the fact. Nothing here grows without bound during a
/// session's normal use; the owning runtime drops the book with the profile.
#[derive(Clone, Debug, Default)]
pub struct LeaseBook {
    by_tab: BTreeMap<TabId, ActorLease>,
}

impl LeaseBook {
    /// An empty book.
    pub const fn new() -> Self {
        Self {
            by_tab: BTreeMap::new(),
        }
    }

    /// Issues a lease for the requested tab.
    ///
    /// Refuses when the mode grants no lease, when the expiry is not in the
    /// future, or when the tab already holds a standing lease. An ended lease
    /// on the same tab is replaced.
    pub fn issue(
        &mut self,
        request: &LeaseRequest,
        ids: &mut impl IdSource,
        now: MonotonicMillis,
    ) -> Result<&ActorLease, LeaseError> {
        if !request.control_mode.grants_lease() {
            return Err(LeaseError::ControlModeGrantsNoLease);
        }
        if request.expires_at.0 <= now.0 {
            return Err(LeaseError::ExpiryNotInFuture);
        }
        if self
            .by_tab
            .get(&request.tab_id)
            .is_some_and(|lease| lease.is_active_at(now))
        {
            return Err(LeaseError::TabAlreadyLeased);
        }

        let lease_id = ids
            .next_id(IdKind::ActorLease)
            .map(ActorLeaseId::new)
            .ok_or(LeaseError::IdSourceExhausted)?;

        let lease = ActorLease {
            lease_id,
            task_id: request.task_id.clone(),
            tab_id: request.tab_id.clone(),
            control_mode: request.control_mode,
            issued_at: now,
            expires_at: request.expires_at,
            state: LeaseState::Active,
            revocation_reason: None,
        };
        // An ended lease on the same tab is replaced rather than kept, so the
        // returned reference is always the lease just issued.
        match self.by_tab.entry(request.tab_id.clone()) {
            Entry::Occupied(mut occupied) => {
                occupied.insert(lease);
                Ok(occupied.into_mut())
            }
            Entry::Vacant(vacant) => Ok(vacant.insert(lease)),
        }
    }

    /// The lease recorded for a tab, whatever state it is in.
    pub fn get(&self, tab_id: &TabId) -> Option<&ActorLease> {
        self.by_tab.get(tab_id)
    }

    /// The standing lease for a tab as of `now`.
    pub fn active(&self, tab_id: &TabId, now: MonotonicMillis) -> Option<&ActorLease> {
        self.by_tab
            .get(tab_id)
            .filter(|lease| lease.is_active_at(now))
    }

    /// The lease with this identifier, whatever state it is in.
    pub fn by_id(&self, lease_id: &ActorLeaseId) -> Option<&ActorLease> {
        self.by_tab
            .values()
            .find(|lease| lease.lease_id() == lease_id)
    }

    /// Ends the lease on a tab, returning its identifier when one ended.
    pub fn revoke(&mut self, tab_id: &TabId, reason: RevocationReason) -> Option<ActorLeaseId> {
        let lease = self.by_tab.get_mut(tab_id)?;
        lease.revoke(reason).then(|| lease.lease_id().clone())
    }

    /// The tabs with a lease recorded, in deterministic order.
    pub fn tabs(&self) -> impl Iterator<Item = &TabId> {
        self.by_tab.keys()
    }
}

#[cfg(test)]
mod tests {
    use super::{
        ActorLease, ControlMode, LeaseBook, LeaseError, LeaseRequest, LeaseState, RevocationReason,
    };
    use crate::time::SequentialIds;
    use bip_types::identity::{MonotonicMillis, TabId, TaskId};

    fn request(mode: ControlMode) -> LeaseRequest {
        LeaseRequest {
            task_id: TaskId::new("task_1"),
            tab_id: TabId::new("tab_1"),
            control_mode: mode,
            expires_at: MonotonicMillis(1_000),
        }
    }

    fn issued(mode: ControlMode) -> (LeaseBook, SequentialIds) {
        let mut book = LeaseBook::new();
        let mut ids = SequentialIds::new();
        let outcome = book.issue(&request(mode), &mut ids, MonotonicMillis(0));
        assert!(outcome.is_ok());
        (book, ids)
    }

    fn lease_of(book: &LeaseBook) -> &ActorLease {
        match book.get(&TabId::new("tab_1")) {
            Some(lease) => lease,
            None => unreachable!("the fixture issued a lease"),
        }
    }

    #[test]
    fn user_control_holds_no_lease_whatever_else_is_asked_for() {
        let mut book = LeaseBook::new();
        let mut ids = SequentialIds::new();
        assert_eq!(
            book.issue(&request(ControlMode::User), &mut ids, MonotonicMillis(0))
                .err(),
            Some(LeaseError::ControlModeGrantsNoLease)
        );
        assert!(book.get(&TabId::new("tab_1")).is_none());
        assert!(!ControlMode::User.grants_lease());
    }

    #[test]
    fn a_tab_holds_at_most_one_standing_lease() {
        let (mut book, mut ids) = issued(ControlMode::Assistant);
        assert_eq!(
            book.issue(
                &request(ControlMode::Assistant),
                &mut ids,
                MonotonicMillis(1)
            )
            .err(),
            Some(LeaseError::TabAlreadyLeased)
        );
    }

    #[test]
    fn a_lease_expires_without_anybody_running_a_timer() {
        let (book, _) = issued(ControlMode::Assistant);
        let lease = lease_of(&book);
        assert!(lease.is_active_at(MonotonicMillis(999)));
        assert_eq!(lease.state_at(MonotonicMillis(1_000)), LeaseState::Expired);
        assert!(!lease.may_authorize_mutation_at(MonotonicMillis(1_000)));
    }

    #[test]
    fn take_over_ends_the_lease_immediately_and_only_once() {
        let (mut book, _) = issued(ControlMode::Assistant);
        let tab = TabId::new("tab_1");
        let revoked = book.revoke(&tab, RevocationReason::UserTookOver);
        assert!(revoked.is_some());
        assert_eq!(book.active(&tab, MonotonicMillis(0)), None);
        assert_eq!(
            lease_of(&book).revocation_reason(),
            Some(RevocationReason::UserTookOver)
        );
        // A second take over is not a second revocation.
        assert_eq!(book.revoke(&tab, RevocationReason::DirectUserInput), None);
        assert_eq!(
            lease_of(&book).revocation_reason(),
            Some(RevocationReason::UserTookOver)
        );
    }

    #[test]
    fn expiry_never_overwrites_the_reason_a_lease_was_revoked() {
        let (mut book, _) = issued(ControlMode::Assistant);
        book.revoke(&TabId::new("tab_1"), RevocationReason::TaskCancelled);
        assert_eq!(
            lease_of(&book).state_at(MonotonicMillis(10_000)),
            LeaseState::Revoked
        );
    }

    #[test]
    fn shared_control_reviews_every_step() {
        assert!(ControlMode::Shared.requires_step_approval());
        assert!(!ControlMode::Assistant.requires_step_approval());
        assert!(ControlMode::Shared.grants_lease());
    }

    #[test]
    fn a_lease_that_expires_at_issue_time_is_refused() {
        let mut book = LeaseBook::new();
        let mut ids = SequentialIds::new();
        let mut plan = request(ControlMode::Assistant);
        plan.expires_at = MonotonicMillis(0);
        assert_eq!(
            book.issue(&plan, &mut ids, MonotonicMillis(0)).err(),
            Some(LeaseError::ExpiryNotInFuture)
        );
    }
}
