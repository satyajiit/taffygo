// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Real reducer commits feed the automatic draft and exact-version acceptance.

mod handover;
mod restore;
mod saved_flows;
pub(super) mod support;

use super::wire;
use crate::contract::Completion;
use crate::procedure_catalogue::{SkillMutationError, SkillMutationPersistence};
use procedure_engine::ProcedureStatus;
use support::{driver, perform_flow, task_id};
use task_engine::{Command, TaskResult, TaskState};

#[test]
fn navigation_withdraws_the_old_page_only_after_its_exact_outcome_commit() {
    use bip_types::identity::{ContentDigest, DigestAlgorithm, DispatchId, MonotonicMillis};
    use task_engine::action::{ActionIntent, BrowserIntent};
    use task_engine::{
        ActionOutcome, ActionProposal, Authorization, CapabilityId, Effect, IdempotencyKey,
        ProposalDecision,
    };

    let mut runtime = driver();
    let (evidence, _) = support::install_page(&mut runtime);
    support::action(
        &mut runtime,
        BrowserIntent::DomRead {
            tab: support::source().tab_id,
            target: None,
        },
        Some(evidence),
    );
    runtime
        .core_mut()
        .loop_state_mut(task_id().as_str())
        .person_answer = Some("keep going".to_owned());
    let proposal = ActionProposal::new(
        ActionIntent::Browser(BrowserIntent::Navigate {
            tab: support::source().tab_id,
            address: "https://example.test/next".to_owned(),
            new_tab: false,
        }),
        None,
        IdempotencyKey::new("navigate-current-page"),
        false,
        None,
        ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "a".repeat(64),
        },
    );
    let accepted = support::apply(&mut runtime, Command::ProposeAction(Box::new(proposal)));
    let Effect::AskPolicy { action_id } = accepted.effects.first().unwrap() else {
        panic!("navigation uses ordinary policy")
    };
    let dispatch = DispatchId::new("navigation-dispatch");
    support::apply(
        &mut runtime,
        Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(ProposalDecision::Authorize(Authorization {
                capability_id: CapabilityId::new("navigation-capability"),
            })),
            dispatch_id: Some(dispatch.clone()),
        },
    );
    let pending = support::begin(
        &mut runtime,
        Command::RecordActionOutcome {
            action_id: action_id.clone(),
            outcome: Box::new(ActionOutcome {
                code: bip_types::ActionResultCode::Verified,
                dispatch_id: Some(dispatch),
                observed_at: MonotonicMillis(1),
                observation: None,
                discovered_source: None,
            }),
        },
    );
    let pages = |runtime: &crate::ProfileServiceRuntime| {
        runtime
            .core()
            .loop_state(task_id().as_str())
            .unwrap()
            .page
            .source_ids()
            .len()
    };
    assert_eq!(
        pages(&runtime),
        1,
        "an outcome awaiting storage is not committed"
    );
    let wrong = pending.with_body(Completion::StorageCommitted {
        committed_revision: pending.task_revision + 1,
    });
    assert!(runtime
        .core_mut()
        .complete_commit(&task_id(), wrong, 2)
        .is_err());
    assert_eq!(pages(&runtime), 1);
    support::commit(&mut runtime, &pending);
    assert_eq!(
        pages(&runtime),
        0,
        "a new document needs a new semantic read"
    );
    assert_eq!(
        runtime
            .core()
            .loop_state(task_id().as_str())
            .unwrap()
            .person_answer
            .as_deref(),
        Some("keep going")
    );
}

#[test]
fn completed_flow_waits_for_storage_and_exact_person_acceptance() {
    let mut runtime = driver();
    perform_flow(&mut runtime);
    assert!(runtime.prepare_completed_flow(&task_id(), 2_000).is_none());
    support::apply(&mut runtime, Command::ResultCandidateReady);
    let commit = support::begin(
        &mut runtime,
        Command::CompleteResultValidated(TaskResult::default()),
    );
    assert_eq!(
        runtime
            .core()
            .task(&task_id())
            .unwrap()
            .view_facts()
            .unwrap()
            .state,
        TaskState::Completed
    );
    assert!(runtime.prepare_completed_flow(&task_id(), 2_000).is_none());
    let wrong = commit.with_body(Completion::StorageCommitted {
        committed_revision: commit.task_revision + 1,
    });
    assert!(runtime
        .core_mut()
        .complete_commit(&task_id(), wrong, 2)
        .is_err());
    assert!(runtime.prepare_completed_flow(&task_id(), 2_000).is_none());
    support::commit(&mut runtime, &commit);

    let draft = runtime.prepare_completed_flow(&task_id(), 2_000).unwrap();
    let id = draft.skill_id().to_owned();
    let version_id = format!("{id}@1");
    let SkillMutationPersistence::Install(install) = draft.persistence() else {
        panic!("a completed recording must prepare a new draft")
    };
    assert_eq!(install.provenance, wire::SkillProvenance::RecordedFromTask);
    let decoded = procedure_engine::decode_beside(&install.definition, install.step_count).unwrap();
    assert_eq!(decoded.status, ProcedureStatus::Draft);
    assert_eq!(
        decoded.recorded_from_task_id.as_deref(),
        Some(task_id().as_str())
    );
    assert_eq!(decoded.steps.len(), 3);
    assert!(runtime.procedure_catalogue().resolve(&version_id).is_none());
    runtime.install_skill_mutation(draft).unwrap();
    assert!(!runtime
        .procedure_catalogue()
        .resolve(&version_id)
        .unwrap()
        .is_runnable());
    assert!(runtime.prepare_completed_flow(&task_id(), 2_001).is_none());

    let mut acceptance = wire::MutateSkillCommand {
        kind: wire::SkillMutationKind::SetEnabled,
        skill_id: id,
        origin: String::new(),
        expected_version: 2,
        enabled: true,
        clauses: Vec::new(),
        steps: Vec::new(),
        admitted: 0,
        recorded_at_epoch_ms: 2_100,
    };
    assert_eq!(
        runtime.prepare_skill_mutation(&acceptance),
        Err(SkillMutationError::StaleVersion)
    );
    acceptance.expected_version = 1;
    let accepted = runtime.prepare_skill_mutation(&acceptance).unwrap();
    assert!(!runtime
        .procedure_catalogue()
        .resolve(&version_id)
        .unwrap()
        .is_runnable());
    runtime.install_skill_mutation(accepted).unwrap();
    assert!(runtime
        .procedure_catalogue()
        .resolve(&version_id)
        .unwrap()
        .is_runnable());
}

#[test]
fn failed_terminal_storage_never_offers_an_undurable_flow() {
    let mut runtime = driver();
    perform_flow(&mut runtime);
    support::apply(&mut runtime, Command::ResultCandidateReady);
    let pending = support::begin(
        &mut runtime,
        Command::CompleteResultValidated(TaskResult::default()),
    );
    let failed = pending.with_body(Completion::Failed(
        crate::contract::RuntimeError::StorageConflict,
    ));
    let _ = runtime.core_mut().complete_commit(&task_id(), failed, 2);
    assert!(runtime.prepare_completed_flow(&task_id(), 2_000).is_none());
}
