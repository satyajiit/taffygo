// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The reviewed, no-model workflow and the page-scoped research shapes.

use super::{operation, selected_source_research, start};
use crate::codec::start_shape::StartTaskDecodeError;
use crate::codec::start_task::decode_start_task;
use crate::ports::TaskEngineLoad;
use core_service_types as wire;
use task_engine::{BuiltinSkillId, BuiltinSkillReference};

#[test]
fn generated_start_command_has_one_canonical_reducer_shape() {
    assert!(decode_start_task(&start(), &operation()).is_ok());
}

#[cfg(not(taffy_candidate_capabilities))]
#[test]
fn development_profile_refuses_a_different_task_milestone() {
    let mut command = start();
    command.milestone = wire::TaskMilestone::M5;
    assert!(matches!(
        decode_start_task(&command, &operation()),
        Err(StartTaskDecodeError::CapabilityProfileMismatch)
    ));
}

#[cfg(taffy_candidate_capabilities)]
#[test]
fn candidate_profile_refuses_even_a_well_formed_start() {
    assert!(matches!(
        decode_start_task(&start(), &operation()),
        Err(StartTaskDecodeError::CapabilityProfileDisabled)
    ));
}

#[test]
fn built_in_identity_is_typed_and_mutually_exclusive_with_a_saved_procedure() {
    let mut command = selected_source_research(wire::TaskTemplateId::BuildSourceTable);
    command.builtin_skill = Some(wire::BuiltinSkillReference {
        skill_id: wire::BuiltinSkillId::DataExtraction,
        version: 1,
    });
    let Ok(TaskEngineLoad::Fresh { seed, .. }) = decode_start_task(&command, &operation()) else {
        unreachable!("a typed built-in binding decodes")
    };
    assert_eq!(
        seed.snapshot.builtin_skill,
        Some(BuiltinSkillReference {
            skill_id: BuiltinSkillId::DataExtraction,
            version: 1,
        })
    );

    command.skill_version_id = Some("saved@1".to_owned());
    assert!(matches!(
        decode_start_task(&command, &operation()),
        Err(StartTaskDecodeError::InvalidBuiltinSkillBinding)
    ));
    command.skill_version_id = None;
    command.builtin_skill.as_mut().unwrap().version = 0;
    assert!(matches!(
        decode_start_task(&command, &operation()),
        Err(StartTaskDecodeError::InvalidBuiltinSkillBinding)
    ));
}

#[test]
fn compare_needs_two_exact_sources_and_summary_needs_one() {
    let summary = selected_source_research(wire::TaskTemplateId::SummarizeEvidence);
    assert!(decode_start_task(&summary, &operation()).is_ok());

    let mut comparison = selected_source_research(wire::TaskTemplateId::CompareProducts);
    assert!(matches!(
        decode_start_task(&comparison, &operation()),
        Err(StartTaskDecodeError::InvalidAgentWorkflow)
    ));
    comparison
        .consent_preview
        .sources
        .push(wire::TaskConsentSource {
            source_id: "11112233445566778899aabbccddeeff".to_owned(),
            tab_id: "tab-b".to_owned(),
            normalized_origin: "https://second.example".to_owned(),
            canonical_locator: None,
        });
    comparison
        .budgets
        .iter_mut()
        .find(|budget| budget.kind == wire::TaskBudgetKind::MaxSources)
        .into_iter()
        .for_each(|budget| budget.limit = 2);
    assert!(decode_start_task(&comparison, &operation()).is_ok());

    comparison.consent_preview.sources[1].tab_id = "tab-a".to_owned();
    comparison.consent_preview.sources[1].normalized_origin = "https://other.example".to_owned();
    assert!(matches!(
        decode_start_task(&comparison, &operation()),
        Err(StartTaskDecodeError::InvalidAgentWorkflow)
    ));
}

#[test]
fn malformed_creation_revision_and_duplicate_budget_fail_closed() {
    let mut invalid_operation = operation();
    invalid_operation.task_revision = 1;
    assert!(matches!(
        decode_start_task(&start(), &invalid_operation),
        Err(StartTaskDecodeError::InvalidCreationRevision)
    ));
    let mut start = start();
    let budget = wire::TaskBudget {
        kind: wire::TaskBudgetKind::MaxSources,
        limit: 2,
    };
    start.budgets = vec![budget.clone(), budget];
    assert!(matches!(
        decode_start_task(&start, &operation()),
        Err(StartTaskDecodeError::DuplicateBudget)
    ));
}

#[test]
fn reviewed_local_workflow_refuses_any_scope_or_model_widening() {
    let mut empty_sources = start();
    empty_sources.consent_preview.sources.clear();
    assert!(matches!(
        decode_start_task(&empty_sources, &operation()),
        Err(StartTaskDecodeError::InvalidReviewedWorkflow)
    ));

    let mut discovery = start();
    discovery.consent_preview.source_discovery_enabled = true;
    assert!(matches!(
        decode_start_task(&discovery, &operation()),
        Err(StartTaskDecodeError::InvalidReviewedWorkflow)
    ));

    let mut model = start();
    model
        .budgets
        .iter_mut()
        .find(|budget| budget.kind == wire::TaskBudgetKind::MaxModelRequests)
        .into_iter()
        .for_each(|budget| budget.limit = 1);
    assert!(matches!(
        decode_start_task(&model, &operation()),
        Err(StartTaskDecodeError::InvalidReviewedWorkflow)
    ));

    // Still refused, and it was before decision 0057 — but for a different
    // reason, and the difference is the whole change. It used to fail
    // because the list was not exactly `["browser.dom.read"]`; it now
    // fails because the tool this workflow proposes is not in the list at
    // all, so admitting the task would journal one that can never advance.
    let mut tool = start();
    tool.tool_allowlist = vec!["browser.navigate".to_owned()];
    assert!(matches!(
        decode_start_task(&tool, &operation()),
        Err(StartTaskDecodeError::InvalidReviewedWorkflow)
    ));
}

#[test]
fn an_empty_allowlist_is_refused_rather_than_read_as_every_tool() {
    // The most consequential of these cases. Downstream, guard evaluation
    // reads an empty allowlist as everything the milestone has reached, so
    // an empty list admitted here would be the widest task this decoder
    // can produce while looking like the narrowest.
    let mut empty = start();
    empty.tool_allowlist.clear();
    assert!(matches!(
        decode_start_task(&empty, &operation()),
        Err(StartTaskDecodeError::InvalidReviewedWorkflow)
    ));
}

#[test]
fn a_wider_allowlist_carrying_the_workflows_tool_decodes() {
    // The positive half: this decoder holds no opinion about which names
    // exist. `tool::REGISTRY` refuses a name the milestone has not
    // reached and the policy engine refuses the effect it would produce,
    // so a second vocabulary here would only be one that could drift.
    let mut wider = start();
    wider.tool_allowlist = vec![
        "browser.dom.read".to_owned(),
        "browser.dom.query".to_owned(),
    ];
    let Ok(TaskEngineLoad::Fresh { seed, .. }) = decode_start_task(&wider, &operation()) else {
        unreachable!("a wider allowlist carrying the workflow's tool decodes")
    };
    assert_eq!(
        seed.snapshot.tool_allowlist,
        vec![
            "browser.dom.read".to_owned(),
            "browser.dom.query".to_owned()
        ]
    );
}
