// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Shared fixtures for the prepare-and-commit suites (decision
//! `docs/decisions/0022-prepare-and-commit-for-consequential-actions.md`).
//!
//! One preparation, staged and rendered, plus the values a commit against it is
//! compared with. Everything here is deterministic: the clock never moves on
//! its own, identifiers count, and no test observes anything a second run of
//! the same script would not observe again.
//!
//! # The two digests
//!
//! [`proposal_digest`] and [`rendered_digest`] are deliberately different
//! values, and keeping them apart is the whole point of the fixture. The first
//! is what a model asked for; the second is what a person read. A suite that
//! used one value for both would pass whether or not the ledger binds the right
//! one, which is exactly the mistake decision 0022 exists to prevent.

#![allow(dead_code)]

use bip_types::action::{
    ActionType, Precondition, PreconditionKind, PreparedEffectBinding, Principal, PrincipalKind,
};
use bip_types::identity::{
    ActionId, ContentDigest, DigestAlgorithm, DocumentLifecycleState, FrameId, GraphRevision,
    MonotonicMillis, PageEpoch, ProfileId, SemanticNodeId, TabId, TaskId,
};
use bip_types::sensitivity::SensitivitySet;
use bip_types::snapshot::{NodeState, SemanticRole};
use bip_types::trust::TrustSet;
use bip_types::ActionResultCode;

use policy_engine::capability::{CapabilityRequest, CapabilityScope};
use policy_engine::precondition::{
    BudgetState, FramePresence, ObservedNode, TabPresence, UserInteraction,
};
use policy_engine::prepared::CommitContext;
use policy_engine::time::SequentialIds;
use policy_engine::{
    ActionClass, ActionPhase, ActorLeaseId, AllowedRedirects, CommitRefusal, ControlMode,
    LeaseRequest, NormalizedOrigin, ObservedState, PolicyEngine, PolicyMilestone, PolicyVersion,
    PrepareRequest, PreparedEffectId, PreparedEffectLedger, RenderedEffect, RenderedEffectDigest,
    RiskClass,
};

/// When the prepare was staged.
pub const STAGED_AT: u64 = 1_000;
/// When the trusted surface rendered the staged effect.
pub const RENDERED_AT: u64 = 1_500;
/// When the preparation stops being committable.
pub const EXPIRES_AT: u64 = 9_000;
/// When the browser received the gesture authorizing the commit.
pub const GESTURE_AT: u64 = 2_000;
/// When the commit arrives.
pub const COMMIT_AT: u64 = 2_500;

pub fn origin() -> NormalizedOrigin {
    match policy_engine::origin::normalize_serialization("https://example.test") {
        Ok(origin) => origin,
        Err(_) => unreachable!("the fixture origin must normalize"),
    }
}

pub fn digest(value: &str) -> ContentDigest {
    ContentDigest {
        algorithm: DigestAlgorithm::Sha256,
        value: value.to_owned(),
    }
}

/// The digest of the *proposal* — what a model asked for.
pub fn proposal_digest() -> ContentDigest {
    digest("proposal-of-the-prepare")
}

/// The digest of the effect *as the trusted surface rendered it* — what a
/// person read. Deliberately a different value from the proposal's.
pub fn rendered_digest() -> ContentDigest {
    digest("the-sentence-the-person-read")
}

pub fn scope() -> CapabilityScope {
    CapabilityScope {
        profile_id: ProfileId::new("profile_1"),
        tab_id: TabId::new("tab_1"),
        frame_id: FrameId::new("frame_main"),
        page_epoch: PageEpoch::new("epoch_a"),
        origin: origin(),
        node_id: Some(SemanticNodeId::new("n_send")),
        destination_scope: None,
        destination_address: None,
        required_graph_revision: GraphRevision(7),
        allowed_redirects: AllowedRedirects::none(),
    }
}

pub fn action_id() -> ActionId {
    ActionId::new("action-prepare-1")
}

pub fn prepared_effect_id() -> PreparedEffectId {
    PreparedEffectId::for_action(&action_id())
}

pub fn lease_id() -> ActorLeaseId {
    ActorLeaseId::new("lease_1")
}

pub fn task_id() -> TaskId {
    TaskId::new("task_1")
}

pub fn prepare_request() -> PrepareRequest {
    PrepareRequest {
        prepared_effect_id: prepared_effect_id(),
        task_id: task_id(),
        lease_id: lease_id(),
        action_class: ActionClass::SendMessage,
        prepare_digest: proposal_digest(),
        scope: scope(),
        expires_at: MonotonicMillis(EXPIRES_AT),
    }
}

pub fn rendered() -> RenderedEffect {
    RenderedEffect {
        digest: RenderedEffectDigest::from_trusted_surface(rendered_digest()),
        rendered_at: MonotonicMillis(RENDERED_AT),
    }
}

