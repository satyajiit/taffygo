// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Completion uses the facts retained by the real observation/workspace path.

use super::recording::support;
use crate::ports::TaskEngineLoad;
use crate::ProfileServiceRuntime;
use task_engine::action::BrowserIntent;
use task_engine::{Command, TaskKind, TaskTemplateId};

fn workspace(runtime: &ProfileServiceRuntime) -> core_api_types::WorkspaceViewState {
    runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap()
        .workspaces
        .remove(0)
}

fn next(runtime: &mut ProfileServiceRuntime) -> loop_kernel::walk::PlannedCommand {
    runtime
        .advance_task_walk(&support::task_id(), 1, 2_000)
        .unwrap()
        .unwrap()
        .unwrap()
}

fn driver() -> ProfileServiceRuntime {
    driver_for_sources(vec![support::source()])
}

fn driver_for_sources(sources: Vec<task_engine::ConsentedSource>) -> ProfileServiceRuntime {
    let mut load = support::load();
    let TaskEngineLoad::Fresh {
        seed,
        initial_consent,
        ..
    } = &mut load
    else {
        panic!("fresh fixture")
    };
    seed.kind = TaskKind::Research;
    seed.snapshot.template_id = if sources.len() == 1 {
        TaskTemplateId::SummarizeEvidence
    } else {
        TaskTemplateId::CompareProducts
    };
    seed.snapshot.tool_allowlist = vec!["browser.dom.read".to_owned()];
    initial_consent.preview.source_discovery_enabled = false;
    initial_consent.preview.new_source_cap = 0;
    initial_consent.preview.budgets = task_engine::TaskBudgets::none()
        .with(task_engine::BudgetKind::MaxSources, sources.len() as u64)
        .with(task_engine::BudgetKind::MaxModelRequests, 64);
    initial_consent.preview.scope = sources
        .iter()
        .fold(task_engine::SourceScope::new(), |scope, source| {
            scope.include(source.source_id)
        });
    initial_consent.preview.sources = sources;
    support::driver_for(load)
}

fn read_page(runtime: &mut ProfileServiceRuntime) {
    read_source(runtime, &support::source());
}

fn read_source(runtime: &mut ProfileServiceRuntime, source: &task_engine::ConsentedSource) {
    let (evidence, _) = support::install_page_for(
        runtime,
        source,
        "epoch-1",
        task_engine::ObservationCompleteness::Complete,
    );
    support::action(
        runtime,
        BrowserIntent::DomRead {
            tab: source.tab_id.clone(),
            target: None,
        },
        Some(evidence),
    );
}

#[test]
fn comparison_requires_durable_facts_from_each_distinct_source() {
    let first = support::source();
    let second = task_engine::ConsentedSource {
        source_id: task_engine::SourceId::from_bytes([4; 16]),
        tab_id: bip_types::identity::TabId::new("tab-2"),
        normalized_origin: "https://other.test".to_owned(),
        canonical_locator: None,
    };
    for include_second in [false, true] {
        let mut runtime = driver_for_sources(vec![first.clone(), second.clone()]);
        read_source(&mut runtime, &first);
        if include_second {
            read_source(&mut runtime, &second);
        }
        support::apply(&mut runtime, Command::ResultCandidateReady);
        let planned = next(&mut runtime);
        if include_second {
            let Command::CompleteResultValidated(result) = planned.envelope.command else {
                panic!("both sources retain facts")
            };
            assert_eq!((result.fact_count, result.source_count), (2, 2));
        } else {
            assert!(matches!(
                planned.envelope.command,
                Command::PartialResultValidated(_)
            ));
        }
    }
}

#[test]
fn retained_page_facts_complete_research_without_an_artifact() {
    let mut runtime = driver();
    read_page(&mut runtime);
    support::apply(&mut runtime, Command::ResultCandidateReady);
    let retained = workspace(&runtime);
    assert_eq!(retained.facts.len(), 1);
    assert_eq!(
        retained.facts[0].sources,
        vec![support::source().source_id.to_text()]
    );
    let planned = next(&mut runtime);
    let repeated = next(&mut runtime);
    assert_eq!(planned.envelope, repeated.envelope);
    assert_eq!(planned.operation, repeated.operation);
    let Command::CompleteResultValidated(result) = planned.envelope.command else {
        panic!(
            "durable cited page facts must complete research: {:?}",
            planned.envelope.command
        )
    };
    assert_eq!(result.fact_count, 1);
    assert_eq!(result.source_count, 1);
    assert!(result.artifact_ids.is_empty());
    assert!(result.unmet.is_empty());
}

