// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Stateless production grant minting over browser-owned lease facts.
//!
//! The browser owns the actor-lease registry and the capability ledger. This
//! module receives one immutable lease fact, validates every binding, and
//! mints one generation-bound grant. It stores no lease and cannot spend or
//! widen the grant it returns.
//!
//! # This is the decider the product runs
//!
//! [`GrantPolicy::decide`] is what `ProductionPolicy` in the core runtime
//! constructs and calls; [`crate::engine::PolicyEngine::decide_proposal`] is
//! the stateful broker beside it. Every rule about what may be authorized has
//! to be written in both, because a rule in one of them is a rule the shipping
//! answer does not have. That is why the milestone surface, the milestone-aware
//! risk gate and the per-use confirmation of decision 0089 each appear twice,
//! and why a test compares the two deciders on the same request.

use bip_types::action::{Principal, PrincipalKind};
use bip_types::identity::{
    ActionId, ApprovalReceiptReference, ContentDigest, MonotonicMillis, ProfileId, TabId, TaskId,
};
use bip_types::sensitivity::SensitivitySet;

use crate::action_class::{ActionClass, PolicyMilestone};
use crate::action_operation::ActionOperationKind;
use crate::capability::{CapabilityId, CapabilityScope, PolicyVersion};
use crate::denial::{Denial, DenialReason};
use crate::field_allowance::per_use_confirmation_required;
use crate::lease::{ActorLeaseId, ControlMode};
use crate::origin::NormalizedOrigin;
use crate::risk::RiskClass;
use crate::time::{IdKind, IdSource};

/// Maximum bytes in the opaque effect identity bound into a grant.
pub const MAX_GRANT_IDEMPOTENCY_KEY_BYTES: usize = 256;
/// Maximum bytes in a typed authority-subject identity.
pub const MAX_AUTHORITY_SUBJECT_ID_BYTES: usize = 128;
const DIRECT_USER_INTENT_PREFIX: &str = "direct-intent-";
/// Product bound for origins a single accepted errand may discover.
pub const MAX_TASK_DISCOVERY_SOURCE_CAP: u32 = 8;

/// Why an authority subject cannot enter policy evaluation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AuthoritySubjectIdError {
    /// No authority can be bound to an empty identity.
    Empty,
    /// The identity crossed the fixed service boundary.
    TooLong,
    /// A direct intent must use its reserved namespace.
    WrongFamily,
}

/// Browser-minted identity for one taskless direct user intent.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct DirectUserIntentId(String);

impl DirectUserIntentId {
    /// Validates one bounded identity in the reserved direct-intent namespace.
    pub fn new(value: impl Into<String>) -> Result<Self, AuthoritySubjectIdError> {
        let value = value.into();
        if value.is_empty() {
            return Err(AuthoritySubjectIdError::Empty);
        }
        if value.len() > MAX_AUTHORITY_SUBJECT_ID_BYTES {
            return Err(AuthoritySubjectIdError::TooLong);
        }
        if !value.starts_with(DIRECT_USER_INTENT_PREFIX) {
            return Err(AuthoritySubjectIdError::WrongFamily);
        }
        Ok(Self(value))
    }

    /// Exact opaque browser identity.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// The identity family to which a lease, grant, and effect are bound.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum AuthoritySubject {
    /// One durable task aggregate.
    Task(TaskId),
    /// One taskless direct user intent.
    DirectUserIntent(DirectUserIntentId),
}

impl AuthoritySubject {
    /// Exact opaque identity without erasing its family in the domain type.
    pub fn as_str(&self) -> &str {
        match self {
            Self::Task(value) => value.as_str(),
            Self::DirectUserIntent(value) => value.as_str(),
        }
    }

    /// The legacy task projection, absent for direct user intent.
    pub const fn task_id(&self) -> Option<&TaskId> {
        match self {
            Self::Task(value) => Some(value),
            Self::DirectUserIntent(_) => None,
        }
    }
}

/// The closed decision context. Discovery is task authority, but it is not
/// ordinary page authority: it admits one search, or one typed navigate to an
/// https address, in one browser-owned tab.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PolicyEvaluationContext {
    /// A durable task proposal.
    Task,
    /// One bounded search or typed navigate from a zero-source task's exact
    /// discovery tab.
    TaskDiscovery,
    /// One explicit read of the selected live page.
    DirectUserObservation,
}

