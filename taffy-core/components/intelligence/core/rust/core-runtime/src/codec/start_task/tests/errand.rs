// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The errand shape of decision 0087, beside the page-scoped refusal it must not widen.

use super::{direct_start, errand_start, operation};
use crate::codec::start_shape::StartTaskDecodeError;
use crate::codec::start_task::decode_start_task;
use crate::ports::TaskEngineLoad;
use core_service_types as wire;
use task_engine::TaskTemplateId;

// --- The errand shape of decision 0087 -------------------------------
//
// The assertions here are the ones the record's Validation section names.
// The page-scoped tests above are deliberately left beside them: a change
// that widens the errand and narrows the research shape by accident shows
// up as a failure in this same file.

#[test]
fn an_errand_decodes_with_no_named_source() {
    let Ok(TaskEngineLoad::Fresh {
        seed,
        initial_consent,
        ..
    }) = decode_start_task(&errand_start(4), &operation())
    else {
        unreachable!("an errand with no named source decodes")
    };
    assert_eq!(seed.snapshot.template_id, TaskTemplateId::WebErrand);
    // The scope lives on the consent admission rather than the seed: the
    // seed is what the task was created with, the preview is what the
    // person was shown and agreed to.
    assert!(initial_consent.preview.sources.is_empty());
    assert!(initial_consent.preview.source_discovery_enabled);
}

#[test]
fn an_errand_decodes_when_the_person_was_already_on_the_site() {
    let mut start = errand_start(4);
    start.consent_preview.sources = vec![wire::TaskConsentSource {
        source_id: "00112233445566778899aabbccddeeff".to_owned(),
        tab_id: "tab-a".to_owned(),
        normalized_origin: "https://example.test".to_owned(),
        canonical_locator: None,
    }];
    // The source budget follows the consent: one named plus the cap.
    start
        .budgets
        .iter_mut()
        .find(|budget| budget.kind == wire::TaskBudgetKind::MaxSources)
        .into_iter()
        .for_each(|budget| budget.limit = 5);
    let Ok(TaskEngineLoad::Fresh {
        initial_consent, ..
    }) = decode_start_task(&start, &operation())
    else {
        unreachable!("an errand with one named source decodes")
    };
    assert_eq!(initial_consent.preview.sources.len(), 1);
    assert!(initial_consent.preview.source_discovery_enabled);
}

#[test]
fn an_errand_cannot_run_without_a_model() {
    // Decision 0087 section 1: an errand is walked from what each page
    // turns out to say. Admitting one on a route that cannot call a model
    // would journal a task that could never advance.
    let mut start = errand_start(4);
    start.provider_route_id = Some("no_model_required".to_owned());
    start.consent_preview.provider_route = wire::TaskProviderRoute::NoModelRequired;
    assert!(matches!(
        decode_start_task(&start, &operation()),
        Err(StartTaskDecodeError::InvalidProviderRoute)
    ));
}

#[test]
fn an_errand_needs_a_cap_and_will_not_take_an_unbounded_one() {
    assert!(matches!(
        decode_start_task(&errand_start(0), &operation()),
        Err(StartTaskDecodeError::InvalidErrandWorkflow)
    ));
    let above = crate::codec::start_shape::ERRAND_MAX_NEW_SOURCE_CAP + 1;
    assert!(matches!(
        decode_start_task(&errand_start(above), &operation()),
        Err(StartTaskDecodeError::InvalidErrandWorkflow)
    ));
}

#[test]
fn an_errands_source_budget_must_agree_with_what_the_person_consented_to() {
    // A budget that disagreed with the consent surface would mean the
    // number the person read was not the number the task ran under.
    let mut start = errand_start(4);
    start
        .budgets
        .iter_mut()
        .find(|budget| budget.kind == wire::TaskBudgetKind::MaxSources)
        .into_iter()
        .for_each(|budget| budget.limit = 40);
    assert!(matches!(
        decode_start_task(&start, &operation()),
        Err(StartTaskDecodeError::InvalidErrandWorkflow)
    ));
}

#[test]
fn an_errand_still_requires_discovery_to_have_been_consented_to() {
    let mut start = errand_start(4);
    start.consent_preview.source_discovery_enabled = false;
    assert!(matches!(
        decode_start_task(&start, &operation()),
        Err(StartTaskDecodeError::InvalidErrandWorkflow)
    ));
}

#[test]
fn an_errand_requires_assistant_control() {
    let mut start = errand_start(4);
    start.control_mode = wire::TaskControlMode::Shared;
    assert!(matches!(
        decode_start_task(&start, &operation()),
        Err(StartTaskDecodeError::InvalidErrandWorkflow)
    ));
}

#[test]
fn the_page_scoped_shape_still_refuses_a_discovered_destination() {
    // The other half of decision 0087: widening the errand must not widen
    // the research task beside it.
    let mut start = direct_start();
    start.consent_preview.source_discovery_enabled = true;
    start.consent_preview.new_source_cap = 4;
    assert!(matches!(
        decode_start_task(&start, &operation()),
        Err(StartTaskDecodeError::InvalidAgentWorkflow)
    ));
}

#[test]
fn an_exact_saved_flow_decodes_locally_with_one_source_and_zero_model_budget() {
    let mut command = super::start();
    command.template_id = wire::TaskTemplateId::WebErrand;
    command.kind = wire::TaskKind::Errand;
    command.skill_version_id = Some("accepted-flow@1".to_owned());
    assert!(decode_start_task(&command, &operation()).is_ok());
    let mut missing = command.clone();
    missing.skill_version_id = None;
    assert!(matches!(
        decode_start_task(&missing, &operation()),
        Err(StartTaskDecodeError::InvalidProviderRoute)
    ));
    let mut unbounded = command.clone();
    unbounded.consent_preview.source_discovery_enabled = true;
    unbounded.consent_preview.new_source_cap = 1;
    assert!(decode_start_task(&unbounded, &operation()).is_err());
    let mut page_missing = command.clone();
    page_missing.consent_preview.sources.clear();
    assert!(decode_start_task(&page_missing, &operation()).is_err());
    let mut paid = command.clone();
    paid.budgets
        .iter_mut()
        .find(|budget| budget.kind == wire::TaskBudgetKind::MaxModelRequests)
        .unwrap()
        .limit = 1;
    assert!(decode_start_task(&paid, &operation()).is_err());
    command.control_mode = wire::TaskControlMode::Shared;
    assert!(decode_start_task(&command, &operation()).is_err());
}
