// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Every capability the broker has issued this session (domain model section
//! 12.4).
//!
//! The ledger is the broker's own record. A message can present an identifier;
//! it can never present a state, because the state is only ever read from here.
//!
//! Issuance is where every static check happens — control mode, lease standing,
//! lease-to-task and lease-to-tab agreement, milestone surface, approval,
//! expiry, and node targeting — so a capability that exists has already passed
//! all of them and the dispatch path only has to check what can change
//! afterwards.

use std::collections::BTreeMap;

use bip_types::identity::MonotonicMillis;

use crate::capability::{
    Capability, CapabilityError, CapabilityId, CapabilityRequest, CapabilityState,
    ConsumptionReceipt, IssueContext, IssueError,
};
use crate::lease::{ActorLease, ActorLeaseId, RevocationReason};
use crate::time::{IdKind, IdSource};

/// How many capabilities one broker session may issue.
///
/// The ledger is the broker's own record and the only thing that can say a
/// capability was revoked, expired, or already spent. An entry it dropped
/// would come back from [`CapabilityLedger::get`] as `None`, which the
/// dispatch path reads as "never issued" — the same refusal, but recorded
/// under the wrong fact, and one that a caller could reach by issuing
/// capabilities until the entry it wanted gone fell out. So nothing is
/// dropped, the register has a ceiling instead, and reaching it refuses
/// issuance with [`IssueError::LedgerFull`].
pub const MAX_CAPABILITIES_PER_SESSION: usize = 4_096;

/// Every capability the broker has issued this session.
///
/// The ledger is the broker's own record. A message can present an identifier;
/// it can never present a state, because the state is only ever read from here.
/// It holds at most [`MAX_CAPABILITIES_PER_SESSION`] entries and never removes
/// one.
#[derive(Clone, Debug, Default)]
pub struct CapabilityLedger {
    entries: BTreeMap<CapabilityId, Capability>,
    consumptions: u64,
}

impl CapabilityLedger {
    /// An empty ledger.
    pub const fn new() -> Self {
        Self {
            entries: BTreeMap::new(),
            consumptions: 0,
        }
    }

    /// Issues a capability under a standing lease.
    ///
    /// Issuance is where every static check happens: control mode, lease
    /// standing, lease-to-task and lease-to-tab agreement, milestone surface,
    /// approval, expiry, and node targeting. A capability that exists has
    /// already passed all of them, so the dispatch path only has to check what
    /// can change afterwards.
    pub fn issue(
        &mut self,
        request: &CapabilityRequest,
        lease: &ActorLease,
        context: IssueContext,
        ids: &mut impl IdSource,
        now: MonotonicMillis,
    ) -> Result<CapabilityId, IssueError> {
        let IssueContext {
            milestone,
            policy_version,
            approval,
        } = context;
        if !lease.control_mode().grants_lease() {
            return Err(IssueError::ControlModeGrantsNoLease);
        }
        if !lease.is_active_at(now) {
            return Err(IssueError::LeaseNotActive);
        }
        if lease.tab_id() != &request.scope.tab_id {
            return Err(IssueError::LeaseTabMismatch);
        }
        if lease.task_id() != &request.task_id {
            return Err(IssueError::LeaseTaskMismatch);
        }
        if !request.action_class.is_authorized_at(milestone) {
            return Err(IssueError::ActionClassNotAuthorized);
        }
        // Defence in depth: the broker already refuses a shared-control
        // proposal without an approval, and the ledger refuses to record one
        // anyway. Two independent checks, because the record is what an audit
        // reader believes.
        if lease.control_mode().requires_step_approval() && approval.is_none() {
            return Err(IssueError::ApprovalRequired);
        }
        if request.expires_at.0 <= now.0 {
            return Err(IssueError::ExpiryNotInFuture);
        }
        if request.expires_at.0 > lease.expires_at().0 {
            return Err(IssueError::ExpiryOutlivesLease);
        }
        if !request.action_class.scope_is_complete(
            request.scope.node_id.is_some(),
            request.scope.destination_scope.is_some(),
        ) {
            return Err(IssueError::NodeTargetMissing);
        }
        // Checked with the other static gates, before an identifier is spent,
        // so a refused issuance leaves the ledger and the identifier source
        // exactly where it found them.
        if self.entries.len() >= MAX_CAPABILITIES_PER_SESSION {
            return Err(IssueError::LedgerFull);
        }

        let capability_id = ids
            .next_id(IdKind::Capability)
            .map(CapabilityId::new)
            .ok_or(IssueError::IdSourceExhausted)?;

        self.entries.insert(
            capability_id.clone(),
            Capability {
                id: capability_id.clone(),
                task_id: request.task_id.clone(),
                lease_id: lease.lease_id().clone(),
                control_mode: lease.control_mode(),
                principal: request.principal.clone(),
                action_class: request.action_class,
                phase: request.phase,
                action_digest: request.action_digest.clone(),
                scope: request.scope.clone(),
                data_classes: request.data_classes,
                effective_risk: request.effective_risk(),
                approval,
                policy_version,
                issued_at: now,
                expires_at: request.expires_at,
                state: CapabilityState::Issued,
                consumed_at: None,
                revocation_reason: None,
            },
        );
        Ok(capability_id)
    }