/// Browser-owned fact narrowing a zero-source errand to one discovery tab.
///
/// This is deliberately not a source or an origin grant. Policy echoes it so
/// the browser ledger can require the same session, tab and remaining cap at
/// dispatch; the resulting tuple source is admitted only by the verified
/// browser terminal that follows the search.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskDiscoveryAuthorityFact {
    pub discovery_tab_id: TabId,
    pub browser_session_id: String,
    pub remaining_new_source_cap: u32,
}

/// Why an effect identity cannot enter policy evaluation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GrantIdempotencyKeyError {
    /// An empty identity cannot make retries recognizable.
    Empty,
    /// The identity crossed the fixed service boundary.
    TooLong,
}

/// Bounded opaque identity binding one proposal, grant, and effect intent.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct GrantIdempotencyKey(String);

impl GrantIdempotencyKey {
    /// Validates one browser-minted opaque identity.
    pub fn new(value: impl Into<String>) -> Result<Self, GrantIdempotencyKeyError> {
        let value = value.into();
        if value.is_empty() {
            return Err(GrantIdempotencyKeyError::Empty);
        }
        if value.len() > MAX_GRANT_IDEMPOTENCY_KEY_BYTES {
            return Err(GrantIdempotencyKeyError::TooLong);
        }
        Ok(Self(value))
    }

    /// Exact opaque value used for equality and persistence.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// Browser-owned actor state proven for one policy evaluation.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ActorLeaseFact {
    /// Opaque identity in the browser's sole lease registry.
    pub lease_id: ActorLeaseId,
    /// Utility generation this fact is valid in.
    pub service_generation: u64,
    /// Exact task or direct user intent the lease covers.
    pub authority_subject: AuthoritySubject,
    /// Profile the lease covers.
    pub profile_id: ProfileId,
    /// Tab the lease covers.
    pub tab_id: TabId,
    /// Visible control relationship selected by the user.
    pub control_mode: ControlMode,
    /// Monotonic expiry recorded by the browser registry.
    pub expires_at: MonotonicMillis,
}

/// Browser-owned proof of one exact visible approval.
#[derive(Clone, Debug, PartialEq)]
pub struct ApprovalFact {
    /// Opaque reference to the browser approval record.
    pub receipt: ApprovalReceiptReference,
    /// Digest of exactly what the approval surface displayed.
    pub proposal_digest: ContentDigest,
    /// Utility generation this proof is valid in.
    pub service_generation: u64,
    /// Monotonic expiry recorded by the browser approval registry.
    pub expires_at: MonotonicMillis,
    /// Absolute expiry used to reject receipts restored across a reboot.
    pub expires_at_utc_ms: u64,
    /// Browser-session identity in which the visible decision was accepted.
    pub browser_session_id: String,
}

/// Complete typed proposal context needed to decide and bind one grant.
#[derive(Clone, Debug, PartialEq)]
pub struct GrantRequest {
    /// Rules under which this exact request may be minted.
    pub context: PolicyEvaluationContext,
    /// Task or taskless direct user intent being authorized.
    pub authority_subject: AuthoritySubject,
    /// Action aggregate being decided.
    pub action_id: ActionId,
    /// Principal whose authority can only be narrowed.
    pub principal: Principal,
    /// Consequence class selected from the closed policy taxonomy.
    pub action_class: ActionClass,
    /// Exact compiled-in operation within that consequence class.
    pub operation_kind: ActionOperationKind,
    /// SHA-256 of the canonical typed intent bytes retained by the browser.
    pub canonical_intent_digest: [u8; 32],
    /// Digest over the exact proposed operation and operands.
    pub proposal_digest: ContentDigest,
    /// Exact browser/document/node scope.
    pub scope: CapabilityScope,
    /// Joined disclosure classification.
    pub data_classes: SensitivitySet,
    /// Contextual risk, joined with the class baseline by policy.
    pub context_risk: RiskClass,
    /// Generation in which the grant may exist.
    pub service_generation: u64,
    /// Idempotency identity bound into the grant.
    pub idempotency_key: GrantIdempotencyKey,
    /// Requested monotonic expiry.
    pub expires_at: MonotonicMillis,
    /// Browser wall-clock reading paired with the monotonic evaluation time.
    pub now_utc_ms: u64,
    /// Browser-owned standing actor fact.
    pub actor_lease: ActorLeaseFact,
    /// Exact approval, when one is required or was supplied.
    pub approval: Option<ApprovalFact>,
    /// Exact zero-source discovery authority, present only in that context.
    pub discovery: Option<TaskDiscoveryAuthorityFact>,
}

