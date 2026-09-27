// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{
    ActionIntent, ActionOutcome, ActionProposal, ActionRecord, ActionState, BrowserIntent,
    DisclosureState,
};
use crate::authority::{Authorization, CapabilityId, Denial, ProposalDecision};
use crate::ids::IdempotencyKey;
use crate::tool::{IdempotencyClass, RecoveryRule};
use bip_types::identity::{
    ActionId, ContentDigest, DigestAlgorithm, DispatchId, FrameId, GraphRevision, MonotonicMillis,
    NodeHandle, Origin, OriginKind, PageEpoch, SemanticNodeId, TabId,
};
use bip_types::ActionResultCode;

fn proposal(idempotency: IdempotencyClass) -> ActionProposal {
    let intent = if idempotency == IdempotencyClass::Consequential {
        ActionIntent::Browser(BrowserIntent::FormSubmit {
            tab: TabId::new("tab_1"),
            control: SemanticNodeId::new("submit_1"),
        })
    } else {
        ActionIntent::Browser(BrowserIntent::DomClick {
            target: super::ObservedNodeHandle::from_node_handle(&NodeHandle::new(
                TabId::new("tab_1"),
                FrameId::new("frame_1"),
                PageEpoch::new("epoch_1"),
                GraphRevision(1),
                SemanticNodeId::new("node_1"),
                Origin {
                    kind: OriginKind::Tuple,
                    serialization: Some("https://example.test".to_owned()),
                    opaque_id: None,
                },
            ))
            .unwrap_or_else(|| unreachable!()),
            expected_state: Some(DisclosureState::Expanded),
        })
    };
    ActionProposal::new(
        intent,
        None,
        IdempotencyKey::new("key_1"),
        true,
        None,
        ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "0".repeat(64),
        },
    )
}

fn record() -> ActionRecord {
    ActionRecord::proposed(
        ActionId::new("act_0"),
        proposal(IdempotencyClass::ConditionallyIdempotent),
    )
}

fn authorization() -> ProposalDecision {
    ProposalDecision::Authorize(Authorization {
        capability_id: CapabilityId::new("cap_0"),
    })
}

#[test]
fn a_terminal_task_cancels_authority_that_arrives_for_it() {
    let mut action = record();
    action.apply_policy_decision(&authorization(), true);
    assert_eq!(action.state(), ActionState::Cancelled);
    assert_eq!(action.capability_id(), None);
}

#[test]
fn a_denial_ends_the_action_with_the_code_policy_gave() {
    let mut action = record();
    action.apply_policy_decision(
        &ProposalDecision::Deny(Denial::new(ActionResultCode::ActorLeaseMissing)),
        false,
    );
    assert_eq!(action.state(), ActionState::Rejected);
    assert_eq!(action.result(), Some(ActionResultCode::ActorLeaseMissing));
}

#[test]
fn only_an_authorized_action_may_begin_dispatch() {
    let mut action = record();
    assert!(!action.begin_dispatch(DispatchId::new("dispatch_0")));
    action.apply_policy_decision(&authorization(), false);
    assert!(action.begin_dispatch(DispatchId::new("dispatch_0")));
    assert_eq!(action.state(), ActionState::Dispatching);
    assert_eq!(action.attempts(), 1);
}

#[test]
fn an_unconfirmed_dispatch_becomes_outcome_unknown_and_not_failed() {
    let mut action = record();
    action.apply_policy_decision(&authorization(), false);
    assert!(action.begin_dispatch(DispatchId::new("dispatch_0")));
    action.record_outcome(&ActionOutcome {
        code: ActionResultCode::RendererCrashed,
        dispatch_id: None,
        observed_at: MonotonicMillis(10),
        observation: None,
        discovered_source: None,
    });
    assert_eq!(action.state(), ActionState::OutcomeUnknown);
    assert_eq!(action.recovery_rule(), RecoveryRule::ReconcileFirst);
    assert!(!action.recovery_rule().permits_unattended_retry());
}

fn unknown_action() -> ActionRecord {
    let mut action = record();
    action.apply_policy_decision(&authorization(), false);
    assert!(action.begin_dispatch(DispatchId::new("dispatch_0")));
    action.record_outcome(&ActionOutcome {
        code: ActionResultCode::OutcomeUnknown,
        dispatch_id: Some(DispatchId::new("dispatch_0")),
        observed_at: MonotonicMillis(10),
        observation: None,
        discovered_source: None,
    });
    action
}

fn reconciled(code: ActionResultCode, dispatch_id: &str) -> ActionOutcome {
    ActionOutcome {
        code,
        dispatch_id: Some(DispatchId::new(dispatch_id)),
        observed_at: MonotonicMillis(20),
        observation: None,
        discovered_source: None,
    }
}

#[test]
fn exact_definitive_reconciliation_may_replace_an_unknown_outcome() {
    let mut action = unknown_action();
    let outcome = reconciled(ActionResultCode::DeniedByPolicy, "dispatch_0");

    assert!(action.accepts_outcome(&outcome));
    action.record_outcome(&outcome);
    assert_eq!(action.state(), ActionState::Failed);
    assert_eq!(action.result(), Some(ActionResultCode::DeniedByPolicy));
}

