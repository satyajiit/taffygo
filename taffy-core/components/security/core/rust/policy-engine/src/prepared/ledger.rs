// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Every prepared effect this session staged, and the one place a commit spends
//! one (decision 0022).
//!
//! The ledger is the broker's own record. A message can name a prepare; it can
//! never present one, because what was staged and what was shown are only ever
//! read from here.
//!
//! Two rules hold over everything below. **Preparation mints nothing**: staging
//! returns no value a later call can spend, and the ledger's only power over a
//! commit is to refuse it. **A commit spends exactly what was prepared and
//! never more**: the class, the place, the rendered effect and the prepare time
//! are all compared, and the record is marked committed once.

use std::collections::BTreeMap;

use bip_types::action::PreparedEffectBinding;
use bip_types::identity::{ContentDigest, MonotonicMillis, TaskId};

use crate::action_class::ActionClass;
use crate::capability::CapabilityScope;
use crate::lease::ActorLeaseId;
use crate::prepared::commit::CommitContext;
use crate::prepared::{
    CommitRefusal, PrepareError, PreparedEffectId, PreparedEffectState, RenderedEffect,
    RenderedEffectDigest,
};

/// How many prepared effects one broker session may stage.
///
/// The ledger is what refuses a commit by name, so a record it dropped would be
/// a commit it could only answer "no such prepare" to for the wrong reason.
/// Nothing is dropped; the register has a ceiling instead, and reaching it
/// refuses to stage with [`PrepareError::LedgerFull`].
pub const MAX_PREPARED_EFFECTS_PER_SESSION: usize = 1_024;

/// What the broker records when a prepare is authorized.
#[derive(Clone, Debug, PartialEq)]
pub struct PrepareRequest {
    /// The identity a later commit will name this prepare by.
    pub prepared_effect_id: PreparedEffectId,
    /// The task the prepare belongs to.
    pub task_id: TaskId,
    /// The actor lease it was staged under. A commit under a different lease is
    /// refused, which is how a take-over between the two steps is caught.
    pub lease_id: ActorLeaseId,
    /// What the effect does. The commit carries the same class or it is a
    /// different effect.
    pub action_class: ActionClass,
    /// The digest of the prepare *proposal*. Held so the rendered digest can be
    /// checked against it, never so it can stand in for one.
    pub prepare_digest: ContentDigest,
    /// Where the effect would happen. The commit lands in the same place or it
    /// is spending more than was prepared.
    pub scope: CapabilityScope,
    /// When the prepared effect stops being committable, on the monotonic
    /// clock.
    pub expires_at: MonotonicMillis,
}

/// Evidence that a prepared effect was spent.
///
/// Returned by [`PreparedEffectLedger::commit`] exactly once per prepared
/// effect. The ordinal counts commits within one ledger, so a journal reader
/// can order them without a clock.
#[derive(Clone, Debug, PartialEq)]
pub struct CommitReceipt {
    /// The prepared effect that was spent.
    pub prepared_effect_id: PreparedEffectId,
    /// The digest of the effect as it was rendered and approved.
    pub rendered_effect: RenderedEffectDigest,
    /// When the commit was made.
    pub committed_at: MonotonicMillis,
    /// Which commit this was, counted within the ledger.
    pub ordinal: u64,
}

/// One staged effect and everything a commit against it is compared with.
#[derive(Clone, Debug, PartialEq)]
pub struct PreparedEffect {
    id: PreparedEffectId,
    task_id: TaskId,
    lease_id: ActorLeaseId,
    action_class: ActionClass,
    prepare_digest: ContentDigest,
    scope: CapabilityScope,
    rendered: Option<RenderedEffect>,
    state: PreparedEffectState,
    staged_at: MonotonicMillis,
    expires_at: MonotonicMillis,
    committed_at: Option<MonotonicMillis>,
}

impl PreparedEffect {
    /// The identity a commit names this prepare by.
    pub const fn prepared_effect_id(&self) -> &PreparedEffectId {
        &self.id
    }

