// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Capabilities: the only thing that authorizes a side effect (domain model
//! section 12.4, threat model section 8.2, protocol specification section 11.3).
//!
//! A capability is narrow by construction. It names one task, one principal,
//! one action class, one action digest, and one exact place in the browser —
//! profile, tab, frame, origin, page epoch, and where the action targets a node,
//! that node at that graph revision. It expires on the monotonic clock and it
//! is consumed once.
//!
//! # Consumption is monotonic
//!
//! The state machine runs one way:
//!
//! ```text
//! ISSUED ──begin_dispatch──▶ IN_FLIGHT ──consume──▶ CONSUMED
//!    │                            │
//!    ├──────────revoke────────────┴──▶ REVOKED
//!    └──────────(expiry)─────────────▶ EXPIRED
//! ```
//!
//! There is no transition back into `ISSUED`, no reset, and no way to raise an
//! expiry after issue. A replay therefore cannot succeed by re-presenting a
//! consumed capability: [`Capability::consume`] answers
//! [`CapabilityError::AlreadyConsumed`], and the state it inspects is the
//! broker's own, never a value that travelled with the message.
//!
//! # What is not here
//!
//! Unforgeable capability material is process-local and lives in the browser
//! process. This record is metadata and decision evidence: holding a copy of it
//! grants nothing. That is why it can safely be written to an audit event and
//! why the reference is never forwarded to a renderer.

pub mod ledger;
pub mod request;

pub use crate::capability::ledger::{
    CapabilityLedger, RevokedAuthority, MAX_CAPABILITIES_PER_SESSION,
};
pub use crate::capability::request::{CapabilityRequest, CapabilityScope, IssueContext};

use core::fmt;

use bip_types::action::Principal;
use bip_types::identity::{
    ApprovalReceiptReference, CapabilityReference, ContentDigest, MonotonicMillis, SemanticNodeId,
    TaskId,
};
use bip_types::sensitivity::SensitivitySet;

use crate::action_class::ActionClass;
use crate::lease::{ActorLeaseId, ControlMode, RevocationReason};
use crate::phase::ActionPhase;
use crate::risk::RiskClass;

/// The version of the policy bundle a decision was made under.
///
/// It is recorded on every capability so an audit reader can tell which rules
/// were in force, and so a policy change can be told apart from a behaviour
/// change.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct PolicyVersion(pub u32);

impl fmt::Display for PolicyVersion {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "policy_v{}", self.0)
    }
}

/// Opaque capability identifier.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct CapabilityId(String);

impl CapabilityId {
    /// Wraps an identifier minted by an [`IdSource`].
    pub fn new(value: impl Into<String>) -> Self {
        Self(value.into())
    }

    /// The opaque value, for equality and audit correlation only.
    pub fn as_str(&self) -> &str {
        &self.0
    }

    /// The protocol reference for this capability.
    ///
    /// The reference is a pointer to a decision record held by the broker, not
    /// transferable authority: the browser process resolves it and never
    /// forwards it to a renderer.
    pub fn to_reference(&self) -> CapabilityReference {
        CapabilityReference(self.0.clone())
    }
}

impl fmt::Display for CapabilityId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Capability lifecycle (domain model section 12.4).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum CapabilityState {
    /// Issued and not yet used.
    Issued,
    /// A dispatch is under way. The side effect may already have reached the
    /// page, so this state is never returned to `ISSUED`.
    InFlight,
    /// Used. One capability authorizes one dispatch.
    Consumed,
    /// Withdrawn before use.
    Revoked,
    /// Past its expiry.
    Expired,
}

impl CapabilityState {
    /// Whether this state may become `next`.
    ///
    /// The relation is a strict progression: every terminal state is terminal,
    /// and nothing re-enters [`Self::Issued`].
    pub const fn may_become(self, next: Self) -> bool {
        matches!(
            (self, next),
            (
                Self::Issued,
                Self::InFlight | Self::Consumed | Self::Revoked | Self::Expired
            ) | (Self::InFlight, Self::Consumed | Self::Expired)
        )
    }

    /// Whether authority still stands.
    pub const fn is_live(self) -> bool {
        matches!(self, Self::Issued | Self::InFlight)
    }
}

/// Evidence that a capability was spent.
///
/// Returned by [`Capability::consume`] exactly once per capability. The
/// ordinal counts consumptions within one ledger, so a journal reader can order
/// dispatches without a clock.
#[derive(Clone, Debug, PartialEq)]
pub struct ConsumptionReceipt {
    /// The capability that was spent.
    pub capability_id: CapabilityId,
    /// The digest of the proposal it authorized.
    pub action_digest: ContentDigest,
    /// When it was spent.
    pub consumed_at: MonotonicMillis,
    /// Which consumption this was, counted within the ledger.
    pub ordinal: u64,
}