impl GrantRequest {
    /// Risk after the class baseline and contextual reading are joined.
    pub fn effective_risk(&self) -> RiskClass {
        self.action_class.baseline_risk().join(self.context_risk)
    }
}

/// One policy-minted grant for the browser ledger to validate and spend.
#[derive(Clone, Debug, PartialEq)]
pub struct MintedGrant {
    /// Opaque reference minted inside policy.
    pub capability_id: CapabilityId,
    /// Generation boundary invalidating every grant on service replacement.
    pub service_generation: u64,
    /// Policy bundle that made the decision.
    pub policy_version: PolicyVersion,
    /// Browser-owned actor lease this grant cannot outlive.
    pub actor_lease_id: ActorLeaseId,
    /// Exact task or taskless direct user intent.
    pub authority_subject: AuthoritySubject,
    /// Action identity. Direct observation deliberately carries the empty
    /// legacy value because it is bound by its direct intent instead.
    pub action_id: ActionId,
    /// Closed consequence class the policy evaluated.
    pub action_class: ActionClass,
    /// Exact compiled-in operation this grant may authorize.
    pub operation_kind: ActionOperationKind,
    /// SHA-256 of the canonical typed intent this grant binds.
    pub canonical_intent_digest: [u8; 32],
    /// Principal whose authority was narrowed.
    pub principal: Principal,
    /// Exact proposal and retry identity.
    pub proposal_digest: ContentDigest,
    pub idempotency_key: GrantIdempotencyKey,
    /// Exact target and disclosure scope.
    pub scope: CapabilityScope,
    pub data_classes: SensitivitySet,
    pub effective_risk: RiskClass,
    /// Exact visible approval digest and receipt, when required.
    pub approval: Option<ApprovalFact>,
    /// Exact discovery fact echoed only for a bounded discovery move.
    pub discovery: Option<TaskDiscoveryAuthorityFact>,
    /// Monotonic issuance and expiry.
    pub issued_at: MonotonicMillis,
    pub expires_at: MonotonicMillis,
}

/// Closed result of stateless production policy evaluation.
#[derive(Clone, Debug, PartialEq)]
pub enum GrantDecision {
    /// Policy minted a fully bound grant.
    Authorize(Box<MintedGrant>),
    /// A visible exact approval is required; no grant exists.
    RequireApproval,
    /// Policy refused the proposal; no grant exists.
    Deny(Denial),
}

/// Stateless production policy using no Rust lease or capability ledger.
#[derive(Clone, Debug)]
pub struct GrantPolicy<I> {
    ids: I,
    milestone: PolicyMilestone,
    policy_version: PolicyVersion,
}

impl<I: IdSource> GrantPolicy<I> {
    /// Freezes the reviewed policy surface and injected ID derivation source.
    pub const fn new(ids: I, milestone: PolicyMilestone, policy_version: PolicyVersion) -> Self {
        Self {
            ids,
            milestone,
            policy_version,
        }
    }