pub fn binding() -> PreparedEffectBinding {
    PreparedEffectBinding {
        prepared_action_id: action_id(),
        prepared_effect_digest: rendered_digest(),
        prepared_at_monotonic_ms: RENDERED_AT,
        gesture_at_monotonic_ms: GESTURE_AT,
    }
}

/// A ledger holding one prepared effect that has been staged and rendered —
/// the only state a commit may be made against.
pub fn consumed_ledger() -> PreparedEffectLedger {
    let mut ledger = PreparedEffectLedger::new();
    ledger
        .stage(&prepare_request(), MonotonicMillis(STAGED_AT))
        .expect("staging a fresh prepared effect succeeds");
    ledger
        .record_rendered(
            &prepared_effect_id(),
            &rendered(),
            MonotonicMillis(RENDERED_AT),
        )
        .expect("recording the rendering of a staged effect succeeds");
    ledger
}

/// What a commit presents about itself, owned so a test can move one field at
/// a time without borrowing a temporary.
///
/// [`CommitContext`] holds references, so the fixture that varies it has to own
/// the values it points at. Keeping them here — rather than leaking them —
/// keeps each case a plain value with the fixture's own defaults.
pub struct Commit {
    pub scope: CapabilityScope,
    pub task_id: TaskId,
    pub lease_id: ActorLeaseId,
    pub action_class: ActionClass,
    pub binding: PreparedEffectBinding,
}

impl Default for Commit {
    fn default() -> Self {
        Self {
            scope: scope(),
            task_id: task_id(),
            lease_id: lease_id(),
            action_class: ActionClass::SendMessage,
            binding: binding(),
        }
    }
}

impl Commit {
    /// The context the ledger compares a commit against.
    pub fn context(&self) -> CommitContext<'_> {
        CommitContext {
            task_id: &self.task_id,
            lease_id: &self.lease_id,
            action_class: self.action_class,
            phase: ActionPhase::Commit,
            scope: &self.scope,
        }
    }
}

/// Runs one commit against a consumed ledger, after moving whatever the case
/// wanted moved.
pub fn commit_verdict(mutate: impl FnOnce(&mut Commit)) -> Result<(), ActionResultCode> {
    let ledger = consumed_ledger();
    let mut commit = Commit::default();
    mutate(&mut commit);
    ledger
        .verdict(
            &commit.binding,
            &commit.context(),
            MonotonicMillis(COMMIT_AT),
        )
        .map_err(CommitRefusal::result_code)
}

/// One precondition of `kind`, carrying `binding` as its operand.
pub fn precondition(
    kind: PreconditionKind,
    binding: Option<PreparedEffectBinding>,
) -> Precondition {
    Precondition {
        kind,
        page_epoch: None,
        min_graph_revision: None,
        origin: None,
        allowed_origins: None,
        expected_role: None,
        expected_action_type: None,
        node_state: None,
        expected_value_digest: None,
        expected_destination: None,
        max_sensitivity: None,
        min_content_trust: None,
        prepared_effect: binding,
    }
}

/// A broker at `milestone` with a standing lease over the fixture tab.
pub fn engine_with_lease(milestone: PolicyMilestone) -> PolicyEngine<SequentialIds> {
    let mut engine = PolicyEngine::new(SequentialIds::new(), milestone, PolicyVersion(1));
    engine
        .issue_lease(
            &LeaseRequest {
                task_id: task_id(),
                tab_id: TabId::new("tab_1"),
                control_mode: ControlMode::Assistant,
                expires_at: MonotonicMillis(100_000),
            },
            MonotonicMillis(0),
        )
        .expect("a lease is issued");
    engine
}

/// One proposal of `class` in `phase`.
pub fn proposal_for(class: ActionClass, phase: ActionPhase) -> CapabilityRequest {
    CapabilityRequest {
        task_id: task_id(),
        principal: Principal {
            kind: PrincipalKind::Assistant,
            skill_version_id: None,
        },
        action_class: class,
        phase,
        action_digest: proposal_digest(),
        scope: scope(),
        data_classes: SensitivitySet::EMPTY,
        context_risk: RiskClass::LocalRead,
        approval: None,
        expires_at: MonotonicMillis(50_000),
    }
}

/// A world in which nothing the sequence checks has moved.
pub fn observed() -> ObservedState {
    ObservedState {
        tab: TabPresence::Present,
        frame: FramePresence::Present,
        lifecycle: DocumentLifecycleState::Active,
        page_epoch: PageEpoch::new("epoch_a"),
        graph_revision: GraphRevision(7),
        origin: origin(),
        node: Some(ObservedNode {
            node_id: SemanticNodeId::new("n_send"),
            role: SemanticRole::Button,
            available_actions: vec![ActionType::Activate],
            states: vec![NodeState::Visible],
            sensitivity: SensitivitySet::EMPTY,
            content_trust: TrustSet::EMPTY,
            destination: None,
            value_digest: None,
        }),
        user_interaction: UserInteraction::NoneSinceLease,
        budget: BudgetState::Remaining,
    }
}
