// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The decision surface the task engine calls.
//!
//! [`PolicyEngine`] owns the four records that decide whether an effect may
//! happen — the lease book, the approval book, the capability ledger, and the
//! prepared-effect ledger — and exposes the moments a task runtime has to ask
//! about:
//!
//! 1. [`PolicyEngine::issue_lease`] — the assistant becomes the actor in a tab;
//! 2. [`PolicyEngine::present_approval`] and
//!    [`PolicyEngine::record_approval_decision`] — a person is asked one exact
//!    question and answers it;
//! 3. [`PolicyEngine::decide_proposal`] — a proposal becomes authority, an
//!    approval request, or a refusal;
//! 4. [`PolicyEngine::authorize_dispatch`] — the state at dispatch time is
//!    rechecked and a one-use [`crate::dispatch::DispatchTicket`] is handed
//!    back;
//! 5. [`PolicyEngine::consume_capability`] — the authority is spent.
//!
//! A consequential effect adds three more between steps four and five, because
//! it is authorized twice (decision
//! `docs/decisions/0022-prepare-and-commit-for-consequential-actions.md`):
//! [`PolicyEngine::stage_prepared_effect`] records what a dispatched prepare
//! staged, [`PolicyEngine::record_rendered_effect`] records what the trusted
//! surface then put in front of a person, and
//! [`PolicyEngine::discard_prepared_effect`] withdraws a preparation that will
//! never be committed. None of the three mints anything: their only power over
//! the commit that follows is to refuse it.
//!
//! Plus one that arrives from outside the task: [`PolicyEngine::user_took_over`],
//! which is synchronous and complete when it returns.
//!
//! # Two gates, not one
//!
//! Every proposal passes the milestone surface *and* the risk lattice. The
//! surface says which classes of effect the product has authorized; the lattice
//! says how consequential this particular one turned out to be once its context
//! was taken into account. Either alone refuses. Neither can be relaxed by a
//! parameter, and no path exists from a page or a model to either of them.
//!
//! Both read the broker's milestone, and they read it separately. Decision 0089
//! ratified the write surface at [`PolicyMilestone::M5`], which meant widening
//! both: a fill's baseline risk is a sensitive disclosure, so a milestone-blind
//! risk gate would have gone on refusing every write the class surface had just
//! authorized — the same refusal under a different name.

use bip_types::identity::{MonotonicMillis, SemanticNodeId, TabId};
use bip_types::ActionResultCode;

use crate::action_class::PolicyMilestone;
use crate::approval::book::{ApprovalBook, ApprovalRequest};
use crate::approval::{Approval, ApprovalDecision, ApprovalError, ApprovalId, UserGestureReceipt};
use crate::capability::{
    Capability, CapabilityError, CapabilityId, CapabilityLedger, CapabilityRequest,
    ConsumptionReceipt, IssueContext, IssueError, PolicyVersion, RevokedAuthority,
};
use crate::denial::{Denial, DenialReason};
use crate::dispatch::{DispatchDecision, DispatchTicket};
use crate::field_allowance::per_use_confirmation_required;
use crate::lease::{
    ActorLease, ActorLeaseId, LeaseBook, LeaseError, LeaseRequest, RevocationReason,
};
use crate::precondition::{
    evaluate_dispatch, BrokerStanding, CapabilityStanding, DispatchCheck, DispatchVerdict,
    LeaseStanding, NormalizedDestination, ObservedState,
};
use crate::prepared::{
    plan_commit, CommitContext, CommitPlan, PrepareError, PrepareRequest, PreparedEffectId,
    PreparedEffectLedger, RenderedEffect,
};
use crate::risk::RiskClass;
use crate::sequence::{SequenceInput, StaleNodeSequence};
use crate::site::assistant_navigation_verdict;
use crate::step::StaleNodeStep;
use crate::time::IdSource;