    /// The task the prepare belongs to.
    pub const fn task_id(&self) -> &TaskId {
        &self.task_id
    }

    /// The actor lease it was staged under.
    pub const fn lease_id(&self) -> &ActorLeaseId {
        &self.lease_id
    }

    /// What the effect does.
    pub const fn action_class(&self) -> ActionClass {
        self.action_class
    }

    /// Where the effect would happen.
    pub const fn scope(&self) -> &CapabilityScope {
        &self.scope
    }

    /// What the trusted surface rendered, once it has.
    pub const fn rendered(&self) -> Option<&RenderedEffect> {
        self.rendered.as_ref()
    }

    /// When the prepared effect stops being committable.
    pub const fn expires_at(&self) -> MonotonicMillis {
        self.expires_at
    }

    /// When the commit was made, if one was.
    pub const fn committed_at(&self) -> Option<MonotonicMillis> {
        self.committed_at
    }

    /// The state as of `now`.
    ///
    /// Expiry is computed rather than stored, so a prepared effect never looks
    /// committable because nobody ran a timer. A record that already ended
    /// keeps the state it ended in.
    ///
    /// Written out state by state rather than over a binding, so a state added
    /// to the lifecycle has to say whether it can expire instead of inheriting
    /// whichever arm happened to be nearest. The two live states are the two
    /// [`PreparedEffectState::is_live`] names, and the compiler is what keeps
    /// them agreeing.
    pub const fn state_at(&self, now: MonotonicMillis) -> PreparedEffectState {
        let expired = now.0 >= self.expires_at.0;
        match self.state {
            PreparedEffectState::Staged | PreparedEffectState::Consumed if expired => {
                PreparedEffectState::Expired
            }
            PreparedEffectState::Staged => PreparedEffectState::Staged,
            PreparedEffectState::Consumed => PreparedEffectState::Consumed,
            PreparedEffectState::Committed => PreparedEffectState::Committed,
            PreparedEffectState::Expired => PreparedEffectState::Expired,
            PreparedEffectState::Discarded => PreparedEffectState::Discarded,
        }
    }
}

/// Every prepared effect this session staged.
///
/// Holds at most [`MAX_PREPARED_EFFECTS_PER_SESSION`] records and never removes
/// one. It is session-lifetime and in memory, so a restart between the two
/// steps leaves nothing to commit against — which is the durable half of "no
/// commit replays".
#[derive(Clone, Debug, Default)]
pub struct PreparedEffectLedger {
    entries: BTreeMap<PreparedEffectId, PreparedEffect>,
    commits: u64,
}

impl PreparedEffectLedger {
    /// An empty ledger.
    pub const fn new() -> Self {
        Self {
            entries: BTreeMap::new(),
            commits: 0,
        }
    }

    /// Records that a prepare was authorized.
    ///
    /// Mints nothing. The return is `()` on purpose: a value handed back here
    /// would be a thing a commit could present, and a commit presents only what
    /// the trusted surface rendered.
    pub fn stage(
        &mut self,
        request: &PrepareRequest,
        now: MonotonicMillis,
    ) -> Result<(), PrepareError> {
        if request.expires_at.0 <= now.0 {
            return Err(PrepareError::ExpiryNotInFuture);
        }
        if self.entries.contains_key(&request.prepared_effect_id) {
            return Err(PrepareError::AlreadyStaged);
        }
        if self.entries.len() >= MAX_PREPARED_EFFECTS_PER_SESSION {
            return Err(PrepareError::LedgerFull);
        }
        self.entries.insert(
            request.prepared_effect_id.clone(),
            PreparedEffect {
                id: request.prepared_effect_id.clone(),
                task_id: request.task_id.clone(),
                lease_id: request.lease_id.clone(),
                action_class: request.action_class,
                prepare_digest: request.prepare_digest.clone(),
                scope: request.scope.clone(),
                rendered: None,
                state: PreparedEffectState::Staged,
                staged_at: now,
                expires_at: request.expires_at,
                committed_at: None,
            },
        );
        Ok(())
    }

