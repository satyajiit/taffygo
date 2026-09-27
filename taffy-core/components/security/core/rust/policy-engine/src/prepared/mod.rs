// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Prepared effects: what was staged, what a person was shown, and what a
//! commit is allowed to spend (decision
//! `docs/decisions/0022-prepare-and-commit-for-consequential-actions.md`).
//!
//! A consequential effect is authorized in two steps. A prepare stages it and
//! has no consequence outside the browser process. The trusted surface then
//! renders the staged effect, and only after that may a commit name the prepare
//! it completes and cause the effect.
//!
//! # Preparation mints nothing
//!
//! Staging is a record, not authority. [`ledger::PreparedEffectLedger::stage`]
//! returns no capability, no receipt, and no reference a later call can spend.
//! The commit asks for authority exactly as any other proposal does, and the
//! ledger's only power is to **refuse** it. Nothing here can widen anything.
//!
//! # The digest is over the effect as rendered
//!
//! This is the whole point of the mechanism and the easiest part of it to get
//! subtly wrong. What a person approved is what they *saw*, so the value the
//! ledger binds is [`RenderedEffectDigest`] — a digest over the effect exactly
//! as the trusted surface rendered it, recorded by
//! [`ledger::PreparedEffectLedger::record_rendered`] at the moment it was
//! shown.
//!
//! A digest over the *proposal* would let the rendering differ from the thing
//! approved while the digest still matched, which is the gap this record exists
//! to close. The type is separate from [`ContentDigest`] so that the proposal
//! digest cannot be passed where a rendered digest is wanted, and the ledger
//! holds the prepare's own proposal digest as well, so a caller that hands it
//! over anyway is refused by name rather than binding nothing at all.
//!
//! # A prepared effect expires
//!
//! Expiry is computed from the record rather than stored as a state, so a
//! prepared effect never looks live because nobody ran a timer. A commit
//! against an expired preparation is refused, and there is no method here that
//! moves an expiry: renewal would make the expiry advisory.
//!
//! | Module | Holds |
//! |---|---|
//! | this one | The identity, the rendered digest, the lifecycle, and the refusals |
//! | [`ledger`] | Every prepared effect this session staged, and the one place a commit spends one |
//! | [`commit`] | What one dispatch may commit, resolved before the section 12 sequence runs |

pub mod commit;
pub mod ledger;

pub use crate::prepared::commit::{plan_commit, CommitContext, CommitPlan, PreparedEffectStanding};
pub use crate::prepared::ledger::{
    CommitReceipt, PrepareRequest, PreparedEffect, PreparedEffectLedger,
    MAX_PREPARED_EFFECTS_PER_SESSION,
};

use core::fmt;

use bip_types::identity::{ActionId, ContentDigest, MonotonicMillis};
use bip_types::ActionResultCode;

use crate::denial::DenialReason;

/// Opaque identity of one prepared effect.
///
/// Minted from the prepare's own action identifier, because that is what a
/// commit names on the wire. Wrapped rather than used directly so the ledger
/// can be ordered and so a task identifier or a capability identifier cannot be
/// presented where a prepare is meant.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct PreparedEffectId(String);

impl PreparedEffectId {
    /// The identity of the effect prepared by `action_id`.
    pub fn for_action(action_id: &ActionId) -> Self {
        Self(action_id.as_str().to_owned())
    }

    /// The opaque value, for equality and audit correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for PreparedEffectId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// A digest over the effect exactly as the trusted surface rendered it.
///
/// Not a [`ContentDigest`], and deliberately so: the two are the same bytes on
/// the wire and opposite things in the argument. A proposal digest says what
/// was asked for; this says what a person read. Only the second is what they
/// answered.
///
/// [`ContentDigest`] is generated from the BIP schema and derives `PartialEq`
/// and no more, so neither this type nor anything holding one is `Eq` or
/// `Hash`. That is the right way round: the equality that matters here is a
/// comparison of two digests the broker itself recorded, never a lookup keyed
/// on a value a message supplied.
#[derive(Clone, Debug, PartialEq)]
pub struct RenderedEffectDigest(ContentDigest);

impl RenderedEffectDigest {
    /// Wraps a digest the trusted surface computed over what it displayed.
    ///
    /// The name is the whole of the contract: a caller that reaches for a
    /// proposal digest here has to write down that it came from the surface,
    /// and the ledger refuses the pairing anyway.
    pub const fn from_trusted_surface(digest: ContentDigest) -> Self {
        Self(digest)
    }