/// Authority granted for exactly one proposal.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Authorization {
    /// The capability that was issued.
    pub capability_id: CapabilityId,
    /// The lease it was issued under.
    pub lease_id: ActorLeaseId,
    /// The policy bundle version the decision was made under.
    pub policy_version: PolicyVersion,
    /// The effective risk the decision was made at.
    pub effective_risk: RiskClass,
}

/// The outcome of asking whether a proposal may become authority.
#[derive(Clone, Debug, PartialEq)]
pub enum ProposalDecision {
    /// Authority was issued.
    Authorize(Authorization),
    /// The user has to decide first. No authority exists yet.
    RequireApproval,
    /// Refused.
    Deny(Denial),
}

impl ProposalDecision {
    /// The capability, when one was issued.
    pub const fn capability_id(&self) -> Option<&CapabilityId> {
        match self {
            Self::Authorize(authorization) => Some(&authorization.capability_id),
            Self::RequireApproval | Self::Deny(_) => None,
        }
    }

    /// The result code this decision reports, for a decision that ends the
    /// action here.
    pub const fn result_code(&self) -> Option<ActionResultCode> {
        match self {
            Self::Authorize(_) => None,
            Self::RequireApproval => Some(ActionResultCode::ApprovalRequired),
            Self::Deny(denial) => Some(denial.code),
        }
    }
}

/// What a take-over reached.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TakeOver {
    /// The lease that ended, when one was standing.
    pub revoked_lease: Option<ActorLeaseId>,
    /// Authority withdrawn before dispatch, and authority already past recall.
    pub authority: RevokedAuthority,
    /// Prepared effects withdrawn because the lease they were staged under
    /// ended (decision 0022).
    ///
    /// A preparation is consent to stage under one lease and no other, so a
    /// take-over between the two steps ends it. Reported rather than silent,
    /// because a person who was shown a staged effect is owed the fact that it
    /// will not be committed.
    pub discarded_prepared_effects: Vec<PreparedEffectId>,
}

impl TakeOver {
    /// Whether anything was still standing when the user took over.
    pub fn changed_anything(&self) -> bool {
        self.revoked_lease.is_some()
            || !self.authority.revoked.is_empty()
            || !self.discarded_prepared_effects.is_empty()
    }
}

/// The parts of a proposal the dispatch check needs that the capability does
/// not already pin.
#[derive(Clone, Debug, PartialEq)]
pub struct DispatchProposal {
    /// The digest of the proposal being dispatched. It must be the one that was
    /// authorized.
    pub action_digest: bip_types::identity::ContentDigest,
    /// The node the proposal targets.
    pub node_id: Option<SemanticNodeId>,
    /// The role the node had when it was observed.
    pub expected_role: bip_types::snapshot::SemanticRole,
    /// The protocol action type.
    pub action_type: bip_types::action::ActionType,
    /// The destination the proposal expects.
    pub expected_destination: Option<NormalizedDestination>,
    /// The proposal's own preconditions.
    pub preconditions: Vec<bip_types::action::Precondition>,
}

/// The policy engine: the only core component that may mint a grant.
#[derive(Clone, Debug)]
pub struct PolicyEngine<I> {
    leases: LeaseBook,
    approvals: ApprovalBook,
    capabilities: CapabilityLedger,
    prepared: PreparedEffectLedger,
    ids: I,
    milestone: PolicyMilestone,
    policy_version: PolicyVersion,
}

impl<I: IdSource> PolicyEngine<I> {
    /// Builds a broker for one profile session.
    pub const fn new(ids: I, milestone: PolicyMilestone, policy_version: PolicyVersion) -> Self {
        Self {
            leases: LeaseBook::new(),
            approvals: ApprovalBook::new(),
            capabilities: CapabilityLedger::new(),
            prepared: PreparedEffectLedger::new(),
            ids,
            milestone,
            policy_version,
        }
    }

    /// The milestone surface this broker decides against.
    pub const fn milestone(&self) -> PolicyMilestone {
        self.milestone
    }