/// Why a capability could not be used.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum CapabilityError {
    /// No capability with that identifier exists.
    Unknown,
    /// The capability is past its expiry.
    Expired,
    /// The capability was withdrawn.
    Revoked,
    /// The capability was already spent. This is what a replay meets.
    AlreadyConsumed,
    /// A dispatch is already under way under this capability.
    AlreadyInFlight,
    /// The presented action digest is not the one that was authorized.
    DigestMismatch,
    /// The presented place is not the place that was authorized.
    ScopeMismatch,
    /// The ledger's consumption counter is exhausted.
    LedgerExhausted,
}

impl fmt::Display for CapabilityError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let text = match self {
            Self::Unknown => "no such capability",
            Self::Expired => "the capability expired",
            Self::Revoked => "the capability was revoked",
            Self::AlreadyConsumed => "the capability was already consumed",
            Self::AlreadyInFlight => "a dispatch is already under way",
            Self::DigestMismatch => "the action digest is not the authorized one",
            Self::ScopeMismatch => "the target is not the authorized one",
            Self::LedgerExhausted => "the consumption counter is exhausted",
        };
        formatter.write_str(text)
    }
}

/// Why a capability could not be issued.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum IssueError {
    /// No lease was presented, or the lease presented is not standing.
    LeaseNotActive,
    /// The lease covers a different tab.
    LeaseTabMismatch,
    /// The lease belongs to a different task.
    LeaseTaskMismatch,
    /// The control mode holds no lease and can authorize nothing.
    ControlModeGrantsNoLease,
    /// Shared control reviews every step, and this request carried no approval
    /// receipt.
    ApprovalRequired,
    /// The action class is not on the ratified surface for this milestone.
    ActionClassNotAuthorized,
    /// The requested expiry is now or in the past.
    ExpiryNotInFuture,
    /// The requested expiry outlives the lease. Authority never outlives the
    /// claim it was issued under.
    ExpiryOutlivesLease,
    /// A node-targeted action class was requested without a node.
    NodeTargetMissing,
    /// The identifier source is exhausted.
    IdSourceExhausted,
    /// The ledger already holds [`MAX_CAPABILITIES_PER_SESSION`] capabilities
    /// and may not drop one to make room: a dropped capability is one the
    /// dispatch path can no longer refuse by name.
    LedgerFull,
}

impl fmt::Display for IssueError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let text = match self {
            Self::LeaseNotActive => "no standing actor lease",
            Self::LeaseTabMismatch => "the lease covers a different tab",
            Self::LeaseTaskMismatch => "the lease belongs to a different task",
            Self::ControlModeGrantsNoLease => "user control authorizes nothing",
            Self::ApprovalRequired => "shared control reviews every step",
            Self::ActionClassNotAuthorized => "the action class is not on the ratified surface",
            Self::ExpiryNotInFuture => "a capability expiry must be in the future",
            Self::ExpiryOutlivesLease => "a capability never outlives its lease",
            Self::NodeTargetMissing => "a node-targeted action needs a node",
            Self::IdSourceExhausted => "the identifier source is exhausted",
            Self::LedgerFull => "the capability ledger is full",
        };
        formatter.write_str(text)
    }
}

/// One short-lived, one-use authorization.
#[derive(Clone, Debug, PartialEq)]
pub struct Capability {
    id: CapabilityId,
    task_id: TaskId,
    lease_id: ActorLeaseId,
    control_mode: ControlMode,
    principal: Principal,
    action_class: ActionClass,
    phase: ActionPhase,
    action_digest: ContentDigest,
    scope: CapabilityScope,
    data_classes: SensitivitySet,
    effective_risk: RiskClass,
    approval: Option<ApprovalReceiptReference>,
    policy_version: PolicyVersion,
    issued_at: MonotonicMillis,
    expires_at: MonotonicMillis,
    state: CapabilityState,
    consumed_at: Option<MonotonicMillis>,
    revocation_reason: Option<RevocationReason>,
}

impl Capability {
    /// The capability identifier.
    pub fn capability_id(&self) -> &CapabilityId {
        &self.id
    }

    /// The task this authority belongs to.
    pub fn task_id(&self) -> &TaskId {
        &self.task_id
    }

    /// The lease this authority was issued under.
    pub fn lease_id(&self) -> &ActorLeaseId {
        &self.lease_id
    }

    /// The control mode in force at issue time.
    pub fn control_mode(&self) -> ControlMode {
        self.control_mode
    }

    /// The principal that proposed the action.
    pub fn principal(&self) -> &Principal {
        &self.principal
    }

    /// What the effect does.
    pub fn action_class(&self) -> ActionClass {
        self.action_class
    }

    /// Which half of a two-step authorization this capability authorizes.
    ///
    /// Read from the record rather than from the request being dispatched, so a
    /// commit cannot present itself as the prepare it was authorized as, or the
    /// other way round.
    pub fn phase(&self) -> ActionPhase {
        self.phase
    }

    /// The digest of the proposal this capability authorizes.
    pub fn action_digest(&self) -> &ContentDigest {
        &self.action_digest
    }

