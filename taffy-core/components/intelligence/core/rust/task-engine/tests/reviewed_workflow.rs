// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Terminal outcomes for the one local, provider-free task workflow.

#[path = "reviewed_workflow/common.rs"]
mod common;

use bip_types::identity::ApprovalReceiptReference;
use common::{complete_observation, preview, seed, Digest, Driver};
use task_engine::{
    Command, Effect, ObservationCompleteness, ObservationGraphSummary, TaskState, WorkflowError,
};

fn finish(driver: &mut Driver) -> task_engine::Accepted {
    driver.apply_next();
    driver.apply_next();
    driver.apply_next();
    driver.apply_next();
    driver.apply_next()
}

#[test]
fn a_real_bounded_observation_completes_without_a_model_result() {
    let mut driver = Driver::new();
    let action_id = driver.enter_observation_dispatch();
    let outcome = complete_observation(&mut driver, action_id, ObservationCompleteness::Complete);
    assert!(outcome.effects.is_empty());
    let terminal = finish(&mut driver);
    assert_eq!(driver.reducer.task().state(), TaskState::Completed);
    assert!(matches!(
        terminal.effects.as_slice(),
        [Effect::ReleaseTaskTabs]
    ));
    let result = driver
        .reducer
        .task()
        .terminal_result()
        .unwrap_or_else(|| unreachable!("completed task has a result"));
    assert_eq!(result.source_count, 1);
    assert_eq!(result.fact_count, ObservationGraphSummary::FACT_COUNT);
    assert!(result.artifact_ids.is_empty());
}

#[test]
fn an_explicitly_incomplete_observation_is_partial_and_not_promoted() {
    let mut driver = Driver::new();
    let action_id = driver.enter_observation_dispatch();
    complete_observation(&mut driver, action_id, ObservationCompleteness::Incomplete);
    let terminal = finish(&mut driver);
    assert_eq!(driver.reducer.task().state(), TaskState::Partial);
    assert!(matches!(
        terminal.effects.as_slice(),
        [Effect::ReleaseTaskTabs]
    ));
    assert_eq!(
        driver
            .reducer
            .task()
            .terminal_result()
            .map(|result| result.unmet.len()),
        Some(1)
    );
}

// The three cases below were one test until decision 0057, which stopped the
// allowlist clause being an equality against `["browser.dom.read"]`. That test
// changed a missing route and a different tool together, so it could not say
// which of the two it was proving — and under the relaxed rule "a different
// tool" no longer means what it did: a *wider* list is now accepted, and only
// a list without this workflow's own tool is refused. Each fact gets its own
// test rather than one test with two edits.
fn started(seed: task_engine::TaskSeed, preview: task_engine::ScopePreview) -> Driver {
    let mut driver = Driver::from_seed(seed);
    driver.apply(Command::StartTask(preview));
    driver.apply(Command::AcceptInitialConsent(ApprovalReceiptReference(
        "consent-1".to_owned(),
    )));
    driver
}

#[test]
fn the_workflow_refuses_a_missing_no_model_route() {
    let mut invalid_seed = seed();
    invalid_seed.snapshot.provider_route = None;
    let mut invalid_preview = preview();
    invalid_preview.provider_route = None;

    assert_eq!(
        started(invalid_seed, invalid_preview)
            .reducer
            .next_reviewed_command(&Digest),
        Err(WorkflowError::InvalidWorkflowConfiguration)
    );
}

#[test]
fn the_workflow_refuses_an_allowlist_without_the_tool_it_proposes() {
    // Not "a different tool from the reviewed one" — that is no longer a
    // question this file asks. It is that the one tool this workflow proposes
    // was not consented to, so the task would be admitted and then never
    // advance.
    let mut invalid_seed = seed();
    invalid_seed.snapshot.tool_allowlist = vec!["browser.dom.query".to_owned()];

    assert_eq!(
        started(invalid_seed, preview())
            .reducer
            .next_reviewed_command(&Digest),
        Err(WorkflowError::InvalidWorkflowConfiguration)
    );
}

#[test]
fn the_workflow_refuses_an_empty_allowlist() {
    // An empty allowlist is read by guard evaluation as everything the
    // milestone has reached, so it is the widest configuration this workflow
    // could run under rather than the narrowest. It is refused here for that
    // reason and not for tidiness.
    let mut invalid_seed = seed();
    invalid_seed.snapshot.tool_allowlist.clear();

    assert_eq!(
        started(invalid_seed, preview())
            .reducer
            .next_reviewed_command(&Digest),
        Err(WorkflowError::InvalidWorkflowConfiguration)
    );
}

#[test]
fn a_wider_allowlist_that_keeps_this_workflows_tool_still_completes() {
    // The positive half of decision 0057. A name beside `browser.dom.read` is
    // one this workflow never proposes; refusing the task over it would be
    // this file holding an opinion about the tool vocabulary, which the
    // registry and the policy engine each already narrow.
    let mut wider = seed();
    wider.snapshot.tool_allowlist = vec![
        "browser.dom.read".to_owned(),
        "browser.dom.query".to_owned(),
    ];
    let mut driver = Driver::from_seed(wider);
    let action_id = driver.enter_observation_dispatch();
    complete_observation(&mut driver, action_id, ObservationCompleteness::Complete);
    finish(&mut driver);

    assert_eq!(driver.reducer.task().state(), TaskState::Completed);
}