    /// The policy bundle version recorded on every decision.
    pub const fn policy_version(&self) -> PolicyVersion {
        self.policy_version
    }

    /// The leases held right now.
    pub const fn leases(&self) -> &LeaseBook {
        &self.leases
    }

    /// The approvals presented this session.
    pub const fn approvals(&self) -> &ApprovalBook {
        &self.approvals
    }

    /// The capabilities issued this session.
    pub const fn capabilities(&self) -> &CapabilityLedger {
        &self.capabilities
    }

    /// Makes the assistant the actor in a tab.
    pub fn issue_lease(
        &mut self,
        request: &LeaseRequest,
        now: MonotonicMillis,
    ) -> Result<ActorLeaseId, LeaseError> {
        self.leases
            .issue(request, &mut self.ids, now)
            .map(|lease| lease.lease_id().clone())
    }

    /// Records that the trusted approval surface asked a person one exact
    /// question.
    pub fn present_approval(
        &mut self,
        request: &ApprovalRequest,
        now: MonotonicMillis,
    ) -> Result<ApprovalId, ApprovalError> {
        self.approvals.present(request, &mut self.ids, now)
    }

    /// Records the answer a person gave.
    pub fn record_approval_decision(
        &mut self,
        approval_id: &ApprovalId,
        decision: ApprovalDecision,
        gesture: Option<UserGestureReceipt>,
        now: MonotonicMillis,
    ) -> Result<(), ApprovalError> {
        self.approvals.decide(approval_id, decision, gesture, now)
    }

    /// One approval this session presented.
    pub fn approval(&self, approval_id: &ApprovalId) -> Option<&Approval> {
        self.approvals.get(approval_id)
    }

    /// Decides whether a proposal becomes authority.
    ///
    /// The class and the effective risk are checked before anything else, both
    /// against this broker's milestone, so a class the surface does not carry
    /// is refused with the reason that names the milestone rather than with a
    /// lease or scope error that would hide it. Shared control turns a proposal
    /// without an approval into [`ProposalDecision::RequireApproval`] rather
    /// than a denial: the user has not said no, they have not been asked.
    ///
    /// A fill of a field class confirmed per use is the same answer for the
    /// same reason (decision 0089 section 3). It is asked as a question about
    /// the field, independently of the two conditions beside it, so a screen
    /// that never drew the sheet is refused here rather than authorized by
    /// default.
    pub fn decide_proposal(
        &mut self,
        request: &CapabilityRequest,
        now: MonotonicMillis,
    ) -> ProposalDecision {
        if let Some(reason) = DenialReason::for_class(request.action_class, self.milestone) {
            return ProposalDecision::Deny(Denial::new(reason));
        }
        let effective_risk = request.effective_risk();
        if let Some(reason) = DenialReason::for_risk(effective_risk, self.milestone) {
            return ProposalDecision::Deny(Denial::new(reason));
        }

        let lease = match self.leases.active(&request.scope.tab_id, now) {
            Some(lease) => lease.clone(),
            None => return ProposalDecision::Deny(Denial::new(DenialReason::NoStandingLease)),
        };

        // The third reason a proposal is put back to a person, and it is a
        // statement about the class of *field* rather than about the control
        // mode or the risk beside it (decision 0089 section 3). The three are
        // independent on purpose: a surface that forgets to ask cannot make the
        // fill happen, and it stays that way even if one of the other two is
        // relaxed.
        let needs_approval = lease.control_mode().requires_step_approval()
            || effective_risk.requires_exact_approval()
            || per_use_confirmation_required(request.action_class, request.data_classes);
        let receipt = match (&request.approval, needs_approval) {
            (Some(approval_id), _) => {
                let binding = request.approval_binding(effective_risk);
                match self
                    .approvals
                    .consume(approval_id, &request.task_id, &binding, now)
                {
                    Ok(receipt) => Some(receipt),
                    Err(error) => return ProposalDecision::Deny(Denial::from_approval(error)),
                }
            }
            (None, true) => return ProposalDecision::RequireApproval,
            (None, false) => None,
        };

        // The destination-class table, for a navigation that names where it
        // goes by origin rather than by a node — the same reading the
        // production decider takes in `GrantPolicy::decide`, stated here too
        // because a rule in one decider is a rule the other does not have. A
        // node-anchored navigation is judged at dispatch over the live node;
        // the origin the scope already stands on is never refused.
        if request.scope.node_id.is_none() {
            if let Some(destination) = request.scope.destination_scope.as_ref() {
                if let Some(reason) = assistant_navigation_verdict(
                    request.action_class,
                    destination,
                    *destination == request.scope.origin,
                ) {
                    return ProposalDecision::Deny(Denial::new(reason));
                }
            }
        }

        let context = IssueContext {
            milestone: self.milestone,
            policy_version: self.policy_version,
            approval: receipt,
        };
        match self
            .capabilities
            .issue(request, &lease, context, &mut self.ids, now)
        {
            Ok(capability_id) => ProposalDecision::Authorize(Authorization {
                capability_id,
                lease_id: lease.lease_id().clone(),
                policy_version: self.policy_version,
                effective_risk,
            }),
            Err(IssueError::ApprovalRequired) => ProposalDecision::RequireApproval,
            Err(error) => ProposalDecision::Deny(Denial::new(issue_denial(error))),
        }
    }