    /// The digest a commit presented on the wire, taken as a rendered one.
    ///
    /// The commit half of the same contract. A commit carries the digest of the
    /// effect it says it is committing, and it is compared only against a value
    /// the surface recorded, never against a proposal.
    pub const fn from_commit_binding(digest: ContentDigest) -> Self {
        Self(digest)
    }

    /// The wrapped value, for audit correlation only.
    pub const fn digest(&self) -> &ContentDigest {
        &self.0
    }
}

/// What the trusted surface put in front of a person.
#[derive(Clone, Debug, PartialEq)]
pub struct RenderedEffect {
    /// A digest over the effect exactly as it was displayed.
    pub digest: RenderedEffectDigest,
    /// When it was displayed, on the monotonic clock. This is the reading a
    /// commit's gesture has to postdate.
    pub rendered_at: MonotonicMillis,
}

/// The lifecycle of one prepared effect.
///
/// Every transition moves forward. There is no path back into [`Self::Staged`],
/// no reset, and no renewal, so a prepared effect that ended cannot be revived
/// by asking again.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PreparedEffectState {
    /// The prepare was recorded and has not run. Nothing has been shown, so
    /// there is nothing a person could have approved.
    Staged,
    /// The prepare ran and the trusted surface rendered the effect. Decision
    /// 0022 calls this consumed, because what was spent is the prepare's own
    /// capability. It is the only state a commit may be made against.
    Consumed,
    /// The one commit this prepare authorizes has been made.
    Committed,
    /// Past its expiry. A commit against it is refused, not renewed.
    Expired,
    /// Withdrawn before any commit — a take-over, a declined confirmation, or a
    /// document that moved.
    Discarded,
}

impl PreparedEffectState {
    /// Every state, in declaration order.
    ///
    /// A test walks it, so "a commit is refused in every state but one" is an
    /// enumerable claim rather than a handful of examples.
    pub const ALL: &'static [Self] = &[
        Self::Staged,
        Self::Consumed,
        Self::Committed,
        Self::Expired,
        Self::Discarded,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::Staged => "staged",
            Self::Consumed => "consumed",
            Self::Committed => "committed",
            Self::Expired => "expired",
            Self::Discarded => "discarded",
        }
    }

    /// Whether a commit may be made against a prepared effect in this state.
    pub const fn is_committable(self) -> bool {
        matches!(self, Self::Consumed)
    }

    /// Whether the prepared effect could still reach a commit.
    pub const fn is_live(self) -> bool {
        matches!(self, Self::Staged | Self::Consumed)
    }
}

/// Why a prepared effect could not be recorded.
///
/// Staging and rendering are broker bookkeeping, not authorization, so these
/// are reported to the caller rather than to a person: a prepare that could not
/// be recorded is simply a prepare that never happened.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PrepareError {
    /// An effect is already staged under that identity. An action identifier is
    /// never reused, so a second stage under one is a different action claiming
    /// a record it does not own.
    AlreadyStaged,
    /// No prepared effect with that identity exists.
    Unknown,
    /// The prepared effect is not waiting to be rendered.
    NotStaged(PreparedEffectState),
    /// The requested expiry is now or in the past.
    ExpiryNotInFuture,
    /// The rendering is dated before the prepare or after the present.
    RenderingOutOfOrder,
    /// The digest offered as the rendered effect is the prepare's own proposal
    /// digest. It binds the request rather than what a person read, which is
    /// the one substitution this record exists to refuse.
    RenderedDigestIsTheProposal,
    /// The ledger already holds [`MAX_PREPARED_EFFECTS_PER_SESSION`] records
    /// and may not drop one to make room: a dropped prepare is one a commit can
    /// no longer be refused against by name.
    LedgerFull,
}