    /// Records that the prepare ran and the trusted surface rendered the effect.
    ///
    /// This is the moment the commit's binding is fixed, and it is fixed to
    /// what was *displayed*. The prepare's own proposal digest is refused here
    /// by name: passing it would bind the request a model made rather than the
    /// sentence a person read, and the record would then match a commit whose
    /// rendering had moved.
    pub fn record_rendered(
        &mut self,
        prepared_effect_id: &PreparedEffectId,
        rendered: &RenderedEffect,
        now: MonotonicMillis,
    ) -> Result<(), PrepareError> {
        let entry = self
            .entries
            .get_mut(prepared_effect_id)
            .ok_or(PrepareError::Unknown)?;
        match entry.state_at(now) {
            PreparedEffectState::Staged => {}
            // Expiry is computed, so the first reading that observes it is
            // what writes it down. Recorded rather than left implicit because
            // a preparation that ran out is over, and nothing here renews it.
            PreparedEffectState::Expired => {
                entry.state = PreparedEffectState::Expired;
                return Err(PrepareError::NotStaged(PreparedEffectState::Expired));
            }
            // Named one at a time rather than swept up behind a binding: a
            // state added to the lifecycle has to decide what rendering
            // against it means, and this arm would otherwise answer for it.
            state @ (PreparedEffectState::Consumed
            | PreparedEffectState::Committed
            | PreparedEffectState::Discarded) => {
                return Err(PrepareError::NotStaged(state));
            }
        }
        if *rendered.digest.digest() == entry.prepare_digest {
            return Err(PrepareError::RenderedDigestIsTheProposal);
        }
        if rendered.rendered_at.0 < entry.staged_at.0 || rendered.rendered_at.0 > now.0 {
            return Err(PrepareError::RenderingOutOfOrder);
        }
        entry.rendered = Some(rendered.clone());
        entry.state = PreparedEffectState::Consumed;
        Ok(())
    }

    /// Whether a commit may be made, without spending anything.
    ///
    /// Separate from [`Self::commit`] because the section 12 sequence still has
    /// to run: a commit refused for a stale epoch never reached the page, and
    /// spending the prepared effect to find that out would burn a preparation
    /// on a refusal.
    pub fn verdict(
        &self,
        binding: &PreparedEffectBinding,
        context: &CommitContext<'_>,
        now: MonotonicMillis,
    ) -> Result<(), CommitRefusal> {
        let id = PreparedEffectId::for_action(&binding.prepared_action_id);
        let entry = self.entries.get(&id).ok_or(CommitRefusal::NoSuchPrepare)?;
        if entry.task_id != *context.task_id {
            return Err(CommitRefusal::OtherTask);
        }
        if entry.lease_id != *context.lease_id {
            return Err(CommitRefusal::OtherLease);
        }
        match entry.state_at(now) {
            PreparedEffectState::Consumed => {}
            PreparedEffectState::Staged => return Err(CommitRefusal::PrepareDidNotRun),
            PreparedEffectState::Committed => return Err(CommitRefusal::AlreadyCommitted),
            PreparedEffectState::Expired => return Err(CommitRefusal::Expired),
            PreparedEffectState::Discarded => return Err(CommitRefusal::Discarded),
        }
        // Only a consumed record reaches here, and a consumed record has a
        // rendering. Its absence would be an invariant this module broke, so it
        // refuses rather than reading past it.
        let rendered = entry
            .rendered
            .as_ref()
            .ok_or(CommitRefusal::EffectDigestChanged)?;
        let presented =
            RenderedEffectDigest::from_commit_binding(binding.prepared_effect_digest.clone());
        if *rendered.digest.digest() != *presented.digest() {
            return Err(CommitRefusal::EffectDigestChanged);
        }
        if entry.action_class != context.action_class {
            return Err(CommitRefusal::ActionClassChanged);
        }
        if entry.scope != *context.scope {
            return Err(CommitRefusal::ScopeChanged);
        }
        if binding.prepared_at_monotonic_ms != rendered.rendered_at.0 {
            return Err(CommitRefusal::PrepareTimeChanged);
        }
        // A gesture exists only because the trusted surface minted it from a
        // real input event. One that does not strictly postdate the prepare
        // cannot have been a response to it, and one dated after the present
        // has not happened.
        if binding.gesture_at_monotonic_ms <= binding.prepared_at_monotonic_ms
            || binding.gesture_at_monotonic_ms > now.0
        {
            return Err(CommitRefusal::GestureDoesNotFollowPrepare);
        }
        Ok(())
    }

