// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The independent risk gate and parity between the two policy deciders.

use bip_types::action::{Principal, PrincipalKind};
use bip_types::identity::{
    ActionId, ApprovalReceiptReference, ContentDigest, DigestAlgorithm, MonotonicMillis, ProfileId,
    TabId, TaskId,
};
use bip_types::sensitivity::SensitivitySet;
use bip_types::snapshot::Sensitivity;
use bip_types::ActionResultCode;
use policy_engine::grant::{ActorLeaseFact, ApprovalFact, AuthoritySubject, GrantIdempotencyKey};
use policy_engine::time::SequentialIds;
use policy_engine::{
    ActionClass, ActionOperationKind, ControlMode, DenialReason, GrantDecision, GrantPolicy,
    GrantRequest, PolicyEvaluationContext, PolicyMilestone, PolicyVersion, ProposalDecision,
    RiskClass,
};

use super::fixtures::{answered, engine, proposal, scope, NOW};

#[test]
fn a_class_on_the_surface_is_still_refused_when_its_context_raises_the_risk() {
    let mut engine = engine(PolicyMilestone::M2);
    for risk in [
        RiskClass::SensitiveDisclosure,
        RiskClass::ExcludedCommitment,
        RiskClass::ProhibitedAbuse,
    ] {
        let mut request = proposal(ActionClass::OpenLink, 0);
        request.context_risk = risk;
        match engine.decide_proposal(&request, NOW) {
            ProposalDecision::Deny(denial) => {
                assert_eq!(denial.reason, DenialReason::EffectiveRiskNotAuthorized);
                assert_eq!(denial.code, ActionResultCode::DeniedByPolicy);
            }
            other => unreachable!("{} must be denied, got {other:?}", risk.label()),
        }
    }

    let mut request = proposal(ActionClass::OpenLink, 1);
    request.context_risk = RiskClass::ReversibleDisclosure;
    assert!(matches!(
        engine.decide_proposal(&request, NOW),
        ProposalDecision::Authorize(_)
    ));
}

#[test]
fn the_write_milestone_moved_one_row_of_the_risk_gate_and_no_more() {
    let mut engine = engine(PolicyMilestone::M5);

    let mut sensitive = proposal(ActionClass::OpenLink, 0);
    sensitive.context_risk = RiskClass::SensitiveDisclosure;
    let decision = engine.decide_proposal(&sensitive, NOW);
    assert!(
        matches!(decision, ProposalDecision::RequireApproval),
        "a sensitive disclosure at M5 decided {decision:?}"
    );

    for risk in [RiskClass::ExcludedCommitment, RiskClass::ProhibitedAbuse] {
        let mut request = proposal(ActionClass::OpenLink, 1);
        request.context_risk = risk;
        match engine.decide_proposal(&request, NOW) {
            ProposalDecision::Deny(denial) => {
                assert_eq!(denial.reason, DenialReason::EffectiveRiskNotAuthorized);
                assert_eq!(denial.code, ActionResultCode::DeniedByPolicy);
            }
            other => unreachable!("{} must be denied at M5, got {other:?}", risk.label()),
        }
    }
}

/// The answer a decider gave, in a shape the two of them can be compared in.
#[derive(Debug, PartialEq)]
enum Answer {
    Authorized,
    ApprovalRequired,
    Denied(DenialReason),
}

fn broker_answer(decision: &ProposalDecision) -> Answer {
    match decision {
        ProposalDecision::Authorize(_) => Answer::Authorized,
        ProposalDecision::RequireApproval => Answer::ApprovalRequired,
        ProposalDecision::Deny(denial) => Answer::Denied(denial.reason),
    }
}

fn production_answer(decision: &GrantDecision) -> Answer {
    match decision {
        GrantDecision::Authorize(_) => Answer::Authorized,
        GrantDecision::RequireApproval => Answer::ApprovalRequired,
        GrantDecision::Deny(denial) => Answer::Denied(denial.reason),
    }
}

