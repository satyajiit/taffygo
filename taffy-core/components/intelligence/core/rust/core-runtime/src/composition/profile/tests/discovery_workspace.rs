// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Verified discoveries join the workspace only with their first durable facts.

use bip_types::identity::{ContentDigest, DigestAlgorithm, DispatchId, MonotonicMillis};
use task_engine::action::{ActionIntent, BrowserIntent};
use task_engine::{
    ActionOutcome, ActionProposal, Authorization, CapabilityId, Command, ConsentedSource, Effect,
    IdempotencyKey, ProposalDecision,
};

use super::recording::support;
use crate::contract::{Completion, EffectRequest, OperationEnvelope, RuntimeError};
use crate::ports::TaskEngineLoad;
use crate::ProfileServiceRuntime;

fn workspace(runtime: &ProfileServiceRuntime) -> core_api_types::WorkspaceViewState {
    runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap()
        .workspaces
        .remove(0)
}

fn driver(empty: bool) -> ProfileServiceRuntime {
    let mut load = support::load();
    if empty {
        let TaskEngineLoad::Fresh {
            initial_consent, ..
        } = &mut load
        else {
            panic!("fresh")
        };
        initial_consent.preview.sources.clear();
        initial_consent.preview.scope = task_engine::SourceScope::new();
        initial_consent.preview.budgets = task_engine::TaskBudgets::none()
            .with(task_engine::BudgetKind::MaxSources, 1)
            .with(task_engine::BudgetKind::MaxModelRequests, 64);
    }
    let mut runtime = support::driver_for(load);
    if empty {
        support::apply(
            &mut runtime,
            Command::RecordDiscoveryTab {
                discovery_tab_id: support::source().tab_id,
                browser_session_id: task_engine::BrowserSessionId::new("browser-session-1")
                    .unwrap(),
            },
        );
    }
    runtime
}

fn source() -> ConsentedSource {
    ConsentedSource {
        source_id: task_engine::SourceId::from_bytes([9; 16]),
        tab_id: support::source().tab_id,
        normalized_origin: "https://official.test".into(),
        canonical_locator: Some("https://official.test/document".into()),
    }
}

fn outcome_command(
    runtime: &mut ProfileServiceRuntime,
    intent: BrowserIntent,
    observation: Option<task_engine::PageObservationEvidence>,
    discovered_source: Option<ConsentedSource>,
) -> Command {
    let ordinal = runtime.core().task(&support::task_id()).unwrap().revision();
    let proposal = ActionProposal::new(
        ActionIntent::Browser(intent),
        None,
        IdempotencyKey::new(format!("action-{ordinal}")),
        true,
        None,
        ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "a".repeat(64),
        },
    );
    let accepted = support::apply(runtime, Command::ProposeAction(Box::new(proposal)));
    let Effect::AskPolicy { action_id } = accepted.effects.first().unwrap() else {
        panic!("policy")
    };
    let dispatch = DispatchId::new(format!("dispatch-{ordinal}"));
    support::apply(
        runtime,
        Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(ProposalDecision::Authorize(Authorization {
                capability_id: CapabilityId::new(format!("cap-{ordinal}")),
            })),
            dispatch_id: Some(dispatch.clone()),
        },
    );
    Command::RecordActionOutcome {
        action_id: action_id.clone(),
        outcome: Box::new(ActionOutcome {
            code: bip_types::ActionResultCode::Verified,
            dispatch_id: Some(dispatch),
            observed_at: MonotonicMillis(1),
            observation,
            discovered_source,
        }),
    }
}

fn stage(
    runtime: &mut ProfileServiceRuntime,
    intent: BrowserIntent,
    observation: Option<task_engine::PageObservationEvidence>,
    discovered_source: Option<ConsentedSource>,
) -> OperationEnvelope<EffectRequest> {
    let command = outcome_command(runtime, intent, observation, discovered_source);
    support::begin(runtime, command)
}

fn discover(runtime: &mut ProfileServiceRuntime) {
    let source = source();
    let pending = stage(
        runtime,
        BrowserIntent::Navigate {
            tab: source.tab_id.clone(),
            address: source.canonical_locator.clone().unwrap(),
            new_tab: false,
        },
        None,
        Some(source),
    );
    support::commit(runtime, &pending);
}

fn stage_read(runtime: &mut ProfileServiceRuntime) -> OperationEnvelope<EffectRequest> {
    let source = source();
    let (evidence, _) = support::install_page_for(
        runtime,
        &source,
        "discovered-epoch",
        task_engine::ObservationCompleteness::Complete,
    );
    stage(
        runtime,
        BrowserIntent::DomRead {
            tab: source.tab_id,
            target: None,
        },
        Some(evidence),
        None,
    )
}

