// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A capability is issued once, spent once, and never runs backwards.

use super::{CapabilityError, CapabilityLedger, CapabilityState, IssueError};

use crate::action_class::{ActionClass, PolicyMilestone};

use crate::capability::{CapabilityRequest, CapabilityScope, PolicyVersion};

use crate::lease::{ActorLease, ControlMode, LeaseBook, LeaseRequest, RevocationReason};

use crate::origin::{normalize_serialization, AllowedRedirects, NormalizedOrigin};

use crate::phase::ActionPhase;

use crate::risk::RiskClass;

use crate::time::SequentialIds;

use bip_types::action::{Principal, PrincipalKind};

use bip_types::identity::{
    ApprovalReceiptReference, ContentDigest, DigestAlgorithm, FrameId, GraphRevision,
    MonotonicMillis, PageEpoch, ProfileId, SemanticNodeId, TabId, TaskId,
};

use bip_types::sensitivity::SensitivitySet;

fn origin() -> NormalizedOrigin {
    match normalize_serialization("https://example.test") {
        Ok(value) => value,
        Err(_) => unreachable!("fixture origin must normalize"),
    }
}

fn digest(value: &str) -> ContentDigest {
    ContentDigest {
        algorithm: DigestAlgorithm::Sha256,
        value: value.to_owned(),
    }
}

fn scope() -> CapabilityScope {
    CapabilityScope {
        profile_id: ProfileId::new("profile_1"),
        tab_id: TabId::new("tab_1"),
        frame_id: FrameId::new("frame_main"),
        page_epoch: PageEpoch::new("epoch_a"),
        origin: origin(),
        node_id: Some(SemanticNodeId::new("n_link")),
        destination_scope: None,
        destination_address: None,
        required_graph_revision: GraphRevision(7),
        allowed_redirects: AllowedRedirects::none(),
    }
}

fn request(class: ActionClass) -> CapabilityRequest {
    CapabilityRequest {
        task_id: TaskId::new("task_1"),
        principal: Principal {
            kind: PrincipalKind::Assistant,
            skill_version_id: None,
        },
        action_class: class,
        phase: ActionPhase::Commit,
        action_digest: digest("a1"),
        scope: scope(),
        data_classes: SensitivitySet::EMPTY,
        context_risk: RiskClass::LocalRead,
        approval: None,
        expires_at: MonotonicMillis(500),
    }
}

struct Fixture {
    book: LeaseBook,
    ledger: CapabilityLedger,
    ids: SequentialIds,
}

impl Fixture {
    fn new(mode: ControlMode) -> Self {
        let mut book = LeaseBook::new();
        let mut ids = SequentialIds::new();
        let outcome = book.issue(
            &LeaseRequest {
                task_id: TaskId::new("task_1"),
                tab_id: TabId::new("tab_1"),
                control_mode: mode,
                expires_at: MonotonicMillis(1_000),
            },
            &mut ids,
            MonotonicMillis(0),
        );
        assert!(outcome.is_ok());
        Self {
            book,
            ledger: CapabilityLedger::new(),
            ids,
        }
    }

    fn lease(&self) -> &ActorLease {
        match self.book.get(&TabId::new("tab_1")) {
            Some(lease) => lease,
            None => unreachable!("the fixture issued a lease"),
        }
    }
}

fn issue(
    fixture: &mut Fixture,
    request: &CapabilityRequest,
    now: MonotonicMillis,
) -> Result<super::CapabilityId, IssueError> {
    issue_with(fixture, request, None, now)
}

fn issue_with(
    fixture: &mut Fixture,
    request: &CapabilityRequest,
    approval: Option<ApprovalReceiptReference>,
    now: MonotonicMillis,
) -> Result<super::CapabilityId, IssueError> {
    let lease = fixture.lease().clone();
    fixture.ledger.issue(
        request,
        &lease,
        crate::capability::IssueContext {
            milestone: PolicyMilestone::M2,
            policy_version: PolicyVersion(1),
            approval,
        },
        &mut fixture.ids,
        now,
    )
}