const fn operation_for_class(class: ActionClass) -> Option<ActionOperationKind> {
    match class {
        ActionClass::ObservePage => Some(ActionOperationKind::DomRead),
        ActionClass::ScrollIntoView => Some(ActionOperationKind::DomScroll),
        ActionClass::OpenLink => Some(ActionOperationKind::Search),
        ActionClass::CreateTaskTab => Some(ActionOperationKind::TabsOpen),
        ActionClass::MoveFocus => Some(ActionOperationKind::TabsActivate),
        ActionClass::SyntheticClick => Some(ActionOperationKind::DomClick),
        ActionClass::FillField => Some(ActionOperationKind::FormFill),
        ActionClass::SelectOption => Some(ActionOperationKind::FormSelect),
        ActionClass::ToggleControl => Some(ActionOperationKind::FormToggle),
        ActionClass::SubmitForm => Some(ActionOperationKind::FormSubmit),
        ActionClass::StartDownload => Some(ActionOperationKind::DownloadStart),
        ActionClass::ExecuteToolJob => Some(ActionOperationKind::ToolJob),
        ActionClass::LibraryRead => Some(ActionOperationKind::LibrarySearch),
        ActionClass::LibraryWrite => Some(ActionOperationKind::LibrarySave),
        ActionClass::MemoryRead => Some(ActionOperationKind::MemorySearch),
        ActionClass::MemoryWrite => Some(ActionOperationKind::MemorySave),
        ActionClass::ControlTab => Some(ActionOperationKind::Reload),
        ActionClass::ProfileStoreRead => Some(ActionOperationKind::HistorySearch),
        ActionClass::UploadFile
        | ActionClass::SendMessage
        | ActionClass::Purchase
        | ActionClass::ExtractCredential
        | ActionClass::BypassAccessControl => None,
    }
}

const GRANT_NOW: MonotonicMillis = MonotonicMillis(100);
const GRANT_GENERATION: u64 = 7;

fn grant_request(class: ActionClass, data_classes: SensitivitySet) -> GrantRequest {
    let operation_kind = operation_for_class(class).unwrap_or(ActionOperationKind::DomRead);
    let mut capability_scope = scope();
    // This fixture compares the two deciders; operation-shape rejection has
    // its own exhaustive tests. Give each exact operation its narrowest valid
    // node scope so parity is not accidentally testing a malformed request.
    if operation_kind.node_scope_is_valid(false) {
        capability_scope.node_id = None;
    }
    if operation_kind.requires_destination_address() {
        capability_scope.destination_scope = Some(capability_scope.origin.clone());
        capability_scope.destination_address = Some("https://example.test/download".to_owned());
    }
    GrantRequest {
        context: PolicyEvaluationContext::Task,
        authority_subject: AuthoritySubject::Task(TaskId::new("task_1")),
        action_id: ActionId::new("action_1"),
        principal: Principal {
            kind: PrincipalKind::Assistant,
            skill_version_id: None,
        },
        action_class: class,
        operation_kind,
        canonical_intent_digest: [3; 32],
        proposal_digest: ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "ab".repeat(32),
        },
        scope: capability_scope,
        data_classes,
        context_risk: RiskClass::LocalRead,
        service_generation: GRANT_GENERATION,
        idempotency_key: GrantIdempotencyKey::new("effect-1")
            .unwrap_or_else(|_| unreachable!("the fixture identity must validate")),
        expires_at: MonotonicMillis(200),
        now_utc_ms: 1_000,
        actor_lease: ActorLeaseFact {
            lease_id: policy_engine::ActorLeaseId::new("lease_browser"),
            service_generation: GRANT_GENERATION,
            authority_subject: AuthoritySubject::Task(TaskId::new("task_1")),
            profile_id: ProfileId::new("profile_1"),
            tab_id: TabId::new("tab_1"),
            control_mode: ControlMode::Assistant,
            expires_at: MonotonicMillis(300),
        },
        approval: None,
        discovery: None,
    }
}