#[test]
fn first_discovered_page_and_source_are_visible_only_after_the_same_exact_commit() {
    let mut runtime = driver(true);
    discover(&mut runtime);
    assert!(workspace(&runtime).sources.is_empty());
    let pending = stage_read(&mut runtime);
    assert!(workspace(&runtime).sources.is_empty());
    let wrong = pending.with_body(Completion::StorageCommitted {
        committed_revision: pending.task_revision + 1,
    });
    assert!(runtime
        .core_mut()
        .complete_commit(&support::task_id(), wrong, 2)
        .is_err());
    assert!(workspace(&runtime).sources.is_empty());
    support::commit(&mut runtime, &pending);
    let result = workspace(&runtime);
    assert_eq!(result.sources.len(), 1);
    assert_eq!(result.sources[0].source_id, source().source_id.to_text());
    assert_eq!(result.sources[0].host, "official.test");
    assert!(!result.facts.is_empty());
    assert!(result
        .facts
        .iter()
        .all(|fact| fact.sources == vec![source().source_id.to_text()]));
}

#[test]
fn failed_first_page_commit_never_publishes_its_new_source_or_facts() {
    let mut runtime = driver(true);
    discover(&mut runtime);
    let pending = stage_read(&mut runtime);
    let failed = pending.with_body(Completion::Failed(RuntimeError::StorageConflict));
    let _ = runtime
        .core_mut()
        .complete_commit(&support::task_id(), failed, 2);
    assert!(workspace(&runtime).sources.is_empty());
    assert!(workspace(&runtime).facts.is_empty());
}

#[test]
fn a_replacement_page_keeps_existing_exclusion_and_public_source_history() {
    let mut runtime = driver(false);
    let initial = workspace(&runtime);
    let excluded = runtime
        .core_mut()
        .begin_workspace_exclusion(
            "exclude-original".into(),
            &initial.workspace_id,
            initial.revision,
            &support::source().source_id.to_text(),
            1_000,
        )
        .unwrap();
    runtime
        .core_mut()
        .complete_workspace_persist("exclude-original", excluded.resulting_revision)
        .unwrap();
    discover(&mut runtime);
    let pending = stage_read(&mut runtime);
    support::commit(&mut runtime, &pending);
    let result = workspace(&runtime);
    assert_eq!(result.sources.len(), 2);
    let original = result
        .sources
        .iter()
        .find(|row| row.source_id == support::source().source_id.to_text())
        .unwrap();
    assert!(original.excluded);
    assert_eq!(original.host, initial.sources[0].host);
    assert!(result
        .facts
        .iter()
        .all(|fact| fact.sources == vec![source().source_id.to_text()]));
}

/// Outside is a tab this task holds no source for.
///
/// It used to be an origin as well: the reducer compared the evidence's
/// `normalized_origin` against the source row byte for byte. That comparison
/// was between two strings the browser itself wrote, over one trusted seam,
/// so it never was an independent gate — and it refused the one case the
/// browser is the only component able to decide, a site answering on a
/// sibling host of its own registrable domain. On a phone that ended the core
/// and every task in the profile with it. `AcceptedApprovalLedger::
/// IsTaskSourceAuthorized` is where that question is answered now, and it is
/// answered before the read is ever dispatched.
#[test]
fn page_bytes_outside_current_accepted_source_cannot_add_workspace_metadata() {
    let mut runtime = driver(false);
    let before = workspace(&runtime);
    let mut source = source();
    source.tab_id = task_engine::deps::TabId::new("tab-the-task-does-not-hold");
    let (evidence, _) = support::install_page_for(
        &mut runtime,
        &source,
        "unaccepted",
        task_engine::ObservationCompleteness::Complete,
    );
    let command = outcome_command(
        &mut runtime,
        BrowserIntent::DomRead {
            tab: source.tab_id,
            target: None,
        },
        Some(evidence),
        None,
    );
    let revision = runtime.core().task(&support::task_id()).unwrap().revision();
    let result = runtime.core_mut().begin_submit(
        &support::task_id(),
        task_engine::CommandEnvelope::new(
            IdempotencyKey::new("unaccepted-result"),
            revision,
            task_engine::TraceId::new("unaccepted"),
            command,
        ),
        crate::contract::OperationId::new("unaccepted-result").unwrap(),
        crate::contract::Deadline::from_millis(10_000),
        1,
        1_000,
    );
    assert!(result.is_err());
    let after = workspace(&runtime);
    assert_eq!(after.sources, before.sources);
    assert_eq!(after.facts, before.facts);
}
