// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Whose lease, whose subject, whose approval: none of them can be substituted.

use super::request;
use crate::action_class::PolicyMilestone;
use crate::capability::PolicyVersion;
use crate::grant::{
    ApprovalFact, AuthoritySubject, DirectUserIntentId, GrantDecision, GrantIdempotencyKey,
    GrantIdempotencyKeyError, GrantPolicy, PolicyEvaluationContext,
    MAX_GRANT_IDEMPOTENCY_KEY_BYTES,
};
use crate::lease::ControlMode;
use crate::time::SequentialIds;
use bip_types::identity::{
    ActionId, ApprovalReceiptReference, GraphRevision, MonotonicMillis, TaskId,
};

#[test]
fn a_browser_lease_for_another_task_cannot_be_substituted() {
    let mut request = request();
    request.actor_lease.authority_subject = AuthoritySubject::Task(TaskId::new("task-other"));
    let mut policy = GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12));
    assert!(matches!(
        policy.decide(&request, MonotonicMillis(100)),
        GrantDecision::Deny(_)
    ));
}

#[test]
fn direct_user_observation_is_taskless_and_accepts_only_a_user_lease() {
    let mut request = request();
    request.context = PolicyEvaluationContext::DirectUserObservation;
    request.authority_subject = AuthoritySubject::DirectUserIntent(
        DirectUserIntentId::new("direct-intent-one").unwrap_or_else(|_| unreachable!()),
    );
    request.action_id = ActionId::new("");
    request.scope.required_graph_revision = GraphRevision(0);
    request.actor_lease.authority_subject = request.authority_subject.clone();
    request.actor_lease.control_mode = ControlMode::User;
    let mut policy = GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12));
    let GrantDecision::Authorize(grant) = policy.decide(&request, MonotonicMillis(100)) else {
        unreachable!("the exact read-only direct context must mint once")
    };
    assert_eq!(grant.authority_subject, request.authority_subject);
    assert!(grant.authority_subject.task_id().is_none());
}

#[test]
fn a_task_subject_cannot_be_smuggled_into_direct_context() {
    let mut request = request();
    request.context = PolicyEvaluationContext::DirectUserObservation;
    request.action_id = ActionId::new("");
    request.scope.required_graph_revision = GraphRevision(0);
    request.actor_lease.control_mode = ControlMode::User;
    let mut policy = GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12));
    assert!(matches!(
        policy.decide(&request, MonotonicMillis(100)),
        GrantDecision::Deny(_)
    ));
}

#[test]
fn effect_identity_is_nonempty_and_bounded() {
    assert_eq!(
        GrantIdempotencyKey::new(""),
        Err(GrantIdempotencyKeyError::Empty)
    );
    assert_eq!(
        GrantIdempotencyKey::new("x".repeat(MAX_GRANT_IDEMPOTENCY_KEY_BYTES + 1)),
        Err(GrantIdempotencyKeyError::TooLong)
    );
}

#[test]
fn a_grant_never_outlives_its_visible_approval() {
    let mut request = request();
    request.approval = Some(ApprovalFact {
        receipt: ApprovalReceiptReference::new("approval-1"),
        proposal_digest: request.proposal_digest.clone(),
        service_generation: request.service_generation,
        expires_at: MonotonicMillis(150),
        expires_at_utc_ms: 1_050,
        browser_session_id: "browser-session".to_owned(),
    });
    let mut policy = GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M3, PolicyVersion(12));
    assert!(matches!(
        policy.decide(&request, MonotonicMillis(100)),
        GrantDecision::Deny(_)
    ));
}