#[test]
fn a_link_download_asks_for_approval_before_any_grant_can_be_minted() {
    let mut request = grant_request(ActionClass::StartDownload, SensitivitySet::EMPTY);
    request.scope.node_id = Some(bip_types::identity::SemanticNodeId::new("download-link"));
    request.context_risk = RiskClass::SensitiveDisclosure;
    let mut policy = GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(1));
    assert_eq!(
        production_answer(&policy.decide(&request, GRANT_NOW)),
        Answer::ApprovalRequired
    );

    request.approval = Some(ApprovalFact {
        receipt: ApprovalReceiptReference::new("approval-download"),
        proposal_digest: request.proposal_digest.clone(),
        service_generation: GRANT_GENERATION,
        expires_at: MonotonicMillis(250),
        expires_at_utc_ms: 1_100,
        browser_session_id: "browser-session".to_owned(),
    });
    assert_eq!(
        production_answer(&policy.decide(&request, GRANT_NOW)),
        Answer::Authorized
    );
    request.proposal_digest.value = "cd".repeat(32);
    assert_eq!(
        production_answer(&policy.decide(&request, GRANT_NOW)),
        Answer::Denied(DenialReason::ApprovalInvalidated)
    );
}

#[test]
fn the_two_deciders_agree_for_every_exact_operation_at_every_milestone() {
    for milestone in PolicyMilestone::ALL {
        for (ordinal, class) in ActionClass::ALL.iter().enumerate() {
            for (label, data_classes) in [
                ("no disclosure", SensitivitySet::EMPTY),
                (
                    "an identity field",
                    SensitivitySet::of(Sensitivity::Identity),
                ),
            ] {
                let mut engine = engine(*milestone);
                let mut request = proposal(*class, ordinal);
                request.data_classes = data_classes;
                let broker = broker_answer(&engine.decide_proposal(&request, NOW));

                let mut policy =
                    GrantPolicy::new(SequentialIds::new(), *milestone, PolicyVersion(1));
                let production = production_answer(
                    &policy.decide(&grant_request(*class, data_classes), GRANT_NOW),
                );

                if operation_for_class(*class).is_some() {
                    assert_eq!(
                        broker,
                        production,
                        "{} at {} over {label}",
                        class.label(),
                        milestone.label()
                    );
                } else {
                    assert_eq!(
                        production,
                        Answer::Denied(DenialReason::AuthorityContextInvalid)
                    );
                }
            }
        }
    }
}

#[test]
fn an_approval_opens_only_the_classified_form_fill() {
    let identity = SensitivitySet::of(Sensitivity::Identity);
    for (ordinal, class) in [
        ActionClass::SelectOption,
        ActionClass::ToggleControl,
        ActionClass::SubmitForm,
    ]
    .into_iter()
    .enumerate()
    {
        let mut engine = engine(PolicyMilestone::M5);
        let mut request = proposal(class, ordinal);
        request.data_classes = identity;
        let answered_request = answered(&mut engine, &request);
        for decision in [
            engine.decide_proposal(&request, NOW),
            engine.decide_proposal(&answered_request, NOW),
        ] {
            assert_eq!(
                broker_answer(&decision),
                Answer::Denied(DenialReason::ActionClassReservedForWriteMilestone),
                "{} escaped through an approval",
                class.label()
            );
        }

        let mut policy =
            GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(1));
        assert_eq!(
            production_answer(&policy.decide(&grant_request(class, identity), GRANT_NOW)),
            Answer::Denied(DenialReason::ActionClassReservedForWriteMilestone),
            "the production decider opened {}",
            class.label()
        );
    }

    let mut engine = engine(PolicyMilestone::M5);
    let mut request = proposal(ActionClass::FillField, 9);
    request.data_classes = identity;
    assert!(matches!(
        engine.decide_proposal(&request, NOW),
        ProposalDecision::RequireApproval
    ));
    let answered_request = answered(&mut engine, &request);
    assert!(matches!(
        engine.decide_proposal(&answered_request, NOW),
        ProposalDecision::Authorize(_)
    ));

    let mut grant = grant_request(ActionClass::FillField, identity);
    grant.approval = Some(ApprovalFact {
        receipt: ApprovalReceiptReference::new("approval-1"),
        proposal_digest: grant.proposal_digest.clone(),
        service_generation: GRANT_GENERATION,
        expires_at: MonotonicMillis(250),
        expires_at_utc_ms: 1_100,
        browser_session_id: "browser-session".to_owned(),
    });
    let mut policy = GrantPolicy::new(SequentialIds::new(), PolicyMilestone::M5, PolicyVersion(1));
    assert_eq!(
        production_answer(&policy.decide(&grant, GRANT_NOW)),
        Answer::Authorized
    );
}
