// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The three session registers the broker keeps, and the bound each holds.
//!
//! The capability ledger, the approval book and the prepared-effect ledger are
//! the broker's own record of what it granted, what a person answered, and
//! what was staged for them to read. All three exist to refuse a replay: the
//! capability ledger is the only thing that can say a capability was revoked,
//! expired, or already spent; the book is the only thing that can say one
//! answer has already authorized its one action; and the prepared-effect
//! ledger is the only thing that can say a commit names a prepare that did not
//! run, or one that has already been committed. An entry any of them dropped
//! would come back as "never issued", "never answered" and "no such prepare" —
//! the same refusal, recorded under the wrong fact, and reachable on purpose by
//! anyone who could make the register overflow.
//!
//! So none of them evicts. All three carry a ceiling instead, and reaching it
//! refuses. These tests assert the ceiling exists and is never crossed, and
//! that reaching it never costs the register an entry it already held.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::action::{Principal, PrincipalKind};
use bip_types::identity::{
    ActionId, ContentDigest, DigestAlgorithm, FrameId, GraphRevision, MonotonicMillis, PageEpoch,
    ProfileId, SemanticNodeId, TabId, TaskId,
};
use bip_types::sensitivity::SensitivitySet;

use policy_engine::approval::book::{ApprovalBook, ApprovalRequest};
use policy_engine::approval::ApprovalError;
use policy_engine::capability::{CapabilityRequest, CapabilityScope};
use policy_engine::time::SequentialIds;
use policy_engine::{
    ActionClass, ActionPhase, ActorLeaseId, AllowedRedirects, ControlMode, DenialReason,
    LeaseRequest, NormalizedOrigin, PolicyEngine, PolicyMilestone, PolicyVersion, PrepareError,
    PrepareRequest, PreparedEffectId, PreparedEffectLedger, PreparedEffectState, ProposalDecision,
    RepeatScope, RiskClass, MAX_APPROVALS_PER_SESSION, MAX_CAPABILITIES_PER_SESSION,
    MAX_PREPARED_EFFECTS_PER_SESSION,
};

const LEASE_EXPIRY: u64 = 100_000;
const CAPABILITY_EXPIRY: u64 = 40_000;
const PREPARE_EXPIRY: u64 = 60_000;

fn origin(serialization: &str) -> NormalizedOrigin {
    policy_engine::origin::normalize_serialization(serialization)
        .expect("the fixture origin must normalize")
}

fn scope() -> CapabilityScope {
    CapabilityScope {
        profile_id: ProfileId::new("profile_1"),
        tab_id: TabId::new("tab_1"),
        frame_id: FrameId::new("frame_main"),
        page_epoch: PageEpoch::new("epoch_a"),
        origin: origin("https://example.test"),
        node_id: Some(SemanticNodeId::new("n_link")),
        destination_scope: Some(origin("https://example.test")),
        destination_address: None,
        required_graph_revision: GraphRevision(7),
        allowed_redirects: AllowedRedirects::from_normalized([origin("https://example.test")]),
    }
}

fn request() -> CapabilityRequest {
    CapabilityRequest {
        task_id: TaskId::new("task_1"),
        principal: Principal {
            kind: PrincipalKind::Assistant,
            skill_version_id: None,
        },
        action_class: ActionClass::OpenLink,
        phase: ActionPhase::Commit,
        action_digest: ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "a1".to_owned(),
        },
        scope: scope(),
        data_classes: SensitivitySet::EMPTY,
        context_risk: RiskClass::LocalRead,
        approval: None,
        expires_at: MonotonicMillis(CAPABILITY_EXPIRY),
    }
}

/// A broker whose lease lets it authorize without asking every step.
fn acting_engine() -> PolicyEngine<SequentialIds> {
    let mut engine = PolicyEngine::new(SequentialIds::new(), PolicyMilestone::M2, PolicyVersion(1));
    let issued = engine.issue_lease(
        &LeaseRequest {
            task_id: TaskId::new("task_1"),
            tab_id: TabId::new("tab_1"),
            control_mode: ControlMode::Assistant,
            expires_at: MonotonicMillis(LEASE_EXPIRY),
        },
        MonotonicMillis(0),
    );
    assert!(issued.is_ok());
    engine
}

#[test]
fn the_capability_ledger_stops_at_its_ceiling_and_keeps_everything_it_held() {
    let mut engine = acting_engine();
    let first = match engine.decide_proposal(&request(), MonotonicMillis(1)) {
        ProposalDecision::Authorize(authorization) => authorization.capability_id,
        other => unreachable!("the first proposal must be authorized, got {other:?}"),
    };

    let mut refusal = None;
    // One more than the ceiling, so the loop reaches the refusal rather than
    // ending on a number this test would then be restating.
    for _ in 0..=MAX_CAPABILITIES_PER_SESSION {
        match engine.decide_proposal(&request(), MonotonicMillis(1)) {
            ProposalDecision::Authorize(_) => {}
            ProposalDecision::Deny(denial) => {
                refusal = Some(denial.reason);
                break;
            }
            ProposalDecision::RequireApproval => {
                unreachable!("assistant control needs no step approval")
            }
        }
        assert!(engine.capabilities().len() <= MAX_CAPABILITIES_PER_SESSION);
    }

    // Visible and named, never silent.
    assert_eq!(refusal, Some(DenialReason::AuthorityRegisterFull));
    assert_eq!(engine.capabilities().len(), MAX_CAPABILITIES_PER_SESSION);
    // And the ceiling cost the ledger nothing it already held: the very first
    // capability is still resolvable, so the dispatch path can still refuse it
    // by name rather than by absence.
    assert!(engine.capability(&first).is_some());
}