    /// Validates all browser facts and mints at most one grant.
    pub fn decide(&mut self, request: &GrantRequest, now: MonotonicMillis) -> GrantDecision {
        if !context_is_valid(request) || !operation_shape_is_valid(request) {
            return GrantDecision::Deny(Denial::new(DenialReason::AuthorityContextInvalid));
        }
        if let Some(reason) = DenialReason::for_class(request.action_class, self.milestone) {
            return GrantDecision::Deny(Denial::new(reason));
        }
        let effective_risk = request.effective_risk();
        if let Some(reason) = DenialReason::for_risk(effective_risk, self.milestone) {
            return GrantDecision::Deny(Denial::new(reason));
        }
        if let Some(reason) = validate_lease(request, now) {
            return GrantDecision::Deny(Denial::new(reason));
        }
        // Decision 0089 section 3, stated here and not only in the stateful
        // broker: this is the decider the product runs, so a rule that lived
        // only there would be a rule the shipping answer does not have.
        //
        // It sits outside the task-context guard the two conditions beside it
        // share. Nothing but a task can propose a fill today — a direct user
        // observation is pinned to `ObservePage` by `context_is_valid` — so the
        // guard would cost nothing to keep; leaving it off is what makes the
        // question about the field rather than about which context asked.
        let needs_approval =
            per_use_confirmation_required(request.action_class, request.data_classes)
                || (matches!(
                    request.context,
                    PolicyEvaluationContext::Task | PolicyEvaluationContext::TaskDiscovery
                ) && (request.actor_lease.control_mode.requires_step_approval()
                    || effective_risk.requires_exact_approval()));
        if needs_approval && request.approval.is_none() {
            return GrantDecision::RequireApproval;
        }
        if let Some(approval) = &request.approval {
            if approval.service_generation != request.service_generation
                || approval.expires_at.0 <= now.0
                || approval.expires_at_utc_ms <= request.now_utc_ms
                || approval.browser_session_id.is_empty()
                || approval.expires_at.0 < request.expires_at.0
                || approval.proposal_digest != request.proposal_digest
            {
                return GrantDecision::Deny(Denial::new(DenialReason::ApprovalInvalidated));
            }
        }
        if !request.action_class.scope_is_complete(
            request.scope.node_id.is_some(),
            request.scope.destination_scope.is_some(),
        ) {
            return GrantDecision::Deny(Denial::new(DenialReason::NodeTargetMissing));
        }
        if let Some(reason) = destination_class_verdict(&request.scope, request.action_class) {
            return GrantDecision::Deny(Denial::new(reason));
        }
        let Some(id) = self.ids.next_id(IdKind::Capability) else {
            return GrantDecision::Deny(Denial::new(DenialReason::IdSourceExhausted));
        };
        GrantDecision::Authorize(Box::new(MintedGrant {
            capability_id: CapabilityId::new(id),
            service_generation: request.service_generation,
            policy_version: self.policy_version,
            actor_lease_id: request.actor_lease.lease_id.clone(),
            authority_subject: request.authority_subject.clone(),
            action_id: request.action_id.clone(),
            action_class: request.action_class,
            operation_kind: request.operation_kind,
            canonical_intent_digest: request.canonical_intent_digest,
            principal: request.principal.clone(),
            proposal_digest: request.proposal_digest.clone(),
            idempotency_key: request.idempotency_key.clone(),
            scope: request.scope.clone(),
            data_classes: request.data_classes,
            effective_risk,
            approval: request.approval.clone(),
            discovery: request.discovery.clone(),
            issued_at: now,
            expires_at: request.expires_at,
        }))
    }
}

/// The destination-class table, read for a navigation that names its
/// destination by origin rather than by a node (decision 0087 section 2 as
/// amended: a model-typed address is admitted under the sites budget, and this
/// is the one class of site it is never admitted to on its own).
///
/// A node-anchored navigation — a click, an observed link — is judged at
/// dispatch by the `DestinationClassAllowed` precondition over the live node;
/// a typed address has no node, so its only reading is here, on the request
/// scope. The destination the scope's own origin already names is the site the
/// task is on, which the person's scope put it on: that is never refused.
fn destination_class_verdict(scope: &CapabilityScope, class: ActionClass) -> Option<DenialReason> {
    if scope.node_id.is_some() {
        return None;
    }
    let destination = scope.destination_scope.as_ref()?;
    crate::site::assistant_navigation_verdict(class, destination, *destination == scope.origin)
}

fn validate_lease(request: &GrantRequest, now: MonotonicMillis) -> Option<DenialReason> {
    let lease = &request.actor_lease;
    let direct_user_lease = request.context == PolicyEvaluationContext::DirectUserObservation
        && lease.control_mode == ControlMode::User;
    if !direct_user_lease && !lease.control_mode.grants_lease() {
        return Some(DenialReason::ControlModeGrantsNoLease);
    }
    if lease.service_generation != request.service_generation || lease.expires_at.0 <= now.0 {
        return Some(DenialReason::NoStandingLease);
    }
    if lease.authority_subject != request.authority_subject
        || lease.profile_id != request.scope.profile_id
        || lease.tab_id != request.scope.tab_id
    {
        return Some(DenialReason::LeaseDoesNotCoverTarget);
    }
    if request.expires_at.0 <= now.0 || request.expires_at.0 > lease.expires_at.0 {
        return Some(DenialReason::ExpiryNotUsable);
    }
    None
}