#[test]
fn a_capability_is_spent_once_and_a_replay_meets_a_refusal() {
    let mut fixture = Fixture::new(ControlMode::Assistant);
    let id = match issue(
        &mut fixture,
        &request(ActionClass::OpenLink),
        MonotonicMillis(0),
    ) {
        Ok(id) => id,
        Err(error) => unreachable!("issue must succeed: {error}"),
    };

    assert!(fixture
        .ledger
        .begin_dispatch(&id, MonotonicMillis(1))
        .is_ok());
    assert!(fixture.ledger.consume(&id, MonotonicMillis(2)).is_ok());
    assert_eq!(
        fixture.ledger.consume(&id, MonotonicMillis(3)).err(),
        Some(CapabilityError::AlreadyConsumed)
    );
    assert_eq!(
        fixture.ledger.begin_dispatch(&id, MonotonicMillis(4)).err(),
        Some(CapabilityError::AlreadyConsumed)
    );
    assert_eq!(fixture.ledger.consumption_count(), 1);
}

#[test]
fn an_expired_capability_authorizes_nothing_even_before_anybody_looks() {
    let mut fixture = Fixture::new(ControlMode::Assistant);
    let id = match issue(
        &mut fixture,
        &request(ActionClass::OpenLink),
        MonotonicMillis(0),
    ) {
        Ok(id) => id,
        Err(error) => unreachable!("issue must succeed: {error}"),
    };
    assert_eq!(
        fixture.ledger.consume(&id, MonotonicMillis(500)).err(),
        Some(CapabilityError::Expired)
    );
    let Some(capability) = fixture.ledger.get(&id) else {
        unreachable!("the capability was issued")
    };
    assert_eq!(
        capability.state_at(MonotonicMillis(500)),
        CapabilityState::Expired
    );
    assert!(!capability.is_live_at(MonotonicMillis(500)));
}

#[test]
fn take_over_withdraws_undispatched_authority_and_reports_what_it_could_not_reach() {
    let mut fixture = Fixture::new(ControlMode::Assistant);
    let unused = match issue(
        &mut fixture,
        &request(ActionClass::OpenLink),
        MonotonicMillis(0),
    ) {
        Ok(id) => id,
        Err(error) => unreachable!("issue must succeed: {error}"),
    };
    let mut second = request(ActionClass::ScrollIntoView);
    second.action_digest = digest("a2");
    let dispatched = match issue(&mut fixture, &second, MonotonicMillis(0)) {
        Ok(id) => id,
        Err(error) => unreachable!("issue must succeed: {error}"),
    };
    assert!(fixture
        .ledger
        .begin_dispatch(&dispatched, MonotonicMillis(1))
        .is_ok());

    let lease_id = fixture.lease().lease_id().clone();
    let outcome = fixture
        .ledger
        .revoke_undispatched_for_lease(&lease_id, RevocationReason::UserTookOver);

    assert_eq!(outcome.revoked, vec![unused.clone()]);
    assert_eq!(outcome.in_flight, vec![dispatched]);
    assert_eq!(
        fixture.ledger.consume(&unused, MonotonicMillis(2)).err(),
        Some(CapabilityError::Revoked)
    );
}

#[test]
fn a_capability_never_outlives_the_lease_it_was_issued_under() {
    let mut fixture = Fixture::new(ControlMode::Assistant);
    let mut plan = request(ActionClass::OpenLink);
    plan.expires_at = MonotonicMillis(5_000);
    assert_eq!(
        issue(&mut fixture, &plan, MonotonicMillis(0)).err(),
        Some(IssueError::ExpiryOutlivesLease)
    );
}