#[test]
fn an_empty_workspace_still_produces_the_empty_research_gap() {
    let mut runtime = driver();
    support::apply(&mut runtime, Command::ResultCandidateReady);
    let planned = next(&mut runtime);
    assert!(matches!(
        planned.envelope.command,
        Command::PartialResultValidated(_)
    ));
}

#[test]
fn result_candidate_must_commit_before_its_facts_can_complete_it() {
    let mut runtime = driver();
    read_page(&mut runtime);
    let pending = support::begin(&mut runtime, Command::ResultCandidateReady);
    assert!(matches!(
        next(&mut runtime).envelope.command,
        Command::PartialResultValidated(_)
    ));
    support::commit(&mut runtime, &pending);
    assert!(matches!(
        next(&mut runtime).envelope.command,
        Command::CompleteResultValidated(_)
    ));
}

#[test]
fn a_new_unverified_epoch_cannot_complete_from_older_retained_facts() {
    let mut runtime = driver();
    read_page(&mut runtime);
    support::apply(&mut runtime, Command::ResultCandidateReady);
    support::install_page_for(
        &mut runtime,
        &support::source(),
        "epoch-2",
        task_engine::ObservationCompleteness::Complete,
    );
    assert!(matches!(
        next(&mut runtime).envelope.command,
        Command::PartialResultValidated(_)
    ));
    assert_eq!(workspace(&runtime).facts.len(), 1);
}

#[test]
fn incomplete_observation_never_retains_completion_evidence() {
    let mut runtime = driver();
    let (evidence, _) = support::install_page_for(
        &mut runtime,
        &support::source(),
        "epoch-1",
        task_engine::ObservationCompleteness::Incomplete,
    );
    support::action(
        &mut runtime,
        BrowserIntent::DomRead {
            tab: support::source().tab_id,
            target: None,
        },
        Some(evidence),
    );
    support::apply(&mut runtime, Command::ResultCandidateReady);
    assert!(workspace(&runtime).facts.is_empty());
    assert!(matches!(
        next(&mut runtime).envelope.command,
        Command::PartialResultValidated(_)
    ));
}

#[test]
fn pending_then_committed_exclusion_never_promotes_the_old_evidence() {
    let mut runtime = driver();
    read_page(&mut runtime);
    support::apply(&mut runtime, Command::ResultCandidateReady);
    let original = next(&mut runtime);
    let retained = workspace(&runtime);
    let pending = runtime
        .core_mut()
        .begin_workspace_exclusion(
            "exclude-source".to_owned(),
            &retained.workspace_id,
            retained.revision,
            &support::source().source_id.to_text(),
            2_000,
        )
        .unwrap();
    assert!(matches!(
        next(&mut runtime).envelope.command,
        Command::PartialResultValidated(_)
    ));
    assert!(runtime
        .core_mut()
        .reject_workspace_persist(&pending.operation_id));
    assert_eq!(next(&mut runtime).envelope, original.envelope);
    let pending = runtime
        .core_mut()
        .begin_workspace_exclusion(
            "exclude-source-again".to_owned(),
            &retained.workspace_id,
            retained.revision,
            &support::source().source_id.to_text(),
            2_001,
        )
        .unwrap();
    runtime
        .core_mut()
        .complete_workspace_persist(&pending.operation_id, pending.resulting_revision)
        .unwrap();
    assert!(matches!(
        next(&mut runtime).envelope.command,
        Command::PartialResultValidated(_)
    ));
}

#[test]
fn corrected_facts_do_not_impersonate_the_current_page_observation() {
    let mut runtime = driver();
    read_page(&mut runtime);
    support::apply(&mut runtime, Command::ResultCandidateReady);
    let retained = workspace(&runtime);
    let pending = runtime
        .core_mut()
        .begin_workspace_correction(
            "correct-fact".to_owned(),
            &retained.workspace_id,
            retained.revision,
            &retained.facts[0].fact_id,
            "A person's correction".to_owned(),
            2_000,
        )
        .unwrap();
    runtime
        .core_mut()
        .complete_workspace_persist(&pending.operation_id, pending.resulting_revision)
        .unwrap();
    assert!(matches!(
        next(&mut runtime).envelope.command,
        Command::PartialResultValidated(_)
    ));
}