impl fmt::Display for PrepareError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::AlreadyStaged => formatter.write_str("that effect is already staged"),
            Self::Unknown => formatter.write_str("no such prepared effect"),
            Self::NotStaged(state) => {
                write!(formatter, "the prepared effect is {}", state.label())
            }
            Self::ExpiryNotInFuture => {
                formatter.write_str("a prepared effect expiry must be in the future")
            }
            Self::RenderingOutOfOrder => {
                formatter.write_str("the rendering does not follow the prepare")
            }
            Self::RenderedDigestIsTheProposal => {
                formatter.write_str("the rendered digest is the proposal digest")
            }
            Self::LedgerFull => formatter.write_str("the prepared-effect ledger is full"),
        }
    }
}

/// Why a commit was refused.
///
/// Each variant names one comparison the ledger made. Several of them report
/// the same reason to a person on purpose — which of them refused belongs in
/// the audit record, and telling them apart on screen would tell a page which
/// of its attempts came closest.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum CommitRefusal {
    /// No prepared effect with that identity exists. A commit cannot invent a
    /// prepare that did not run, and after a restart nothing was staged at all.
    NoSuchPrepare,
    /// The prepared effect belongs to another task.
    OtherTask,
    /// The prepared effect was staged under another actor lease. A take-over
    /// between the two steps lands here.
    OtherLease,
    /// The prepare was recorded and never ran, so nothing was ever shown.
    PrepareDidNotRun,
    /// The prepared effect was already committed. One prepare, one commit.
    AlreadyCommitted,
    /// The prepared effect expired before the commit arrived.
    Expired,
    /// The prepared effect was withdrawn before the commit arrived.
    Discarded,
    /// The effect being committed is not the effect that was rendered.
    EffectDigestChanged,
    /// The commit would land somewhere other than the place that was prepared.
    ScopeChanged,
    /// The commit carries a different class of effect from the one prepared.
    ActionClassChanged,
    /// The commit names a prepare time the ledger did not record.
    PrepareTimeChanged,
    /// The gesture does not postdate the prepare, so it cannot have been a
    /// response to it. This is what makes a programmatic commit impossible
    /// rather than merely unlikely.
    GestureDoesNotFollowPrepare,
}

impl CommitRefusal {
    /// Every refusal, in declaration order.
    pub const ALL: &'static [Self] = &[
        Self::NoSuchPrepare,
        Self::OtherTask,
        Self::OtherLease,
        Self::PrepareDidNotRun,
        Self::AlreadyCommitted,
        Self::Expired,
        Self::Discarded,
        Self::EffectDigestChanged,
        Self::ScopeChanged,
        Self::ActionClassChanged,
        Self::PrepareTimeChanged,
        Self::GestureDoesNotFollowPrepare,
    ];

    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::NoSuchPrepare => "no_such_prepare",
            Self::OtherTask => "other_task",
            Self::OtherLease => "other_lease",
            Self::PrepareDidNotRun => "prepare_did_not_run",
            Self::AlreadyCommitted => "already_committed",
            Self::Expired => "expired",
            Self::Discarded => "discarded",
            Self::EffectDigestChanged => "effect_digest_changed",
            Self::ScopeChanged => "scope_changed",
            Self::ActionClassChanged => "action_class_changed",
            Self::PrepareTimeChanged => "prepare_time_changed",
            Self::GestureDoesNotFollowPrepare => "gesture_does_not_follow_prepare",
        }
    }

    /// The refusal a person is shown, and the audit reason behind it.
    pub const fn denial_reason(self) -> DenialReason {
        DenialReason::for_commit(self)
    }

    /// The protocol result code this refusal ends the action with.
    pub const fn result_code(self) -> ActionResultCode {
        self.denial_reason().result_code()
    }
}

impl fmt::Display for CommitRefusal {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.label())
    }
}