    /// Runs steps one to six and hands back a one-use dispatch ticket.
    ///
    /// The capability's own scope supplies the tab, frame, epoch, origin,
    /// revision, node, destination, and redirect set, so a caller cannot
    /// present a wider check than the one that was authorized: the fields it
    /// does supply are the ones that describe the proposal, not the ones that
    /// describe the authority.
    ///
    /// The returned ticket borrows this broker for as long as another dispatch
    /// could still be started from it. See [`crate::dispatch`] for what that
    /// makes unrepresentable.
    pub fn authorize_dispatch(
        &mut self,
        capability_id: &CapabilityId,
        proposal: &DispatchProposal,
        observed: &ObservedState,
        now: MonotonicMillis,
    ) -> DispatchDecision<'_, I> {
        let Some(capability) = self.capabilities.get(capability_id) else {
            return refuse(
                StaleNodeStep::ConfirmLeaseAndCapability,
                ActionResultCode::CapabilityExpired,
            );
        };
        let capability = capability.clone();

        if let Err(error) = capability.matches(&proposal.action_digest, proposal.node_id.as_ref()) {
            return refuse(
                StaleNodeStep::ConfirmLeaseAndCapability,
                dispatch_code(error),
            );
        }

        let lease_standing = match self.leases.by_id(capability.lease_id()) {
            Some(lease) if lease.is_active_at(now) => LeaseStanding::Active,
            _ => LeaseStanding::Missing,
        };
        let capability_standing = if capability.is_live_at(now) {
            CapabilityStanding::Valid
        } else {
            CapabilityStanding::NotValid
        };

        let scope = capability.scope();

        // Decision 0022. What this request may commit is read from the ledger
        // before the sequence runs, and the reading spends nothing: a commit
        // refused for a stale epoch never reached the page and must not cost
        // the preparation that was staged for it.
        let commit_context = CommitContext {
            task_id: capability.task_id(),
            lease_id: capability.lease_id(),
            action_class: capability.action_class(),
            phase: capability.phase(),
            scope,
        };
        let plan = plan_commit(
            &self.prepared,
            &commit_context,
            &proposal.preconditions,
            now,
        );
        // A malformed shape is a fact about the authority rather than about the
        // world, so it refuses beside the digest and scope checks at step four
        // rather than being reported as a precondition that failed.
        if let Some(code) = plan.malformed_code() {
            return refuse(StaleNodeStep::ConfirmLeaseAndCapability, code);
        }