    /// The capability with this identifier.
    pub fn get(&self, capability_id: &CapabilityId) -> Option<&Capability> {
        self.entries.get(capability_id)
    }

    /// Marks a dispatch under way.
    pub fn begin_dispatch(
        &mut self,
        capability_id: &CapabilityId,
        now: MonotonicMillis,
    ) -> Result<(), CapabilityError> {
        self.entries
            .get_mut(capability_id)
            .ok_or(CapabilityError::Unknown)?
            .begin_dispatch(now)
    }

    /// Spends a capability, once.
    pub fn consume(
        &mut self,
        capability_id: &CapabilityId,
        now: MonotonicMillis,
    ) -> Result<ConsumptionReceipt, CapabilityError> {
        let ordinal = self
            .consumptions
            .checked_add(1)
            .ok_or(CapabilityError::LedgerExhausted)?;
        let receipt = self
            .entries
            .get_mut(capability_id)
            .ok_or(CapabilityError::Unknown)?
            .consume(now, ordinal)?;
        self.consumptions = ordinal;
        Ok(receipt)
    }

    /// Withdraws every unspent capability issued under a lease.
    ///
    /// This is what take over calls. It returns the capabilities it withdrew
    /// and, separately, the ones already in flight, because those are past
    /// recall and their outcome has to be reconciled rather than cancelled.
    pub fn revoke_undispatched_for_lease(
        &mut self,
        lease_id: &ActorLeaseId,
        reason: RevocationReason,
    ) -> RevokedAuthority {
        let mut revoked = Vec::new();
        let mut in_flight = Vec::new();
        for capability in self.entries.values_mut() {
            if capability.lease_id() != lease_id {
                continue;
            }
            if capability.revoke(reason) {
                revoked.push(capability.capability_id().clone());
            } else if capability.state == CapabilityState::InFlight {
                in_flight.push(capability.capability_id().clone());
            }
        }
        RevokedAuthority { revoked, in_flight }
    }

    /// Every capability, in deterministic identifier order.
    pub fn entries(&self) -> impl Iterator<Item = &Capability> {
        self.entries.values()
    }

    /// How many capabilities have been spent.
    pub fn consumption_count(&self) -> u64 {
        self.consumptions
    }

    /// How many capabilities the ledger holds.
    ///
    /// Public so a test can assert the bound itself rather than the mechanism
    /// that keeps it. Never exceeds [`MAX_CAPABILITIES_PER_SESSION`].
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether the ledger has issued nothing.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

/// What a revocation reached and what it could not reach.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RevokedAuthority {
    /// Capabilities withdrawn before dispatch. These will never take effect.
    pub revoked: Vec<CapabilityId>,
    /// Capabilities already dispatched. Their effect cannot be ruled out and is
    /// reconciled from the observed outcome, never assumed to have been
    /// stopped.
    pub in_flight: Vec<CapabilityId>,
}

#[cfg(test)]
mod tests;