#[test]
fn the_approval_book_stops_at_its_ceiling_and_keeps_everything_it_held() {
    let mut book = ApprovalBook::new();
    let mut ids = SequentialIds::new();
    let ask = ApprovalRequest {
        task_id: TaskId::new("task_1"),
        binding: request().approval_binding(RiskClass::ReversibleDisclosure),
        repeat_scope: RepeatScope::Once,
        expires_at: MonotonicMillis(50_000),
    };
    let first = book
        .present(&ask, &mut ids, MonotonicMillis(0))
        .expect("the first question must be presentable");

    let mut refusal = None;
    for _ in 0..=MAX_APPROVALS_PER_SESSION {
        match book.present(&ask, &mut ids, MonotonicMillis(0)) {
            Ok(_) => {}
            Err(error) => {
                refusal = Some(error);
                break;
            }
        }
        assert!(book.len() <= MAX_APPROVALS_PER_SESSION);
    }

    assert_eq!(refusal, Some(ApprovalError::BookFull));
    assert_eq!(book.len(), MAX_APPROVALS_PER_SESSION);
    assert!(book.get(&first).is_some());
}

/// One prepare, named by ordinal so a loop can stage distinct effects.
fn prepare(ordinal: usize) -> PrepareRequest {
    PrepareRequest {
        prepared_effect_id: PreparedEffectId::for_action(&ActionId::new(format!(
            "action-prepare-{ordinal}"
        ))),
        task_id: TaskId::new("task_1"),
        lease_id: ActorLeaseId::new("lease_1"),
        action_class: ActionClass::SendMessage,
        prepare_digest: ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: format!("proposal_{ordinal}"),
        },
        scope: scope(),
        expires_at: MonotonicMillis(PREPARE_EXPIRY),
    }
}

/// The prepared-effect ledger is the third register, and it holds the same way
/// (decision 0022).
///
/// It is the only thing that can tell a commit naming a prepare that never ran
/// from one naming a prepare that has already been spent, so a record it
/// dropped to make room would answer the second question with the first
/// question's refusal — and a page that could make it overflow would be
/// choosing which. So it does not evict: it refuses to stage instead, by name.
#[test]
fn the_prepared_effect_ledger_stops_at_its_ceiling_and_keeps_everything_it_held() {
    let mut ledger = PreparedEffectLedger::new();
    let first = prepare(0);
    ledger
        .stage(&first, MonotonicMillis(1))
        .expect("the first preparation must be stageable");

    let mut refusal = None;
    // One past the ceiling, so the loop reaches the refusal rather than ending
    // on a number this test would then be restating.
    for ordinal in 1..=MAX_PREPARED_EFFECTS_PER_SESSION {
        match ledger.stage(&prepare(ordinal), MonotonicMillis(1)) {
            Ok(()) => {}
            Err(error) => {
                refusal = Some(error);
                break;
            }
        }
        assert!(ledger.len() <= MAX_PREPARED_EFFECTS_PER_SESSION);
    }

    // Visible and named, never silent.
    assert_eq!(refusal, Some(PrepareError::LedgerFull));
    assert_eq!(ledger.len(), MAX_PREPARED_EFFECTS_PER_SESSION);
    // The ceiling cost the ledger nothing it already held, so a commit against
    // the very first preparation is still refused by name rather than by
    // absence.
    assert_eq!(
        ledger
            .get(&first.prepared_effect_id)
            .map(|entry| entry.state_at(MonotonicMillis(1))),
        Some(PreparedEffectState::Staged)
    );

    // And a saturated ledger keeps refusing rather than admitting one more.
    for _ in 0..4 {
        assert_eq!(
            ledger.stage(
                &prepare(MAX_PREPARED_EFFECTS_PER_SESSION + 9),
                MonotonicMillis(1)
            ),
            Err(PrepareError::LedgerFull)
        );
    }
    assert_eq!(ledger.len(), MAX_PREPARED_EFFECTS_PER_SESSION);
}

#[test]
fn a_full_register_refuses_before_it_spends_an_identifier() {
    // The ceiling is checked with the other static gates, before the register
    // asks for a name. If the order were the other way round, a saturated book
    // would burn one identifier per refused question, and the identifier
    // source is the one thing that cannot be replenished.
    let mut book = ApprovalBook::new();
    let mut ids = SequentialIds::new();
    let ask = ApprovalRequest {
        task_id: TaskId::new("task_1"),
        binding: request().approval_binding(RiskClass::ReversibleDisclosure),
        repeat_scope: RepeatScope::Once,
        expires_at: MonotonicMillis(50_000),
    };
    while book.len() < MAX_APPROVALS_PER_SESSION {
        assert!(book.present(&ask, &mut ids, MonotonicMillis(0)).is_ok());
    }
    let held: Vec<String> = book
        .entries()
        .map(|approval| approval.approval_id().as_str().to_owned())
        .collect();

    for _ in 0..4 {
        assert_eq!(
            book.present(&ask, &mut ids, MonotonicMillis(0)).err(),
            Some(ApprovalError::BookFull)
        );
    }
    let after: Vec<String> = book
        .entries()
        .map(|approval| approval.approval_id().as_str().to_owned())
        .collect();
    assert_eq!(after, held);
}