        let check = DispatchCheck {
            tab_id: &scope.tab_id,
            frame_id: &scope.frame_id,
            required_page_epoch: &scope.page_epoch,
            required_graph_revision: scope.required_graph_revision,
            required_origin: &scope.origin,
            required_destination: scope.destination_scope.as_ref(),
            allowed_redirects: &scope.allowed_redirects,
            node_id: scope.node_id.as_ref(),
            expected_role: proposal.expected_role,
            action_type: proposal.action_type,
            action_class: capability.action_class(),
            expected_destination: proposal.expected_destination.as_ref(),
            preconditions: &proposal.preconditions,
        };

        let standing =
            BrokerStanding::new(lease_standing, capability_standing).with_prepared(plan.standing());
        let verdict = evaluate_dispatch(&check, standing, observed);
        let mut sequence = StaleNodeSequence::new();
        sequence.step(SequenceInput::Preconditions(verdict));

        match verdict {
            DispatchVerdict::Refuse { step, code } => DispatchDecision::Refuse { step, code },
            DispatchVerdict::Proceed => {
                match self.capabilities.begin_dispatch(capability_id, now) {
                    Ok(()) => {
                        let action_class = capability.action_class();
                        let node_id = scope.node_id.clone();
                        // The sequence has proceeded and the capability is
                        // spent, so the prepared effect is spent here and
                        // exactly here. A replayed commit meets
                        // `AlreadyCommitted` rather than a second success.
                        if let CommitPlan::Committable(binding) = &plan {
                            if let Err(refusal) =
                                self.prepared.commit(binding, &commit_context, now)
                            {
                                return refuse(
                                    StaleNodeStep::ReevaluateNode,
                                    refusal.result_code(),
                                );
                            }
                        }
                        DispatchDecision::Dispatch(DispatchTicket::new(
                            self,
                            capability_id.clone(),
                            action_class,
                            node_id,
                            sequence,
                        ))
                    }
                    Err(error) => refuse(
                        StaleNodeStep::ConfirmLeaseAndCapability,
                        dispatch_code(error),
                    ),
                }
            }
        }
    }

    /// Records that a prepare was authorized and has staged an effect
    /// (decision 0022).
    ///
    /// Mints nothing, and returns nothing a commit could present. The staged
    /// record's only power is to let a later commit be **refused** by name; a
    /// commit still asks for authority exactly as any other proposal does.
    ///
    /// The ordinary caller is the broker, immediately after the prepare half of
    /// a two-phase action has dispatched.
    pub fn stage_prepared_effect(
        &mut self,
        request: &PrepareRequest,
        now: MonotonicMillis,
    ) -> Result<(), PrepareError> {
        self.prepared.stage(request, now)
    }

    /// Records what the trusted surface put in front of a person.
    ///
    /// This is the moment a commit's binding is fixed, and it is fixed to what
    /// was *displayed*. Passing the prepare's own proposal digest here is
    /// refused by name: it would bind the request a model made rather than the
    /// sentence a person read.
    pub fn record_rendered_effect(
        &mut self,
        prepared_effect_id: &PreparedEffectId,
        rendered: &RenderedEffect,
        now: MonotonicMillis,
    ) -> Result<(), PrepareError> {
        self.prepared
            .record_rendered(prepared_effect_id, rendered, now)
    }

    /// Withdraws one prepared effect before it is committed.
    ///
    /// Returns `false` when there was nothing live to withdraw — including a
    /// record already committed, because an effect that has left the broker
    /// cannot be recalled.
    pub fn discard_prepared_effect(&mut self, prepared_effect_id: &PreparedEffectId) -> bool {
        self.prepared.discard(prepared_effect_id)
    }

    /// Every prepared effect this session staged, read-only.
    pub const fn prepared_effects(&self) -> &PreparedEffectLedger {
        &self.prepared
    }

    /// Spends a capability.
    ///
    /// A capability is spent by having been dispatched, not by having
    /// succeeded, so a failed action never leaves reusable authority behind.
    /// The ordinary caller is [`crate::dispatch::SettledDispatch::record`];
    /// this is also how authority is spent for an action that ended before any
    /// ticket existed.
    pub fn consume_capability(
        &mut self,
        capability_id: &CapabilityId,
        now: MonotonicMillis,
    ) -> Result<ConsumptionReceipt, CapabilityError> {
        self.capabilities.consume(capability_id, now)
    }

    /// The user took over a tab.
    ///
    /// Synchronous and local: when this returns, the lease has ended and every
    /// undispatched capability issued under it has been withdrawn. Nothing
    /// waits for a page, a model, or a network. Authority already in flight is
    /// reported rather than pretended away — its effect cannot be ruled out and
    /// has to be reconciled from what is observed.
    pub fn user_took_over(&mut self, tab_id: &TabId, reason: RevocationReason) -> TakeOver {
        let revoked_lease = self.leases.revoke(tab_id, reason);
        let authority = match revoked_lease.as_ref() {
            Some(lease_id) => self
                .capabilities
                .revoke_undispatched_for_lease(lease_id, reason),
            None => RevokedAuthority::default(),
        };
        // A prepared effect outliving the lease it was staged under would be a
        // commit the next lease could make against a sheet this person read.
        let discarded_prepared_effects = match revoked_lease.as_ref() {
            Some(lease_id) => self.prepared.discard_for_lease(lease_id),
            None => Vec::new(),
        };
        TakeOver {
            revoked_lease,
            authority,
            discarded_prepared_effects,
        }
    }

    /// The lease covering a tab right now.
    pub fn active_lease(&self, tab_id: &TabId, now: MonotonicMillis) -> Option<&ActorLease> {
        self.leases.active(tab_id, now)
    }

    /// One issued capability.
    pub fn capability(&self, capability_id: &CapabilityId) -> Option<&Capability> {
        self.capabilities.get(capability_id)
    }
}