    /// Spends a prepared effect for exactly one commit.
    ///
    /// Runs every comparison [`Self::verdict`] does and then marks the record
    /// committed, so a replayed commit meets [`CommitRefusal::AlreadyCommitted`]
    /// rather than a second success.
    pub fn commit(
        &mut self,
        binding: &PreparedEffectBinding,
        context: &CommitContext<'_>,
        now: MonotonicMillis,
    ) -> Result<CommitReceipt, CommitRefusal> {
        self.verdict(binding, context, now)?;
        let ordinal = self.commits;
        let id = PreparedEffectId::for_action(&binding.prepared_action_id);
        let entry = self
            .entries
            .get_mut(&id)
            .ok_or(CommitRefusal::NoSuchPrepare)?;
        let rendered = entry
            .rendered
            .as_ref()
            .ok_or(CommitRefusal::EffectDigestChanged)?
            .digest
            .clone();
        entry.state = PreparedEffectState::Committed;
        entry.committed_at = Some(now);
        self.commits = self.commits.saturating_add(1);
        Ok(CommitReceipt {
            prepared_effect_id: id,
            rendered_effect: rendered,
            committed_at: now,
            ordinal,
        })
    }

    /// Withdraws one prepared effect before it is committed.
    ///
    /// Returns `false` when the record was already committed: an effect that
    /// has left the broker cannot be recalled, and pretending otherwise would
    /// let a caller believe it was prevented when it was not.
    pub fn discard(&mut self, prepared_effect_id: &PreparedEffectId) -> bool {
        match self.entries.get_mut(prepared_effect_id) {
            Some(entry) if entry.state.is_live() => {
                entry.state = PreparedEffectState::Discarded;
                true
            }
            Some(_) | None => false,
        }
    }

    /// Withdraws every uncommitted prepared effect staged under one lease.
    ///
    /// A take-over ends the lease, and a prepared effect is consent to stage
    /// under that lease and no other. Returns what was withdrawn, in
    /// deterministic identifier order.
    pub fn discard_for_lease(&mut self, lease_id: &ActorLeaseId) -> Vec<PreparedEffectId> {
        let mut discarded = Vec::new();
        for entry in self.entries.values_mut() {
            if entry.lease_id == *lease_id && entry.state.is_live() {
                entry.state = PreparedEffectState::Discarded;
                discarded.push(entry.id.clone());
            }
        }
        discarded
    }

    /// The prepared effect with this identity.
    pub fn get(&self, prepared_effect_id: &PreparedEffectId) -> Option<&PreparedEffect> {
        self.entries.get(prepared_effect_id)
    }

    /// How many prepared effects the ledger holds.
    ///
    /// Public so a test can assert the bound itself rather than the mechanism
    /// that keeps it. Never exceeds [`MAX_PREPARED_EFFECTS_PER_SESSION`].
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether the ledger has staged nothing.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// How many commits the ledger has spent.
    pub const fn commits(&self) -> u64 {
        self.commits
    }

    /// Every prepared effect, in deterministic identifier order.
    pub fn entries(&self) -> impl Iterator<Item = &PreparedEffect> {
        self.entries.values()
    }
}