#[test]
fn reconciliation_refuses_another_dispatch_or_an_ambiguous_result() {
    let action = unknown_action();

    assert!(!action.accepts_outcome(&reconciled(
        ActionResultCode::DeniedByPolicy,
        "dispatch_other"
    )));
    assert!(!action.accepts_outcome(&reconciled(
        ActionResultCode::PostconditionTimeout,
        "dispatch_0"
    )));
}

#[test]
fn a_payload_free_page_action_can_recover_verified() {
    let action = unknown_action();

    assert!(action.accepts_outcome(&reconciled(ActionResultCode::Verified, "dispatch_0")));
}

#[test]
fn a_dispatched_action_is_never_cancelled_behind_its_own_back() {
    let mut action = record();
    action.apply_policy_decision(&authorization(), false);
    assert!(action.begin_dispatch(DispatchId::new("dispatch_0")));
    assert!(!action.cancel_if_undispatched());
    assert_eq!(action.state(), ActionState::Dispatching);
}

#[test]
fn an_undispatched_action_is_cancelled_when_authority_is_withdrawn() {
    let mut action = record();
    action.apply_policy_decision(&authorization(), false);
    assert!(action.cancel_if_undispatched());
    assert_eq!(action.state(), ActionState::Cancelled);
}

#[test]
fn only_a_verified_postcondition_reaches_verified() {
    for code in [
        ActionResultCode::PostconditionFailed,
        ActionResultCode::PostconditionTimeout,
        ActionResultCode::NavigationStarted,
        ActionResultCode::DispatchFailed,
    ] {
        let outcome = ActionOutcome {
            code,
            dispatch_id: None,
            observed_at: MonotonicMillis(0),
            observation: None,
            discovered_source: None,
        };
        assert_ne!(outcome.resulting_state(), ActionState::Verified, "{code:?}");
    }
    let verified = ActionOutcome {
        code: ActionResultCode::Verified,
        dispatch_id: None,
        observed_at: MonotonicMillis(0),
        observation: None,
        discovered_source: None,
    };
    assert_eq!(verified.resulting_state(), ActionState::Verified);
}

#[test]
fn a_consequential_tool_never_permits_an_unattended_retry() {
    let action = ActionRecord::proposed(
        ActionId::new("act_1"),
        proposal(IdempotencyClass::Consequential),
    );
    assert_eq!(action.recovery_rule(), RecoveryRule::NeverAutomatically);
    assert!(!action.recovery_rule().permits_unattended_retry());
}

fn page_evidence() -> crate::PageObservationEvidence {
    crate::PageObservationEvidence {
        service_generation: 1,
        schema_version: "2.4".to_owned(),
        tab_id: TabId::new("tab_1"),
        frame_id: FrameId::new("frame_1"),
        page_epoch: PageEpoch::new("epoch_1"),
        graph_revision: 1,
        normalized_origin: "https://example.test".to_owned(),
        private_profile: false,
        completeness: crate::ObservationCompleteness::Complete,
        graph: crate::ObservationGraphSummary {
            node_count: 1,
            relationship_count: 0,
            named_node_count: 1,
            text_run_count: 0,
            text_byte_count: 0,
        },
        total_bytes: 128,
        truncated: false,
        may_change_answer: false,
        redacted_field_count: 0,
        suppressed_secret_value_count: 0,
        sensitive_zone_count: 0,
        policy_filtered_frame_count: 0,
        highest_sensitivity: bip_types::Sensitivity::NotSensitive,
    }
}

#[test]
fn browser_table_receipts_and_page_evidence_are_distinct() {
    let browser_session_id =
        crate::BrowserSessionId::new("browser-1").unwrap_or_else(|_| unreachable!());
    for (intent, requires_page) in [
        (
            BrowserIntent::TabsList {
                context: TabId::new("tab_1"),
                browser_session_id: browser_session_id.clone(),
            },
            false,
        ),
        (
            BrowserIntent::DownloadList {
                tab: TabId::new("tab_1"),
                browser_session_id,
            },
            false,
        ),
        (
            BrowserIntent::DomRead {
                tab: TabId::new("tab_1"),
                target: None,
            },
            true,
        ),
    ] {
        let proposal = ActionProposal::new(
            ActionIntent::Browser(intent),
            None,
            IdempotencyKey::new("table-read"),
            true,
            None,
            proposal(IdempotencyClass::Consequential)
                .proposal_digest
                .clone(),
        );
        let mut action = ActionRecord::proposed(ActionId::new("act_1"), proposal);
        action.apply_policy_decision(&authorization(), false);
        assert!(action.begin_dispatch(DispatchId::new("dispatch_0")));
        let mut result = reconciled(ActionResultCode::Verified, "dispatch_0");
        assert_eq!(action.accepts_outcome(&result), !requires_page);
        result.observation = Some(page_evidence());
        assert_eq!(action.accepts_outcome(&result), requires_page);
        result.dispatch_id = Some(DispatchId::new("unrelated"));
        assert!(!action.accepts_outcome(&result));
    }
}