/// Builds a dispatch refusal.
const fn refuse<'engine, I: IdSource>(
    step: StaleNodeStep,
    code: ActionResultCode,
) -> DispatchDecision<'engine, I> {
    DispatchDecision::Refuse { step, code }
}

/// Maps an issue failure to the reason a person is shown.
const fn issue_denial(error: IssueError) -> DenialReason {
    match error {
        IssueError::LeaseNotActive => DenialReason::NoStandingLease,
        IssueError::LeaseTabMismatch | IssueError::LeaseTaskMismatch => {
            DenialReason::LeaseDoesNotCoverTarget
        }
        IssueError::ControlModeGrantsNoLease => DenialReason::ControlModeGrantsNoLease,
        IssueError::ApprovalRequired => DenialReason::ApprovalNotFound,
        IssueError::ActionClassNotAuthorized => DenialReason::ActionClassNotAuthorized,
        IssueError::ExpiryNotInFuture | IssueError::ExpiryOutlivesLease => {
            DenialReason::ExpiryNotUsable
        }
        IssueError::NodeTargetMissing => DenialReason::NodeTargetMissing,
        IssueError::IdSourceExhausted => DenialReason::IdSourceExhausted,
        IssueError::LedgerFull => DenialReason::AuthorityRegisterFull,
    }
}

/// Maps a capability failure to the code the action ends with.
///
/// Expiry, revocation, and prior consumption all end the action the same way at
/// the page-facing boundary. The difference between them is a decision fact and
/// belongs in the audit record, not in a code a page can observe.
const fn dispatch_code(error: CapabilityError) -> ActionResultCode {
    match error {
        CapabilityError::DigestMismatch | CapabilityError::ScopeMismatch => {
            ActionResultCode::DeniedByPolicy
        }
        _ => ActionResultCode::CapabilityExpired,
    }
}