#[test]
fn shared_control_issues_no_authority_without_an_approval_receipt() {
    let mut fixture = Fixture::new(ControlMode::Shared);
    assert_eq!(
        issue(
            &mut fixture,
            &request(ActionClass::OpenLink),
            MonotonicMillis(0)
        )
        .err(),
        Some(IssueError::ApprovalRequired)
    );

    assert!(issue_with(
        &mut fixture,
        &request(ActionClass::OpenLink),
        Some(ApprovalReceiptReference("receipt_1".to_owned())),
        MonotonicMillis(0)
    )
    .is_ok());
}

#[test]
fn a_write_class_is_refused_at_issue_time() {
    let mut fixture = Fixture::new(ControlMode::Assistant);
    for class in ActionClass::WRITE_MILESTONE {
        assert_eq!(
            issue(&mut fixture, &request(*class), MonotonicMillis(0)).err(),
            Some(IssueError::ActionClassNotAuthorized),
            "{}",
            class.label()
        );
    }
}

#[test]
fn a_changed_target_or_proposal_is_a_different_action() {
    let mut fixture = Fixture::new(ControlMode::Assistant);
    let id = match issue(
        &mut fixture,
        &request(ActionClass::OpenLink),
        MonotonicMillis(0),
    ) {
        Ok(id) => id,
        Err(error) => unreachable!("issue must succeed: {error}"),
    };
    let Some(capability) = fixture.ledger.get(&id) else {
        unreachable!("the capability was issued")
    };
    let node = SemanticNodeId::new("n_link");
    assert!(capability.matches(&digest("a1"), Some(&node)).is_ok());
    assert_eq!(
        capability.matches(&digest("a2"), Some(&node)).err(),
        Some(CapabilityError::DigestMismatch)
    );
    assert_eq!(
        capability
            .matches(&digest("a1"), Some(&SemanticNodeId::new("n_other")))
            .err(),
        Some(CapabilityError::ScopeMismatch)
    );
}

#[test]
fn an_in_flight_capability_is_spendable_after_its_expiry_passes() {
    // Spending records an outcome; it authorizes nothing. Refusing to
    // record one because the expiry passed during the round trip would
    // leave authority that was used looking unused.
    let mut fixture = Fixture::new(ControlMode::Assistant);
    let id = match issue(
        &mut fixture,
        &request(ActionClass::OpenLink),
        MonotonicMillis(0),
    ) {
        Ok(id) => id,
        Err(error) => unreachable!("issue must succeed: {error}"),
    };
    assert!(fixture
        .ledger
        .begin_dispatch(&id, MonotonicMillis(1))
        .is_ok());
    assert!(fixture.ledger.consume(&id, MonotonicMillis(9_999)).is_ok());
    assert_eq!(
        fixture.ledger.consume(&id, MonotonicMillis(9_999)).err(),
        Some(CapabilityError::AlreadyConsumed)
    );
}

#[test]
fn an_effective_risk_is_the_join_of_the_class_and_its_context() {
    let mut plan = request(ActionClass::OpenLink);
    assert_eq!(plan.effective_risk(), RiskClass::ReversibleDisclosure);
    plan.context_risk = RiskClass::SensitiveDisclosure;
    assert_eq!(plan.effective_risk(), RiskClass::SensitiveDisclosure);
    plan.context_risk = RiskClass::LocalRead;
    assert_eq!(plan.effective_risk(), RiskClass::ReversibleDisclosure);
}

#[test]
fn the_state_machine_never_runs_backwards() {
    for from in [
        CapabilityState::Issued,
        CapabilityState::InFlight,
        CapabilityState::Consumed,
        CapabilityState::Revoked,
        CapabilityState::Expired,
    ] {
        assert!(!from.may_become(CapabilityState::Issued));
    }
    assert!(CapabilityState::Issued.may_become(CapabilityState::InFlight));
    assert!(CapabilityState::InFlight.may_become(CapabilityState::Consumed));
    assert!(!CapabilityState::Consumed.may_become(CapabilityState::InFlight));
    assert!(!CapabilityState::Revoked.may_become(CapabilityState::Consumed));
}