fn context_is_valid(request: &GrantRequest) -> bool {
    match (&request.context, &request.authority_subject) {
        (PolicyEvaluationContext::Task, AuthoritySubject::Task(task_id)) => {
            !task_id.as_str().starts_with(DIRECT_USER_INTENT_PREFIX) && request.discovery.is_none()
        }
        (PolicyEvaluationContext::TaskDiscovery, AuthoritySubject::Task(task_id)) => {
            !task_id.as_str().starts_with(DIRECT_USER_INTENT_PREFIX)
                && task_discovery_is_valid(request)
        }
        (PolicyEvaluationContext::DirectUserObservation, AuthoritySubject::DirectUserIntent(_)) => {
            request.action_id.as_str().is_empty()
                && request.discovery.is_none()
                && request.action_class == ActionClass::ObservePage
                && request.operation_kind == ActionOperationKind::DomRead
                && request.context_risk == RiskClass::LocalRead
                && request.data_classes == SensitivitySet::EMPTY
                && request.approval.is_none()
                && request.scope.node_id.is_none()
                && request.scope.destination_scope.is_none()
                && request.scope.destination_address.is_none()
                && request.scope.allowed_redirects.origins().is_empty()
                && request.scope.required_graph_revision.0 == 0
        }
        _ => false,
    }
}

fn task_discovery_is_valid(request: &GrantRequest) -> bool {
    let Some(discovery) = request.discovery.as_ref() else {
        return false;
    };
    let browser_session = discovery.browser_session_id.as_str();
    let discovery_tab = discovery.discovery_tab_id.as_str();
    let opaque_origin_is_bounded = match &request.scope.origin {
        NormalizedOrigin::Opaque { opaque_id } => {
            !opaque_id.is_empty()
                && opaque_id.len() <= MAX_AUTHORITY_SUBJECT_ID_BYTES
                && !opaque_id.chars().any(char::is_control)
        }
        NormalizedOrigin::Tuple { .. } => false,
    };
    request.action_class == ActionClass::OpenLink
        && matches!(
            request.operation_kind,
            ActionOperationKind::Search | ActionOperationKind::Navigate
        )
        && request.principal.kind == PrincipalKind::Assistant
        && request.principal.skill_version_id.is_none()
        && request.context_risk == RiskClass::ReversibleDisclosure
        && request.data_classes == SensitivitySet::EMPTY
        && request.approval.is_none()
        && request.actor_lease.control_mode == ControlMode::Assistant
        && request.scope.tab_id == discovery.discovery_tab_id
        && request.scope.node_id.is_none()
        && matches!(
            request.scope.destination_scope.as_ref(),
            Some(NormalizedOrigin::Tuple { .. })
        )
        && request.scope.destination_address.is_some()
        && request.scope.allowed_redirects.origins().is_empty()
        && request.scope.required_graph_revision.0 == 0
        && opaque_origin_is_bounded
        && !discovery_tab.is_empty()
        && discovery_tab.len() <= MAX_AUTHORITY_SUBJECT_ID_BYTES
        && !discovery_tab.chars().any(char::is_control)
        && !browser_session.is_empty()
        && browser_session.len() <= MAX_AUTHORITY_SUBJECT_ID_BYTES
        && !browser_session.chars().any(char::is_control)
        && (1..=MAX_TASK_DISCOVERY_SOURCE_CAP).contains(&discovery.remaining_new_source_cap)
}

fn operation_shape_is_valid(request: &GrantRequest) -> bool {
    request.action_class == request.operation_kind.action_class()
        && request
            .operation_kind
            .node_scope_is_valid(request.scope.node_id.is_some())
        && (!request.operation_kind.requires_destination_address()
            || request.scope.destination_address.is_some())
        && (request.operation_kind.admits_destination_address()
            || request.scope.destination_address.is_none())
}

#[cfg(test)]
mod tests;