    /// Where the effect may happen.
    pub fn scope(&self) -> &CapabilityScope {
        &self.scope
    }

    /// The data classes the action discloses.
    pub fn data_classes(&self) -> SensitivitySet {
        self.data_classes
    }

    /// How consequential the action was judged to be.
    pub fn effective_risk(&self) -> RiskClass {
        self.effective_risk
    }

    /// The approval receipt, where one was required.
    pub fn approval(&self) -> Option<&ApprovalReceiptReference> {
        self.approval.as_ref()
    }

    /// The policy bundle version this decision was made under.
    pub fn policy_version(&self) -> PolicyVersion {
        self.policy_version
    }

    /// When the authority was issued.
    pub fn issued_at(&self) -> MonotonicMillis {
        self.issued_at
    }

    /// When the authority stops standing.
    pub fn expires_at(&self) -> MonotonicMillis {
        self.expires_at
    }

    /// When the authority was spent.
    pub fn consumed_at(&self) -> Option<MonotonicMillis> {
        self.consumed_at
    }

    /// Why the authority was withdrawn, when it was.
    pub fn revocation_reason(&self) -> Option<RevocationReason> {
        self.revocation_reason
    }

    /// The state as of `now`.
    ///
    /// Expiry is computed, so a capability never looks live because a timer did
    /// not fire. A capability that already ended keeps the state it ended in.
    pub fn state_at(&self, now: MonotonicMillis) -> CapabilityState {
        match self.state {
            state if state.is_live() && now.0 >= self.expires_at.0 => CapabilityState::Expired,
            other => other,
        }
    }

    /// Whether the authority still stands as of `now`.
    pub fn is_live_at(&self, now: MonotonicMillis) -> bool {
        self.state_at(now).is_live()
    }

    /// Checks that a presented digest and target are the ones authorized.
    ///
    /// A changed target or a changed proposal is a different action, and a
    /// different action needs a different capability and normally a new
    /// approval.
    pub fn matches(
        &self,
        action_digest: &ContentDigest,
        node_id: Option<&SemanticNodeId>,
    ) -> Result<(), CapabilityError> {
        if self.action_digest.algorithm != action_digest.algorithm
            || self.action_digest.value != action_digest.value
        {
            return Err(CapabilityError::DigestMismatch);
        }
        if self.scope.node_id.as_ref() != node_id {
            return Err(CapabilityError::ScopeMismatch);
        }
        Ok(())
    }

    /// Marks a dispatch as under way.
    ///
    /// After this returns `Ok`, the side effect can no longer be ruled out, so
    /// the capability never becomes reusable again whatever happens next.
    pub fn begin_dispatch(&mut self, now: MonotonicMillis) -> Result<(), CapabilityError> {
        match self.state_at(now) {
            CapabilityState::Issued => {
                self.state = CapabilityState::InFlight;
                Ok(())
            }
            CapabilityState::InFlight => Err(CapabilityError::AlreadyInFlight),
            CapabilityState::Consumed => Err(CapabilityError::AlreadyConsumed),
            CapabilityState::Revoked => Err(CapabilityError::Revoked),
            CapabilityState::Expired => Err(CapabilityError::Expired),
        }
    }

    /// Spends the capability.
    ///
    /// Succeeds once. Every later call, and every call after a revocation or an
    /// expiry, fails — which is what makes a replayed authorization useless.
    fn consume(
        &mut self,
        now: MonotonicMillis,
        ordinal: u64,
    ) -> Result<ConsumptionReceipt, CapabilityError> {
        // An in-flight capability is spendable whatever the clock says.
        // Spending records an outcome; it authorizes nothing, and refusing to
        // record one because the expiry passed during the round trip would
        // leave authority that was used looking unused.
        let state = if self.state == CapabilityState::InFlight {
            CapabilityState::InFlight
        } else {
            self.state_at(now)
        };
        match state {
            CapabilityState::Issued | CapabilityState::InFlight => {
                self.state = CapabilityState::Consumed;
                self.consumed_at = Some(now);
                Ok(ConsumptionReceipt {
                    capability_id: self.id.clone(),
                    action_digest: self.action_digest.clone(),
                    consumed_at: now,
                    ordinal,
                })
            }
            CapabilityState::Consumed => Err(CapabilityError::AlreadyConsumed),
            CapabilityState::Revoked => Err(CapabilityError::Revoked),
            CapabilityState::Expired => Err(CapabilityError::Expired),
        }
    }

    /// Withdraws unspent authority.
    ///
    /// Returns `false` when the capability was already spent: a dispatch that
    /// has left the broker cannot be recalled, and pretending otherwise would
    /// let a caller believe an effect was prevented when it was not.
    pub fn revoke(&mut self, reason: RevocationReason) -> bool {
        if self.state == CapabilityState::Issued {
            self.state = CapabilityState::Revoked;
            self.revocation_reason = Some(reason);
            true
        } else {
            false
        }
    }
}
